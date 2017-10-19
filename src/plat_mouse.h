/* Copyright holders: Sarah Walker
   see COPYING for more details
*/


extern int	mousecapture;
extern int	mouse_buttons;


#ifdef __cplusplus
extern "C" {
#endif

extern void	mouse_init(void);
extern void	mouse_close(void);
extern void	mouse_process(void);
extern void	mouse_poll_host(void);
extern void	mouse_get_mickeys(int *x, int *y, int *z);

#ifdef __cplusplus
}
#endif
