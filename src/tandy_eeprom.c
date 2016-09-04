#include <stdlib.h>
#include "ibm.h"
#include "device.h"
#include "tandy_eeprom.h"

typedef struct
{
        int state;
        int count;
        int addr;
        int clock;
        uint16_t data;
        uint16_t store[64];
        
        int romset;
} tandy_eeprom_t;

enum
{
        EEPROM_IDLE,
        EEPROM_GET_OPERATION,
        EEPROM_READ,
        EEPROM_WRITE
};

static int eeprom_data_out;

void tandy_eeprom_write(uint16_t addr, uint8_t val, void *p)
{
        tandy_eeprom_t *eeprom = (tandy_eeprom_t *)p;
        
        if ((val & 4) && !eeprom->clock)
        {
//                pclog("eeprom_write %02x %i %i\n", val, eeprom->state, eeprom->count);
                switch (eeprom->state)
                {
                        case EEPROM_IDLE:
                        switch (eeprom->count)
                        {
                                case 0:
                                if (!(val & 3))
                                        eeprom->count = 1;
                                else
                                        eeprom->count = 0;
                                break;
                                case 1:
                                if ((val & 3) == 2)
                                        eeprom->count = 2;
                                else
                                        eeprom->count = 0;
                                break;
                                case 2:
                                if ((val & 3) == 3)
                                        eeprom->state = EEPROM_GET_OPERATION;
                                eeprom->count = 0;
                                break;
                        }
                        break;
                        case EEPROM_GET_OPERATION:
                        eeprom->data = (eeprom->data << 1) | (val & 1);
                        eeprom->count++;
                        if (eeprom->count == 8)
                        {
                                eeprom->count = 0;
//                                pclog("EEPROM get operation %02x\n", eeprom->data);
                                eeprom->addr = eeprom->data & 0x3f;
                                switch (eeprom->data & 0xc0)
                                {
                                        case 0x40:
                                        eeprom->state = EEPROM_WRITE;
                                        break;
                                        case 0x80:
                                        eeprom->state = EEPROM_READ;
                                        eeprom->data = eeprom->store[eeprom->addr];
//                                        pclog("EEPROM read data %02x %04x\n", eeprom->addr, eeprom->data);
                                        break;
                                        default:
                                        eeprom->state = EEPROM_IDLE;
                                        break;
                                }
                        }
                        break;
                        
                        case EEPROM_READ:
                        eeprom_data_out = eeprom->data & 0x8000;
                        eeprom->data <<= 1;
                        eeprom->count++;
                        if (eeprom->count == 16)
                        {
                                eeprom->count = 0;
                                eeprom->state = EEPROM_IDLE;
                        }
                        break;
                        case EEPROM_WRITE:
                        eeprom->data = (eeprom->data << 1) | (val & 1);
                        eeprom->count++;
                        if (eeprom->count == 16)
                        {
                                eeprom->count = 0;
                                eeprom->state = EEPROM_IDLE;
//                                pclog("EEPROM write %04x to %02x\n", eeprom->data, eeprom->addr);
                                eeprom->store[eeprom->addr] = eeprom->data;
                        }
                        break;
                }
        }
        
        eeprom->clock = val & 4;
}

int tandy_eeprom_read()
{
//        pclog("tandy_eeprom_read: data_out=%x\n", eeprom_data_out);
        return eeprom_data_out;
}

void *tandy_eeprom_init()
{
        tandy_eeprom_t *eeprom = malloc(sizeof(tandy_eeprom_t));
        FILE *f;

        memset(eeprom, 0, sizeof(tandy_eeprom_t));
        
        eeprom->romset = romset;
        switch (romset)
        {
                case ROM_TANDY1000HX:
                f = romfopen("nvr/tandy1000hx.bin" ,"rb");
                break;
                case ROM_TANDY1000SL2:
                f = romfopen("nvr/tandy1000sl2.bin" ,"rb");
                break;
        }
        if (f)
        {
                fread(eeprom->store, 128, 1, f);
                fclose(f);
        }
        else
                memset(eeprom->store, 0, 128);

        io_sethandler(0x037c, 0x0001, NULL, NULL, NULL, tandy_eeprom_write, NULL, NULL, eeprom);
        
        return eeprom;
}

void tandy_eeprom_close(void *p)
{
        tandy_eeprom_t *eeprom = (tandy_eeprom_t *)p;
        FILE *f;
                
        switch (eeprom->romset)
        {
                case ROM_TANDY1000HX:
                f = romfopen("nvr/tandy1000hx.bin" ,"wb");
                break;
                case ROM_TANDY1000SL2:
                f = romfopen("nvr/tandy1000sl2.bin" ,"wb");
                break;
        }
        fwrite(eeprom->store, 128, 1, f);
        fclose(f);

        free(eeprom);
}

device_t tandy_eeprom_device =
{
        "Tandy EEPROM",
        0,
        tandy_eeprom_init,
        tandy_eeprom_close,
        NULL,
        NULL,
        NULL,
        NULL,
        NULL
};
