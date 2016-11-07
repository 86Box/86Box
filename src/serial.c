/* Copyright holders: Sarah Walker
   see COPYING for more details
*/
#include "ibm.h"
#include "io.h"
#include "mouse.h"
#include "pic.h"
#include "serial.h"
#include "timer.h"

enum
{
        SERIAL_INT_LSR = 1,
        SERIAL_INT_RECEIVE = 2,
        SERIAL_INT_TRANSMIT = 4,
        SERIAL_INT_MSR = 8
};

SERIAL serial1, serial2;

int mousepos=-1;
int mousedelay;

void serial_reset()
{
        serial1.iir = serial1.ier = serial1.lcr = 0;
        serial2.iir = serial2.ier = serial2.lcr = 0;
        mousedelay = 0;
        serial1.fifo_read = serial1.fifo_write = 0;
        serial2.fifo_read = serial2.fifo_write = 0;
}

void serial_update_ints(SERIAL *serial)
{
        int stat = 0;
        
        serial->iir = 1;

        if ((serial->ier & 4) && (serial->int_status & SERIAL_INT_LSR)) /*Line status interrupt*/
        {
                stat = 1;
                serial->iir = 6;
        }
        else if ((serial->ier & 1) && (serial->int_status & SERIAL_INT_RECEIVE)) /*Recieved data available*/
        {
                stat = 1;
                serial->iir = 4;
        }
        else if ((serial->ier & 2) && (serial->int_status & SERIAL_INT_TRANSMIT)) /*Transmit data empty*/
        {
                stat = 1;
                serial->iir = 2;
        }
        else if ((serial->ier & 8) && (serial->int_status & SERIAL_INT_MSR)) /*Modem status interrupt*/
        {
                stat = 1;
                serial->iir = 0;
        }

        if (stat && ((serial->mctrl & 8) || PCJR))
                picintlevel(1 << serial->irq);               
        else
                picintc(1 << serial->irq);
}

void serial_write_fifo(SERIAL *serial, uint8_t dat)
{
//        pclog("serial_write_fifo %02X\n", serial->lsr);
        serial->fifo[serial->fifo_write] = dat;
        serial->fifo_write = (serial->fifo_write + 1) & 0xFF;
        if (!(serial->lsr & 1))
        {
                serial->lsr |= 1;
                serial->int_status |= SERIAL_INT_RECEIVE;
                serial_update_ints(serial);
        }
}

uint8_t serial_read_fifo(SERIAL *serial)
{
        if (serial->fifo_read != serial->fifo_write)
        {
                serial->dat = serial->fifo[serial->fifo_read];
                serial->fifo_read = (serial->fifo_read + 1) & 0xFF;
        }
        return serial->dat;
}

void serial_write(uint16_t addr, uint8_t val, void *p)
{
        SERIAL *serial = (SERIAL *)p;
//        pclog("Write serial %03X %02X %04X:%04X\n",addr,val,CS,pc);
        switch (addr&7)
        {
                case 0:
                if (serial->lcr & 0x80)
                {
                        serial->dlab1 = val;
                        return;
                }
                serial->thr = val;
                serial->lsr |= 0x20;
                serial->int_status |= SERIAL_INT_TRANSMIT;
                serial_update_ints(serial);
                if (serial->mctrl & 0x10)
                {
                        serial_write_fifo(serial, val);
                }
                break;
                case 1:
                if (serial->lcr & 0x80)
                {
                        serial->dlab2 = val;
                        return;
                }
                serial->ier = val & 0xf;
                serial_update_ints(serial);
                break;
                case 3:
                serial->lcr = val;
                break;
                case 4:
                if ((val & 2) && !(serial->mctrl & 2))
                {
                        if (serial->rcr_callback)
                                serial->rcr_callback(serial);
//                        pclog("RCR raised! sending M\n");
                }
                serial->mctrl = val;
                if (val & 0x10)
                {
                        uint8_t new_msr;
                        
                        new_msr = (val & 0x0c) << 4;
                        new_msr |= (val & 0x02) ? 0x10: 0;
                        new_msr |= (val & 0x01) ? 0x20: 0;
                        
                        if ((serial->msr ^ new_msr) & 0x10)
                                new_msr |= 0x01;
                        if ((serial->msr ^ new_msr) & 0x20)
                                new_msr |= 0x02;
                        if ((serial->msr ^ new_msr) & 0x80)
                                new_msr |= 0x08;
                        if ((serial->msr & 0x40) && !(new_msr & 0x40))
                                new_msr |= 0x04;
                        
                        serial->msr = new_msr;
                }
                break;
                case 5:
                serial->lsr = val;
                if (serial->lsr & 0x01)
                        serial->int_status |= SERIAL_INT_RECEIVE;
                if (serial->lsr & 0x1e)
                        serial->int_status |= SERIAL_INT_LSR;
                if (serial->lsr & 0x20)
                        serial->int_status |= SERIAL_INT_TRANSMIT;
                serial_update_ints(serial);
                break;
                case 6:
                serial->msr = val;
                if (serial->msr & 0x0f)
                        serial->int_status |= SERIAL_INT_MSR;
                serial_update_ints(serial);
                break;
                case 7:
                serial->scratch = val;
                break;
        }
}

