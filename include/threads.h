#ifndef THREADS_H
#define THREADS_H
#include <pthread.h>
#include <stdbool.h>
#include <stddef.h>

// Forward declaration
typedef struct Work Work;

// Structure to represent a work item
struct Work
{
    void (*func)(void *); // Function to execute
    void *arg;            // Argument to pass to function
    Work *next;           // Next work item in queue
};

// Thread pool structure
typedef struct
{
    pthread_t *threads;          // Array of worker threads
    Work *work_first;            // First work item in queue
    Work *work_last;             // Last work item in queue
    pthread_mutex_t work_mutex;  // Mutex for work queue
    pthread_cond_t work_cond;    // Condition variable for work
    pthread_cond_t working_cond; // Condition variable for working threads
    size_t thread_count;         // Number of threads in pool
    size_t working_count;        // Number of threads currently working
    bool stop;                   // Flag to indicate pool should stop
} ThreadPool;

// Thread pool functions
ThreadPool *thread_pool_create(size_t num_threads);
void thread_pool_destroy(ThreadPool *pool);
bool thread_pool_add_work(ThreadPool *pool, void (*func)(void *), void *arg);
void thread_pool_wait(ThreadPool *pool);
bool thread_pool_is_working(ThreadPool *pool);

#endif // THREADS_H