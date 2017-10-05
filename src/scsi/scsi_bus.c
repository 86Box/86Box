/*
 * 86Box	A hypervisor and IBM PC system emulator that specializes in
 *		running old operating systems and software designed for IBM
 *		PC systems and compatibles from 1981 through fairly recent
 *		system designs based on the PCI bus.
 *
 *		This file is part of the 86Box distribution.
 *
 *		The generic SCSI bus operations handler.
 *
 * Version:	@(#)scsi_bus.c	1.0.1	2017/10/04
 *
 * NOTES: 	For now ported from PCem with some modifications
 *			but at least it's a start.	
 *
 * Authors:	TheCollector1995, <mariogplayer@gmail.com>
 */
#include <stdarg.h>
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <stdlib.h>
#include <wchar.h>
#include "../ibm.h"
#include "scsi.h"
#include "scsi_device.h"

#define STATE_IDLE 0
#define STATE_COMMAND 1
#define STATE_COMMANDWAIT 2
#define STATE_DATAIN 3
#define STATE_DATAOUT 4
#define STATE_STATUS 5
#define STATE_MESSAGEIN 6
#define STATE_PHASESEL 7

#define SET_BUS_STATE(bus, state) bus->bus_out = (bus->bus_out & ~(SCSI_PHASE_MESSAGE_IN)) | (state & (SCSI_PHASE_MESSAGE_IN))

uint32_t SCSI_BufferLength;

#define ENABLE_SCSI_BUS_LOG 0
int scsi_bus_do_log = 0;


void scsi_bus_log(const char *format, ...)
{
#ifdef ENABLE_SCSI_BUS_LOG
	if (scsi_bus_do_log)
	{
		va_list ap;
		va_start(ap, format);
		vprintf(format, ap);
		va_end(ap);
		fflush(stdout);
	}
#endif
}


/* get the length of a SCSI command based on its command byte type */
static int get_cmd_len(int cbyte)
{
	int len;
	int group;

	group = (cbyte>>5) & 7;

	if (group == 0) len = 6;
	if (group == 1 || group == 2) len = 10;
	if (group == 5) len = 12;

	//scsi_bus_log("Command group %d, length %d\n", group, len);
	
	return len;
}

static int get_dev_id(uint8_t data)
{
	int c;	

	for (c = 0; c < SCSI_ID_MAX; c++)
	{
			if (data & (1 << c))
			{
				return c;
			}
	}
	
	return -1;
}

