/******************************************************************************
 * aesdsocket.c
 *
 * Usage: aesdsocket [-d]
 *
 * Author: Matt Hartnett
 *
 * 1) Listens on port 9000 for TCP connections.
 * 2) When a client connects, logs the client IP address using syslog.
 * 3) Receives data from the client and appends complete newline‐terminated
 *     packets to /var/tmp/aesdsocketdata.
 * 4) As soon as a packet is complete (ends with a newline), reads the full
 *     contents of /var/tmp/aesdsocketdata and sends it back to the client.
 * 5) After the connection closes, logs the connection close.
 * 6) Repeats for further client connections until SIGINT or SIGTERM is received.
 * 7) On SIGINT/SIGTERM, finishes any active connection, cleans up, and deletes
 *    the file.
 *
 * The -d option causes the program to run as a daemon.
 *
 *****************************************************************************/
#define _GNU_SOURCE
#include <pthread.h>
#include <sys/queue.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <errno.h>
#include <unistd.h>
#include <string.h>
#include <signal.h>
#include <syslog.h>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <sys/stat.h>

// Copied from BSD queue.h code https://github.com/freebsd/freebsd-src/blob/main/sys/sys/queue.h
#define	SLIST_FOREACH_SAFE(var, head, field, tvar)			\
	for ((var) = SLIST_FIRST((head));				\
	    (var) && ((tvar) = SLIST_NEXT((var), field), 1);		\
	    (var) = (tvar))

#define PORT 9000
#define BACKLOG 10
#define DATA_FILE "/var/tmp/aesdsocketdata"
#define READ_BUFFER_SIZE 1024

// Global so that the signal handler can close the socket
//int sockfd = -1;

// Timestamping thread
pthread_t timestamp_thread;

// Global flag set by signal handlers to indicate termination requested
volatile sig_atomic_t terminate_flag = 0;

// Mutex to synchronize file writes (and associated reads)
pthread_mutex_t file_mutex = PTHREAD_MUTEX_INITIALIZER;

// Structure to hold thread information:
typedef struct thread_info_t {
    pthread_t thread_id;
    int client_fd;
    struct sockaddr_in client_addr;
    bool complete;
    SLIST_ENTRY(thread_info_t) entries;
} thread_info_t;

// Linked list head
SLIST_HEAD(slisthead, thread_info_t) thread_list_head =     SLIST_HEAD_INITIALIZER(thread_list_head);

// Handle signals
static void *signal_thread_func(void *arg)
{
    // We'll receive the listening socket fd via `arg`
    int sockfd = *(int *)arg;

    // This is the same set we blocked in main
    sigset_t set;
    sigemptyset(&set);
    sigaddset(&set, SIGINT);
    sigaddset(&set, SIGTERM);

    int sig_caught = 0;
    // Wait for either SIGINT or SIGTERM
    if (sigwait(&set, &sig_caught) != 0) {
        perror("sigwait");
        pthread_exit(NULL);
    }

    // We got a signal => set the global flag
    terminate_flag = 1;
    syslog(LOG_INFO, "Caught signal, exiting");

    // Force the accept() call to return in the main thread.
    // This will cause accept() to fail with an error,
    // so the main thread sees terminate_flag and exits.
    shutdown(sockfd, SHUT_RDWR);

    return NULL;
}


// Helper function to perform cleanup
void cleanup(int sockfd)
{
    if (sockfd >= 0) {
        close(sockfd);
    }
    if (remove(DATA_FILE) != 0) {
        perror("remove");
    }
    closelog();
}

