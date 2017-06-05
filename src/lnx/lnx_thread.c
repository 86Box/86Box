/* Copyright holders: Sarah Walker
   see COPYING for more details
*/
#include <stdlib.h>
#include <stdio.h>
#include <time.h>
#include <pthread.h>
#include "plat_thread.h"


typedef struct {
    pthread_cond_t cond;
    pthread_mutex_t mutex;
} event_pthread_t;


thread_t *thread_create(void (*thread_rout)(void *param), void *param)
{
    pthread_t *thread = malloc(sizeof(pthread_t));

    if (thread != NULL)
	pthread_create(thread, NULL, thread_rout, param);

    return(thread);
}


void thread_kill(thread_t *handle)
{
    pthread_t *thread = (pthread_t *)handle;

    if (thread != NULL) {
	pthread_cancel(*thread);
	pthread_join(*thread, NULL);

	free(thread);
    }
}


event_t *thread_create_event(void)
{
    event_pthread_t *event = malloc(sizeof(event_pthread_t));

    if (event != NULL) {
	pthread_cond_init(&event->cond, NULL);
	pthread_mutex_init(&event->mutex, NULL);
    }

    return((event_t *)event);
}


void thread_set_event(event_t *handle)
{
    event_pthread_t *event = (event_pthread_t *)handle;

    if (event != NULL) {
	pthread_mutex_lock(&event->mutex);
	pthread_cond_broadcast(&event->cond);
	pthread_mutex_unlock(&event->mutex);
    }
}


void thread_reset_event(event_t *handle)
{
}


int thread_wait_event(event_t *handle, int timeout)
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
    pthread_cond_timedwait(&event->cond, &event->mutex, &abstime);
    pthread_mutex_unlock(&event->mutex);

    return(0);
}


void thread_destroy_event(event_t *handle)
{
    event_pthread_t *event = (event_pthread_t *)handle;

    if (event != NULL) {
	pthread_cond_destroy(&event->cond);
	pthread_mutex_destroy(&event->mutex);

	free(event);
    }
}


void thread_sleep(int t)
{
    usleep(t * 1000);
}
