/*
 * 86Box	A hypervisor and IBM PC system emulator that specializes in
 *		running old operating systems and software designed for IBM
 *		PC systems and compatibles from 1981 through fairly recent
 *		system designs based on the PCI bus.
 *
 *		This file is part of the 86Box distribution.
 *
 *		Definitions for the generic SCSI device command handler.
 *
 * Version:	@(#)scsi_devic3.h	1.0.1	2017/08/21
 *
 * Authors:	Miran Grca, <mgrca8@gmail.com>
 *		Fred N. van Kempen, <decwiz@yahoo.com>
 */
#ifndef SCSI_DEVICE_H
# define SCSI_DEVICE_H


extern uint8_t	*scsi_device_sense(uint8_t id, uint8_t lun);
extern void	scsi_device_type_data(uint8_t id, uint8_t lun,
				      uint8_t *type, uint8_t *rmb);
extern int	scsi_device_read_capacity(uint8_t id, uint8_t lun,
					  uint8_t *cdb, uint8_t *buffer,
					  uint32_t *len);
extern int	scsi_device_present(uint8_t id, uint8_t lun);
extern int	scsi_device_valid(uint8_t id, uint8_t lun);
extern int	scsi_device_cdb_length(uint8_t id, uint8_t lun);
extern int	scsi_device_block_shift(uint8_t id, uint8_t lun);
extern void	scsi_device_command(int cdb_len, uint8_t id, uint8_t lun,
				    uint8_t *cdb);


#endif	/*SCSI_DEVICE_H*/
