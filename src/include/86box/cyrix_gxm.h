/*
 * 86Box    A hypervisor and IBM PC system emulator that specializes in
 *          running old operating systems and software designed for IBM
 *          PC systems and compatibles from 1981 through fairly recent
 *          system designs based on the PCI bus.
 *
 *          This file is part of the 86Box distribution.
 *
 *          Definitions for the Cyrix MediaGX MMX's integrated functions
 *          and the CS5530 that goes with it.
 *
 * Authors: Cacodemon345
 *
 *          Copyright 2026 Cacodemon345.
 */
#ifndef EMU_CYRIX_GXM_H
#define EMU_CYRIX_GXM_H

/* PM_STAT_SMI, GX_BASE+8500h (MediaGX MMX data book, Table 6-2). */
#define GXM_PM_SMI_PIN 0x01
#define GXM_PM_SMI_IO  0x02
#define GXM_PM_SMI_MEM 0x04

/* BC_XMAP_1's graphics enables, for the A, B0 and B8 regions. */
#define GXM_VGA_A      0x01
#define GXM_VGA_B0     0x02
#define GXM_VGA_B8     0x04

/* The integrated functions (src/chipset/cyrix_gxm.c). */
extern void     cyrix_gxm_smi_pin(void);
extern void     cyrix_gxm_smi_status(uint8_t status);
extern void     cyrix_gxm_vid_irq(void);
extern uint32_t cyrix_gxm_gmem_base(void);
extern uint32_t cyrix_gxm_gmem_size(void);

/* The graphics pipeline, its VGA hardware and the display controller
   (src/video/vid_cyrix_gxm.c). */
extern void    *gxm_video;

extern uint32_t gxm_gp_read(uint32_t addr, void *priv);
extern void     gxm_gp_write(uint32_t addr, uint32_t val, uint32_t mask, void *priv);
extern uint32_t gxm_dc_read(uint32_t addr, void *priv);
extern void     gxm_dc_write(uint32_t addr, uint32_t val, uint32_t mask, void *priv);
extern void     gxm_video_set_vga(void *priv, uint8_t enables);
extern void     gxm_video_set_gmem(void *priv, uint32_t base, uint32_t size);
extern void     gxm_video_gmem_written(void *priv, uint32_t addr, int size);

/* The CS5530's display interface (src/chipset/cs5530.c) gives the
   display controller its dot clock, its DACs, its gamma RAM and its video
   overlay registers (F4BAR+00h-18h as dword indexes). */
extern void     gxm_video_set_dotclock(void *priv, double clock);
extern void     gxm_video_set_display(void *priv, uint32_t display_cfg, const uint32_t *gamma);
extern void     gxm_video_set_overlay(void *priv, int reg, uint32_t val);

extern const device_t cyrix_gxm_device;
extern const device_t cyrix_gxm_video_device;
extern const device_t cs5530_device;
extern const device_t cs5530_xpressaudio_device;

/* The board's MIDI UART at the Sound Blaster base (see cs5530.c): the
   board says whether it is enabled, by default it is. */
extern void cs5530_set_midi_uart_enabled(int (*enabled)(void));

#endif /*EMU_CYRIX_GXM_H*/
