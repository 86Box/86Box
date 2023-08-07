/* Copyright holders: Sarah Walker
   see COPYING for more details
*/

#ifndef VIDEO_MDA_H
#define VIDEO_MDA_H

typedef struct mda_t {
    mem_mapping_t mapping;

    uint8_t crtc[32];
    int     crtcreg;

    uint8_t ctrl;
    uint8_t stat;

    uint64_t   dispontime;
    uint64_t   dispofftime;
    pc_timer_t timer;

    int firstline;
    int lastline;

    int      linepos;
    int      displine;
    int      vc;
    int      sc;
    uint16_t ma;
    uint16_t maback;
    int      con;
    int      coff;
    int      cursoron;
    int      dispon;
    int      blink;
    int      vsynctime;
    int      vadj;
    int      monitor_index;
    int      prev_monitor_index;

    uint8_t *vram;
} mda_t;

#define VIDEO_MONITOR_PROLOGUE()                        \
    {                                                   \
        mda->prev_monitor_index = monitor_index_global; \
        monitor_index_global    = mda->monitor_index;   \
    }
#define VIDEO_MONITOR_EPILOGUE()                        \
    {                                                   \
        monitor_index_global = mda->prev_monitor_index; \
    }

void    mda_init(mda_t *mda);
void    mda_setcol(int chr, int blink, int fg, uint8_t cga_ink);
void    mda_out(uint16_t addr, uint8_t val, void *priv);
uint8_t mda_in(uint16_t addr, void *priv);
void    mda_write(uint32_t addr, uint8_t val, void *priv);
uint8_t mda_read(uint32_t addr, void *priv);
void    mda_recalctimings(mda_t *mda);
void    mda_poll(void *priv);

#ifdef EMU_DEVICE_H
extern const device_t mda_device;
#endif

#endif /*VIDEO_MDA_H*/
