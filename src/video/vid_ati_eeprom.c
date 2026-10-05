/*
 * 86Box    A hypervisor and IBM PC system emulator that specializes in
 *          running old operating systems and software designed for IBM
 *          PC systems and compatibles from 1981 through fairly recent
 *          system designs based on the PCI bus.
 *
 *          This file is part of the 86Box distribution.
 *
 *          Emulation of the EEPROM on select ATI cards.
 *
 * Authors: Sarah Walker, <https://pcem-emulator.co.uk/>
 *          Miran Grca, <mgrca8@gmail.com>
 *
 *          Copyright 2008-2020 Sarah Walker.
 *          Copyright 2016-2020 Miran Grca.
 */
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <wchar.h>
#include <86box/86box.h>
#include <86box/device.h>
#include <86box/mem.h>
#include <86box/timer.h>
#include <86box/nvr.h>
#include <86box/vid_ati_eeprom.h>
#include <86box/plat_fallthrough.h>

void
ati_eeprom_load(ati_eeprom_t *eeprom, char *fn, int type)
{
    FILE *fp;
    int   size;
    int   n;
    eeprom->type = type;
    strncpy(eeprom->fn, fn, sizeof(eeprom->fn) - 1);
    fp   = nvr_fopen(eeprom->fn, "rb");
    size = eeprom->type ? 512 : 128;
    /* The 93Cx6 parts power up with erase and write disabled (EWDS). */
    eeprom->wp = 1;
    if (!fp) {
        memset(eeprom->data, 0xff, size);
        return;
    }
    /* What the file does not hold is erased. */
    n = (int) fread(eeprom->data, 1, size, fp);
    if (n < size)
        memset((uint8_t *) eeprom->data + n, 0xff, size - n);

    fclose(fp);
}

void ati_eeprom_save(ati_eeprom_t *eeprom);

/* As ati_eeprom_load(), but a card whose file is missing starts from
   `image` -- the EEPROM as the card's own configuration utility leaves it
   when it first sets the card up -- and the file is written with it. The
   rest of the part stays erased. */
void
ati_eeprom_load_default(ati_eeprom_t *eeprom, char *fn, int type, const uint16_t *image, int words)
{
    FILE *fp;

    eeprom->type = type;
    strncpy(eeprom->fn, fn, sizeof(eeprom->fn) - 1);
    fp = nvr_fopen(eeprom->fn, "rb");
    if (fp) {
        fclose(fp);
        ati_eeprom_load(eeprom, fn, type);
        return;
    }
    eeprom->wp = 1;
    memset(eeprom->data, 0xff, type ? 512 : 128);
    memcpy(eeprom->data, image, words * sizeof(uint16_t));
    ati_eeprom_save(eeprom);
}

void
ati_eeprom_load_mach8(ati_eeprom_t *eeprom, char *fn, int mca)
{
    FILE *fp;
    int   size;
    eeprom->type = 0;
    eeprom->wp   = 1;
    strncpy(eeprom->fn, fn, sizeof(eeprom->fn) - 1);
    fp   = nvr_fopen(eeprom->fn, "rb");
    size = 128;
    if (fp == NULL) {
        if (mca) {
            memset(eeprom->data + 2, 0xff, size - 2);
            fp = nvr_fopen(eeprom->fn, "wb");
            fwrite(eeprom->data, 1, size, fp);
            fclose(fp);
        } else
            memset(eeprom->data, 0xff, size);
        return;
    }
    if (fread(eeprom->data, 1, size, fp) != size)
        memset(eeprom->data, 0, size);

    fclose(fp);
}

void
ati_eeprom_load_mach8_vga(ati_eeprom_t *eeprom, char *fn)
{
    FILE *fp;
    int   size;
    eeprom->type = 0;
    eeprom->wp   = 1;
    strncpy(eeprom->fn, fn, sizeof(eeprom->fn) - 1);
    fp   = nvr_fopen(eeprom->fn, "rb");
    size = 128;
    if (!fp) { /*The ATI Graphics Ultra bios expects a fresh nvram zero'ed at boot time otherwise
            it would hang the machine.*/
        memset(eeprom->data, 0, size);
        fp = nvr_fopen(eeprom->fn, "wb");
        fwrite(eeprom->data, 1, size, fp);
        fclose(fp);
        return;
    }
    if (fread(eeprom->data, 1, size, fp) != size)
        memset(eeprom->data, 0, size);

    fclose(fp);
}

void
ati_eeprom_save(ati_eeprom_t *eeprom)
{
    FILE *fp = nvr_fopen(eeprom->fn, "wb");
    if (!fp)
        return;
    fwrite(eeprom->data, 1, eeprom->type ? 512 : 128, fp);
    fclose(fp);
}

