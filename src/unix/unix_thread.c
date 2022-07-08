#include <stdlib.h>
#include <stdio.h>
#include <time.h>
#include <pthread.h>
#include <inttypes.h>
#include <86box/86box.h>
#include <86box/plat.h>
#include <86box/thread.h>


typedef struct event_pthread_t
{
    pthread_cond_t	cond;
    pthread_mutex_t	mutex;
    int state;
} event_pthread_t;


typedef struct thread_param
{
    void		(*thread_rout)(void*);
    void *		param;
} thread_param;


typedef struct pt_mutex_t
{
    pthread_mutex_t	mutex;
} pt_mutex_t;


void *
thread_run_wrapper(thread_param* arg)
{
    thread_param localparam = *arg;
    free(arg);
    localparam.thread_rout(localparam.param);
    return NULL;
}


thread_t *
thread_create(void (*thread_rout)(void *param), void *param)
{
    pthread_t *thread = malloc(sizeof(pthread_t));
    thread_param *thrparam = malloc(sizeof(thread_param));
    thrparam->thread_rout = thread_rout;
    thrparam->param = param;

    pthread_create(thread, NULL, (void* (*)(void*))thread_run_wrapper, thrparam);

    return thread;
}


int
thread_wait(thread_t *arg)
{
    return pthread_join(*(pthread_t*)(arg), NULL);
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

    clock_gettime(CLOCK_REALTIME, &abstime);
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


int
thread_wait_mutex(mutex_t *_mutex)
{
    if (_mutex == NULL)
	return(0);
    pt_mutex_t *mutex = (pt_mutex_t *)_mutex;

    return
	pthread_mutex_lock(&mutex->mutex) != 0;
}


int
thread_test_mutex(mutex_t *_mutex)
{
    if (_mutex == NULL)
	return(0);
    pt_mutex_t *mutex = (pt_mutex_t *)_mutex;

    return
	pthread_mutex_trylock(&mutex->mutex) != 0;
}


int
thread_release_mutex(mutex_t *_mutex)
{
    if (_mutex == NULL)
	return(0);
    pt_mutex_t *mutex = (pt_mutex_t *)_mutex;

    return pthread_mutex_unlock(&mutex->mutex) != 0;
}


void
thread_close_mutex(mutex_t *_mutex)
{
    pt_mutex_t *mutex = (pt_mutex_t *)_mutex;

    pthread_mutex_destroy(&mutex->mutex);

    free(mutex);
}
