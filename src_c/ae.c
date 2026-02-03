#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <errno.h>

#ifdef _WIN32
#include <winsock2.h>
#else
#include <sys/time.h>
#include <sys/types.h>
#include <unistd.h>
#endif

#include "ae.h"

// Helper to get current time in ms
static void aeGetTime(long *seconds, long *milliseconds) {
#ifdef _WIN32
    SYSTEMTIME st;
    GetSystemTime(&st);
    *seconds = time(NULL); // Approximate sync
    *milliseconds = st.wMilliseconds;
#else
    struct timeval tv;
    gettimeofday(&tv, NULL);
    *seconds = tv.tv_sec;
    *milliseconds = tv.tv_usec/1000;
#endif
}

static void aeAddMillisecondsToNow(long long milliseconds, long *sec, long *ms) {
    long cur_sec, cur_ms, when_sec, when_ms;
    aeGetTime(&cur_sec, &cur_ms);
    when_sec = cur_sec + milliseconds/1000;
    when_ms = cur_ms + milliseconds%1000;
    if (when_ms >= 1000) {
        when_sec ++;
        when_ms -= 1000;
    }
    *sec = when_sec;
    *ms = when_ms;
}

aeEventLoop *aeCreateEventLoop(int setsize) {
    aeEventLoop *eventLoop;
    if ((eventLoop = malloc(sizeof(*eventLoop))) == NULL) return NULL;
    
    eventLoop->events = malloc(sizeof(aeFileEvent) * setsize);
    eventLoop->fired = malloc(sizeof(aeFiredEvent) * setsize);
    if (!eventLoop->events || !eventLoop->fired) {
        if (eventLoop->events) free(eventLoop->events);
        if (eventLoop->fired) free(eventLoop->fired);
        free(eventLoop);
        return NULL;
    }
    
    eventLoop->setsize = setsize;
    eventLoop->timeEventNextId = 0;
    eventLoop->timeEventHead = NULL;
    eventLoop->stop = 0;
    eventLoop->maxfd = -1;
    eventLoop->apidata = NULL;
    
    for (int i = 0; i < setsize; i++)
        eventLoop->events[i].mask = AE_NONE;
        
    return eventLoop;
}

void aeDeleteEventLoop(aeEventLoop *eventLoop) {
    free(eventLoop->events);
    free(eventLoop->fired);
    free(eventLoop);
}

void aeStop(aeEventLoop *eventLoop) {
    eventLoop->stop = 1;
}

int aeCreateFileEvent(aeEventLoop *eventLoop, int fd, int mask,
        aeFileProc *proc, void *clientData)
{
    if (fd >= eventLoop->setsize) return AE_ERR;
    aeFileEvent *fe = &eventLoop->events[fd];
    
    if (mask & AE_READABLE) fe->rfileProc = proc;
    if (mask & AE_WRITABLE) fe->wfileProc = proc;
    fe->clientData = clientData;
    fe->mask |= mask;
    
    if (fd > eventLoop->maxfd)
        eventLoop->maxfd = fd;
        
    return AE_OK;
}

void aeDeleteFileEvent(aeEventLoop *eventLoop, int fd, int mask) {
    if (fd >= eventLoop->setsize) return;
    aeFileEvent *fe = &eventLoop->events[fd];
    if (fe->mask == AE_NONE) return;
    
    fe->mask = fe->mask & (~mask);
    if (fd == eventLoop->maxfd && fe->mask == AE_NONE) {
        /* Update the max fd */
        int j;
        for (j = eventLoop->maxfd-1; j >= 0; j--)
            if (eventLoop->events[j].mask != AE_NONE) break;
        eventLoop->maxfd = j;
    }
}

int aeGetFileEvents(aeEventLoop *eventLoop, int fd) {
    if (fd >= eventLoop->setsize) return 0;
    return eventLoop->events[fd].mask;
}

long long aeCreateTimeEvent(aeEventLoop *eventLoop, long long milliseconds,
        aeTimeProc *proc, void *clientData,
        aeEventFinalizerProc *finalizerProc)
{
    long long id = eventLoop->timeEventNextId++;
    aeTimeEvent *te;
    
    te = malloc(sizeof(*te));
    if (te == NULL) return AE_ERR;
    
    te->id = id;
    aeAddMillisecondsToNow(milliseconds, &te->when_sec, &te->when_ms);
    te->timeProc = proc;
    te->finalizerProc = finalizerProc;
    te->clientData = clientData;
    te->next = eventLoop->timeEventHead;
    eventLoop->timeEventHead = te;
    
    return id;
}

int aeDeleteTimeEvent(aeEventLoop *eventLoop, long long id) {
    aeTimeEvent *te = eventLoop->timeEventHead;
    aeTimeEvent *prev = NULL;
    while(te) {
        if (te->id == id) {
            if (prev == NULL)
                eventLoop->timeEventHead = te->next;
            else
                prev->next = te->next;
            if (te->finalizerProc)
                te->finalizerProc(eventLoop, te->clientData);
            free(te);
            return AE_OK;
        }
        prev = te;
        te = te->next;
    }
    return AE_ERR;
}

