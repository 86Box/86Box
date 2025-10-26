/*
 * 86Box    A hypervisor and IBM PC system emulator that specializes in
 *          running old operating systems and software designed for IBM
 *          PC systems and compatibles from 1981 through fairly recent
 *          system designs based on the PCI bus.
 *
 *          This file is part of the 86Box distribution.
 *
 *          Emulation of the IBM Monochrome Display and Printer card.
 *
 * Authors: Sarah Walker, starfrost
 *
 *          Copyright 2007-2024 Sarah Walker
 *          Copyright 2025 Connor Hyde / starfrost, <mario64crashed@gmail.com>
 */
#ifndef VIDEO_MDA_H
#define VIDEO_MDA_H

// Defines
#define MDA_CRTC_NUM_REGISTERS      32

// Enums & structures

typedef enum mda_registers_e {
    MDA_REGISTER_START = 0x3B0,

    MDA_REGISTER_CRTC_INDEX = 0x3B4,
    MDA_REGISTER_CRTC_DATA = 0x3B5,
    MDA_REGISTER_MODE_CONTROL = 0x3B8,
    MDA_REGISTER_CRT_STATUS = 0x3BA,
    MDA_REGISTER_PARALLEL_DATA = 0x3BC,
    MDA_REGISTER_PRINTER_STATUS = 0x3BD,
    MDA_REGISTER_PRINTER_CONTROL = 0x3BE,
    
    MDA_REGISTER_END = 0x3BF,
} mda_registers; 

// Motorola MC6845 CRTC registers (without light pen for some reason)
typedef enum mda_crtc_registers_e {
    MDA_CRTC_HTOTAL = 0x0,                              // Horizontal total (total number of characters incl. hsync)
    MDA_CRTC_HDISP = 0x1,                               // Horizontal display 
    MDA_CRTC_HSYNC_POS = 0x2,                           // Horizontal position of horizontal ysnc
    MDA_CRTC_HSYNC_WIDTH = 0x3,                         // Width of horizontal sync
    MDA_CRTC_VTOTAL = 0x4,                              // Vertical total (total number of scanlines incl. vsync)
    MDA_CRTC_VTOTAL_ADJUST = 0x5,                       // Vertical total adjust value
    MDA_CRTC_VDISP = 0x6,                               // Vertical display (total number of displayed scanline)
    MDA_CRTC_VSYNC = 0x7,                               // Vertical sync scanline number
    MDA_CRTC_INTERLACE = 0x8,                           // Interlacing mode
    MDA_CRTC_MAX_SCANLINE_ADDR = 0x9,                   // Maximum scanline address
    MDA_CRTC_CURSOR_START = 0xA,                        // Cursor start scanline
    MDA_CRTC_CURSOR_END = 0xB,                          // Cursor end scanline
    MDA_CRTC_START_ADDR_HIGH = 0xC,                     // Screen start address high 8 bits
    MDA_CRTC_START_ADDR_LOW = 0xD,                      // Screen start address low 8 bits
    MDA_CRTC_CURSOR_ADDR_HIGH = 0xE,                    // Cursor address high 8 bits
    MDA_CRTC_CURSOR_ADDR_LOW = 0xF,                     // Cursor address low 8 bits
} mda_crtc_registers;

typedef enum mda_mode_flags_e {
    MDA_MODE_HIGHRES = 1 << 0,                          // MUST be enabled for sane operation
    MDA_MODE_BW = 1 << 1,                    // UNUSED in most cases. Not present on Hercules
    MDA_MODE_VIDEO_ENABLE = 1 << 3, 
    MDA_MODE_BLINK = 1 << 5,
} mda_mode_flags;

typedef enum mda_colors_e {
    MDA_COLOR_BLACK = 0,
    MDA_COLOR_BLUE = 1,
    MDA_COLOR_GREEN = 2,
    MDA_COLOR_CYAN = 3,
    MDA_COLOR_RED = 4,
    MDA_COLOR_MAGENTA = 5,
    MDA_COLOR_BROWN = 6,
    MDA_COLOR_WHITE = 7,
    MDA_COLOR_GREY = 8,
    MDA_COLOR_BRIGHT_BLUE = 9,
    MDA_COLOR_BRIGHT_GREEN = 10,
    MDA_COLOR_BRIGHT_CYAN = 11,
    MDA_COLOR_BRIGHT_RED = 12,
    MDA_COLOR_BRIGHT_MAGENTA = 13,
    MDA_COLOR_BRIGHT_YELLOW = 14,
    MDA_COLOR_BRIGHT_WHITE = 15,
} mda_colors; 

typedef struct mda_t {
    mem_mapping_t mapping;

    uint8_t     crtc[MDA_CRTC_NUM_REGISTERS];
    int32_t     crtcreg;

    uint8_t     mode;
    uint8_t     status;

    uint64_t    dispontime;
    uint64_t    dispofftime;
    pc_timer_t  timer;

    int32_t     firstline;
    int32_t     lastline;

    int32_t     fontbase;
    int32_t     linepos;
    int32_t     displine;
    int32_t     vc;
    int32_t     scanline;
    uint16_t    memaddr;
    uint16_t    memaddr_backup;
    int32_t     cursorvisible;
    int32_t     cursoron;
    int32_t     dispon;
    int32_t     blink;
    int32_t     vsynctime;
    int32_t     vadj;
    int32_t     monitor_index;
    int32_t     prev_monitor_index;
    int32_t     monitor_type;           // Used for MDA Colour support (REV0 u64)

    uint8_t    *vram;
    lpt_t      *lpt;
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