int scsi_bus_update(scsi_bus_t *bus, int bus_assert)
{
	scsi_device_t *dev;
	uint8_t lun = 0;
	
	dev = &SCSIDevices[bus->dev_id][0];
	
	if (bus_assert & BUS_ARB)
			bus->state = STATE_IDLE;	
		
	dev->CmdBuffer = (uint8_t *)bus->buffer;

	switch (bus->state)
	{
		case STATE_IDLE:
			scsi_bus_log("State Idle\n");
			bus->clear_req = bus->change_state_delay = bus->new_req_delay = 0;
			if ((bus_assert & BUS_SEL) && !(bus_assert & BUS_BSY))
			{
					uint8_t sel_data = BUS_GETDATA(bus_assert);
				
					bus->dev_id = get_dev_id(sel_data);
					if (scsi_device_present(bus->dev_id, 0))
					{
							bus->bus_out |= BUS_BSY;
							bus->state = STATE_PHASESEL;
					}
					//scsi_bus_log("Device id %i\n", bus->dev_id);
					break;
			}
			break;
			
		case STATE_PHASESEL:
			scsi_bus_log("State Phase Sel\n");
			if (!(bus_assert & BUS_SEL))
			{
				if (!(bus_assert & BUS_ATN))
				{
					if (scsi_device_present(bus->dev_id, 0))
					{
							bus->state = STATE_COMMAND;
							bus->bus_out = BUS_BSY | BUS_REQ;
							bus->command_pos = 0;
							SET_BUS_STATE(bus, SCSI_PHASE_COMMAND);
					}
					else
					{
							bus->state = STATE_IDLE;
							bus->bus_out = 0;
					}
				}
				else
					fatal("dropped sel %x\n", bus_assert & BUS_ATN);
			}
			break;
			
		case STATE_COMMAND:
			if ((bus_assert & BUS_ACK) && !(bus->bus_in & BUS_ACK))
			{		
				scsi_bus_log("State Command\n");
				bus->command[bus->command_pos++] = BUS_GETDATA(bus_assert);		
				
				bus->clear_req = 3;
				bus->new_state = bus->bus_out & SCSI_PHASE_MESSAGE_IN;
				bus->bus_out &= ~BUS_REQ;
				
				if (get_cmd_len(bus->command[0]) == bus->command_pos)
				{
					lun = (bus->command[1] >> 5) & 7;
					bus->data_pos = 0;
					
					/* Get the allocation length right */
					if (bus->command[0] == GPCMD_REQUEST_SENSE || bus->command[0] == GPCMD_INQUIRY)
					{
						SCSI_BufferLength = bus->command[4];
					}

					/* For some reason, they default to 0, skipping the r/w routines, so here we go for the SCSI disk */
					if ((bus->command[0] == GPCMD_READ_6 || bus->command[0] == GPCMD_WRITE_6) && (dev->LunType == SCSI_DISK))
					{
						SCSI_BufferLength = bus->command[4] << 9;
					}
					
					if ((bus->command[0] == GPCMD_READ_10 || bus->command[0] == GPCMD_WRITE_10) && (dev->LunType == SCSI_DISK))
					{
						SCSI_BufferLength = ((bus->command[7] << 8) | bus->command[8]) << 9;
					}
					
					scsi_bus_log("Command 0x%02X\n", bus->command[0]);

					if ((bus->command[0] == GPCMD_WRITE_6) || (bus->command[0] == GPCMD_MODE_SELECT_6) || (bus->command[0] == GPCMD_WRITE_10) || (bus->command[0] == GPCMD_MODE_SELECT_10) || (bus->command[0] == GPCMD_WRITE_12))
					{
						/* Write direction commands have delayed execution - only execute them after the bus has gotten all the data from the host. */
						scsi_bus_log("Next state is data out\n");
						bus->state = STATE_DATAOUT;
						SET_BUS_STATE(bus, STATE_DATAOUT);
						bus->bus_out |= BUS_REQ;				

						bus->change_state_delay = 0;
						bus->clear_req = 0;
					}
					else
					{
						/* Other command - execute immediately. */
						scsi_device_command(bus->dev_id, lun, get_cmd_len(bus->command[0]), bus->command);
						scsi_bus_log("Next state is defined by command\n");
						bus->new_state = SCSIPhase;

						bus->change_state_delay = 4;
						bus->clear_req = 4;
					}
				}
			}
			break;
                                        
		case STATE_DATAIN:
			if ((bus_assert & BUS_ACK) && !(bus->bus_in & BUS_ACK))
			{
				scsi_bus_log("State Data In\n");
				
				/* This seems to be read, so we first execute the command, then we return the bytes to the host. */
				
				if (bus->data_pos >= SCSI_BufferLength)
				{
					bus->bus_out &= ~BUS_REQ;
					bus->new_state = SCSI_PHASE_STATUS;
					bus->change_state_delay = 4;
					bus->new_req_delay = 8;
				}
				else
				{
					uint8_t val = dev->CmdBuffer[bus->data_pos++];
	
					bus->bus_out = (bus->bus_out & ~BUS_DATAMASK) | BUS_SETDATA(val) | BUS_DBP | BUS_REQ;
					bus->clear_req = 3;
					bus->bus_out &= ~BUS_REQ;
					bus->new_state = SCSI_PHASE_DATA_IN;
				}
			}
			break;

		case STATE_DATAOUT:
			if ((bus_assert & BUS_ACK) && !(bus->bus_in & BUS_ACK))
			{
				scsi_bus_log("State Data Out\n");	
				
				/* This is write, so first get the data from the host, then execute the last phase of the command. */
				dev->CmdBuffer[bus->data_pos++] = BUS_GETDATA(bus_assert);	

				if (bus->data_pos >= SCSI_BufferLength)
				{
					pclog("%04X bytes written (%02X %02X)\n", bus->data_pos, bus->command[0], bus->command[1]);
					scsi_bus_log("Actually executing write command\n");
					lun = (bus->command[1] >> 5) & 7;
					bus->bus_out &= ~BUS_REQ;
					scsi_device_command(bus->dev_id, lun, get_cmd_len(bus->command[0]), bus->command);
					bus->new_state = SCSI_PHASE_STATUS;
					bus->change_state_delay = 4;
					bus->new_req_delay = 8;	
				}
				else
				{
					bus->bus_out |= BUS_REQ;				
				}
			}
			break;
                
		case STATE_STATUS:
			scsi_bus_log("State Status\n");
		
			if ((bus_assert & BUS_ACK) && !(bus->bus_in & BUS_ACK))
			{	
					pclog("Preparing for message in\n");
					bus->bus_out &= ~BUS_REQ;
					bus->new_state = SCSI_PHASE_MESSAGE_IN;
					bus->change_state_delay = 4;
					bus->new_req_delay = 8;
			}
			break;
                
		case STATE_MESSAGEIN:
			scsi_bus_log("State Message In\n");
		
			if ((bus_assert & BUS_ACK) && !(bus->bus_in & BUS_ACK))
			{			
					bus->bus_out &= ~BUS_REQ;
					bus->new_state = BUS_IDLE;
					bus->change_state_delay = 4;
			}
			break;
	}
	bus->bus_in = bus_assert;	
	
	return bus->bus_out | bus->bus_in;
}

