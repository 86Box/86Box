/*
 * 86Box    A hypervisor and IBM PC system emulator that specializes in
 *          running old operating systems and software designed for IBM
 *          PC systems and compatibles from 1981 through fairly recent
 *          system designs based on the PCI bus.
 *
 *          This file is part of the 86Box distribution.
 *
 *          Emulation of the EGA and Chips & Technologies SuperEGA
 *          graphics cards.
 *
 *
 *
 * Authors: Sarah Walker, <https://pcem-emulator.co.uk/>
 *          Miran Grca, <mgrca8@gmail.com>
 *
 *          Copyright 2008-2020 Sarah Walker.
 *          Copyright 2016-2020 Miran Grca.
 */

#ifndef VIDEO_EGA_H
#define VIDEO_EGA_H

#if defined(EMU_MEM_H) && defined(EMU_ROM_H)
typedef struct ega_t {
    mem_mapping_t mapping;

    rom_t bios_rom;

    uint8_t crtcreg;
    uint8_t gdcaddr;
    uint8_t attraddr;
    uint8_t attrff;
    uint8_t attr_palette_enable;
    uint8_t seqaddr;
    uint8_t miscout;
    uint8_t writemask;
    uint8_t la;
    uint8_t lb;
    uint8_t lc;
    uint8_t ld;
    uint8_t stat;
    uint8_t colourcompare;
    uint8_t colournocare;
    uint8_t scrblank;
    uint8_t plane_mask;
    uint8_t ctl_mode;
    uint8_t color_mux;
    uint8_t dot;
    uint8_t crtc[32];
    uint8_t gdcreg[16];
    uint8_t attrregs[32];
    uint8_t seqregs[64];
    uint8_t egapal[16];
    uint8_t regs[256];

    uint8_t *vram;

    uint16_t light_pen;

    int vidclock;
    int fast;
    int extvram;
    int vres;
    int readmode;
    int writemode;
    int readplane;
    int vrammask;
    int chain4;
    int chain2_read;
    int chain2_write;
    int con;
    int oddeven_page;
    int oddeven_chain;
    int vc;
    int sc;
    int dispon;
    int hdisp_on;
    int cursoron;
    int blink;
    int fullchange;
    int linepos;
    int vslines;
    int linecountff;
    int oddeven;
    int lowres;
    int interlace;
    int linedbl;
    int lindebl;
    int rowcount;
    int vtotal;
    int dispend;
    int vsyncstart;
    int split;
    int hdisp;
    int hdisp_old;
    int htotal;
    int hdisp_time;
    int rowoffset;
    int vblankstart;
    int scrollcache;
    int firstline;
    int lastline;
    int firstline_draw;
    int lastline_draw;
    int x_add;
    int y_add;
    int displine;
    int res_x;
    int res_y;
    int bpp;
    int index;
    int remap_required;
    int actual_type;
    int chipset;

    uint32_t charseta;
    uint32_t charsetb;
    uint32_t ma_latch;
    uint32_t ma;
    uint32_t maback;
    uint32_t ca;
    uint32_t vram_limit;
    uint32_t overscan_color;
    uint32_t cca;

    uint32_t *pallook;

    uint64_t   dispontime;
    uint64_t   dispofftime;

    uint64_t   dot_time;

    pc_timer_t timer;
    pc_timer_t dot_timer;

    double     dot_clock;

    void *     eeprom;

    uint32_t   (*remap_func)(struct ega_t *ega, uint32_t in_addr);
    void       (*render)(struct ega_t *svga);

    /*If set then another device is driving the monitor output and the EGA
      card should not attempt to display anything */
      void (*render_override)(void *priv);
      void *priv_parent;
} ega_t;
#endif

#ifdef EMU_DEVICE_H
extern const device_t ega_device;
extern const device_t cpqega_device;
extern const device_t sega_device;
extern const device_t atiega800p_device;
extern const device_t iskra_ega_device;
extern const device_t et2000_device;
extern const device_t jega_device;
#endif

extern int update_overscan;

#define DISPLAY_RGB          0
#define DISPLAY_COMPOSITE    1
#define DISPLAY_RGB_NO_BROWN 2
#define DISPLAY_GREEN        3
#define DISPLAY_AMBER        4
#define DISPLAY_WHITE        5

#if defined(EMU_MEM_H) && defined(EMU_ROM_H)
extern void ega_init(ega_t *ega, int monitor_type, int is_mono);
extern void ega_recalctimings(struct ega_t *ega);
extern void ega_recalc_remap_func(struct ega_t *ega);
#endif

extern void    ega_out(uint16_t addr, uint8_t val, void *priv);
extern uint8_t ega_in(uint16_t addr, void *priv);
extern void    ega_poll(void *priv);
extern void    ega_write(uint32_t addr, uint8_t val, void *priv);
extern uint8_t ega_read(uint32_t addr, void *priv);

extern int firstline_draw;
extern int lastline_draw;
extern int displine;
extern int sc;

extern uint32_t ma;
extern uint32_t ca;
extern int      con;
extern int      cursoron;
extern int      cgablink;

extern int scrollcache;

extern uint8_t edatlookup[4][4];
extern uint8_t egaremap2bpp[256];

#if defined(EMU_MEM_H) && defined(EMU_ROM_H)
void ega_render_blank(ega_t *ega);

void ega_render_overscan_left(ega_t *ega);
void ega_render_overscan_right(ega_t *ega);

void ega_render_text(ega_t *ega);
void ega_render_graphics(ega_t *ega);
#endif

enum {
  EGA_IBM = 0,
  EGA_COMPAQ,
  EGA_SUPEREGA,
  EGA_ATI800P,
  EGA_ISKRA,
  EGA_TSENG
};

enum {
  EGA_TYPE_IBM    = 0,
  EGA_TYPE_OTHER  = 1,
  EGA_TYPE_COMPAQ = 2
};

#endif /*VIDEO_EGA_H*/
