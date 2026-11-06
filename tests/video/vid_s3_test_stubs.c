/*
 * 86Box    A hypervisor and IBM PC system emulator that specializes in
 *          running old operating systems and software designed for IBM
 *          PC systems and compatibles from 1981 through fairly recent
 *          system designs based on the PCI bus.
 *
 *          This file is part of the 86Box distribution.
 *
 *          Stubs shared by the S3 tests, for what vid_s3.c calls from
 *          code the tests never run: the device init and close, and the
 *          RAMDACs and clock chips the cards add.
 *
 *          The Linux linker drops that code with --gc-sections, and its
 *          references with it. The Windows linker reports them anyway.
 *
 * Authors: Andreas Zdziarstek, <https://github.com/gmcn42>
 *
 *          Copyright 2026 Andreas Zdziarstek.
 */
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <86box/86box.h>
#include <86box/device.h>
#include <86box/timer.h>
#include <86box/mem.h>
#include <86box/pci.h>
#include <86box/rom.h>
#include <86box/nmc93cxx.h>
#include <86box/thread.h>
#include <86box/video.h>
#include <86box/i2c.h>
#include <86box/vid_ddc.h>
#include <86box/vid_svga.h>

uint8_t      edatlookup[4][4];
dbcs_font_t *fontdatksc5601;
dbcs_font_t *fontdatksc5601_user;

const device_t att490_ramdac_device     = { 0 };
const device_t att491_ramdac_device     = { 0 };
const device_t att492_ramdac_device     = { 0 };
const device_t att498_ramdac_device     = { 0 };
const device_t bt485_ramdac_device      = { 0 };
const device_t ch9204_device            = { 0 };
const device_t gendac_ramdac_device     = { 0 };
const device_t ibm_rgb528_ramdac_device = { 0 };
const device_t mu9c1880_ramdac_device   = { 0 };
const device_t nmc93cxx_device          = { 0 };
const device_t sc11483_ramdac_device    = { 0 };
const device_t sc1502x_ramdac_device    = { 0 };
const device_t sdac_ramdac_device       = { 0 };
const device_t stg1700_ramdac_device    = { 0 };
const device_t tvp3026_ramdac_device    = { 0 };

// clang-format off
void *device_add(const device_t *dev) { abort(); }
void *device_add_inst_params(const device_t *dev, int inst, void *params) { abort(); }
uint64_t device_get_bios_flags(const device_t *dev, const char *internal_name) { abort(); }
uint32_t device_get_bios_local(const device_t *dev, const char *internal_name) { abort(); }
const char *device_get_config_bios(const char *name) { abort(); }
int device_get_config_int(const char *name) { abort(); }
int device_get_instance(void) { abort(); }

void mem_mapping_add(mem_mapping_t *map, uint32_t base, uint32_t size,
                     uint8_t (*read_b)(uint32_t addr, void *priv),
                     uint16_t (*read_w)(uint32_t addr, void *priv),
                     uint32_t (*read_l)(uint32_t addr, void *priv),
                     void (*write_b)(uint32_t addr, uint8_t val, void *priv),
                     void (*write_w)(uint32_t addr, uint16_t val, void *priv),
                     void (*write_l)(uint32_t addr, uint32_t val, void *priv),
                     uint8_t *exec, uint32_t flags, void *priv) { abort(); }
void mem_mapping_set_p(mem_mapping_t *map, void *priv) { abort(); }
void mem_debug_add(uint8_t (*read_b)(uint32_t addr, void *priv),
                   void (*write_b)(uint32_t addr, uint8_t val, void *priv),
                   mem_debug_ptr_t debug_ptr) { abort(); }
void pci_add_card(uint8_t add_type, uint8_t (*read)(int func, int addr, int len, void *priv),
                  void (*write)(int func, int addr, int len, uint8_t val, void *priv), void *priv, uint8_t *slot) { abort(); }
int rom_init(rom_t *rom, const char *fn, uint32_t address, int size, int mask, int file_offset, uint32_t flags) { abort(); }

event_t *thread_create_event(void) { abort(); }
void thread_destroy_event(event_t *arg) { abort(); }
thread_t *thread_create_named(void (*thread_func)(void *param), void *param, const char *name) { abort(); }
int thread_wait(thread_t *arg) { abort(); }

void *i2c_gpio_init(char *bus_name) { abort(); }
void i2c_gpio_close(void *dev_handle) { abort(); }
void *i2c_gpio_get_bus(void *dev_handle) { abort(); }
void *ddc_init(void *i2c) { abort(); }
void ddc_close(void *eeprom) { abort(); }

int svga_init(const device_t *info, svga_t *svga, void *priv, int memsize,
              void (*recalctimings_ex)(struct svga_t *svga),
              uint8_t (*video_in)(uint16_t addr, void *priv),
              void (*video_out)(uint16_t addr, uint8_t val, void *priv),
              void (*hwcursor_draw)(struct svga_t *svga, int displine),
              void (*overlay_draw)(struct svga_t *svga, int displine)) { abort(); }
void svga_close(svga_t *svga) { abort(); }
uint8_t *svga_debug_ptr_common(uint32_t addr, uint8_t linear, int write, svga_t *svga) { abort(); }
void video_clamp_vram(uint64_t bios_flags, int *vram) { abort(); }
void video_inform_monitor(int type, const video_timings_t *ptr, int monitor_index) { abort(); }

float ch9204_getclock(int clock, void *priv) { abort(); }
float ibm_rgb528_getclock(int clock, void *priv) { abort(); }
void ibm_rgb528_hwcursor_draw(svga_t *svga, int displine) { abort(); }
void ibm_rgb528_ramdac_set_ref_clock(void *priv, svga_t *svga, float ref_clock) { abort(); }
float sdac_getclock(int clock, void *priv) { abort(); }
void sdac_set_ref_clock(void *priv, float ref_clock) { abort(); }
uint32_t tvp3026_conv_16to32(svga_t *svga, uint16_t color, uint8_t bpp) { abort(); }
float tvp3026_getclock(int clock, void *priv) { abort(); }
void tvp3026_hwcursor_draw(svga_t *svga, int displine) { abort(); }
// clang-format on
