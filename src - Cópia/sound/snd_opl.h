/* Copyright holders: Sarah Walker
   see COPYING for more details
*/
typedef struct opl_t
{
        int chip_nr[2];
        
        int64_t timers[2][2];
        int64_t timers_enable[2][2];

        int16_t filtbuf[2];

        int16_t buffer[SOUNDBUFLEN * 2];
        int     pos;
} opl_t;

uint8_t opl2_read(uint16_t a, void *priv);
void opl2_write(uint16_t a, uint8_t v, void *priv);
uint8_t opl2_l_read(uint16_t a, void *priv);
void opl2_l_write(uint16_t a, uint8_t v, void *priv);
uint8_t opl2_r_read(uint16_t a, void *priv);
void opl2_r_write(uint16_t a, uint8_t v, void *priv);
uint8_t opl3_read(uint16_t a, void *priv);
void opl3_write(uint16_t a, uint8_t v, void *priv);

void opl2_poll(opl_t *opl, int16_t *bufl, int16_t *bufr);
void opl3_poll(opl_t *opl, int16_t *bufl, int16_t *bufr);

void opl2_init(opl_t *opl);
void opl3_init(opl_t *opl);

void opl2_update2(opl_t *opl);
void opl3_update2(opl_t *opl);
