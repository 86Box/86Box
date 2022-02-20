/* Copyright holders: Sarah Walker
   see COPYING for more details
*/

#ifndef VIDEO_MDA_H
# define VIDEO_MDA_H

typedef struct mda_t
{
        mem_mapping_t mapping;

        uint8_t crtc[32];
        int crtcreg;

        uint8_t ctrl, stat;

        uint64_t dispontime, dispofftime;
        pc_timer_t timer;

        int firstline, lastline;

        int linepos, displine;
        int vc, sc;
        uint16_t ma, maback;
        int con, coff, cursoron;
        int dispon, blink;
        int vsynctime;
	int vadj;

        uint8_t *vram;
} mda_t;

void    mda_init(mda_t *mda);
void 	mda_setcol(int chr, int blink, int fg, uint8_t cga_ink);
void    mda_out(uint16_t addr, uint8_t val, void *p);
uint8_t mda_in(uint16_t addr, void *p);
void    mda_write(uint32_t addr, uint8_t val, void *p);
uint8_t mda_read(uint32_t addr, void *p);
void    mda_recalctimings(mda_t *mda);
void    mda_poll(void *p);

#ifdef EMU_DEVICE_H
extern const device_t mda_device;
#endif

#endif /*VIDEO_MDA_H*/
