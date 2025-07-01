#ifndef VIDEO_ATI_EEPROM_H
#define VIDEO_ATI_EEPROM_H

/* Copyright holders: Sarah Walker
   see COPYING for more details
*/

enum {
    EEPROM_IDLE,
    EEPROM_WAIT,
    EEPROM_OPCODE,
    EEPROM_INPUT,
    EEPROM_OUTPUT
};

enum {
    EEPROM_OP_EW    = 4,
    EEPROM_OP_WRITE = 5,
    EEPROM_OP_READ  = 6,
    EEPROM_OP_ERASE = 7,

    EEPROM_OP_WRALMAIN = -1
};

enum {
    EEPROM_OP_EWDS = 0,
    EEPROM_OP_WRAL = 1,
    EEPROM_OP_ERAL = 2,
    EEPROM_OP_EWEN = 3
};

typedef struct ati_eeprom_t {
    uint16_t data[256];

    int      oldclk;
    int      oldena;
    int      opcode;
    int      state;
    int      count;
    int      out;
    int      wp;
    uint32_t dat;
    int      type;
    int      address;

    char fn[256];
} ati_eeprom_t;

void ati_eeprom_load(ati_eeprom_t *eeprom, char *fn, int type);
void ati_eeprom_load_mach8(ati_eeprom_t *eeprom, char *fn, int mca);
void ati_eeprom_load_mach8_vga(ati_eeprom_t *eeprom, char *fn);
void ati_eeprom_write(ati_eeprom_t *eeprom, int ena, int clk, int dat);
int  ati_eeprom_read(ati_eeprom_t *eeprom);

#endif /*VIDEO_ATI_EEPROM_H*/
