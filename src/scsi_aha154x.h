#ifndef SCSI_AHA154X_H
# define SCSI_AHA154X_H


typedef struct {
    uint8_t	flags;		/* local flags */
    uint8_t	bid;		/* board ID */
    char	fwl, fwh;	/* firmware info */
} aha_info;
#define AHA_GLAG_MEMEN	0x01	/* BIOS Shadow RAM enabled */


extern device_t aha1540b_device;
extern device_t aha1542cf_device;
extern device_t aha1640_device;
  
  
#endif	/*SCSI_AHA154X_H*/
