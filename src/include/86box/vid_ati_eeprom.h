/* Copyright holders: Sarah Walker
   see COPYING for more details
*/
typedef struct ati_eeprom_t
{
        uint16_t data[256];

        int oldclk, oldena;
        int opcode, state, count, out;
        int wp;
        uint32_t dat;
        int type;

        wchar_t fn[256];
} ati_eeprom_t;

void ati_eeprom_load(ati_eeprom_t *eeprom, wchar_t *fn, int type);
void ati_eeprom_write(ati_eeprom_t *eeprom, int ena, int clk, int dat);
int ati_eeprom_read(ati_eeprom_t *eeprom);
