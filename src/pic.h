extern void pic_init(void);
extern void pic2_init(void);
extern void pic_reset(void);

extern void picint(uint16_t num);
extern void picintlevel(uint16_t num);
extern void picintc(uint16_t num);
extern uint8_t picinterrupt(void);
extern void picclear(int num);
extern void dumppic(void);
