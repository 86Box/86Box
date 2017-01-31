/* Copyright holders: The DOSBox Team, SA1988
   see COPYING for more details
*/
#ifdef __cplusplus
extern "C" {
#endif
        void opl_init(void (*timer_callback)(void *param, int timer, int64_t period), void *timer_param, int nr, int is_opl3);
        void opl_write(int nr, uint16_t addr, uint8_t val);
        uint8_t opl_read(int nr, uint16_t addr);
        void opl_timer_over(int nr, int timer);
        void opl2_update(int nr, int16_t *buffer, int samples);
        void opl3_update(int nr, int16_t *buffer, int samples);

	extern int opl3_type;
#ifdef __cplusplus
}
#endif
