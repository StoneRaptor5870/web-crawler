#include <stdlib.h>
#include "../include/threads.h"

// Create a new work item
static Work *work_create(void (*func)(void *), void *arg)
{
    Work *work = malloc(sizeof(Work));
    if (!work)
        return NULL;

    work->func = func;
    work->arg = arg;
    work->next = NULL;

    return work;
}

// Get work from the queue
static Work *work_get(ThreadPool *pool)
{
    Work *work = pool->work_first;
    if (!work)
        return NULL;

    if (work->next == NULL)
    {
        pool->work_first = NULL;
        pool->work_last = NULL;
    }
    else
    {
        pool->work_first = work->next;
    }

    return work;
}

// Worker thread function
static void *worker(void *arg)
{
    ThreadPool *pool = (ThreadPool *)arg;
    Work *work;

    while (1)
    {
        pthread_mutex_lock(&pool->work_mutex);

        while (pool->work_first == NULL && !pool->stop)
        {
            pthread_cond_wait(&pool->work_cond, &pool->work_mutex);
        }

        if (pool->stop)
            break;

        work = work_get(pool);
        pool->working_count++;
        pthread_mutex_unlock(&pool->work_mutex);

        if (work)
        {
            work->func(work->arg);
            free(work);
        }

        pthread_mutex_lock(&pool->work_mutex);
        pool->working_count--;
        if (!pool->stop && pool->working_count == 0 && pool->work_first == NULL)
        {
            pthread_cond_signal(&pool->working_cond);
        }
        pthread_mutex_unlock(&pool->work_mutex);
    }

    pool->thread_count--;
    pthread_cond_signal(&pool->working_cond);
    pthread_mutex_unlock(&pool->work_mutex);
    return NULL;
}

// Create a new thread pool
ThreadPool *thread_pool_create(size_t num_threads)
{
    if (num_threads == 0)
        num_threads = 2;

    ThreadPool *pool = calloc(1, sizeof(ThreadPool));
    if (!pool)
        return NULL;

    pool->thread_count = num_threads;
    pool->threads = calloc(num_threads, sizeof(pthread_t));
    if (!pool->threads)
    {
        free(pool);
        return NULL;
    }

    pthread_mutex_init(&pool->work_mutex, NULL);
    pthread_cond_init(&pool->work_cond, NULL);
    pthread_cond_init(&pool->working_cond, NULL);

    for (size_t i = 0; i < num_threads; i++)
    {
        pthread_create(&pool->threads[i], NULL, worker, pool);
    }

    return pool;
}

// Destroy the thread pool
void thread_pool_destroy(ThreadPool *pool)
{
    if (!pool)
        return;

    pthread_mutex_lock(&pool->work_mutex);
    pool->stop = true;
    pthread_cond_broadcast(&pool->work_cond);
    pthread_mutex_unlock(&pool->work_mutex);

    for (size_t i = 0; i < pool->thread_count; i++)
    {
        pthread_join(pool->threads[i], NULL);
    }

    pthread_mutex_destroy(&pool->work_mutex);
    pthread_cond_destroy(&pool->work_cond);
    pthread_cond_destroy(&pool->working_cond);

    free(pool->threads);
    free(pool);
}

// Add work to the thread pool
bool thread_pool_add_work(ThreadPool *pool, void (*func)(void *), void *arg)
{
    Work *work = work_create(func, arg);
    if (!work)
        return false;

    pthread_mutex_lock(&pool->work_mutex);
    if (pool->work_first == NULL)
    {
        pool->work_first = work;
        pool->work_last = work;
    }
    else
    {
        pool->work_last->next = work;
        pool->work_last = work;
    }

    pthread_cond_broadcast(&pool->work_cond);
    pthread_mutex_unlock(&pool->work_mutex);

    return true;
}

// Wait for all work to complete
void thread_pool_wait(ThreadPool *pool)
{
    pthread_mutex_lock(&pool->work_mutex);
    while (pool->work_first != NULL ||
           (!pool->stop && pool->working_count != 0) ||
           (pool->stop && pool->thread_count != 0))
    {
        pthread_cond_wait(&pool->working_cond, &pool->work_mutex);
    }
    pthread_mutex_unlock(&pool->work_mutex);
}

// Check if thread pool has active work
bool thread_pool_is_working(ThreadPool *pool)
{
    if (!pool)
        return false;

    pthread_mutex_lock(&pool->work_mutex);
    bool is_working = (pool->working_count > 0 || pool->work_first != NULL);
    pthread_mutex_unlock(&pool->work_mutex);

    return is_working;
}