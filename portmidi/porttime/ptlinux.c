/* ptlinux.c -- portable timer implementation for linux */


/* IMPLEMENTATION NOTES (by Mark Nelson): 

Unlike Windows, Linux has no system call to request a periodic callback,
so if Pt_Start() receives a callback parameter, it must create a thread
that wakes up periodically and calls the provided callback function.
If running as superuser, use setpriority() to renice thread to -20.  
One could also set the timer thread to a real-time priority (SCHED_FIFO
and SCHED_RR), but this is dangerous for This is necessary because  
if the callback hangs it'll never return. A more serious reason
is that the current scheduler implementation busy-waits instead 
of sleeping when realtime threads request a sleep of <=2ms (as a way 
to get around the 10ms granularity), which means the thread would never 
let anyone else on the CPU.

CHANGE LOG

18-Jul-03 Roger Dannenberg -- Simplified code to set priority of timer
            thread. Simplified implementation notes. 

*/
/* stdlib, stdio, unistd, and sys/types were added because they appeared
 * in a Gentoo patch, but I'm not sure why they are needed. -RBD
 */

#define OP_HAVE_CLOCK_GETTIME  TRUE

#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <sys/types.h>
#include "porttime.h"
#include "sys/time.h"
#include "sys/resource.h"
#include "pthread.h"
#ifdef OP_HAVE_CLOCK_GETTIME
#include <time.h>
typedef struct timespec op_time;
#else
#include <syc/timeb.h>
typedef struct timeb op_time;
#endif

#define TRUE 1
#define FALSE 0

static int time_started_flag = FALSE;
static op_time time_offset;
static pthread_t pt_thread_pid;

/* note that this is static data -- we only need one copy */
typedef struct {
    int id;
    int resolution;
    PtCallback *callback;
    void *userData;
} pt_callback_parameters;

static int pt_callback_proc_id = 0;

static void *Pt_CallbackProc(void *p)
{
    pt_callback_parameters *parameters = (pt_callback_parameters *) p;
    int mytime = 1;
    /* to kill a process, just increment the pt_callback_proc_id */
    /* printf("pt_callback_proc_id %d, id %d\n", pt_callback_proc_id,
           parameters->id); */
    if (geteuid() == 0) setpriority(PRIO_PROCESS, 0, -20);
	while (pt_callback_proc_id == parameters->id) {
        /* wait for a multiple of resolution ms */
        struct timeval timeout;
        int delay = mytime++ * parameters->resolution - Pt_Time();
        if (delay < 0) delay = 0;
        timeout.tv_sec = 0;
        timeout.tv_usec = delay * 1000;
        select(0, NULL, NULL, NULL, &timeout);
        (*(parameters->callback))(Pt_Time(), parameters->userData);
    }
    /* printf("Pt_CallbackProc exiting\n"); */
    // free(parameters);
    return NULL;
}

static inline long timediff_ms(op_time now)
{
#ifdef OP_HAVE_CLOCK_GETTIME
    long seconds = now.tv_sec - time_offset.tv_sec;
    long nanoseconds = now.tv_nsec - time_offset.tv_nsec;
    return (seconds * 1000) + (nanoseconds / 1000000l);
#else
    long seconds = now.time - time_offset.time;
    long milliseconds = now.millitm - time_offset.millitm;
    return seconds * 1000 + milliseconds;
#endif
}

#ifdef OP_HAVE_CLOCK_GETTIME
inline static void timespec_gettime(op_time * const now)
{
#ifdef CLOCK_BOOTTIME
    if (clock_gettime(CLOCK_BOOTTIME, now) != 0)
#endif
#ifdef CLOCK_MONOTONIC
    if (clock_gettime(CLOCK_MONOTONIC, now) != 0)
#endif
    clock_gettime(CLOCK_REALTIME, now);          /* WARNING: ignored return value */
}
#endif

inline static void get_time(op_time * const now)
{
#ifdef OP_HAVE_CLOCK_GETTIME
    timespec_gettime(now);
#else
    ftime(now);
#endif
}

PtError Pt_Start(int resolution, PtCallback *callback, void *userData)
{
    if (time_started_flag) return ptNoError;
    get_time(&time_offset); /* need this set before process runs */
    if (callback) {
        int res;
        pt_callback_parameters *parms = (pt_callback_parameters *) 
            malloc(sizeof(pt_callback_parameters));
        if (!parms) return ptInsufficientMemory;
        parms->id = pt_callback_proc_id;
        parms->resolution = resolution;
        parms->callback = callback;
        parms->userData = userData;
        res = pthread_create(&pt_thread_pid, NULL, 
                             Pt_CallbackProc, parms);
        if (res != 0) return ptHostError;
    }
    time_started_flag = TRUE;
    return ptNoError;
}


PtError Pt_Stop()
{
    /* printf("Pt_Stop called\n"); */
    pt_callback_proc_id++;
    pthread_join(pt_thread_pid, NULL);
    time_started_flag = FALSE;
    return ptNoError;
}


int Pt_Started()
{
    return time_started_flag;
}


PtTimestamp Pt_Time()
{
    op_time now;
    get_time(&now);
    return timediff_ms(now);
}


void Pt_Sleep(long duration)
{
    usleep(duration * 1000);
}



