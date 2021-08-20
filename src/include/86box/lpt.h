#ifndef EMU_LPT_H
# define EMU_LPT_H

typedef struct
{
    const char *name;

    void *	(*init)(void *lpt);
    void	(*close)(void *p);
    void	(*write_data)(uint8_t val, void *p);
    void	(*write_ctrl)(uint8_t val, void *p);
    uint8_t	(*read_data)(void *p);
    uint8_t	(*read_status)(void *p);
    uint8_t	(*read_ctrl)(void *p);
} lpt_device_t;


extern void lpt_init(void);
extern void lpt_port_init(int i, uint16_t port);
extern void lpt_port_irq(int i, uint8_t irq);
extern void lpt_port_remove(int i);
extern void lpt1_remove_ams(void);

#define lpt1_init(a)	lpt_port_init(0, a)
#define lpt1_irq(a)	lpt_port_irq(0, a)
#define lpt1_remove()	lpt_port_remove(0)
#define lpt2_init(a)	lpt_port_init(1, a)
#define lpt2_irq(a)	lpt_port_irq(1, a)
#define lpt2_remove()	lpt_port_remove(1)
#define lpt3_init(a)	lpt_port_init(2, a)
#define lpt3_irq(a)	lpt_port_irq(2, a)
#define lpt3_remove()	lpt_port_remove(2)


void lpt_devices_init(void);
void lpt_devices_close(void);


typedef struct {
    uint8_t		enabled, irq,
			dat, ctrl;
    uint16_t		addr, pad0;
    int			device, enable_irq;
    lpt_device_t *	dt;
    void *		priv;
} lpt_port_t;

extern lpt_port_t	lpt_ports[3];

extern void	lpt_write(uint16_t port, uint8_t val, void *priv);
extern uint8_t	lpt_read(uint16_t port, void *priv);

extern void	lpt_irq(void *priv, int raise);

extern char *	lpt_device_get_name(int id);
extern char *	lpt_device_get_internal_name(int id);

extern int	lpt_device_get_from_internal_name(char *s);

extern const lpt_device_t lpt_dac_device;
extern const lpt_device_t lpt_dac_stereo_device;

extern const lpt_device_t dss_device;

extern const lpt_device_t lpt_hasp_savquest_device;

#endif /*EMU_LPT_H*/
