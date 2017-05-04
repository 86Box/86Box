#include "ibm.h"
#include "io.h"
#include "plat-midi.h"
#include "sound_mpu401_uart.h"

enum
{
        STATUS_OUTPUT_NOT_READY = 0x40,
        STATUS_INPUT_NOT_READY  = 0x80
};

static void mpu401_uart_write(uint16_t addr, uint8_t val, void *p)
{
        mpu401_uart_t *mpu = (mpu401_uart_t *)p;
        
        if (addr & 1) /*Command*/
        {
                switch (val)
                {
                        case 0xff: /*Reset*/
                        mpu->rx_data = 0xfe; /*Acknowledge*/
                        mpu->status = 0;
                        mpu->uart_mode = 0;
                        break;
                        
                        case 0x3f: /*Enter UART mode*/
                        mpu->rx_data = 0xfe; /*Acknowledge*/
                        mpu->status = 0;
                        mpu->uart_mode = 1;
                        break;
                }
                return;
        }
                        
        /*Data*/
        if (mpu->uart_mode)
                midi_write(val);
}

static uint8_t mpu401_uart_read(uint16_t addr, void *p)
{
        mpu401_uart_t *mpu = (mpu401_uart_t *)p;
        
        if (addr & 1) /*Status*/
                return mpu->status;
        
        /*Data*/
        mpu->status |= STATUS_INPUT_NOT_READY;
        return mpu->rx_data;
}

void mpu401_uart_init(mpu401_uart_t *mpu, uint16_t addr)
{
        mpu->status = STATUS_INPUT_NOT_READY;
        mpu->uart_mode = 0;
        
        io_sethandler(addr, 0x0002, mpu401_uart_read, NULL, NULL, mpu401_uart_write, NULL, NULL, mpu);
}
