#include <stdio.h>
#include <stdint.h>
#include <string.h>
/*
 * 86Box	A hypervisor and IBM PC system emulator that specializes in
 *		running old operating systems and software designed for IBM
 *		PC systems and compatibles from 1981 through fairly recent
 *		system designs based on the PCI bus.
 *
 *		This file is part of the 86Box distribution.
 *
 *		DDC monitor emulation.
 *
 *
 *
 * Authors:	Sarah Walker, <http://pcem-emulator.co.uk/>
 *
 *		Copyright 2008-2020 Sarah Walker.
 */
#include <stdlib.h>
#include <stddef.h>
#include <wchar.h>
#include <math.h>
#include <86box/86box.h>
#include "cpu.h"
#include <86box/vid_ddc.h>


static uint8_t edid_data[128] =
{
        0x00, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0x00, /*Fixed header pattern*/
        0x09, 0xf8, /*Manufacturer "BOX" - apparently unassigned by UEFI - and it has to be big endian*/
        0x00, 0x00, /*Product code*/
        0x12, 0x34, 0x56, 0x78, /*Serial number*/
        0x01, 9, /*Manufacturer week and year*/
        0x01, 0x03, /*EDID version (1.3)*/

        0x08, /*Analogue input, separate sync*/
        34, 0, /*Landscape, 4:3*/
        0, /*Gamma*/
        0x08, /*RGB colour*/
        0x81, 0xf1, 0xa3, 0x57, 0x53, 0x9f, 0x27, 0x0a, 0x50, /*Chromaticity*/

        0xff, 0xff, 0xff, /*Established timing bitmap*/
        0x00, 0x00, /*Standard timing information*/
        0x00, 0x00,
        0x00, 0x00,
        0x00, 0x00,
        0x00, 0x00,
        0x00, 0x00,
        0x00, 0x00,
        0x00, 0x00,

        /*Detailed mode descriptions*/
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,

        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,

        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,

        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,

        0x00, /*No extensions*/
        0x00
};

/*This should probably be split off into a separate I2C module*/
enum
{
        TRANSMITTER_MONITOR = 1,
        TRANSMITTER_HOST    = -1
};

enum
{
        I2C_IDLE = 0,
        I2C_RECEIVE,
        I2C_RECEIVE_WAIT,
        I2C_TRANSMIT_START,
        I2C_TRANSMIT,
        I2C_ACKNOWLEDGE,
        I2C_TRANSACKNOWLEDGE,
        I2C_TRANSMIT_WAIT
};

enum
{
        PROM_IDLE = 0,
        PROM_RECEIVEADDR,
        PROM_RECEIVEDATA,
        PROM_SENDDATA,
        PROM_INVALID
};

static struct
{
        int clock, data;
        int state;
        int last_data;
        int pos;
        int transmit;
        uint8_t byte;
} i2c;

static struct
{
        int state;
        int addr;
        int rw;
} prom;

static void prom_stop(void)
{
//        pclog("prom_stop()\n");
        prom.state = PROM_IDLE;
        i2c.transmit = TRANSMITTER_HOST;
}

static void prom_next_byte(void)
{
//        pclog("prom_next_byte(%d)\n", prom.addr);
        i2c.byte = edid_data[(prom.addr++) & 0x7F];
}

static void prom_write(uint8_t byte)
{
//        pclog("prom_write: byte=%02x\n", byte);
        switch (prom.state)
        {
                case PROM_IDLE:
                if ((byte & 0xfe) != 0xa0)
                {
//                        pclog("I2C address not PROM\n");
                        prom.state = PROM_INVALID;
                        break;
                }
                prom.rw = byte & 1;
                if (prom.rw)
                {
                        prom.state = PROM_SENDDATA;
                        i2c.transmit = TRANSMITTER_MONITOR;
                        i2c.byte = edid_data[(prom.addr++) & 0x7F];
//                        pclog("PROM - %02X from %02X\n",i2c.byte, prom.addr-1);
//                        pclog("Transmitter now PROM\n");
                }
                else
                {
                        prom.state = PROM_RECEIVEADDR;
                        i2c.transmit = TRANSMITTER_HOST;
                }
//                pclog("PROM R/W=%i\n",promrw);
                return;

                case PROM_RECEIVEADDR:
//                pclog("PROM addr=%02X\n",byte);
                prom.addr = byte;
                if (prom.rw)
                        prom.state = PROM_SENDDATA;
                else
                        prom.state = PROM_RECEIVEDATA;
                break;

                case PROM_RECEIVEDATA:
//                pclog("PROM write %02X %02X\n",promaddr,byte);
                break;

                case PROM_SENDDATA:
                break;
        }
}

