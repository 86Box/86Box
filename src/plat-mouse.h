#ifdef __cplusplus
extern "C" {
#endif

        void mouse_init();
        void mouse_close();
        extern int mouse_buttons;
        void mouse_poll_host();
        void mouse_get_mickeys(int *x, int *y);
	extern int mousecapture;
#ifdef __cplusplus
}
#endif