int scsi_bus_read(scsi_bus_t *bus)
{
	scsi_device_t *dev;

	dev = &SCSIDevices[bus->dev_id][0];

	dev->CmdBuffer = (uint8_t *)bus->buffer;
	
	if (bus->clear_req)
	{
		bus->clear_req--;
		if (!bus->clear_req)
		{
			scsi_bus_log("Clear REQ\n");

			SET_BUS_STATE(bus, bus->new_state);
			bus->bus_out |= BUS_REQ;
		}
	}
        
	if (bus->change_state_delay)
	{
		bus->change_state_delay--;
		if (!bus->change_state_delay)
		{
			uint8_t val;

			scsi_bus_log("Change state delay\n");
			
			SET_BUS_STATE(bus, bus->new_state);

			switch (bus->bus_out & SCSI_PHASE_MESSAGE_IN)
			{
				case SCSI_PHASE_DATA_IN:
					scsi_bus_log("Phase data in\n");
					bus->state = STATE_DATAIN;
					val = dev->CmdBuffer[bus->data_pos++];
					bus->bus_out = (bus->bus_out & ~BUS_DATAMASK) | BUS_SETDATA(val) | BUS_DBP;
					break;
					
				case SCSI_PHASE_DATA_OUT:
					scsi_bus_log("Phase data out\n");
					if (bus->new_state & BUS_IDLE)
					{
						bus->state = STATE_IDLE;
						bus->bus_out &= ~BUS_BSY;
					}
					else
					{
						bus->state = STATE_DATAOUT;
					}
					break;

				case SCSI_PHASE_STATUS:
					scsi_bus_log("Phase status\n");
					bus->state = STATE_STATUS;
					bus->bus_out |= BUS_REQ;
					bus->bus_out = (bus->bus_out & ~BUS_DATAMASK) | BUS_SETDATA(SCSIStatus) | BUS_DBP;
					pclog("SCSI Status (command %02X): %02X (%08X)\n", bus->command[0], SCSIStatus, bus->bus_out);
					break;
					
				case SCSI_PHASE_MESSAGE_IN:
					scsi_bus_log("Phase message in\n");
					pclog("Message in\n");
					bus->state = STATE_MESSAGEIN;
					bus->bus_out = (bus->bus_out & ~BUS_DATAMASK) | BUS_SETDATA(0) | BUS_DBP;
					break;
					
				default:
					fatal("change_state_delay bad state %x\n", bus->bus_out);
			}
		}
	}
	if (bus->new_req_delay)
	{
		bus->new_req_delay--;
		if (!bus->new_req_delay)
		{
			bus->bus_out |= BUS_REQ;
		}
	}
	
	return bus->bus_out;
}

int scsi_bus_match(scsi_bus_t *bus, int bus_assert)
{
	return (bus_assert & (BUS_CD | BUS_IO | BUS_MSG)) == (bus->bus_out & (BUS_CD | BUS_IO | BUS_MSG));
}
