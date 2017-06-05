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


#endif	/*PLAT_THREAD_H*/
