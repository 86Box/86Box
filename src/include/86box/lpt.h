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

typedef struct lpt_device_s {
    const char   *name;
    const char   *internal_name;

    void         *(*init)(void *lpt);
    void          (*close)(void *priv);
    void          (*write_data)(uint8_t val, void *priv);
    void          (*write_ctrl)(uint8_t val, void *priv);
    void          (*strobe)(uint8_t old, uint8_t val,void *priv);
    uint8_t       (*read_status)(void *priv);
    uint8_t       (*read_ctrl)(void *priv);
    void          (*epp_write_data)(uint8_t is_addr, uint8_t val, void *priv);
    void          (*epp_request_read)(uint8_t is_addr, void *priv);

    void         *priv;
    struct lpt_t *lpt;
//#ifdef EMU_DEVICE_H
//    struct device_t *cfgdevice;
//#else
    void            *cfgdevice;
//#endif
} lpt_device_t;

#ifdef _TIMER_H_
typedef struct lpt_t {
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
    uint8_t       ret_ecr;
    uint8_t       in_dat;
    uint8_t       fifo_stat;
    uint8_t       dma_stat;
    uint8_t       state;
    uint8_t       autofeed;
    uint8_t       strobe;
    uint8_t       lv2;
    uint8_t       cnfga_readout;
    uint8_t       cnfgb_readout;
    uint8_t       cfg_regs_enabled;
    uint8_t       inst;
    uint8_t       eir;
    uint8_t       pad;
    uint8_t       ext_regs[8];
    uint16_t      addr;
    uint16_t      id;
    uint16_t      pad0[2];
    int           enable_irq;
    lpt_device_t *dt;
#ifdef FIFO_H
    fifo16_t *    fifo;
#else
    void *        fifo;
#endif

    pc_timer_t    fifo_out_timer;
} lpt_t;
#endif /* _TIMER_H_ */

typedef struct lpt_port_s {
    uint8_t       enabled;

    int           device;
} lpt_port_t;

extern lpt_port_t lpt_ports[PARALLEL_MAX];

typedef enum {
    LPT_STATE_IDLE = 0,
    LPT_STATE_READ_DMA,
    LPT_STATE_WRITE_FIFO
} lpt_state_t;

extern void                lpt_write(uint16_t port, uint8_t val, void *priv);

extern void                lpt_write_to_fifo(void *priv, uint8_t val);

extern uint8_t             lpt_read(uint16_t port, void *priv);

extern uint8_t             lpt_read_port(lpt_t *dev, uint16_t reg);

extern uint8_t             lpt_read_status(lpt_t *dev);
extern uint8_t             lpt_read_ecp_mode(lpt_t *dev);

extern void                lpt_irq(void *priv, int raise);

extern int                 lpt_device_get_from_internal_name(const char *str);

extern const char         *lpt_device_get_name(int id);
extern const char         *lpt_device_get_internal_name(int id);

#ifdef EMU_DEVICE_H
extern const device_t     *lpt_device_getdevice(const int id);
#endif

extern int                 lpt_device_has_config(const int id);

extern const lpt_device_t  lpt_dac_device;
extern const lpt_device_t  lpt_dac_stereo_device;

extern const lpt_device_t  dss_device;

extern const lpt_device_t  lpt_hasp_savquest_device;

extern void                lpt_set_ext(lpt_t *dev, uint8_t ext);
extern void                lpt_set_ecp(lpt_t *dev, uint8_t ecp);
extern void                lpt_set_epp(lpt_t *dev, uint8_t epp);
extern void                lpt_set_lv2(lpt_t *dev, uint8_t lv2);
extern void                lpt_set_cfg_regs_enabled(lpt_t *dev, uint8_t cfg_regs_enabled);
extern void                lpt_set_fifo_threshold(lpt_t *dev, int threshold);
extern void                lpt_set_cnfga_readout(lpt_t *dev, const uint8_t cnfga_readout);
extern void                lpt_set_cnfgb_readout(lpt_t *dev, const uint8_t cnfgb_readout);
extern void                lpt_port_setup(lpt_t *dev, uint16_t port);
extern void                lpt_port_irq(lpt_t *dev, uint8_t irq);
extern void                lpt_port_dma(lpt_t *dev, uint8_t dma);
extern void                lpt1_dma(const uint8_t dma);
extern void                lpt_port_remove(lpt_t *dev);
extern void                lpt1_remove_ams(lpt_t *dev);

extern void                lpt_devices_init(void);
extern void                lpt_devices_close(void);

extern void                lpt_set_next_inst(int ni);
extern void                lpt_set_3bc_used(int is_3bc_used);

extern void                lpt_standalone_init(void);

extern const device_t      lpt_port_device;

#endif /*EMU_LPT_H*/
