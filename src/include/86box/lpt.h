#ifndef EMU_LPT_H
#define EMU_LPT_H

#define LPT1_ADDR 0x0378
#define LPT1_IRQ  7
#define LPT2_ADDR 0x0278
#define LPT2_IRQ  5
// LPT 1 on machines when installed
#define LPT_MDA_ADDR 0x03bc
#define LPT_MDA_IRQ  7
#define LPT4_ADDR    0x0268
#define LPT4_IRQ     5
#if 0
#define LPT5_ADDR 0x027c
#define LPT5_IRQ  7
#define LPT6_ADDR 0x026c
#define LPT6_IRQ  5
#endif

typedef struct lpt_device_t {
    const char *name;
    const char *internal_name;

    void   *(*init)(void *lpt);
    void    (*close)(void *priv);
    void    (*write_data)(uint8_t val, void *priv);
    void    (*write_ctrl)(uint8_t val, void *priv);
    void    (*autofeed)(uint8_t val,void *priv);
    void    (*strobe)(uint8_t old, uint8_t val,void *priv);
    uint8_t (*read_status)(void *priv);
    uint8_t (*read_ctrl)(void *priv);
    void    (*epp_write_data)(uint8_t is_addr, uint8_t val, void *priv);
    void    (*epp_request_read)(uint8_t is_addr, void *priv);
} lpt_device_t;

extern void lpt_set_ext(int port, uint8_t ext);
extern void lpt_set_ecp(int port, uint8_t ecp);
extern void lpt_set_epp(int port, uint8_t epp);
extern void lpt_set_lv2(int port, uint8_t lv2);
extern void lpt_reset(void);
extern void lpt_close(void);
extern void lpt_init(void);
extern void lpt_port_setup(int i, uint16_t port);
extern void lpt_port_irq(int i, uint8_t irq);
extern void lpt_port_dma(int i, uint8_t dma);
extern void lpt_port_remove(int i);
extern void lpt1_remove_ams(void);

#define lpt1_setup(a) lpt_port_setup(0, a)
#define lpt1_irq(a)   lpt_port_irq(0, a)
#define lpt1_remove() lpt_port_remove(0)

#define lpt2_setup(a) lpt_port_setup(1, a)
#define lpt2_irq(a)   lpt_port_irq(1, a)
#define lpt2_remove() lpt_port_remove(1)

#define lpt3_setup(a) lpt_port_setup(2, a)
#define lpt3_irq(a)   lpt_port_irq(2, a)
#define lpt3_remove() lpt_port_remove(2)

#define lpt4_setup(a) lpt_port_setup(3, a)
#define lpt4_irq(a)   lpt_port_irq(3, a)
#define lpt4_remove() lpt_port_remove(3)

#if 0
#define lpt5_setup(a) lpt_port_setup(4, a)
#define lpt5_irq(a)   lpt_port_irq(4, a)
#define lpt5_remove() lpt_port_remove(4)

#define lpt6_setup(a) lpt_port_setup(5, a)
#define lpt6_irq(a)   lpt_port_irq(5, a)
#define lpt6_remove() lpt_port_remove(5)
#endif

void lpt_devices_init(void);
void lpt_devices_close(void);

typedef struct lpt_port_t {
    uint8_t       enabled;
    uint8_t       irq;
    uint8_t       irq_state;
    uint8_t       dma;
    uint8_t       dat;
    uint8_t       ctrl;
    uint8_t       ext;
    uint8_t       epp;
    uint8_t       ecp;
    uint8_t       ecr;
    uint8_t       in_dat;
    uint8_t       fifo_stat;
    uint8_t       dma_stat;
    uint8_t       state;
    uint8_t       autofeed;
    uint8_t       strobe;
    uint8_t       lv2;
    uint8_t       pad[7];
    uint16_t      addr;
    uint16_t      id;
    uint16_t      pad0[2];
    int           device;
    int           enable_irq;
    lpt_device_t *dt;
#ifdef FIFO_H
    fifo16_t     *fifo;
#else
    void         *fifo;
#endif
    void         *priv;

    pc_timer_t    fifo_out_timer;
} lpt_port_t;

typedef enum {
    LPT_STATE_IDLE = 0,
    LPT_STATE_READ_DMA,
    LPT_STATE_WRITE_FIFO
} lpt_state_t;

extern lpt_port_t lpt_ports[PARALLEL_MAX];

extern void        lpt_write(uint16_t port, uint8_t val, void *priv);

extern void        lpt_write_to_fifo(void *priv, uint8_t val);

extern uint8_t     lpt_read(uint16_t port, void *priv);

extern uint8_t     lpt_read_port(int port, uint16_t reg);

extern uint8_t     lpt_read_status(int port);
extern void        lpt_irq(void *priv, int raise);

extern int         lpt_device_get_from_internal_name(const char *s);

extern const char *lpt_device_get_name(int id);
extern const char *lpt_device_get_internal_name(int id);

extern const lpt_device_t lpt_dac_device;
extern const lpt_device_t lpt_dac_stereo_device;

extern const lpt_device_t dss_device;

extern const lpt_device_t lpt_hasp_savquest_device;

#endif /*EMU_LPT_H*/
