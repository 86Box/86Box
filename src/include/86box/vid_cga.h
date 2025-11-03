/*
 * 86Box    A hypervisor and IBM PC system emulator that specializes in
 *          running old operating systems and software designed for IBM
 *          PC systems and compatibles from 1981 through fairly recent
 *          system designs based on the PCI bus.
 *
 *          This file is part of the 86Box distribution.
 *
 *          Emulation of the old and new IBM CGA graphics cards.
 *
 * Authors: Sarah Walker, <https://pcem-emulator.co.uk/>
 *          Miran Grca, <mgrca8@gmail.com>
 *          Connor Hyde / starfrost, <mario64crashed@gmail.com>
 *
 *          Copyright 2008-2018 Sarah Walker.
 *          Copyright 2016-2018 Miran Grca.
 *          Copyright 2025      starfrost (refactoring).
 */
#ifndef VIDEO_CGA_H
#define VIDEO_CGA_H

// Mode flags for the CGA.
// Set by writing to 3D8
typedef enum cga_mode_flags_e {
    CGA_MODE_FLAG_HIGHRES = 1 << 0,                     // 80-column text mode
    CGA_MODE_FLAG_GRAPHICS = 1 << 1,                    // Graphics mode
    CGA_MODE_FLAG_BW = 1 << 2,                          // Black and white 
    CGA_MODE_FLAG_VIDEO_ENABLE = 1 << 3,                // 0 = no video (as if the video was 0)
    CGA_MODE_FLAG_HIGHRES_GRAPHICS = 1 << 4,            // 640*200 mode. Corrupts text mode if CGA_MODE_FLAG_GRAPHICS not set.
    CGA_MODE_FLAG_BLINK = 1 << 5,                       // If this is set, bit 5 of textmode characters blinks. Otherwise it is a high-intensity bg mode.
} cga_mode_flags;

// Motorola MC6845 CRTC registers
typedef enum cga_crtc_registers_e {
    CGA_CRTC_HTOTAL = 0x0,                              // Horizontal total (total number of characters incl. hsync)
    CGA_CRTC_HDISP = 0x1,                               // Horizontal display 
    CGA_CRTC_HSYNC_POS = 0x2,                           // Horizontal position of horizontal ysnc
    CGA_CRTC_HSYNC_WIDTH = 0x3,                         // Width of horizontal sync
    CGA_CRTC_VTOTAL = 0x4,                              // Vertical total (total number of scanlines incl. vsync)
    CGA_CRTC_VTOTAL_ADJUST = 0x5,                       // Vertical total adjust value
    CGA_CRTC_VDISP = 0x6,                               // Vertical display (total number of displayed scanline)
    CGA_CRTC_VSYNC = 0x7,                               // Vertical sync scanline number
    CGA_CRTC_INTERLACE = 0x8,                           // Interlacing mode
    CGA_CRTC_MAX_SCANLINE_ADDR = 0x9,                   // Maximum scanline address
    CGA_CRTC_CURSOR_START = 0xA,                        // Cursor start scanline
    CGA_CRTC_CURSOR_END = 0xB,                          // Cursor end scanline
    CGA_CRTC_START_ADDR_HIGH = 0xC,                     // Screen start address high 8 bits
    CGA_CRTC_START_ADDR_LOW = 0xD,                      // Screen start address low 8 bits
    CGA_CRTC_CURSOR_ADDR_HIGH = 0xE,                    // Cursor address high 8 bits
    CGA_CRTC_CURSOR_ADDR_LOW = 0xF,                     // Cursor address low 8 bits
    CGA_CRTC_LIGHT_PEN_ADDR_HIGH = 0x10,                // Light pen address high 8 bits (not currently supported)
    CGA_CRTC_LIGHT_PEN_ADDR_LOW = 0x11,                 // Light pen address low 8 bits (not currently supported)
} cga_crtc_registers;

// Registers for the CGA
typedef enum cga_registers_e {
    CGA_REGISTER_CRTC_INDEX = 0x3D4,
    CGA_REGISTER_CRTC_DATA = 0x3D5,
    CGA_REGISTER_MODE_CONTROL = 0x3D8,
    CGA_REGISTER_COLOR_SELECT = 0x3D9,
    CGA_REGISTER_STATUS = 0x3DA,
    CGA_REGISTER_CLEAR_LIGHT_PEN_LATCH = 0x3DB,
    CGA_REGISTER_SET_LIGHT_PEN_LATCH = 0x3DC,
} cga_registers;

#define CGA_NUM_CRTC_REGS   32

typedef struct cga_t {
    mem_mapping_t mapping;

    int     crtcreg;
    uint8_t crtc[CGA_NUM_CRTC_REGS];

    uint8_t cgastat;

    uint8_t cgamode;
    uint8_t cgacol;

    uint8_t lp_strobe;

    int      fontbase;
    int      linepos;
    int      displine;
    int      scanline;
    int      vc;
    int      cgadispon;
    int      cursorvisible;             // Determines if the cursor is visible FOR THE CURRENT SCANLINE.
    int      cursoron;
    int      cgablink;
    int      vsynctime;
    int      vadj;
    uint16_t memaddr;
    uint16_t memaddr_backup;
    int      oddeven;

    uint64_t   dispontime;
    uint64_t   dispofftime;
    pc_timer_t timer;

    int firstline;
    int lastline;

    int drawcursor;

    int fullchange;

    uint8_t *vram;

    uint8_t charbuffer[256];

    int revision;
    int composite;
    int snow_enabled;
    int rgb_type;
    int double_type;
} cga_t;

extern void    cga_init(cga_t *cga);
extern void    cga_out(uint16_t addr, uint8_t val, void *priv);
extern uint8_t cga_in(uint16_t addr, void *priv);
extern void    cga_write(uint32_t addr, uint8_t val, void *priv);
extern uint8_t cga_read(uint32_t addr, void *priv);
extern void    cga_recalctimings(cga_t *cga);
extern void    cga_interpolate_init(void);
extern void    cga_blit_memtoscreen(int x, int y, int w, int h, int double_type);
extern void    cga_do_blit(int vid_xsize, int firstline, int lastline, int double_type);
extern void    cga_poll(void *priv);

//#ifdef EMU_DEVICE_H
//extern const device_config_t cga_config[];
//
//extern const device_t cga_device;
//extern const device_t cga_pravetz_device;
//#endif
//
#endif /*VIDEO_CGA_H*/
