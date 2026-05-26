#ifdef _WIN32
#include <windows.h>
#include <process.h>
#else
#include <pthread.h>
#endif
#include <stdio.h>
#include <stdlib.h>
#include "tpool.h"

// Task node
typedef struct task {
    tpool_task_func func;
    void *arg;
    struct task *next;
} task_t;

// Thread pool structure
typedef struct {
#ifdef _WIN32
    HANDLE *threads;
    CRITICAL_SECTION lock;
    CONDITION_VARIABLE cond;
#else
    pthread_t *threads;
    pthread_mutex_t lock;
    pthread_cond_t cond;
#endif
    int num_threads;
    task_t *head;
    task_t *tail;
    int stop;
} tpool_t;

static tpool_t *g_pool = NULL;

// Worker thread function
#ifdef _WIN32
static DWORD WINAPI worker_thread(LPVOID arg) {
#else
static void *worker_thread(void *arg) {
#endif
    (void)arg;
    while (1) {
#ifdef _WIN32
        EnterCriticalSection(&g_pool->lock);
#else
        pthread_mutex_lock(&g_pool->lock);
#endif
        
        while (g_pool->head == NULL && !g_pool->stop) {
#ifdef _WIN32
            SleepConditionVariableCS(&g_pool->cond, &g_pool->lock, INFINITE);
#else
            pthread_cond_wait(&g_pool->cond, &g_pool->lock);
#endif
        }
        
        if (g_pool->stop && g_pool->head == NULL) {
#ifdef _WIN32
            LeaveCriticalSection(&g_pool->lock);
#else
            pthread_mutex_unlock(&g_pool->lock);
#endif
            break;
        }
        
        task_t *t = g_pool->head;
        g_pool->head = t->next;
        if (g_pool->head == NULL) g_pool->tail = NULL;
        
#ifdef _WIN32
        LeaveCriticalSection(&g_pool->lock);
#else
        pthread_mutex_unlock(&g_pool->lock);
#endif
        
        // Execute task
        t->func(t->arg);
        free(t);
    }
    return 0;
}

void tpool_init(int num_threads) {
    if (g_pool) return;
    
    g_pool = (tpool_t*)malloc(sizeof(tpool_t));
    g_pool->num_threads = num_threads;
    g_pool->head = NULL;
    g_pool->tail = NULL;
    g_pool->stop = 0;
    
#ifdef _WIN32
    InitializeCriticalSection(&g_pool->lock);
    InitializeConditionVariable(&g_pool->cond);
    
    g_pool->threads = (HANDLE*)malloc(sizeof(HANDLE) * num_threads);
    
    for (int i = 0; i < num_threads; ++i) {
        g_pool->threads[i] = CreateThread(NULL, 0, worker_thread, NULL, 0, NULL);
    }
#else
    pthread_mutex_init(&g_pool->lock, NULL);
    pthread_cond_init(&g_pool->cond, NULL);
    
    g_pool->threads = (pthread_t*)malloc(sizeof(pthread_t) * num_threads);
    
    for (int i = 0; i < num_threads; ++i) {
        pthread_create(&g_pool->threads[i], NULL, worker_thread, NULL);
    }
#endif
}

int tpool_add_work(tpool_task_func func, void *arg) {
    if (!g_pool) return -1;
    
    task_t *t = (task_t*)malloc(sizeof(task_t));
    t->func = func;
    t->arg = arg;
    t->next = NULL;
    
#ifdef _WIN32
    EnterCriticalSection(&g_pool->lock);
#else
    pthread_mutex_lock(&g_pool->lock);
#endif
    
    if (g_pool->tail) {
        g_pool->tail->next = t;
        g_pool->tail = t;
    } else {
        g_pool->head = g_pool->tail = t;
    }
    
#ifdef _WIN32
    WakeConditionVariable(&g_pool->cond);
    LeaveCriticalSection(&g_pool->lock);
#else
    pthread_cond_signal(&g_pool->cond);
    pthread_mutex_unlock(&g_pool->lock);
#endif
    
    return 0;
}

void tpool_shutdown(void) {
    if (!g_pool) return;
    
#ifdef _WIN32
    EnterCriticalSection(&g_pool->lock);
    g_pool->stop = 1;
    WakeAllConditionVariable(&g_pool->cond);
    LeaveCriticalSection(&g_pool->lock);
    
    for (int i = 0; i < g_pool->num_threads; ++i) {
        WaitForSingleObject(g_pool->threads[i], INFINITE);
        CloseHandle(g_pool->threads[i]);
    }
    
    free(g_pool->threads);
    DeleteCriticalSection(&g_pool->lock);
#else
    pthread_mutex_lock(&g_pool->lock);
    g_pool->stop = 1;
    pthread_cond_broadcast(&g_pool->cond);
    pthread_mutex_unlock(&g_pool->lock);
    
    for (int i = 0; i < g_pool->num_threads; ++i) {
        pthread_join(g_pool->threads[i], NULL);
    }
    
    free(g_pool->threads);
    pthread_mutex_destroy(&g_pool->lock);
    pthread_cond_destroy(&g_pool->cond);
#endif
    
    free(g_pool);
    g_pool = NULL;
}
