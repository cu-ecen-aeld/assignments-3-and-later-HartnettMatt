#include "threading.h"
#include <pthread.h>
#include <unistd.h>
#include <stdlib.h>
#include <stdio.h>

// Optional: use these functions to add debug or error prints to your application
#define DEBUG_LOG(msg,...)
//#define DEBUG_LOG(msg,...) printf("threading: " msg "\n" , ##__VA_ARGS__)
#define ERROR_LOG(msg,...) printf("threading ERROR: " msg "\n" , ##__VA_ARGS__)

void* threadfunc(void* thread_param)
{
    struct thread_data* thread_func_args = (struct thread_data *) thread_param;
    // Wait in microseconds
    usleep(thread_func_args->wait_to_obtain_ms);
    // Lock mutex, and if success, wait again (if not, report and exit)
    int rc = pthread_mutex_lock(thread_func_args->mutex);
    if(rc != 0){
        ERROR_LOG("pthread mutex lock failed with %d", rc);
        thread_func_args->thread_complete_success = false;
    } else {
        usleep(thread_func_args->wait_to_release_ms);
        // Unlock mutex and report status
        rc = pthread_mutex_unlock(thread_func_args->mutex);
        if(rc != 0){
            ERROR_LOG("pthread mutex unlock failed with %d", rc);
            thread_func_args->thread_complete_success = false;
        } else {
            thread_func_args->thread_complete_success = true;
        }
    }
    return thread_param;
}


bool start_thread_obtaining_mutex(pthread_t *thread, pthread_mutex_t *mutex,int wait_to_obtain_ms, int wait_to_release_ms)
{
    // Allocate memory
    struct thread_data *thread_func = malloc(sizeof(struct thread_data));
    // Populate struct data
    thread_func->mutex = mutex;
    thread_func->wait_to_obtain_ms = wait_to_obtain_ms;
    thread_func->wait_to_release_ms = wait_to_release_ms;
    // Create the thread and report accordingly
    int rc = pthread_create(thread, NULL, threadfunc, thread_func);
    if(rc != 0){
        ERROR_LOG("pthread create failed with %d", rc);
        thread_func->thread_complete_success = false;
        return false;
    }
    return true;
}