void ddc_i2c_change(int new_clock, int new_data)
{
//        pclog("I2C update clock %i->%i data %i->%i state %i\n",i2c.clock,new_clock,i2c.last_data,new_data,i2c.state);
        switch (i2c.state)
        {
                case I2C_IDLE:
                if (i2c.clock && new_clock)
                {
                        if (i2c.last_data && !new_data) /*Start bit*/
                        {
//                                pclog("Start bit received\n");
                                i2c.state = I2C_RECEIVE;
                                i2c.pos = 0;
                        }
                }
                break;

                case I2C_RECEIVE_WAIT:
                if (!i2c.clock && new_clock)
                        i2c.state = I2C_RECEIVE;
                case I2C_RECEIVE:
                if (!i2c.clock && new_clock)
                {
                        i2c.byte <<= 1;
                        if (new_data)
                                i2c.byte |= 1;
                        else
                                i2c.byte &= 0xFE;
                        i2c.pos++;
                        if (i2c.pos == 8)
                        {
                                prom_write(i2c.byte);
                                i2c.state = I2C_ACKNOWLEDGE;
                        }
                }
                else if (i2c.clock && new_clock && new_data && !i2c.last_data) /*Stop bit*/
                {
//                        pclog("Stop bit received\n");
                        i2c.state = I2C_IDLE;
                        prom_stop();
                }
                else if (i2c.clock && new_clock && !new_data && i2c.last_data) /*Start bit*/
                {
//                        pclog("Start bit received\n");
                        i2c.pos = 0;
                        prom.state = PROM_IDLE;
                }
                break;

                case I2C_ACKNOWLEDGE:
                if (!i2c.clock && new_clock)
                {
//                        pclog("Acknowledging transfer\n");
                        new_data = 0;
                        i2c.pos = 0;
                        if (i2c.transmit == TRANSMITTER_HOST)
                                i2c.state = I2C_RECEIVE_WAIT;
                        else
                                i2c.state = I2C_TRANSMIT;
                }
                break;

                case I2C_TRANSACKNOWLEDGE:
                if (!i2c.clock && new_clock)
                {
                        if (new_data) /*It's not acknowledged - must be end of transfer*/
                        {
//                                pclog("End of transfer\n");
                                i2c.state = I2C_IDLE;
                                prom_stop();
                        }
                        else /*Next byte to transfer*/
                        {
                                i2c.state = I2C_TRANSMIT_START;
                                prom_next_byte();
                                i2c.pos = 0;
//                                pclog("Next byte - %02X\n",i2c.byte);
                        }
                }
                break;

                case I2C_TRANSMIT_WAIT:
                if (i2c.clock && new_clock)
                {
                        if (i2c.last_data && !new_data) /*Start bit*/
                        {
                                prom_next_byte();
                                i2c.pos = 0;
//                                pclog("Next byte - %02X\n",i2c.byte);
                        }
                        if (!i2c.last_data && new_data) /*Stop bit*/
                        {
//                                pclog("Stop bit received\n");
                                i2c.state = I2C_IDLE;
                                prom_stop();
                        }
                }
                break;

                case I2C_TRANSMIT_START:
                if (!i2c.clock && new_clock)
                        i2c.state = I2C_TRANSMIT;
                if (i2c.clock && new_clock && !i2c.last_data && new_data) /*Stop bit*/
                {
//                        pclog("Stop bit received\n");
                        i2c.state = I2C_IDLE;
                        prom_stop();
                }
                case I2C_TRANSMIT:
                if (!i2c.clock && new_clock)
                {
                        i2c.clock = new_clock;
//                        if (!i2c.pos)
//                                pclog("Transmit byte %02x\n", i2c.byte);
                        i2c.data = new_data = i2c.byte & 0x80;
//                        pclog("Transmit bit %i %i\n", i2c.byte, i2c.pos);
                        i2c.byte <<= 1;
                        i2c.pos++;
                        return;
                }
                if (i2c.clock && !new_clock && i2c.pos == 8)
                {
                        i2c.state = I2C_TRANSACKNOWLEDGE;
//                        pclog("Acknowledge mode\n");
                }
                break;

        }
        if (!i2c.clock && new_clock)
                i2c.data = new_data;
        i2c.last_data = new_data;
        i2c.clock = new_clock;
}

int ddc_read_clock(void)
{
        return i2c.clock;
}
int ddc_read_data(void)
{
        if (i2c.state == I2C_TRANSMIT || i2c.state == I2C_ACKNOWLEDGE)
                return i2c.data;
        if (i2c.state == I2C_RECEIVE_WAIT)
                return 0; /*ACK*/
        return 1;
}

void ddc_init(void)
{
        int c;
        uint8_t checksum = 0;

        for (c = 0; c < 127; c++)
                checksum += edid_data[c];
        edid_data[127] = 256 - checksum;

        i2c.clock = 1;
        i2c.data = 1;
}