// Thread function to process connection
void *connection_handler(void *arg)
{
    // Pull client info from arg
    thread_info_t *tinfo = (thread_info_t *)arg;
    int client_fd = tinfo->client_fd;
    struct sockaddr_in client_addr = tinfo->client_addr;

    // Process connection (used to be in main):
    // Log the accepted connection
    char client_ip[INET_ADDRSTRLEN];
    if (inet_ntop(AF_INET, &client_addr.sin_addr, client_ip, sizeof(client_ip)) != NULL) {
        syslog(LOG_INFO, "Accepted connection from %s", client_ip);
    } else {
        syslog(LOG_INFO, "Accepted connection from unknown client");
    }

    // Process the connection
    char recv_buf[READ_BUFFER_SIZE];
    char *incomplete_buf = NULL;
    size_t incomplete_buf_len = 0;
    bool connection_error = false;

    while (!terminate_flag) {
        ssize_t bytes_rcvd = recv(client_fd, recv_buf, sizeof(recv_buf), 0);
        if (bytes_rcvd < 0) {
            if (errno == EINTR) {
                continue;
            }
            perror("recv");
            connection_error = true;
            break;
        } else if (bytes_rcvd == 0) {
            // Client closed connection
            break;
        }

        // Append new data to the buffer
        char *temp = realloc(incomplete_buf, incomplete_buf_len + bytes_rcvd);
        if (!temp) {
            perror("realloc");
            free(incomplete_buf);
            incomplete_buf = NULL;
            connection_error = true;
            break;
        }
        incomplete_buf = temp;
        memcpy(incomplete_buf + incomplete_buf_len, recv_buf, bytes_rcvd);
        incomplete_buf_len += bytes_rcvd;

        // Process complete packets (lines ending with newline)
        char *line_start = incomplete_buf;
        char *newline_ptr = NULL;
        while ((newline_ptr = memchr(line_start, '\n',
                      (incomplete_buf + incomplete_buf_len) - line_start))) {
            size_t line_len = newline_ptr - line_start + 1;
            // Lock the mutex to synchronize file access
            pthread_mutex_lock(&file_mutex);
            // Append packet to file
            FILE *fp = fopen(DATA_FILE, "a");
            if (fp == NULL) {
                perror("fopen for append");
                pthread_mutex_unlock(&file_mutex);
                connection_error = true;
                break;
            }
            if (fwrite(line_start, 1, line_len, fp) != line_len) {
                perror("fwrite");
                fclose(fp);
                pthread_mutex_unlock(&file_mutex);
                connection_error = true;
                break;
            }
            if (fflush(fp) != 0) {
                perror("fflush");
                fclose(fp);
                pthread_mutex_unlock(&file_mutex);
                connection_error = true;
                break;
            }

            fclose(fp);
            // Send the file contents back to the client
            fp = fopen(DATA_FILE, "r");
            if (fp == NULL) {
                perror("fopen for reading");
                pthread_mutex_unlock(&file_mutex);
                connection_error = true;
                break;
            }
            char file_buf[READ_BUFFER_SIZE];
            size_t bytes_read;
            while ((bytes_read = fread(file_buf, 1, sizeof(file_buf), fp)) > 0) {
                size_t bytes_sent = 0;
                while (bytes_sent < bytes_read) {
                    ssize_t rc = send(client_fd, file_buf + bytes_sent,
                                      bytes_read - bytes_sent, 0);
                    if (rc < 0) {
                        perror("send");
                        fclose(fp);
                        pthread_mutex_unlock(&file_mutex);
                        connection_error = true;
                        break;
                    }
                    bytes_sent += rc;
                }
            }
            fclose(fp);
            pthread_mutex_unlock(&file_mutex);
            line_start = newline_ptr + 1;
        }
        if (connection_error) {
//            pthread_mutex_unlock(&file_mutex);
            break;
        }

        // Remove processed data from the buffer
        size_t remaining = (incomplete_buf + incomplete_buf_len) - line_start;
        if (remaining > 0) {
            memmove(incomplete_buf, line_start, remaining);
        }
        incomplete_buf_len = remaining;
        if (remaining == 0) {
            free(incomplete_buf);
            incomplete_buf = NULL;
        } else {
            char *temp2 = realloc(incomplete_buf, remaining);
            if (temp2)
                incomplete_buf = temp2;
        }
    }

    free(incomplete_buf);
    incomplete_buf = NULL;
    close(client_fd);

    // Log connection closure
    if (inet_ntop(AF_INET, &client_addr.sin_addr, client_ip, sizeof(client_ip)) != NULL) {
        syslog(LOG_INFO, "Closed connection from %s", client_ip);
    } else {
        syslog(LOG_INFO, "Closed connection from unknown client");
    }

    tinfo->complete = true;
    return NULL;
}


