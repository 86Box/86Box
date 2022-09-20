#ifdef __cplusplus
extern "C" {
#endif

#ifdef __APPLE__
#    define thread_t                            plat_thread_t
#    define event_t                             plat_event_t
#    define mutex_t                             plat_mutex_t

#    define thread_create                       plat_thread_create
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

extern thread_t *thread_create(void (*thread_func)(void *param), void *param);
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