static aeTimeEvent *aeSearchNearestTimer(aeEventLoop *eventLoop) {
    aeTimeEvent *te = eventLoop->timeEventHead;
    aeTimeEvent *nearest = NULL;
    
    while(te) {
        if (!nearest || te->when_sec < nearest->when_sec ||
                (te->when_sec == nearest->when_sec &&
                 te->when_ms < nearest->when_ms))
            nearest = te;
        te = te->next;
    }
    return nearest;
}

static int processTimeEvents(aeEventLoop *eventLoop) {
    int processed = 0;
    aeTimeEvent *te;
    long long maxId;
    long now_sec, now_ms;
    
    te = eventLoop->timeEventHead;
    maxId = eventLoop->timeEventNextId - 1;
    
    aeGetTime(&now_sec, &now_ms);
    
    while(te) {
        long long id;
        
        // Skip removed events (if we support deletion while iterating? Not implemented safe here yet)
        // Simplified approach: iterate and process one if fired, or standard impl.
        
        if (te->id > maxId) {
            te = te->next;
            continue;
        }
        
        if (now_sec > te->when_sec ||
            (now_sec == te->when_sec && now_ms >= te->when_ms))
        {
            int retval;
            
            id = te->id;
            retval = te->timeProc(eventLoop, id, te->clientData);
            processed++;
            
            if (retval != AE_NOMORE) {
                aeAddMillisecondsToNow(retval, &te->when_sec, &te->when_ms);
            } else {
                aeDeleteTimeEvent(eventLoop, id);
            }
            
            // Restart iteration as list might change because of callbacks
            // Optimized Redis approach is slightly different but this is safe MVP
            te = eventLoop->timeEventHead;
            // Update time used for checks
            aeGetTime(&now_sec, &now_ms);
        } else {
            te = te->next;
        }
    }
    return processed;
}

int aeProcessEvents(aeEventLoop *eventLoop, int flags) {
    int processed = 0, numevents;
    
    /* Nothing to do? return */
    if (!(flags & AE_TIME_EVENTS) && !(flags & AE_FILE_EVENTS)) return 0;
    
    /* Note that we want to scan for time events even if AE_TIME_EVENTS
     * is not set if AE_DONT_WAIT is set, because we want to run
     * existing timers if they are ready to fire. */

    struct timeval tv, *tvp;
    
    if (flags & AE_TIME_EVENTS && !(flags & AE_DONT_WAIT)) {
        aeTimeEvent *shortest = aeSearchNearestTimer(eventLoop);
        if (shortest) {
            long now_sec, now_ms;
            aeGetTime(&now_sec, &now_ms);
            tvp = &tv;
            
            long long ms = (shortest->when_sec - now_sec)*1000 +
                (shortest->when_ms - now_ms);
            
            if (ms > 0) {
                tv.tv_sec = ms/1000;
                tv.tv_usec = (ms%1000)*1000;
            } else {
                tv.tv_sec = 0;
                tv.tv_usec = 0;
            }
        } else {
            /* If no timers, we can wait forever unless AE_DONT_WAIT */
            if (flags & AE_DONT_WAIT) {
                tv.tv_sec = 0;
                tv.tv_usec = 0;
                tvp = &tv;
            } else {
                tvp = NULL; /* wait forever */
            }
        }
    } else {
        tv.tv_sec = 0;
        tv.tv_usec = 0;
        tvp = &tv;
    }
    
    // Select
    fd_set rfds, wfds;
    FD_ZERO(&rfds);
    FD_ZERO(&wfds);
    
    // Populate sets
    int j;
    if (eventLoop->maxfd != -1) {
        for (j = 0; j <= eventLoop->maxfd; j++) {
            if (eventLoop->events[j].mask == AE_NONE) continue;
            if (eventLoop->events[j].mask & AE_READABLE) FD_SET(j, &rfds);
            if (eventLoop->events[j].mask & AE_WRITABLE) FD_SET(j, &wfds);
        }
    }
    
    numevents = select(eventLoop->maxfd+1, &rfds, &wfds, NULL, tvp);
    
    if (numevents > 0) {
        for (j = 0; j <= eventLoop->maxfd; j++) {
            int mask = 0;
            aeFileEvent *fe = &eventLoop->events[j];
            if (fe->mask == AE_NONE) continue;
            
            if (fe->mask & AE_READABLE && FD_ISSET(j, &rfds))
                mask |= AE_READABLE;
            if (fe->mask & AE_WRITABLE && FD_ISSET(j, &wfds))
                mask |= AE_WRITABLE;
            
            if (mask != 0) { // Fire
                int rfired = 0;
                if (fe->mask & mask & AE_READABLE) {
                    rfired = 1;
                    fe->rfileProc(eventLoop, j, fe->clientData, mask);
                }
                if (fe->mask & mask & AE_WRITABLE) {
                    if (!rfired || fe->wfileProc != fe->rfileProc)
                        fe->wfileProc(eventLoop, j, fe->clientData, mask);
                }
                processed++;
            }
        }
    }
    
    /* Process time events */
    if (flags & AE_TIME_EVENTS)
        processed += processTimeEvents(eventLoop);
        
    return processed;
}

void aeMain(aeEventLoop *eventLoop) {
    eventLoop->stop = 0;
    while (!eventLoop->stop) {
        aeProcessEvents(eventLoop, AE_ALL_EVENTS);
    }
}
