#include "ibm.h"
#include "vid_ati_eeprom.h"

enum
{
        EEPROM_IDLE,
        EEPROM_WAIT,
        EEPROM_OPCODE,
        EEPROM_INPUT,
        EEPROM_OUTPUT
};

enum
{
        EEPROM_OP_EW    = 4,
        EEPROM_OP_WRITE = 5,
        EEPROM_OP_READ  = 6,
        EEPROM_OP_ERASE = 7,
        
        EEPROM_OP_WRALMAIN = -1
};

enum
{
        EEPROM_OP_EWDS = 0,
        EEPROM_OP_WRAL = 1,
        EEPROM_OP_ERAL = 2,
        EEPROM_OP_EWEN = 3
};

void ati_eeprom_load(ati_eeprom_t *eeprom, char *fn, int type)
{
        FILE *f;
        eeprom->type = type;
        strcpy(eeprom->fn, fn);
        f = romfopen(eeprom->fn, "rb");
        if (!f)
        {
                memset(eeprom->data, 0, eeprom->type ? 512 : 128);
                return;
        }
        fread(eeprom->data, 1, eeprom->type ? 512 : 128, f);
        fclose(f);
}

void ati_eeprom_save(ati_eeprom_t *eeprom)
{
        FILE *f = romfopen(eeprom->fn, "wb");
        if (!f) return;
        fwrite(eeprom->data, 1, eeprom->type ? 512 : 128, f);
        fclose(f);
}

void ati_eeprom_write(ati_eeprom_t *eeprom, int ena, int clk, int dat)
{
        int c;
//        pclog("EEPROM write %i %i %i\n", ena, clk, dat);
        if (!ena)
        {
                eeprom->out = 1;
        }
        if (clk && !eeprom->oldclk)
        {
                if (ena && !eeprom->oldena)
                {
                        eeprom->state = EEPROM_WAIT;
                        eeprom->opcode = 0;
                        eeprom->count = 3;
                        eeprom->out = 1;
                }
                else if (ena)
                {
//                        pclog("EEPROM receive %i %i %i\n", ena, clk, dat);
                        switch (eeprom->state)
                        {
                                case EEPROM_WAIT:
                                if (!dat)
                                        break;
                                eeprom->state = EEPROM_OPCODE;
                                /* fall through */
                                case EEPROM_OPCODE:
                                eeprom->opcode = (eeprom->opcode << 1) | (dat ? 1 : 0);
                                eeprom->count--;
                                if (!eeprom->count)
                                {
//                                        pclog("EEPROM opcode - %i\n", eeprom->opcode);
                                        switch (eeprom->opcode)
                                        {
                                                case EEPROM_OP_WRITE:
                                                eeprom->count = eeprom->type ? 24 : 22;
                                                eeprom->state = EEPROM_INPUT;
                                                eeprom->dat = 0;
                                                break;
                                                case EEPROM_OP_READ:
                                                eeprom->count = eeprom->type ? 8 : 6;
                                                eeprom->state = EEPROM_INPUT;
                                                eeprom->dat = 0;
                                                break;
                                                case EEPROM_OP_EW:
                                                eeprom->count = 2;
                                                eeprom->state = EEPROM_INPUT;
                                                eeprom->dat = 0;
                                                break;
                                                case EEPROM_OP_ERASE:
                                                eeprom->count = eeprom->type ? 8 : 6;
                                                eeprom->state = EEPROM_INPUT;
                                                eeprom->dat = 0;
                                                break;
                                        }
                                }
                                break;
                                
                                case EEPROM_INPUT:
                                eeprom->dat = (eeprom->dat << 1) | (dat ? 1 : 0);
                                eeprom->count--;
                                if (!eeprom->count)
                                {
//                                        pclog("EEPROM dat - %02X\n", eeprom->dat);
                                        switch (eeprom->opcode)
                                        {
                                                case EEPROM_OP_WRITE:
//                                                pclog("EEPROM_OP_WRITE addr %02X eeprom_dat %04X\n", (eeprom->dat >> 16) & (eeprom->type ? 255 : 63), eeprom->dat & 0xffff);
                                                if (!eeprom->wp)
                                                {
                                                        eeprom->data[(eeprom->dat >> 16) & (eeprom->type ? 255 : 63)] = eeprom->dat & 0xffff;
                                                        ati_eeprom_save(eeprom);
                                                }
                                                eeprom->state = EEPROM_IDLE;
                                                eeprom->out = 1;
                                                break;

                                                case EEPROM_OP_READ:
                                                eeprom->count = 17;
                                                eeprom->state = EEPROM_OUTPUT;
                                                eeprom->dat = eeprom->data[eeprom->dat];
//                                                pclog("Trigger EEPROM_OUTPUT %04X\n", eeprom->dat);
                                                break;
                                                case EEPROM_OP_EW:
//                                                pclog("EEPROM_OP_EW %i\n", eeprom->dat);
                                                switch (eeprom->dat)
                                                {
                                                        case EEPROM_OP_EWDS:
                                                        eeprom->wp = 1;
                                                        break;
                                                        case EEPROM_OP_WRAL:
                                                        eeprom->opcode = EEPROM_OP_WRALMAIN;
                                                        eeprom->count = 20;
                                                        break;
                                                        case EEPROM_OP_ERAL:
                                                        if (!eeprom->wp)
                                                        {
                                                                memset(eeprom->data, 0xff, 128);
                                                                ati_eeprom_save(eeprom);
                                                        }
                                                        break;
                                                        case EEPROM_OP_EWEN:
                                                        eeprom->wp = 0;
                                                        break;
                                                }
                                                eeprom->state = EEPROM_IDLE;
                                                eeprom->out = 1;
                                                break;

                                                case EEPROM_OP_ERASE:
//                                                pclog("EEPROM_OP_ERASE %i\n", eeprom->dat);
                                                if (!eeprom->wp)
                                                {
                                                        eeprom->data[eeprom->dat] = 0xffff;
                                                        ati_eeprom_save(eeprom);
                                                }
                                                eeprom->state = EEPROM_IDLE;
                                                eeprom->out = 1;
                                                break;

                                                case EEPROM_OP_WRALMAIN:
//                                                pclog("EEPROM_OP_WRAL %04X\n", eeprom->dat);
                                                if (!eeprom->wp)
                                                {
                                                        for (c = 0; c < 256; c++)
                                                                eeprom->data[c] = eeprom->dat;
                                                        ati_eeprom_save(eeprom);
                                                }
                                                eeprom->state = EEPROM_IDLE;
                                                eeprom->out = 1;
                                                break;
                                        }
                                }
                                break;
                        }
                }                
                eeprom->oldena = ena;
        }
        else if (!clk && eeprom->oldclk)
        {
                if (ena)
                {
                        switch (eeprom->state)
                        {
                                case EEPROM_OUTPUT:
                                eeprom->out = (eeprom->dat & 0x10000) ? 1 : 0;
                                eeprom->dat <<= 1;
//                                pclog("EEPROM_OUTPUT - data %i\n", eeprom->out);
                                eeprom->count--;
                                if (!eeprom->count)
                                {
//                                        pclog("EEPROM_OUTPUT complete\n");
                                        eeprom->state = EEPROM_IDLE;
                                }
                                break;
                        }
                }
        }
        eeprom->oldclk = clk;
}

int ati_eeprom_read(ati_eeprom_t *eeprom)
{
        return eeprom->out;
}

