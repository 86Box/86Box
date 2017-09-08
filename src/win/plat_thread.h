/* Copyright holders: Sarah Walker
   see COPYING for more details
*/
#ifndef PLAT_THREAD_H
# define PLAT_THREAD_H


typedef void thread_t;
typedef void event_t;


extern thread_t	*thread_create(void (*thread_rout)(void *param), void *param);
extern void	thread_kill(thread_t *handle);

extern event_t	*thread_create_event(void);
extern void	thread_set_event(event_t *event);
extern void	thread_reset_event(event_t *_event);
extern int	thread_wait_event(event_t *event, int timeout);
extern void	thread_destroy_event(event_t *_event);

extern void	thread_sleep(int t);

extern void	*thread_create_mutex(wchar_t *name);
extern void	thread_close_mutex(void *mutex);
extern uint8_t	thread_wait_mutex(void *mutex);
extern uint8_t	thread_release_mutex(void *mutex);

extern void	startslirp(void);
extern void	endslirp(void);


#endif	/*PLAT_THREAD_H*/
