/*
 * 86Box    A hypervisor and IBM PC system emulator that specializes in
 *          running old operating systems and software designed for IBM
 *          PC systems and compatibles from 1981 through fairly recent
 *          system designs based on the PCI bus.
 *
 *          This file is part of the 86Box distribution.
 *
 *          Thread API header.
 *
 * Authors: Sarah Walker, <https://pcem-emulator.co.uk/>
 *          Miran Grca, <mgrca8@gmail.com>
 *
 *          Copyright 2008-2023 Sarah Walker.
 *          Copyright 2016-2023 Miran Grca.
 */
#ifndef THREAD_H
#    define THREAD_H

#ifdef __cplusplus
extern "C" {
#endif

#ifdef __APPLE__
#    define thread_t                            plat_thread_t
#    define event_t                             plat_event_t
#    define mutex_t                             plat_mutex_t

#    define thread_create_named                 plat_thread_create_named
#    define thread_wait                         plat_thread_wait
#    define thread_create_event                 plat_thread_create_event
#    define thread_set_event                    plat_thread_set_event
#    define thread_reset_event                  plat_thread_reset_event
#    define thread_wait_event                   plat_thread_wait_event
#    define thread_destroy_event                plat_thread_destroy_event

#    define thread_create_mutex                 plat_thread_create_mutex
#    define thread_create_mutex_with_spin_count plat_thread_create_mutex_with_spin_count
#    define thread_close_mutex                  plat_thread_close_mutex
#    define thread_wait_mutex                   plat_thread_wait_mutex
#    define thread_release_mutex                plat_thread_release_mutex
#endif

/* Thread support. */
typedef void thread_t;
typedef void event_t;
typedef void mutex_t;

#define thread_create(thread_func, param) thread_create_named((thread_func), (param), #thread_func)
extern thread_t *thread_create_named(void (*thread_func)(void *param), void *param, const char *name);
extern int       thread_wait(thread_t *arg);
extern event_t  *thread_create_event(void);
extern void      thread_set_event(event_t *arg);
extern void      thread_reset_event(event_t *arg);
extern int       thread_wait_event(event_t *arg, int timeout);
extern void      thread_destroy_event(event_t *arg);

extern mutex_t *thread_create_mutex(void);
extern void     thread_close_mutex(mutex_t *arg);
extern int      thread_test_mutex(mutex_t *arg);
extern int      thread_wait_mutex(mutex_t *arg);
extern int      thread_release_mutex(mutex_t *mutex);

#ifdef __cplusplus
}
#endif

#endif /*THREAD_H*/
