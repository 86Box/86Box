#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#ifdef __APPLE__
#include <sys/time.h>
#endif
#include <86box/86box.h>
#include <86box/plat.h>
#if (defined WIN32) || (defined _WIN32) || (defined _WIN32)
#include <windows.h>
#include <process.h>

typedef struct {
    HANDLE handle;
} win_event_t;


thread_t *
thread_create(void (*func)(void *param), void *param)
{
    uintptr_t bt = _beginthread(func, 0, param);
    return((thread_t *)bt);
}


int
thread_wait(thread_t *arg, int timeout)
{
    if (arg == NULL) return(0);

    if (timeout == -1)
	timeout = INFINITE;

    if (WaitForSingleObject(arg, timeout)) return(1);

    return(0);
}


event_t *
thread_create_event(void)
{
    win_event_t *ev = malloc(sizeof(win_event_t));

    ev->handle = CreateEvent(NULL, FALSE, FALSE, NULL);

    return((event_t *)ev);
}


void
thread_set_event(event_t *arg)
{
    win_event_t *ev = (win_event_t *)arg;

    if (arg == NULL) return;

    SetEvent(ev->handle);
}


void
thread_reset_event(event_t *arg)
{
    win_event_t *ev = (win_event_t *)arg;

    if (arg == NULL) return;

    ResetEvent(ev->handle);
}


int
thread_wait_event(event_t *arg, int timeout)
{
    win_event_t *ev = (win_event_t *)arg;

    if (arg == NULL) return(0);

    if (ev->handle == NULL) return(0);

    if (timeout == -1)
	timeout = INFINITE;

    if (WaitForSingleObject(ev->handle, timeout)) return(1);

    return(0);
}


void
thread_destroy_event(event_t *arg)
{
    win_event_t *ev = (win_event_t *)arg;

    if (arg == NULL) return;

    CloseHandle(ev->handle);

    free(ev);
}


mutex_t *
thread_create_mutex(void)
{    
    mutex_t *mutex = malloc(sizeof(CRITICAL_SECTION));

    InitializeCriticalSection(mutex);

    return mutex;
}


mutex_t *
thread_create_mutex_with_spin_count(unsigned int spin_count)
{
    mutex_t *mutex = malloc(sizeof(CRITICAL_SECTION));

    InitializeCriticalSectionAndSpinCount(mutex, spin_count);

    return mutex;
}


int
thread_wait_mutex(mutex_t *mutex)
{
    if (mutex == NULL) return(0);

    LPCRITICAL_SECTION critsec = (LPCRITICAL_SECTION)mutex;

    EnterCriticalSection(critsec);

    return 1;
}


int
thread_release_mutex(mutex_t *mutex)
{
    if (mutex == NULL) return(0);

    LPCRITICAL_SECTION critsec = (LPCRITICAL_SECTION)mutex;

    LeaveCriticalSection(critsec);

    return 1;
}


void
thread_close_mutex(mutex_t *mutex)
{
    if (mutex == NULL) return;

    LPCRITICAL_SECTION critsec = (LPCRITICAL_SECTION)mutex;

    DeleteCriticalSection(critsec);

    free(critsec);
}
#else
#include <pthread.h>
#include <unistd.h>


typedef struct event_pthread_t
{
    pthread_cond_t	cond;
    pthread_mutex_t	mutex;
    int			state;
} event_pthread_t;


typedef struct pt_mutex_t
{
    pthread_mutex_t	mutex;
} pt_mutex_t;


thread_t *
thread_create(void (*thread_rout)(void *param), void *param)
{
    pthread_t *thread = malloc(sizeof(pthread_t));

    pthread_create(thread, NULL, (void*)thread_rout, param);

    return thread;
}


int
thread_wait(thread_t *arg, int timeout)
{
    return pthread_join(*(pthread_t*)(arg), NULL) != 0;
}


event_t *
thread_create_event()
{
    event_pthread_t *event = malloc(sizeof(event_pthread_t));

    pthread_cond_init(&event->cond, NULL);
    pthread_mutex_init(&event->mutex, NULL);
    event->state = 0;

    return (event_t *)event;
}


void
thread_set_event(event_t *handle)
{
    event_pthread_t *event = (event_pthread_t *)handle;

    pthread_mutex_lock(&event->mutex);
    event->state = 1;
    pthread_cond_broadcast(&event->cond);
    pthread_mutex_unlock(&event->mutex);
}


void
thread_reset_event(event_t *handle)
{
    event_pthread_t *event = (event_pthread_t *)handle;

    pthread_mutex_lock(&event->mutex);
    event->state = 0;
    pthread_mutex_unlock(&event->mutex);
}


int
thread_wait_event(event_t *handle, int timeout)
{
    event_pthread_t *event = (event_pthread_t *)handle;
    struct timespec abstime;

#ifdef __linux__
    clock_gettime(CLOCK_REALTIME, &abstime);
#else
    struct timeval now;
    gettimeofday(&now, 0);
    abstime.tv_sec = now.tv_sec;
    abstime.tv_nsec = now.tv_usec*1000UL;
#endif
    abstime.tv_nsec += (timeout % 1000) * 1000000;
    abstime.tv_sec += (timeout / 1000);
    if (abstime.tv_nsec > 1000000000) {
	abstime.tv_nsec -= 1000000000;
	abstime.tv_sec++;
    }

    pthread_mutex_lock(&event->mutex);
    if (timeout == -1) {
	while (!event->state)
		pthread_cond_wait(&event->cond, &event->mutex);
    } else if (!event->state)
	pthread_cond_timedwait(&event->cond, &event->mutex, &abstime);
    pthread_mutex_unlock(&event->mutex);

    return 0;
}


void
thread_destroy_event(event_t *handle)
{
    event_pthread_t *event = (event_pthread_t *)handle;

    pthread_cond_destroy(&event->cond);
    pthread_mutex_destroy(&event->mutex);

    free(event);
}


mutex_t *
thread_create_mutex(void)
{
    pt_mutex_t *mutex = malloc(sizeof(pt_mutex_t));

    pthread_mutex_init(&mutex->mutex, NULL);

    return mutex;
}


mutex_t *
thread_create_mutex_with_spin_count(unsigned int spin_count)
{
    /* Setting spin count of a mutex is not possible with pthreads. */
    return thread_create_mutex();
}


int
thread_wait_mutex(mutex_t *_mutex)
{
    pt_mutex_t *mutex = (pt_mutex_t *)_mutex;

    pthread_mutex_lock(&mutex->mutex);

    return 1;
}


int
thread_release_mutex(mutex_t *_mutex)
{
    pt_mutex_t *mutex = (pt_mutex_t *)_mutex;

    pthread_mutex_unlock(&mutex->mutex);

    return 1;
}


void
thread_close_mutex(mutex_t *_mutex)
{
    pt_mutex_t *mutex = (pt_mutex_t *)_mutex;

    pthread_mutex_destroy(&mutex->mutex);

    free(mutex);
}
#endif
