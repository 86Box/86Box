#ifndef BUSLOGIC_H
# define BUSLOGIC_H


typedef struct {
    uint8_t	flags;		/* local flags */
    uint8_t	bid;		/* board ID */
    char	fwl, fwh;	/* firmware info */
} aha_info;
#define AHA_GLAG_MEMEN	0x01	/* BIOS Shadow RAM enabled */


extern device_t aha1540b_device;
extern device_t aha1542cf_device;
extern device_t buslogic_device;
extern device_t buslogic_pci_device;


extern int	buslogic_dev_present(uint8_t id, uint8_t lun);

extern void	aha154x_init(uint16_t, uint32_t, aha_info *);
extern uint8_t	aha154x_shram(uint8_t);
extern uint8_t	aha154x_eeprom(uint8_t,uint8_t,uint8_t,uint8_t,uint8_t *);
extern uint8_t	aha154x_memory(uint8_t);


#endif	/*BUSLOGIC_H*/
