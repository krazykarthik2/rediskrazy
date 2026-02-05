#include <windows.h>
#include <process.h>
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
    HANDLE *threads;
    int num_threads;
    task_t *head;
    task_t *tail;
    CRITICAL_SECTION lock;
    CONDITION_VARIABLE cond;
    int stop;
} tpool_t;

static tpool_t *g_pool = NULL;

// Worker thread function
static DWORD WINAPI worker_thread(LPVOID arg) {
    (void)arg;
    while (1) {
        EnterCriticalSection(&g_pool->lock);
        
        while (g_pool->head == NULL && !g_pool->stop) {
            SleepConditionVariableCS(&g_pool->cond, &g_pool->lock, INFINITE);
        }
        
        if (g_pool->stop && g_pool->head == NULL) {
            LeaveCriticalSection(&g_pool->lock);
            break;
        }
        
        task_t *t = g_pool->head;
        g_pool->head = t->next;
        if (g_pool->head == NULL) g_pool->tail = NULL;
        
        LeaveCriticalSection(&g_pool->lock);
        
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
    
    InitializeCriticalSection(&g_pool->lock);
    InitializeConditionVariable(&g_pool->cond);
    
    g_pool->threads = (HANDLE*)malloc(sizeof(HANDLE) * num_threads);
    
    for (int i = 0; i < num_threads; ++i) {
        g_pool->threads[i] = CreateThread(NULL, 0, worker_thread, NULL, 0, NULL);
    }
}

int tpool_add_work(tpool_task_func func, void *arg) {
    if (!g_pool) return -1;
    
    task_t *t = (task_t*)malloc(sizeof(task_t));
    t->func = func;
    t->arg = arg;
    t->next = NULL;
    
    EnterCriticalSection(&g_pool->lock);
    
    if (g_pool->tail) {
        g_pool->tail->next = t;
        g_pool->tail = t;
    } else {
        g_pool->head = g_pool->tail = t;
    }
    
    WakeConditionVariable(&g_pool->cond);
    LeaveCriticalSection(&g_pool->lock);
    
    return 0;
}

void tpool_shutdown(void) {
    if (!g_pool) return;
    
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
    free(g_pool);
    g_pool = NULL;
}