uint8_t serial_read(uint16_t addr, void *p)
{
        SERIAL *serial = (SERIAL *)p;
        uint8_t temp = 0;
//        pclog("Read serial %03X %04X(%08X):%04X %i %i  ", addr, CS, cs, pc, mousedelay, ins);
        switch (addr&7)
        {
                case 0:
                if (serial->lcr & 0x80)
                {
                        temp = serial->dlab1;
                        break;
                }

                serial->lsr &= ~1;
                serial->int_status &= ~SERIAL_INT_RECEIVE;
                serial_update_ints(serial);
                temp = serial_read_fifo(serial);
                if (serial->fifo_read != serial->fifo_write)
                        serial->recieve_delay = 1000 * TIMER_USEC;
                break;
                case 1:
                if (serial->lcr & 0x80)
                        temp = serial->dlab2;
                else
                        temp = serial->ier;
                break;
                case 2: 
                temp = serial->iir;
                if ((temp & 0xe) == 2)
                {
                        serial->int_status &= ~SERIAL_INT_TRANSMIT;
                        serial_update_ints(serial);
                }
                break;
                case 3:
                temp = serial->lcr;
                break;
                case 4:
                temp = serial->mctrl;
                break;
                case 5:
                if (serial->lsr & 0x20)
                        serial->lsr |= 0x40;
                serial->lsr |= 0x20;
                temp = serial->lsr;
                if (serial->lsr & 0x1f)
                        serial->lsr &= ~0x1e;
//                serial.lsr |= 0x60;
                serial->int_status &= ~SERIAL_INT_LSR;
                serial_update_ints(serial);
                break;
                case 6:
                temp = serial->msr;
                serial->msr &= ~0x0f;
                serial->int_status &= ~SERIAL_INT_MSR;
                serial_update_ints(serial);
                break;
                case 7:
                temp = serial->scratch;
                break;
        }
//        pclog("%02X\n",temp);
        return temp;
}

void serial_recieve_callback(void *p)
{
        SERIAL *serial = (SERIAL *)p;
        
        serial->recieve_delay = 0;
        
        if (serial->fifo_read != serial->fifo_write)
        {
                serial->lsr |= 1;
                serial->int_status |= SERIAL_INT_RECEIVE;
                serial_update_ints(serial);
        }
}

/*Tandy might need COM1 at 2f8*/
void serial1_init(uint16_t addr, int irq)
{
        memset(&serial1, 0, sizeof(serial1));
        io_sethandler(addr, 0x0008, serial_read,  NULL, NULL, serial_write,  NULL, NULL, &serial1);
        serial1.irq = irq;
        serial1.rcr_callback = NULL;
        timer_add(serial_recieve_callback, &serial1.recieve_delay, &serial1.recieve_delay, &serial1);
}
void serial1_set(uint16_t addr, int irq)
{
        serial1_remove();
        io_sethandler(addr, 0x0008, serial_read,  NULL, NULL, serial_write,  NULL, NULL, &serial1);
        serial1.irq = irq;
}
void serial1_remove()
{
        io_removehandler(0x2e8, 0x0008, serial_read,  NULL, NULL, serial_write,  NULL, NULL, &serial1);
        io_removehandler(0x2f8, 0x0008, serial_read,  NULL, NULL, serial_write,  NULL, NULL, &serial1);
        io_removehandler(0x3e8, 0x0008, serial_read,  NULL, NULL, serial_write,  NULL, NULL, &serial1);
        io_removehandler(0x3f8, 0x0008, serial_read,  NULL, NULL, serial_write,  NULL, NULL, &serial1);
}

void serial2_init(uint16_t addr, int irq)
{
        memset(&serial2, 0, sizeof(serial2));
        io_sethandler(addr, 0x0008, serial_read, NULL, NULL, serial_write, NULL, NULL, &serial2);
        serial2.irq = irq;
        serial2.rcr_callback = NULL;
        timer_add(serial_recieve_callback, &serial2.recieve_delay, &serial2.recieve_delay, &serial2);
}
void serial2_set(uint16_t addr, int irq)
{
        serial2_remove();
        io_sethandler(addr, 0x0008, serial_read, NULL, NULL, serial_write, NULL, NULL, &serial2);
        serial2.irq = irq;
}
void serial2_remove()
{
        io_removehandler(0x2e8, 0x0008, serial_read, NULL, NULL, serial_write, NULL, NULL, &serial2);
        io_removehandler(0x2f8, 0x0008, serial_read, NULL, NULL, serial_write, NULL, NULL, &serial2);
        io_removehandler(0x3e8, 0x0008, serial_read, NULL, NULL, serial_write, NULL, NULL, &serial2);
        io_removehandler(0x3f8, 0x0008, serial_read, NULL, NULL, serial_write, NULL, NULL, &serial2);
}
