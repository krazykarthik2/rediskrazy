/*
 * ae_poll.c - poll() based I/O multiplexing backend
 * 
 * Available on POSIX systems and Windows (via WSAPoll).
 * Better than select() for large numbers of connections as it doesn't
 * have a fixed FD_SETSIZE limit.
 */

#ifdef _WIN32
#include <winsock2.h>
#define poll WSAPoll
typedef WSAPOLLFD pollfd_t;
#else
#include <poll.h>
typedef struct pollfd pollfd_t;
#endif

#include <stdlib.h>
#include "ae.h"

typedef struct aeApiState {
    pollfd_t *pfds;      // Array of pollfd structures
    int *fd_to_idx;      // Maps fd -> index in pfds
    int nfds;            // Number of fds currently registered
    int capacity;        // Capacity of pfds array
} aeApiState;

static int aeApiCreate(aeEventLoop *eventLoop) {
    aeApiState *state = malloc(sizeof(aeApiState));
    if (!state) return -1;
    
    state->capacity = eventLoop->setsize;
    state->pfds = malloc(sizeof(pollfd_t) * state->capacity);
    state->fd_to_idx = malloc(sizeof(int) * eventLoop->setsize);
    
    if (!state->pfds || !state->fd_to_idx) {
        if (state->pfds) free(state->pfds);
        if (state->fd_to_idx) free(state->fd_to_idx);
        free(state);
        return -1;
    }
    
    state->nfds = 0;
    for (int i = 0; i < eventLoop->setsize; i++)
        state->fd_to_idx[i] = -1;
        
    eventLoop->apidata = state;
    return 0;
}

static int aeApiResize(aeEventLoop *eventLoop, int setsize) {
    aeApiState *state = eventLoop->apidata;
    
    pollfd_t *new_pfds = realloc(state->pfds, sizeof(pollfd_t) * setsize);
    int *new_idx = realloc(state->fd_to_idx, sizeof(int) * setsize);
    
    if (!new_pfds || !new_idx) return -1;
    
    state->pfds = new_pfds;
    state->fd_to_idx = new_idx;
    state->capacity = setsize;
    
    for (int i = eventLoop->setsize; i < setsize; i++)
        state->fd_to_idx[i] = -1;
        
    return 0;
}

static void aeApiFree(aeEventLoop *eventLoop) {
    aeApiState *state = eventLoop->apidata;
    free(state->pfds);
    free(state->fd_to_idx);
    free(state);
}

static int aeApiAddEvent(aeEventLoop *eventLoop, int fd, int mask) {
    aeApiState *state = eventLoop->apidata;
    int idx = state->fd_to_idx[fd];
    
    if (idx == -1) {
        // New fd
        if (state->nfds >= state->capacity) return -1;
        idx = state->nfds++;
        state->fd_to_idx[fd] = idx;
        state->pfds[idx].fd = fd;
        state->pfds[idx].events = 0;
        state->pfds[idx].revents = 0;
    }
    
    if (mask & AE_READABLE) state->pfds[idx].events |= POLLIN;
    if (mask & AE_WRITABLE) state->pfds[idx].events |= POLLOUT;
    
    return 0;
}

static void aeApiDelEvent(aeEventLoop *eventLoop, int fd, int mask) {
    aeApiState *state = eventLoop->apidata;
    int idx = state->fd_to_idx[fd];
    
    if (idx == -1) return;
    
    if (mask & AE_READABLE) state->pfds[idx].events &= ~POLLIN;
    if (mask & AE_WRITABLE) state->pfds[idx].events &= ~POLLOUT;
    
    // If no events left, remove from array
    if (state->pfds[idx].events == 0) {
        int last = state->nfds - 1;
        if (idx != last) {
            // Swap with last element
            state->pfds[idx] = state->pfds[last];
            state->fd_to_idx[state->pfds[idx].fd] = idx;
        }
        state->fd_to_idx[fd] = -1;
        state->nfds--;
    }
}

static int aeApiPoll(aeEventLoop *eventLoop, struct timeval *tvp) {
    aeApiState *state = eventLoop->apidata;
    int retval, numevents = 0;
    
    int timeout_ms = tvp ? (int)(tvp->tv_sec * 1000 + tvp->tv_usec / 1000) : -1;
    
    retval = poll(state->pfds, state->nfds, timeout_ms);
    
    if (retval > 0) {
        for (int i = 0; i < state->nfds; i++) {
            int mask = 0;
            pollfd_t *pfd = &state->pfds[i];
            
            if (pfd->revents & POLLIN) mask |= AE_READABLE;
            if (pfd->revents & POLLOUT) mask |= AE_WRITABLE;
            if (pfd->revents & (POLLERR | POLLHUP)) mask |= AE_READABLE | AE_WRITABLE;
            
            if (mask) {
                eventLoop->fired[numevents].fd = pfd->fd;
                eventLoop->fired[numevents].mask = mask;
                numevents++;
            }
        }
    }
    return numevents;
}

static char *aeApiName(void) {
    return "poll";
}
