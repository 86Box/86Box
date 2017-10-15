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
 * Version:	@(#)scsi_device.h	1.0.4	2017/10/10
 *
 * Authors:	Miran Grca, <mgrca8@gmail.com>
 *		Fred N. van Kempen, <decwiz@yahoo.com>
 */
#ifndef SCSI_DEVICE_H
# define SCSI_DEVICE_H

typedef struct
{
	int state;
	int new_state;
	int clear_req;
	uint32_t bus_in, bus_out;
	int dev_id;

	int command_pos;
	uint8_t command[20];
	int data_pos;
	
	int change_state_delay;
	int new_req_delay;	
} scsi_bus_t;

extern uint8_t	*scsi_device_sense(uint8_t id, uint8_t lun);
extern void	scsi_device_type_data(uint8_t id, uint8_t lun,
				      uint8_t *type, uint8_t *rmb);
extern void	scsi_device_request_sense(uint8_t scsi_id, uint8_t scsi_lun,
					  uint8_t *buffer,
					  uint8_t alloc_length);
extern int	scsi_device_read_capacity(uint8_t id, uint8_t lun,
					  uint8_t *cdb, uint8_t *buffer,
					  uint32_t *len);
extern int	scsi_device_present(uint8_t id, uint8_t lun);
extern int	scsi_device_valid(uint8_t id, uint8_t lun);
extern int	scsi_device_cdb_length(uint8_t id, uint8_t lun);
extern int	scsi_device_block_shift(uint8_t id, uint8_t lun);
extern void	scsi_device_command(uint8_t id, uint8_t lun, int cdb_len,
				    uint8_t *cdb);
extern void	scsi_device_command_phase0(uint8_t scsi_id, uint8_t scsi_lun,
					   int cdb_len, uint8_t *cdb);
extern void	scsi_device_command_phase1(uint8_t scsi_id, uint8_t scsi_lun);
extern int32_t	*scsi_device_get_buf_len(uint8_t scsi_id, uint8_t scsi_lun);

extern int scsi_bus_update(scsi_bus_t *bus, int bus_assert);
extern int scsi_bus_read(scsi_bus_t *bus);
extern int scsi_bus_match(scsi_bus_t *bus, int bus_assert);

#endif	/*SCSI_DEVICE_H*/