// Timestamping thread function
void *timestamp_thread_func(void *arg)
{
    while (!terminate_flag) {
        struct timespec ts;
        clock_gettime(CLOCK_MONOTONIC, &ts);

        // Calculate time until the next 10-second boundary
        int seconds_to_sleep = 10 - (ts.tv_sec % 10);
        int nanoseconds_to_sleep = -ts.tv_nsec; // Adjust to the exact start of the second

        struct timespec sleep_time = {seconds_to_sleep, nanoseconds_to_sleep};
        nanosleep(&sleep_time, NULL);

        // Generate and write timestamp
        time_t now = time(NULL);
        struct tm *tm_info = localtime(&now);
        if (!tm_info) {
            perror("localtime");
            continue;
        }
        char time_str[128];
        if (strftime(time_str, sizeof(time_str), "%a, %d %b %Y %H:%M:%S %z", tm_info) == 0) {
            perror("strftime returned 0");
            continue;
        }
        char timestamp[256];
        snprintf(timestamp, sizeof(timestamp), "timestamp:%s\n", time_str);

        // Use mutex to ensure atomic write with respect to socket data
        pthread_mutex_lock(&file_mutex);
        FILE *fp = fopen(DATA_FILE, "a");
        if (fp) {
            fwrite(timestamp, 1, strlen(timestamp), fp);
            fclose(fp);
        } else {
            perror("fopen in timestamp thread");
        }
        pthread_mutex_unlock(&file_mutex);
    }
    return NULL;
}