void
ati_eeprom_write(ati_eeprom_t *eeprom, int ena, int clk, int dat)
{
    /* Chip select acts by level, clock or none, as on the 93Cx6 parts:
       dropping it ends whatever command was under way, and raising it
       leaves the part waiting for a start bit. Taken only at a rising
       clock, a select pulsed low between two commands with the clock held
       low went unseen, and the commands ran together. */
    if (!ena) {
        eeprom->out = 1;
        if (eeprom->oldena)
            eeprom->state = EEPROM_IDLE;
    } else if (!eeprom->oldena) {
        eeprom->state  = EEPROM_WAIT;
        eeprom->opcode = 0;
        eeprom->count  = 3;
        eeprom->out    = 1;
    }
    eeprom->oldena = ena;

    if (clk && !eeprom->oldclk) {
        if (ena) {
            switch (eeprom->state) {
                case EEPROM_WAIT:
                    if (!dat)
                        break;
                    eeprom->state = EEPROM_OPCODE;
                    fallthrough;
                case EEPROM_OPCODE:
                    eeprom->opcode = (eeprom->opcode << 1) | (dat ? 1 : 0);
                    eeprom->count--;
                    if (!eeprom->count) {
                        switch (eeprom->opcode) {
                            case EEPROM_OP_WRITE:
                                eeprom->count = eeprom->type ? 24 : 22;
                                eeprom->state = EEPROM_INPUT;
                                eeprom->dat   = 0;
                                break;
                            case EEPROM_OP_READ:
                                eeprom->count = eeprom->type ? 8 : 6;
                                eeprom->state = EEPROM_INPUT;
                                eeprom->dat   = 0;
                                break;
                            case EEPROM_OP_EW:
                                eeprom->count = 2;
                                eeprom->state = EEPROM_INPUT;
                                eeprom->dat   = 0;
                                break;
                            case EEPROM_OP_ERASE:
                                eeprom->count = eeprom->type ? 8 : 6;
                                eeprom->state = EEPROM_INPUT;
                                eeprom->dat   = 0;
                                break;

                            default:
                                break;
                        }
                    }
                    break;

                case EEPROM_INPUT:
                    eeprom->dat = (eeprom->dat << 1) | (dat ? 1 : 0);
                    eeprom->count--;
                    if (!eeprom->count) {
                        switch (eeprom->opcode) {
                            case EEPROM_OP_WRITE:
                                if (!eeprom->wp) {
                                    eeprom->data[(eeprom->dat >> 16) & (eeprom->type ? 255 : 63)] = eeprom->dat & 0xffff;
                                    ati_eeprom_save(eeprom);
                                }
                                eeprom->state = EEPROM_IDLE;
                                eeprom->out   = 1;
                                break;

                            case EEPROM_OP_READ:
                                /* A dummy 0 follows the last address bit, and
                                   D15..D0 come out on the rising edges after
                                   it (93C46/93C56 data sheets). */
                                eeprom->count = 16;
                                eeprom->state = EEPROM_OUTPUT;
                                eeprom->dat   = eeprom->data[eeprom->dat];
                                eeprom->out   = 0;
                                break;
                            case EEPROM_OP_EW:
                                switch (eeprom->dat) {
                                    case EEPROM_OP_EWDS:
                                        eeprom->wp = 1;
                                        break;
                                    case EEPROM_OP_WRAL:
                                        /* The rest of the address field -- four bits on
                                           the 64-word part, six on the 256-word one, the
                                           two above them being this sub-command -- then
                                           the 16-bit word to write everywhere. */
                                        eeprom->opcode = EEPROM_OP_WRALMAIN;
                                        eeprom->count  = eeprom->type ? 22 : 20;
                                        break;
                                    case EEPROM_OP_ERAL:
                                        /* Erase all: every word, not the first 128 bytes. */
                                        if (!eeprom->wp) {
                                            memset(eeprom->data, 0xff, eeprom->type ? 512 : 128);
                                            ati_eeprom_save(eeprom);
                                        }
                                        break;
                                    case EEPROM_OP_EWEN:
                                        eeprom->wp = 0;
                                        break;

                                    default:
                                        break;
                                }
                                /* Write-all goes on to take its address bits and data word. */
                                if (eeprom->opcode != EEPROM_OP_WRALMAIN) {
                                    eeprom->state = EEPROM_IDLE;
                                    eeprom->out   = 1;
                                } else
                                    eeprom->dat = 0;
                                break;

                            case EEPROM_OP_ERASE:
                                if (!eeprom->wp) {
                                    eeprom->data[eeprom->dat] = 0xffff;
                                    ati_eeprom_save(eeprom);
                                }
                                eeprom->state = EEPROM_IDLE;
                                eeprom->out   = 1;
                                break;

                            case EEPROM_OP_WRALMAIN:
                                if (!eeprom->wp) {
                                    for (uint16_t c = 0; c < (eeprom->type ? 256 : 64); c++)
                                        eeprom->data[c] = eeprom->dat;
                                    ati_eeprom_save(eeprom);
                                }
                                eeprom->state = EEPROM_IDLE;
                                eeprom->out   = 1;
                                break;

                            default:
                                break;
                        }
                    }
                    break;

                case EEPROM_OUTPUT:
                    eeprom->out = (eeprom->dat & 0x8000) ? 1 : 0;
                    eeprom->dat <<= 1;
                    eeprom->count--;
                    if (!eeprom->count)
                        eeprom->state = EEPROM_IDLE;
                    break;

                default:
                    break;
            }
        }
    }
    eeprom->oldclk = clk;
}

int
ati_eeprom_read(ati_eeprom_t *eeprom)
{
    return eeprom->out;
}
