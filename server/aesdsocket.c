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

#define PORT 9000
#define BACKLOG 10
#define DATA_FILE "/var/tmp/aesdsocketdata"
#define READ_BUFFER_SIZE 1024

// Global flag set by signal handlers to indicate termination requested
volatile sig_atomic_t terminate_flag = 0;

// Signal handler: set flag and log the event
static void signal_handler(int signo)
{
    terminate_flag = 1;
    syslog(LOG_INFO, "Caught signal, exiting");
}

// Setup signal handlers for SIGINT and SIGTERM
static int setup_signal_handlers(void)
{
    struct sigaction sa;
    memset(&sa, 0, sizeof(sa));
    sa.sa_handler = signal_handler;
    if (sigaction(SIGINT, &sa, NULL) != 0) {
        perror("sigaction SIGINT");
        return -1;
    }
    if (sigaction(SIGTERM, &sa, NULL) != 0) {
        perror("sigaction SIGTERM");
        return -1;
    }
    return 0;
}

// Helper function to send the entire contents of DATA_FILE to client_fd
static int send_file_to_client(int client_fd)
{
    FILE *fp = fopen(DATA_FILE, "r");
    if (fp == NULL) {
        perror("fopen for reading");
        return -1;
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
                return -1;
            }
            bytes_sent += rc;
        }
    }
    fclose(fp);
    return 0;
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
    if (setup_signal_handlers() != 0) {
        cleanup(sockfd);
        return -1;
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
    if (setsockopt(sockfd, SOL_SOCKET, SO_REUSEADDR, &optval, sizeof(optval)) != 0) {
        perror("setsockopt");
        cleanup(sockfd);
        return -1;
    }

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

    // Main loop: accept and process connections until termination is requested
    while (!terminate_flag) {
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
                FILE *fp = fopen(DATA_FILE, "a");
                if (fp == NULL) {
                    perror("fopen for append");
                    connection_error = true;
                    break;
                }
                if (fwrite(line_start, 1, line_len, fp) != line_len) {
                    perror("fwrite");
                    fclose(fp);
                    connection_error = true;
                    break;
                }
                fclose(fp);
                if (send_file_to_client(client_fd) != 0) {
                    connection_error = true;
                    break;
                }
                line_start = newline_ptr + 1;
            }
            if (connection_error) {
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
    }

    // Final cleanup and exit
    cleanup(sockfd);
    return 0;
}