int main(int argc, char *argv[])
{
    int sockfd = -1, client_fd = -1;
    bool run_as_daemon = false;
    int opt;

    // Parse command-line options
    while ((opt = getopt(argc, argv, "d")) != -1) {
        switch (opt) {
            case 'd':
                run_as_daemon = true;
                break;
            default:
                fprintf(stderr, "Usage: %s [-d]\n", argv[0]);
                exit(EXIT_FAILURE);
        }
    }

    // Open syslog for logging
    openlog("aesdsocket", LOG_PID | LOG_NDELAY, LOG_USER);

    // Setup signal handlers
//    if (setup_signal_handlers() != 0) {
//        cleanup(sockfd);
//        return -1;
//    }
    sigset_t set;
    sigemptyset(&set);
    sigaddset(&set, SIGINT);
    sigaddset(&set, SIGTERM);

    // This blocks SIGINT and SIGTERM so they will NOT be delivered
    // to any thread until we explicitly wait for them with sigwait()
    if (pthread_sigmask(SIG_BLOCK, &set, NULL) != 0) {
        perror("pthread_sigmask");
        exit(EXIT_FAILURE);
    }

    // Create a TCP socket
    sockfd = socket(AF_INET, SOCK_STREAM, 0);
    if (sockfd < 0) {
        perror("socket");
        cleanup(sockfd);
        return -1;
    }

    // Allow the socket to reuse the address
    int optval = 1;
    if (setsockopt(sockfd, SOL_SOCKET, SO_REUSEADDR, &optval, sizeof(optval)) < 0) {
        perror("setsockopt SO_REUSEADDR");
        cleanup(sockfd);
        return -1;
    }

//    if (setsockopt(sockfd, SOL_SOCKET, SO_REUSEPORT, &optval, sizeof(optval)) < 0) {
//        perror("setsockopt SO_REUSEPORT");
//        cleanup(sockfd);
//        return -1;
//    }


    // Bind the socket to port 9000
    struct sockaddr_in server_addr;
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(PORT);
    server_addr.sin_addr.s_addr = INADDR_ANY;
    if (bind(sockfd, (struct sockaddr *)&server_addr, sizeof(server_addr)) != 0) {
        perror("bind");
        cleanup(sockfd);
        return -1;
    }

    // In daemon mode, fork after binding succeeds
    if (run_as_daemon) {
        pid_t pid = fork();
        if (pid < 0) {
            perror("fork");
            cleanup(sockfd);
            return -1;
        } else if (pid > 0) {
            // Parent exits
            exit(EXIT_SUCCESS);
        } else {
            if (setsid() < 0) {
                perror("setsid");
                cleanup(sockfd);
                return -1;
            }
            close(STDIN_FILENO);
            close(STDOUT_FILENO);
            close(STDERR_FILENO);
        }
    }

    // Listen for incoming connections
    if (listen(sockfd, BACKLOG) != 0) {
        perror("listen");
        cleanup(sockfd);
        return -1;
    }

    // Start timestamp thread to run in the background
    if (pthread_create(&timestamp_thread, NULL, timestamp_thread_func, NULL) != 0) {
        perror("pthread_create for timestamp thread");
        cleanup(sockfd);
        exit(EXIT_FAILURE);
    }

    // Start the signal handler thread to run in the background
    pthread_t signal_thread;
    if (pthread_create(&signal_thread, NULL, signal_thread_func, &sockfd) != 0) {
        perror("pthread_create for signal handling thread");
        cleanup(sockfd);
        exit(EXIT_FAILURE);
    }

    // Main loop: accept and process connections until termination is requested
    while (!terminate_flag) {
        // Check for completed threads before accepting a new connection
        thread_info_t *cur = NULL;
        thread_info_t *temp = NULL;
        SLIST_FOREACH_SAFE(cur, &thread_list_head, entries, temp) {
            if (cur->complete) {
                pthread_join(cur->thread_id, NULL);
                SLIST_REMOVE(&thread_list_head, cur, thread_info_t, entries);
                free(cur);
            }
        }

        struct sockaddr_in client_addr;
        socklen_t client_addr_len = sizeof(client_addr);
        client_fd = accept(sockfd, (struct sockaddr *)&client_addr, &client_addr_len);
        if (client_fd < 0) {
            if (errno == EINTR && terminate_flag) {
                break;
            }
            perror("accept");
            continue;
        }

        // Allocate a new thread for the connection and add client info
        thread_info_t *tinfo = malloc(sizeof(thread_info_t));
        if (!tinfo) {
            perror("malloc thread_info");
            close(client_fd);
            continue;
        }
        tinfo->client_fd = client_fd;
        tinfo->complete = false;
        memcpy(&tinfo->client_addr, &client_addr, sizeof(client_addr));

        // Create a thread to handle this connection
        int ret = pthread_create(&tinfo->thread_id, NULL, connection_handler, tinfo);
        if (ret != 0) {
            errno = ret;
            perror("pthread_create");
            close(client_fd);
            free(tinfo);
            continue;
        }

        // Add thread to linked list
        SLIST_INSERT_HEAD(&thread_list_head, tinfo, entries);
    }

    // Final cleanup: join any remaining threads, exit
    thread_info_t *cur = NULL;
    thread_info_t *temp = NULL;
    SLIST_FOREACH_SAFE(cur, &thread_list_head, entries, temp) {
        SLIST_REMOVE(&thread_list_head, cur, thread_info_t, entries);
        pthread_join(cur->thread_id, NULL);
        free(cur);
    }
    // End timestamp and signal thread
    pthread_join(timestamp_thread, NULL);
    pthread_join(signal_thread, NULL);
    cleanup(sockfd);
    return 0;
}