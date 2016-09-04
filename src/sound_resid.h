#ifdef __cplusplus
extern "C" {
#endif
        void *sid_init();
        void sid_close(void *p);
        void sid_reset(void *p);
        uint8_t sid_read(uint16_t addr, void *p);
        void sid_write(uint16_t addr, uint8_t val, void *p);
        void sid_fillbuf(int16_t *buf, int len, void *p);
#ifdef __cplusplus
}
#endif
