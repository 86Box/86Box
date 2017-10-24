/* Copyright holders: Sarah Walker
   see COPYING for more details
*/


#ifdef __cplusplus
extern "C" {
#endif

extern void	mouse_init(void);
extern void	mouse_close(void);
extern void	mouse_process(void);
extern void	mouse_poll_host(void);

#ifdef __cplusplus
}
#endif
