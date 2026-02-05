#ifndef TPOOL_H
#define TPOOL_H

// Task function signature
typedef void (*tpool_task_func)(void *arg);

// Initialize thread pool with N threads
void tpool_init(int num_threads);

// Submit a task to the pool
// Returns 0 on success, -1 on error
int tpool_add_work(tpool_task_func func, void *arg);

// Shutdown the pool (wait for remaining tasks?)
// For MVP, likely just a cleanup function.
void tpool_shutdown(void);

#endif
