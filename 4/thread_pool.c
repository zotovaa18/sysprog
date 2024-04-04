#include "thread_pool.h"
#include <pthread.h>
#include <stdlib.h>
#include <stdbool.h>

struct thread_task {
	thread_task_f function;
    void *arg;
    pthread_t thread;
    bool finished;
    bool is_pushed;
};

struct thread_pool {
    pthread_t *threads;
    int max_thread_count;
    int thread_count;
    struct thread_task **tasks;
    int task_count;
    pthread_mutex_t task_mutex;
    pthread_cond_t task_completed;
};

int thread_pool_new(int max_thread_count, struct thread_pool **pool) {
    if (max_thread_count <= 0 || max_thread_count > TPOOL_MAX_THREADS) {
        return TPOOL_ERR_INVALID_ARGUMENT;
    }
    *pool = (struct thread_pool*)malloc(sizeof(struct thread_pool));
    if (*pool == NULL) {
        return TPOOL_ERR_INVALID_ARGUMENT;
    }

    (*pool)->max_thread_count = max_thread_count;
    (*pool)->thread_count = 0;
    (*pool)->tasks = (struct thread_task**)malloc(sizeof(struct thread_task*) * TPOOL_MAX_TASKS);
    if ((*pool)->tasks == NULL) {
        free(*pool);
        return TPOOL_ERR_INVALID_ARGUMENT;
    }

    (*pool)->task_count = 0;
    pthread_mutex_init(&(*pool)->task_mutex, NULL);
    pthread_cond_init(&(*pool)->task_completed, NULL);

    return 0;
}

int thread_pool_thread_count(const struct thread_pool *pool) {
    return pool->thread_count;
}

int thread_pool_delete(struct thread_pool *pool) {
    pthread_mutex_lock(&pool->task_mutex);
    while (pool->task_count > 0) {
        pthread_cond_wait(&pool->task_completed, &pool->task_mutex);
    }
    pthread_mutex_unlock(&pool->task_mutex);

    free(pool->tasks);
    free(pool);
    return 0;
}

void *thread_function(void *arg) {
    struct thread_task *task = (struct thread_task*)arg;
    task->function(task->arg);
    task->finished = true;
    return NULL;
}

int thread_pool_push_task(struct thread_pool *pool, struct thread_task *task) {
    pthread_mutex_lock(&pool->task_mutex);
    if (pool->task_count >= TPOOL_MAX_TASKS) {
        pthread_mutex_unlock(&pool->task_mutex);
        return TPOOL_ERR_TOO_MANY_TASKS;
    }

    pool->tasks[pool->task_count++] = task;
    task->is_pushed = true;
    pthread_mutex_unlock(&pool->task_mutex);

    pthread_create(&(task->thread), NULL, thread_function, task);

    return 0;
}

int thread_task_new(struct thread_task **task, thread_task_f function, void *arg) {
    *task = (struct thread_task*)malloc(sizeof(struct thread_task));
    if (*task == NULL) {
        return TPOOL_ERR_INVALID_ARGUMENT;
    }

    (*task)->function = function;
    (*task)->arg = arg;
    (*task)->finished = false;
    (*task)->is_pushed = false;

    return 0;
}


bool thread_task_is_finished(const struct thread_task *task) {
    return task->finished;
}

bool thread_task_is_running(const struct thread_task *task) {
    return !task->finished;
}

int thread_task_join(struct thread_task *task, void **result) {
    if (!task->is_pushed) {
        return TPOOL_ERR_TASK_NOT_PUSHED;
    }

    if (pthread_join(task->thread, result) != 0) {
        return TPOOL_ERR_TASK_IN_POOL;
    }

    task->finished = true;

    return 0;
}

int thread_task_delete(struct thread_task *task) {
    if(task->is_pushed) {
        return TPOOL_ERR_TASK_IN_POOL;
    }

    free(task);
    return 0;
}
