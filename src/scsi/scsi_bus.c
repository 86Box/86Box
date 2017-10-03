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

#define SET_BUS_STATE(bus, state) bus->bus_out = (bus->bus_out & ~(BUS_CD | BUS_IO | BUS_MSG)) | (state & (BUS_CD | BUS_IO | BUS_MSG))

uint32_t SCSI_BufferLength;

/* get the length of a SCSI command based on its command byte type */
static int get_cmd_len(int cbyte)
{
	int len;
	int group;

	group = (cbyte>>5) & 7;

	if (group == 0) len = 6;
	if (group == 1 || group == 2) len = 10;
	if (group == 5) len = 12;

	//pclog("Command group %d, length %d\n", group, len);
	
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
	
	return 7;
}

int scsi_bus_update(scsi_bus_t *bus, int bus_assert)
{
	scsi_device_t *dev;
	
	dev = &SCSIDevices[bus->dev_id][0];
	
	if (bus_assert & BUS_ARB)
			bus->state = STATE_IDLE;	
		
	dev->CmdBuffer = (uint8_t *)bus->buffer;

	//pclog("SCSI Bus State %x\n", bus->state);	
	switch (bus->state)
	{
		case STATE_IDLE:
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
					pclog("Device id %i\n", bus->dev_id);
					break;
			}
			break;
			
		case STATE_PHASESEL:
			if (!(bus_assert & BUS_SEL))
			{
				if (!(bus_assert & BUS_ATN))
				{
					if (scsi_device_present(bus->dev_id, 0))
					{
							bus->state = STATE_COMMAND;
							bus->bus_out = BUS_BSY | BUS_REQ;
							bus->command_pos = 0;
							SET_BUS_STATE(bus, BUS_CD);
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
				bus->command[bus->command_pos++] = BUS_GETDATA(bus_assert);		
				
				bus->clear_req = 3;
				bus->new_state = bus->bus_out & (BUS_IO | BUS_CD | BUS_MSG);
				bus->bus_out &= ~BUS_REQ;
				
				if (get_cmd_len(bus->command[0]) == bus->command_pos)
				{
					bus->data_pos = 0;
					
					if (bus->command[0] == 0x03)
					{
						SCSI_BufferLength = bus->command[4];
						//pclog("Request Sense len %i, buffer len %i\n", bus->command[4], SCSI_BufferLength);
					}
					
					scsi_device_command(bus->dev_id, 0, get_cmd_len(bus->command[0]), bus->command);					
					
					if ((SCSIPhase & (BUS_IO | BUS_CD | BUS_MSG)) == BUS_CD)
					{
							bus->state = STATE_COMMANDWAIT;
							bus->clear_req = 0;
					}
					else
					{
							bus->new_state = SCSIPhase;
							bus->change_state_delay = 4;	
					}		
				}
			}
			break;

		case STATE_COMMANDWAIT:
			{
				scsi_device_command(bus->dev_id, 0, get_cmd_len(bus->command[0]), bus->command);
				
				if ((SCSIPhase & (BUS_IO | BUS_CD | BUS_MSG)) != BUS_CD)
				{
						bus->new_state = SCSIPhase;
						bus->change_state_delay = 4;
						bus->clear_req = 4;
				}
			}
			break;
                                        
		case STATE_DATAIN:
			if ((bus_assert & BUS_ACK) && !(bus->bus_in & BUS_ACK))
			{
				if (bus->data_pos >= SCSI_BufferLength)
				{
					bus->data_pos = SCSI_BufferLength;
					bus->bus_out &= ~BUS_REQ;
					bus->new_state = BUS_CD | BUS_IO;
					bus->change_state_delay = 4;
					bus->new_req_delay = 8;
				}
				else
				{
					uint8_t val = dev->CmdBuffer[bus->data_pos++];

					bus->bus_out = (bus->bus_out & ~BUS_DATAMASK) | BUS_SETDATA(val) | BUS_DBP | BUS_REQ;
					bus->clear_req = 3;
					bus->bus_out &= ~BUS_REQ;
					bus->new_state = BUS_IO;
				}
			}
			break;

		case STATE_DATAOUT:
			if ((bus_assert & BUS_ACK) && !(bus->bus_in & BUS_ACK))
			{
					dev->CmdBuffer[bus->data_pos++] = SCSI_BufferLength;				
					
					if (bus->data_pos >= SCSI_BufferLength)
					{
							bus->data_pos = SCSI_BufferLength;
							bus->bus_out &= ~BUS_REQ;
							scsi_device_command(bus->dev_id, 0, get_cmd_len(bus->command[0]), bus->command);
							bus->new_state = SCSIPhase;
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
			if ((bus_assert & BUS_ACK) && !(bus->bus_in & BUS_ACK))
			{	
					bus->bus_out &= ~BUS_REQ;
					bus->new_state = BUS_CD | BUS_IO | BUS_MSG;
					bus->change_state_delay = 4;
					bus->new_req_delay = 8;
			}
			break;
                
		case STATE_MESSAGEIN:
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
			
			SET_BUS_STATE(bus, bus->new_state);

			switch (bus->bus_out & (BUS_IO | BUS_CD | BUS_MSG))
			{
				case BUS_IO:
					bus->state = STATE_DATAIN;
					val = dev->CmdBuffer[bus->data_pos++];
					bus->bus_out = (bus->bus_out & ~BUS_DATAMASK) | BUS_SETDATA(val) | BUS_DBP;
					break;
					
				case 0:
					if (bus->new_state & BUS_IDLE)
					{
							bus->state = STATE_IDLE;
							bus->bus_out &= ~BUS_BSY;
					}
					else
							bus->state = STATE_DATAOUT;
					break;

				case (BUS_IO | BUS_CD):
					bus->state = STATE_STATUS;
					bus->bus_out = (bus->bus_out & ~BUS_DATAMASK) | BUS_SETDATA(SCSIStatus) | BUS_DBP;
					break;
					
				case (BUS_CD | BUS_IO | BUS_MSG):
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
