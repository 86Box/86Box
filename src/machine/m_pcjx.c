/*
 * 86Box    A hypervisor and IBM PC system emulator that specializes in
 *          running old operating systems and software designed for IBM
 *          PC systems and compatibles from 1981 through fairly recent
 *          system designs based on the PCI bus.
 *
 *          This file is part of the 86Box distribution.
 *
 *          Early machine bring-up for the IBM PCjx.
 */

#define ENABLE_PCJX_LOG 1

#ifdef ENABLE_PCJX_LOG
#include <stdarg.h>
#endif
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include <86box/86box.h>
#include <86box/device.h>
#include <86box/mem.h>
#include <86box/timer.h>
#include <86box/io.h>
#include <86box/gameport.h>
#include <86box/lpt.h>
#include <86box/log.h>
#include <86box/m_pcjr.h>
#include <86box/machine.h>
#include <86box/fdd.h>
#include <86box/fdc.h>
#include <86box/pic.h>
#include <86box/pit.h>
#include <86box/rom.h>
#include <86box/plat_unused.h>
#include <86box/sound.h>
#include <86box/snd_sn76489.h>
#include <86box/vid_pcjx.h>

#include "cpu.h"

#define PCJX_BASE_ROM_PATH     "roms/machines/ibmpcjx/BASE.ROM"
#include <86box/serial.h>
#define PCJX_FONT_ROM_PATH     "roms/machines/ibmpcjx/FONT.ROM"
#define PCJX_FONT_ROM_SIZE     0x00040000
#define PCJX_FONT_ROM_MASK     (PCJX_FONT_ROM_SIZE - 1)

#define PCJX_MAPPER_PORT       0x01ff
#define PCJX_RTC_BASE_PORT     0x0360
#define PCJX_RTC_PORTS         0x0010
#define PCJX_MAPPER_BLOCKS     0x94

typedef struct pcjx_block_config_s {
    uint16_t andreg1;
    uint16_t andreg2;
    uint16_t orreg1;
    uint16_t orreg2;
    uint8_t  cmpand;
    uint8_t  cmp;
    uint8_t  has_cmp;
} pcjx_block_config_t;

typedef struct pcjx_block_state_s {
    uint8_t reg1;
    uint8_t reg2;
} pcjx_block_state_t;

typedef struct pcjx_s {
    pcjr_t            *pcjr;
    uint8_t            mapper_index;
    uint8_t            mapper_phase;
    uint8_t            mapper_status_log_valid;
    pcjx_block_state_t blocks[PCJX_MAPPER_BLOCKS];
    mem_mapping_t      exmem_mapping;
    mem_mapping_t      font_mapping;
    pcjx_video_t       video;
    uint8_t           *font_rom;
    void              *log;
    uint8_t            status_base1_rom;
    uint8_t            status_base2_rom;
    uint8_t            status_ex_video;
    uint8_t            mapper_status_log_value;
    uint16_t           mapper_status_log_cs;
    uint32_t           mapper_status_log_pc;
    uint8_t            rtc_latch[0x0d];
    uint8_t            rtc_write_mode;
    time_t             rtc_time;
    time_t             rtc_host_time;
} pcjx_t;

#ifdef ENABLE_PCJX_LOG
uint8_t pcjx_do_log = ENABLE_PCJX_LOG;

static void
pcjx_log(void *priv, const char *fmt, ...)
{
    va_list ap;

    if (pcjx_do_log) {
        va_start(ap, fmt);
        log_out(priv, fmt, ap);
        va_end(ap);
    }
}
#else
#    define pcjx_log(priv, fmt, ...)
#endif

static void
pcjx_log_mapping(const pcjx_t *pcjx, const char *name, uint32_t base,
                 uint32_t size, uint8_t can_read, uint8_t can_write)
{
    if (pcjx == NULL)
        return;

    if (size == 0) {
        pcjx_log(pcjx->log,
                 "[%04X:%08X] %s disabled read=%u write=%u\n",
                 CS, cpu_state.pc, name, can_read, can_write);
        return;
    }

    pcjx_log(pcjx->log,
             "[%04X:%08X] %s base=%05X size=%05X read=%u write=%u\n",
             CS, cpu_state.pc, name, (unsigned int) base,
             (unsigned int) size, can_read, can_write);
}

static uint8_t
pcjx_should_log_mapper_status(pcjx_t *pcjx, uint8_t status)
{
    if (pcjx == NULL)
        return 0;

    if (!pcjx->mapper_status_log_valid ||
        (pcjx->mapper_status_log_cs != CS) ||
        (pcjx->mapper_status_log_pc != cpu_state.pc) ||
        (pcjx->mapper_status_log_value != status)) {
        pcjx->mapper_status_log_valid = 1;
        pcjx->mapper_status_log_cs    = CS;
        pcjx->mapper_status_log_pc    = cpu_state.pc;
        pcjx->mapper_status_log_value = status;
        return 1;
    }

    return 0;
}

static const pcjx_block_config_t pcjx_block_configs[PCJX_MAPPER_BLOCKS] = {
    [0x00] = { 0203, 0003, 0074, 0040, 00, 00, 0 },
    [0x01] = { 0357, 0157, 0020, 0000, 00, 00, 0 },
    [0x02] = { 0357, 0157, 0020, 0000, 00, 00, 0 },
    [0x03] = { 0217, 0017, 0060, 0040, 00, 00, 0 },
    [0x04] = { 0217, 0017, 0060, 0040, 00, 00, 0 },
    [0x05] = { 0217, 0017, 0060, 0040, 00, 00, 0 },
    [0x06] = { 0217, 0017, 0060, 0040, 00, 00, 0 },
    [0x07] = { 0277, 0177, 0000, 0000, 00, 00, 0 },
    [0x08] = { 0237, 0037, 0040, 0140, 00, 00, 0 },
    [0x09] = { 0237, 0137, 0140, 0040, 00, 00, 0 },
    [0x0a] = { 0237, 0037, 0040, 0140, 00, 00, 0 },
    [0x80] = { 0200, 0000, 0004, 0000, 00, 00, 0 },
    [0x81] = { 0200, 0000, 0010, 0000, 00, 00, 0 },
    [0x82] = { 0200, 0000, 0014, 0000, 00, 00, 0 },
    [0x83] = { 0200, 0000, 0024, 0000, 00, 00, 0 },
    [0x84] = { 0200, 0000, 0030, 0000, 00, 00, 0 },
    [0x85] = { 0377, 0177, 0000, 0000, 00, 00, 0 },
    [0x86] = { 0200, 0000, 0100, 0000, 00, 00, 0 },
    [0x87] = { 0200, 0000, 0100, 0000, 00, 00, 0 },
    [0x88] = { 0200, 0000, 0157, 0000, 00, 00, 0 },
    [0x89] = { 0200, 0000, 0137, 0000, 00, 00, 0 },
    [0x8a] = { 0200, 0000, 0172, 0000, 00, 00, 0 },
    [0x8b] = { 0200, 0000, 0173, 0000, 07, 00, 1 },
    [0x8c] = { 0200, 0000, 0173, 0000, 07, 02, 1 },
    [0x8d] = { 0200, 0000, 0173, 0000, 07, 02, 1 },
    [0x8e] = { 0200, 0000, 0173, 0000, 07, 05, 1 },
    [0x8f] = { 0200, 0000, 0173, 0000, 03, 02, 1 },
    [0x90] = { 0200, 0000, 0173, 0000, 07, 01, 1 },
    [0x91] = { 0200, 0000, 0173, 0000, 07, 07, 1 },
    [0x92] = { 0200, 0000, 0177, 0000, 00, 00, 0 },
    [0x93] = { 0200, 0000, 0000, 0177, 00, 00, 0 },
};

extern int             bios_only;
extern int             machine_pcjr_common_init_with_video(const char *bios_path, uint32_t bios_address,
                                                           int bios_size, const char *font_path,
                                                           const device_t *video_device,
                                                           void (*video_init)(pcjr_t *pcjr));

static void *pcjx_extension_init(const device_t *info);
static void  pcjx_extension_close(void *priv);

static uint8_t
pcjx_font_read(uint32_t addr, void *priv)
{
    const pcjx_t *pcjx = (const pcjx_t *) priv;
    uint32_t      offset;

    if ((pcjx == NULL) || (pcjx->font_rom == NULL))
        return 0xff;
    if (addr < pcjx->font_mapping.base)
        return 0xff;
    if (addr >= (pcjx->font_mapping.base + pcjx->font_mapping.size))
        return 0xff;

    offset = (addr - pcjx->font_mapping.base) & 0x3ffff;

    /* The JX font window is not fully linear: 0x88000-0x8FFFF aliases a
       narrow CG1 slice, which the BIOS uses while building graphics text. */
    if ((offset >= 0x8000) && (offset <= 0xffff))
        offset = 0x8000 | (offset & 0x07ff);

    return pcjx->font_rom[offset & PCJX_FONT_ROM_MASK];
}

static void
pcjx_font_write(uint32_t addr, uint8_t val, void *priv)
{
    pcjx_t  *pcjx = (pcjx_t *) priv;
    uint32_t offset;

    if ((pcjx == NULL) || (pcjx->font_rom == NULL))
        return;
    if (addr < pcjx->font_mapping.base)
        return;
    if (addr >= (pcjx->font_mapping.base + pcjx->font_mapping.size))
        return;

    offset = (addr - pcjx->font_mapping.base) & 0x3ffff;
    if ((offset < 0x8000) || (offset > 0xffff))
        return;

    pcjx->font_rom[(0x8000 | (offset & 0x07ff)) & PCJX_FONT_ROM_MASK] = val;
}

static void
pcjx_font_writew(uint32_t addr, uint16_t val, void *priv)
{
    pcjx_font_write(addr, (uint8_t) (val & 0xff), priv);
    pcjx_font_write(addr + 1, (uint8_t) (val >> 8), priv);
}

static void
pcjx_font_writel(uint32_t addr, uint32_t val, void *priv)
{
    pcjx_font_write(addr, (uint8_t) (val & 0xff), priv);
    pcjx_font_write(addr + 1, (uint8_t) ((val >> 8) & 0xff), priv);
    pcjx_font_write(addr + 2, (uint8_t) ((val >> 16) & 0xff), priv);
    pcjx_font_write(addr + 3, (uint8_t) ((val >> 24) & 0xff), priv);
}

static uint16_t
pcjx_font_readw(uint32_t addr, void *priv)
{
    return (uint16_t) (pcjx_font_read(addr, priv) |
                       (pcjx_font_read(addr + 1, priv) << 8));
}

static uint32_t
pcjx_font_readl(uint32_t addr, void *priv)
{
    return (uint32_t) (pcjx_font_read(addr, priv) |
                       (pcjx_font_read(addr + 1, priv) << 8) |
                       (pcjx_font_read(addr + 2, priv) << 16) |
                       (pcjx_font_read(addr + 3, priv) << 24));
}

static const device_t pcjx_extension_device = {
    .name          = "IBM PCjx Extensions",
    .internal_name = "pcjx_ext",
    .flags         = DEVICE_ONBOARD,
    .local         = 0,
    .init          = pcjx_extension_init,
    .close         = pcjx_extension_close,
    .reset         = NULL,
    .available     = NULL,
    .speed_changed = NULL,
    .force_redraw  = NULL,
    .config        = NULL,
};

static void
pcjx_rtc_sync(pcjx_t *pcjx)
{
    time_t now;

    if (pcjx == NULL)
        return;

    now = time(NULL);
    if (pcjx->rtc_host_time == (time_t) 0) {
        pcjx->rtc_host_time = now;
        return;
    }

    if (now > pcjx->rtc_host_time)
        pcjx->rtc_time += (now - pcjx->rtc_host_time);
    pcjx->rtc_host_time = now;
}

static void
pcjx_rtc_latch_read(pcjx_t *pcjx)
{
    const struct tm *current;
    struct tm        copy;
    struct tm        time_buf;

    pcjx_rtc_sync(pcjx);

    current = NULL;
#ifdef _WIN32
    if (localtime_s(&time_buf, &pcjx->rtc_time) == 0)
        current = &time_buf;
#else
    current = localtime_r(&pcjx->rtc_time, &time_buf);
#endif
    if (current == NULL)
        return;

    copy = *current;
    pcjx->rtc_latch[0]  = (uint8_t) (copy.tm_sec % 10);
    pcjx->rtc_latch[1]  = (uint8_t) (copy.tm_sec / 10);
    pcjx->rtc_latch[2]  = (uint8_t) (copy.tm_min % 10);
    pcjx->rtc_latch[3]  = (uint8_t) (copy.tm_min / 10);
    pcjx->rtc_latch[4]  = (uint8_t) (copy.tm_hour % 10);
    pcjx->rtc_latch[5]  = (uint8_t) (copy.tm_hour / 10);
    pcjx->rtc_latch[6]  = (uint8_t) (copy.tm_mday % 10);
    pcjx->rtc_latch[7]  = (uint8_t) (copy.tm_mday / 10);
    pcjx->rtc_latch[8]  = (uint8_t) ((copy.tm_mon + 1) % 10);
    pcjx->rtc_latch[9]  = (uint8_t) ((copy.tm_mon + 1) / 10);
    pcjx->rtc_latch[10] = (uint8_t) (copy.tm_year % 10);
    pcjx->rtc_latch[11] = (uint8_t) ((copy.tm_year / 10) % 10);
    pcjx->rtc_latch[12] = (uint8_t) copy.tm_wday;
}

static void
pcjx_rtc_commit_write(pcjx_t *pcjx)
{
    struct tm value;

    memset(&value, 0, sizeof(value));
    value.tm_sec  = ((pcjx->rtc_latch[1] & 0x07) * 10) + (pcjx->rtc_latch[0] & 0x0f);
    value.tm_min  = ((pcjx->rtc_latch[3] & 0x07) * 10) + (pcjx->rtc_latch[2] & 0x0f);
    value.tm_hour = ((pcjx->rtc_latch[5] & 0x03) * 10) + (pcjx->rtc_latch[4] & 0x0f);
    value.tm_mday = ((pcjx->rtc_latch[7] & 0x03) * 10) + (pcjx->rtc_latch[6] & 0x0f);
    value.tm_mon  = (((pcjx->rtc_latch[9] & 0x01) * 10) + (pcjx->rtc_latch[8] & 0x0f)) - 1;
    value.tm_year = ((pcjx->rtc_latch[11] & 0x0f) * 10) + (pcjx->rtc_latch[10] & 0x0f);
    if (value.tm_year < 80)
        value.tm_year += 100;

    pcjx->rtc_time      = mktime(&value);
    pcjx->rtc_host_time = time(NULL);
}

static uint8_t
pcjx_mapper_in(uint16_t port, void *priv)
{
    pcjx_t  *pcjx = (pcjx_t *) priv;
    uint8_t  status;

    if (port != PCJX_MAPPER_PORT)
        return 0xff;

    pcjx->mapper_phase = 0;
    status             = 0xff;
    if (pcjx->status_base1_rom)
        status ^= 0x20;
    if (pcjx->status_base2_rom)
        status ^= 0x40;
    if (pcjx->status_ex_video)
        status ^= 0x80;

    if (pcjx_should_log_mapper_status(pcjx, status)) {
        pcjx_log(pcjx->log, "[%04X:%08X] [R] %04X = %02X (mapper status)\n",
                 CS, cpu_state.pc, port, status);
    }

    return status;
}

static uint8_t
pcjx_block_has_config(uint8_t index)
{
    return (index < PCJX_MAPPER_BLOCKS) && (pcjx_block_configs[index].andreg1 != 0);
}

static uint8_t
pcjx_decode_block_range(const pcjx_block_state_t *block, uint32_t *base,
                        uint32_t *size, uint8_t *can_read, uint8_t *can_write)
{
    uint8_t  mask;
    uint8_t  first_segment = 0xff;
    uint8_t  last_segment  = 0;
    uint8_t  segment_count = 0;

    if (!(block->reg1 & 0x80) || !(block->reg1 & 0x20))
        return 0;

    *can_read  = !!(block->reg2 & 0x20);
    *can_write = !!(block->reg2 & 0x40);
    if (!*can_read && !*can_write)
        return 0;

    mask = (block->reg2 & 0x1f) ^ 0x1f;
    for (uint8_t segment = 0; segment < 32; segment++) {
        if ((mask & block->reg1) != (mask & segment))
            continue;

        if (first_segment == 0xff)
            first_segment = segment;
        last_segment = segment;
        segment_count++;
    }

    if ((segment_count == 0) || ((last_segment - first_segment + 1) != segment_count))
        return 0;

    *base = first_segment << 15;
    *size = segment_count << 15;
    return 1;
}

static uint8_t
pcjx_decode_io_block(const pcjx_block_state_t *block,
                     const pcjx_block_config_t *config, uint16_t port)
{
    uint8_t mask;
    uint8_t addr_shift;

    if ((block == NULL) || (config == NULL))
        return 0;

    if (!(block->reg1 & 0x80))
        return 0;

    addr_shift = (uint8_t) ((port >> 3) & 0x7f);
    mask       = (uint8_t) ((block->reg2 & 0x7f) ^ 0x7f);
    if ((mask & block->reg1) != (mask & addr_shift))
        return 0;

    if (config->has_cmp && ((port & config->cmpand) != config->cmp))
        return 0;

    return 1;
}

static uint8_t
pcjx_block_has_state(const pcjx_t *pcjx, uint8_t index)
{
    return (pcjx != NULL) && ((pcjx->blocks[index].reg1 != 0) ||
                              (pcjx->blocks[index].reg2 != 0));
}

static uint8_t
pcjx_decode_gate_io_mask(const pcjx_t *pcjx)
{
    uint8_t gate_io_mask = 0;

    if (pcjx == NULL)
        return 0;

    if (pcjx_decode_io_block(&pcjx->blocks[0x8c], &pcjx_block_configs[0x8c], 0x03da))
        gate_io_mask |= 0x01;
    if (pcjx_decode_io_block(&pcjx->blocks[0x8d], &pcjx_block_configs[0x8d], 0x03da))
        gate_io_mask |= 0x02;

    return gate_io_mask;
}

static uint8_t
pcjx_decode_control_io_mask(const pcjx_t *pcjx)
{
    uint8_t control_io_mask = 0;

    if (pcjx == NULL)
        return 0;

    if (pcjx_decode_io_block(&pcjx->blocks[0x90], &pcjx_block_configs[0x90], 0x03d9))
        control_io_mask |= 0x01;
    if (pcjx_decode_io_block(&pcjx->blocks[0x8e], &pcjx_block_configs[0x8e], 0x03dd))
        control_io_mask |= 0x02;
    if (pcjx_decode_io_block(&pcjx->blocks[0x91], &pcjx_block_configs[0x91], 0x03df))
        control_io_mask |= 0x04;
    if (pcjx_decode_io_block(&pcjx->blocks[0x8a], &pcjx_block_configs[0x8a], 0x03d4))
        control_io_mask |= 0x08;

    return control_io_mask;
}

static uint8_t
pcjx_decode_pic_io_enabled(const pcjx_t *pcjx)
{
    if (!pcjx_block_has_state(pcjx, 0x80))
        return 1;

    return (uint8_t) pcjx_decode_io_block(&pcjx->blocks[0x80],
                                          &pcjx_block_configs[0x80],
                                          0x0020);
}

static uint8_t
pcjx_decode_pit_io_enabled(const pcjx_t *pcjx)
{
    if (!pcjx_block_has_state(pcjx, 0x81))
        return 1;

    return (uint8_t) pcjx_decode_io_block(&pcjx->blocks[0x81],
                                          &pcjx_block_configs[0x81],
                                          0x0040);
}

static uint8_t
pcjx_decode_ppi_io_enabled(const pcjx_t *pcjx)
{
    if (!pcjx_block_has_state(pcjx, 0x82))
        return 1;

    return (uint8_t) pcjx_decode_io_block(&pcjx->blocks[0x82],
                                          &pcjx_block_configs[0x82],
                                          0x0060);
}

static uint8_t
pcjx_decode_a0_io_enabled(const pcjx_t *pcjx)
{
    if (!pcjx_block_has_state(pcjx, 0x83))
        return 1;

    return (uint8_t) pcjx_decode_io_block(&pcjx->blocks[0x83],
                                          &pcjx_block_configs[0x83],
                                          0x00a0);
}

static uint8_t
pcjx_decode_sound_io_enabled(const pcjx_t *pcjx)
{
    if (!pcjx_block_has_state(pcjx, 0x84))
        return 1;

    return (uint8_t) pcjx_decode_io_block(&pcjx->blocks[0x84],
                                          &pcjx_block_configs[0x84],
                                          0x00c0);
}

static uint8_t
pcjx_decode_joystick_write_enabled(const pcjx_t *pcjx)
{
    if (!pcjx_block_has_state(pcjx, 0x86))
        return 1;

    return (uint8_t) pcjx_decode_io_block(&pcjx->blocks[0x86],
                                          &pcjx_block_configs[0x86],
                                          0x0201);
}

static uint8_t
pcjx_decode_joystick_read_enabled(const pcjx_t *pcjx)
{
    if (!pcjx_block_has_state(pcjx, 0x87))
        return 1;

    return (uint8_t) pcjx_decode_io_block(&pcjx->blocks[0x87],
                                          &pcjx_block_configs[0x87],
                                          0x0201);
}

static uint8_t
pcjx_decode_fdc_io_enabled(const pcjx_t *pcjx)
{
    if (!pcjx_block_has_state(pcjx, 0x85))
        return 1;

    return (uint8_t) pcjx_decode_io_block(&pcjx->blocks[0x85],
                                          &pcjx_block_configs[0x85],
                                          FDC_PRIMARY_PCJR_ADDR);
}

static uint16_t
pcjx_decode_serial_port(const pcjx_t *pcjx, uint8_t *irq)
{
    static const struct {
        uint16_t port;
        uint8_t  irq;
    } pcjx_serial_ports[] = {
        { COM1_ADDR, COM1_IRQ },
        { COM2_ADDR, COM2_IRQ },
        { COM3_ADDR, COM3_IRQ },
        { COM4_ADDR, COM4_IRQ },
        { COM5_ADDR, COM5_IRQ },
        { COM6_ADDR, COM6_IRQ },
        { COM7_ADDR, COM7_IRQ },
    };

    if ((pcjx == NULL) || (irq == NULL) || !pcjx_block_has_state(pcjx, 0x89))
        return 0x0000;

    for (uint8_t port_index = 0; port_index < (sizeof(pcjx_serial_ports) / sizeof(pcjx_serial_ports[0])); port_index++) {
        if (!pcjx_decode_io_block(&pcjx->blocks[0x89], &pcjx_block_configs[0x89],
                                  pcjx_serial_ports[port_index].port))
            continue;

        *irq = pcjx_serial_ports[port_index].irq;
        return pcjx_serial_ports[port_index].port;
    }

    return 0x0000;
}

static void
pcjx_apply_pic_io_gate(uint8_t pic_io_enabled)
{
    pic_set_io_enabled(&pic, pic_io_enabled, pic_io_enabled);
}

static void
pcjx_apply_pit_io_gate(uint8_t pit_io_enabled)
{
    pit_set_io_enabled((pit_t *) pit_devs[0].data, pit_io_enabled,
                       pit_io_enabled);
}

static void
pcjx_apply_ppi_io_gate(const pcjx_t *pcjx, uint8_t ppi_io_enabled)
{
    if ((pcjx == NULL) || (pcjx->pcjr == NULL))
        return;

    pcjx->pcjr->pcjx_60_io_enabled = ppi_io_enabled;
}

static void
pcjx_apply_sound_io_gate(uint8_t sound_io_enabled)
{
    sn76489_set_io_enabled(sn76489_get_device(0), sound_io_enabled);
}

static void
pcjx_apply_fdc_io_gate(const pcjx_t *pcjx, uint8_t fdc_io_enabled)
{
    uint16_t desired_fdc_base;

    if ((pcjx == NULL) || (pcjx->pcjr == NULL) || !pcjx->pcjr->option_fdc ||
        (fdd_fdc == NULL))
        return;

    desired_fdc_base = fdc_io_enabled ? FDC_PRIMARY_PCJR_ADDR : 0x0000;
    if (fdd_fdc->base_address == desired_fdc_base)
        return;

    fdc_remove(fdd_fdc);
    fdc_set_base(fdd_fdc, desired_fdc_base);
}

static void
pcjx_apply_joystick_io_gate(uint8_t joystick_read_enabled,
                            uint8_t joystick_write_enabled)
{
    void *gameport;

    gameport = gameport_get_instance(0);
    if (gameport == NULL)
        return;

    gameport_set_io_enabled(gameport, joystick_read_enabled,
                            joystick_write_enabled);
}

static uint16_t
pcjx_decode_parallel_port(const pcjx_t *pcjx, uint8_t *irq)
{
    static const struct {
        uint16_t port;
        uint8_t  irq;
    } pcjx_parallel_ports[] = {
        { LPT1_ADDR, LPT1_IRQ },
        { LPT2_ADDR, LPT2_IRQ },
        { LPT_MDA_ADDR, LPT_MDA_IRQ },
        { LPT4_ADDR, LPT4_IRQ },
    };

    if ((pcjx == NULL) || (irq == NULL) || !pcjx_block_has_state(pcjx, 0x88))
        return 0x0000;

    for (uint8_t port_index = 0; port_index < (sizeof(pcjx_parallel_ports) / sizeof(pcjx_parallel_ports[0])); port_index++) {
        if (!pcjx_decode_io_block(&pcjx->blocks[0x88], &pcjx_block_configs[0x88],
                                  pcjx_parallel_ports[port_index].port))
            continue;

        *irq = pcjx_parallel_ports[port_index].irq;
        return pcjx_parallel_ports[port_index].port;
    }

    return 0x0000;
}

static void
pcjx_apply_parallel_io_gate(uint16_t lpt_port, uint8_t lpt_irq)
{
    lpt_t *lpt;

    if (lpt_ports[0].lpt == NULL)
        return;

    lpt = lpt_ports[0].lpt;
    if (lpt->addr != lpt_port) {
        if (lpt_port == 0x0000)
            lpt_port_remove(lpt);
        else {
            lpt_port_irq(lpt, lpt_irq);
            lpt_port_setup(lpt, lpt_port);
        }
        return;
    }

    if ((lpt_port != 0x0000) && (lpt->irq != lpt_irq))
        lpt_port_irq(lpt, lpt_irq);
}

static void
pcjx_apply_serial_io_gate(uint16_t serial_port, uint8_t serial_irq_line)
{
    serial_t *serial;

    serial = serial_get_device(0);
    if (serial == NULL)
        return;

    if (serial->base_address != serial_port) {
        if (serial_port == 0x0000)
            serial_remove(serial);
        else
            serial_setup(serial, serial_port, serial_irq_line);
        return;
    }

    if ((serial_port != 0x0000) && (serial->irq != serial_irq_line))
        serial_irq(serial, serial_irq_line);
}

static void
pcjx_sync_video_io(pcjx_t *pcjx)
{
    uint8_t gate_io_mask;
    uint8_t control_io_mask;
    uint8_t pic_io_enabled;
    uint8_t pit_io_enabled;
    uint8_t ppi_io_enabled;
    uint8_t a0_io_enabled;
    uint8_t sound_io_enabled;
    uint8_t joystick_write_enabled;
    uint8_t joystick_read_enabled;
    uint8_t fdc_io_enabled;
    uint16_t serial_port;
    uint8_t  serial_irq_line;
    uint16_t lpt_port       = 0x0000;
    uint8_t  lpt_irq        = 0xff;

    if (pcjx == NULL)
        return;

    gate_io_mask    = pcjx_decode_gate_io_mask(pcjx);
    control_io_mask = pcjx_decode_control_io_mask(pcjx);
    pic_io_enabled  = pcjx_decode_pic_io_enabled(pcjx);
    pit_io_enabled  = pcjx_decode_pit_io_enabled(pcjx);
    ppi_io_enabled  = pcjx_decode_ppi_io_enabled(pcjx);
    a0_io_enabled   = pcjx_decode_a0_io_enabled(pcjx);
    sound_io_enabled = pcjx_decode_sound_io_enabled(pcjx);
    joystick_write_enabled = pcjx_decode_joystick_write_enabled(pcjx);
    joystick_read_enabled  = pcjx_decode_joystick_read_enabled(pcjx);
    fdc_io_enabled  = pcjx_decode_fdc_io_enabled(pcjx);
    serial_irq_line = 0xff;
    serial_port     = pcjx_decode_serial_port(pcjx, &serial_irq_line);
    lpt_port = pcjx_decode_parallel_port(pcjx, &lpt_irq);

    pcjx_video_set_gate_array_io_mask(&pcjx->video, gate_io_mask);
    pcjx_video_set_control_io_mask(&pcjx->video, control_io_mask);
    pcjx_apply_pic_io_gate(pic_io_enabled);
    pcjx_apply_pit_io_gate(pit_io_enabled);
    pcjx_apply_ppi_io_gate(pcjx, ppi_io_enabled);
    if (pcjx->pcjr != NULL)
        pcjx->pcjr->pcjx_a0_io_enabled = a0_io_enabled;
    pcjx_apply_sound_io_gate(sound_io_enabled);
    pcjx_apply_joystick_io_gate(joystick_read_enabled, joystick_write_enabled);
    pcjx_apply_fdc_io_gate(pcjx, fdc_io_enabled);
    pcjx_apply_serial_io_gate(serial_port, serial_irq_line);
    pcjx_apply_parallel_io_gate(lpt_port, lpt_irq);

    pcjx_log(pcjx->log,
             "[%04X:%08X] io gate=%02X ctrl=%02X pic=%u pit=%u ppi=%u a0=%u sound=%u joy_r=%u joy_w=%u fdc=%u serial=%04X irq=%u lpt=%04X irq=%u\n",
             CS, cpu_state.pc, gate_io_mask, control_io_mask,
             pic_io_enabled, pit_io_enabled, ppi_io_enabled, a0_io_enabled,
             sound_io_enabled, joystick_read_enabled, joystick_write_enabled,
             fdc_io_enabled, (unsigned int) serial_port, serial_irq_line,
             (unsigned int) lpt_port, lpt_irq);
}

static void
pcjx_clear_range(uint32_t base, uint32_t size)
{
    if (size == 0)
        return;

    mem_set_mem_state_both(base, size, MEM_READ_EXTANY | MEM_WRITE_EXTANY);
}

static uint32_t
pcjx_configured_ram_size(void)
{
    if (mem_size <= 0)
        return 0;

    if (mem_size >= 640)
        return 0x000a0000;

    return mem_size * 1024U;
}

static void
pcjx_apply_system_rom(const pcjx_t *pcjx)
{
    const pcjx_block_state_t *block = &pcjx->blocks[0x00];
    uint32_t                  base  = 0;
    uint32_t                  size  = 0;
    uint8_t                   can_read;
    uint8_t                   can_write;

    pcjx_clear_range(bios_mapping.base, bios_mapping.size);
    mem_mapping_disable(&bios_mapping);

    if (!pcjx_decode_block_range(block, &base, &size, &can_read, &can_write)) {
        pcjx_log_mapping(pcjx, "system ROM", 0, 0, 0, 0);
        return;
    }

    if (!can_read) {
        pcjx_log_mapping(pcjx, "system ROM", base, size, can_read, can_write);
        return;
    }

    mem_mapping_set_addr(&bios_mapping, base, size);
    mem_mapping_enable(&bios_mapping);
    mem_set_mem_state_both(base, size,
                           MEM_READ_ROMCS |
                               (can_write ? MEM_WRITE_ROMCS : MEM_WRITE_DISABLED));
    pcjx_log_mapping(pcjx, "system ROM", base, size, can_read, can_write);
}

static void
pcjx_apply_font_window(pcjx_t *pcjx)
{
    const pcjx_block_state_t *block = &pcjx->blocks[0x07];
    uint32_t                  base  = 0;
    uint32_t                  size  = 0;
    uint8_t                   can_read;
    uint8_t                   can_write;

    pcjx_clear_range(pcjx->font_mapping.base, pcjx->font_mapping.size);
    mem_mapping_disable(&pcjx->font_mapping);

    if (!pcjx_decode_block_range(block, &base, &size, &can_read, &can_write)) {
        pcjx_log_mapping(pcjx, "font window", 0, 0, 0, 0);
        return;
    }

    if (!can_read) {
        pcjx_log_mapping(pcjx, "font window", base, size, can_read, can_write);
        return;
    }

    if (size > PCJX_FONT_ROM_SIZE)
        size = PCJX_FONT_ROM_SIZE;

    mem_mapping_set_addr(&pcjx->font_mapping, base, size);
    mem_mapping_enable(&pcjx->font_mapping);
    mem_set_mem_state_both(base, size,
                           MEM_READ_ROMCS |
                               (can_write ? MEM_WRITE_ROMCS : MEM_WRITE_DISABLED));
    pcjx_log_mapping(pcjx, "font window", base, size, can_read, can_write);
}

static void
pcjx_apply_main_ram(pcjx_t *pcjx)
{
    const pcjx_block_state_t *block          = &pcjx->blocks[0x08];
    uint32_t                  base           = 0;
    uint32_t                  size           = 0;
    uint32_t                  configured_ram = pcjx_configured_ram_size();
    uint8_t                   can_read;
    uint8_t                   can_write;

    pcjx_clear_range(ram_low_mapping.base, ram_low_mapping.size);
    pcjx_clear_range(pcjx->exmem_mapping.base, pcjx->exmem_mapping.size);
    mem_mapping_disable(&pcjx->exmem_mapping);

    if (!pcjx_decode_block_range(block, &base, &size, &can_read, &can_write)) {
        pcjx_log_mapping(pcjx, "main RAM", 0, 0, 0, 0);
        return;
    }

    if ((base != 0) || !can_read || !can_write) {
        pcjx_log_mapping(pcjx, "main RAM", base, size, can_read, can_write);
        return;
    }

    if (size > configured_ram)
        size = configured_ram;
    if (size == 0) {
        pcjx_log_mapping(pcjx, "main RAM", base, size, can_read, can_write);
        return;
    }

    mem_mapping_set_addr(&ram_low_mapping, 0, size);
    mem_mapping_set_exec(&ram_low_mapping, ram);
    mem_set_mem_state_both(0, size, MEM_READ_INTERNAL | MEM_WRITE_INTERNAL);
    pcjx_log_mapping(pcjx, "main RAM", 0, size, can_read, can_write);

    if (configured_ram > size) {
        uint32_t extra_size = configured_ram - size;

        mem_mapping_set_addr(&pcjx->exmem_mapping, size, extra_size);
        mem_mapping_set_exec(&pcjx->exmem_mapping, ram + size);
        mem_mapping_enable(&pcjx->exmem_mapping);
        mem_set_mem_state_both(size, extra_size,
                               MEM_READ_INTERNAL | MEM_WRITE_INTERNAL);
        pcjx_log_mapping(pcjx, "extended RAM", size, extra_size,
                         can_read, can_write);
    }
}

static void
pcjx_apply_video_window(pcjx_t *pcjx)
{
    const pcjx_block_state_t *block = &pcjx->blocks[0x09];
    uint32_t                  base  = 0;
    uint32_t                  size  = 0;
    uint8_t                   can_read  = 0;
    uint8_t                   can_write = 0;

    if (pcjx->pcjr == NULL)
        return;

    pcjx_clear_range(pcjx->pcjr->mapping.base, pcjx->pcjr->mapping.size);
    mem_mapping_disable(&pcjx->pcjr->mapping);

    if (!pcjx_decode_block_range(block, &base, &size, &can_read, &can_write))
        base = size = 0;

    pcjx_video_apply_first_window(&pcjx->video, base, size, can_read, can_write);
    pcjx_log_mapping(pcjx, "video window 1", base, size, can_read, can_write);
}

static void
pcjx_apply_video_window_2(pcjx_t *pcjx)
{
    const pcjx_block_state_t *block = &pcjx->blocks[0x0a];
    uint32_t                  base  = 0;
    uint32_t                  size  = 0;
    uint8_t                   can_read;
    uint8_t                   can_write;

    if (!pcjx_decode_block_range(block, &base, &size, &can_read, &can_write)) {
        pcjx_video_apply_second_window(&pcjx->video, 0, 0, 0, 0);
        pcjx_log_mapping(pcjx, "video window 2", 0, 0, 0, 0);
        return;
    }

    pcjx_video_apply_second_window(&pcjx->video, base, size, can_read, can_write);
    pcjx_log_mapping(pcjx, "video window 2", base, size, can_read, can_write);
}

static void
pcjx_apply_block(pcjx_t *pcjx, uint8_t index)
{
    switch (index) {
        case 0x00:
            pcjx_apply_system_rom(pcjx);
            break;

        case 0x07:
            pcjx_apply_font_window(pcjx);
            break;

        case 0x08:
            pcjx_apply_main_ram(pcjx);
            break;

        case 0x09:
            /* Only the first JX VRAM window fits the current PCjr video core. */
            pcjx_apply_video_window(pcjx);
            break;

        case 0x0a:
            pcjx_apply_video_window_2(pcjx);
            break;

        case 0x80:
        case 0x81:
        case 0x82:
        case 0x8c:
        case 0x8d:
        case 0x8e:
        case 0x83:
        case 0x84:
        case 0x86:
        case 0x87:
        case 0x85:
        case 0x88:
        case 0x89:
        case 0x8a:
        case 0x90:
        case 0x91:
            pcjx_sync_video_io(pcjx);
            break;

        default:
            break;
    }
}

static void
pcjx_store_mapper_reg1(pcjx_t *pcjx, uint8_t value)
{
    const pcjx_block_config_t *config;

    if (!pcjx_block_has_config(pcjx->mapper_index))
        return;

    config = &pcjx_block_configs[pcjx->mapper_index];
    pcjx->blocks[pcjx->mapper_index].reg1 = (uint8_t) ((value & config->andreg1) | config->orreg1);
    pcjx_log(pcjx->log,
             "[%04X:%08X] mapper[%02X].reg1 raw=%02X stored=%02X\n",
             CS, cpu_state.pc, pcjx->mapper_index, value,
             pcjx->blocks[pcjx->mapper_index].reg1);

    switch (pcjx->mapper_index) {
        case 0x80:
        case 0x81:
        case 0x82:
        case 0x8c:
        case 0x8d:
        case 0x8e:
        case 0x83:
        case 0x84:
        case 0x86:
        case 0x87:
        case 0x85:
        case 0x88:
        case 0x89:
        case 0x8a:
        case 0x90:
        case 0x91:
            pcjx_apply_block(pcjx, pcjx->mapper_index);
            break;

        default:
            break;
    }
}

static void
pcjx_store_mapper_reg2(pcjx_t *pcjx, uint8_t value)
{
    const pcjx_block_config_t *config;

    if (!pcjx_block_has_config(pcjx->mapper_index))
        return;

    config = &pcjx_block_configs[pcjx->mapper_index];
    pcjx->blocks[pcjx->mapper_index].reg2 = (uint8_t) ((value & config->andreg2) | config->orreg2);
    pcjx_log(pcjx->log,
             "[%04X:%08X] mapper[%02X].reg2 raw=%02X stored=%02X\n",
             CS, cpu_state.pc, pcjx->mapper_index, value,
             pcjx->blocks[pcjx->mapper_index].reg2);
    pcjx_apply_block(pcjx, pcjx->mapper_index);
}

static void
pcjx_mapper_out(uint16_t port, uint8_t value, void *priv)
{
    pcjx_t *pcjx = (pcjx_t *) priv;

    if (port != PCJX_MAPPER_PORT)
        return;

    switch (pcjx->mapper_phase) {
        case 0:
            pcjx_log(pcjx->log,
                     "[%04X:%08X] [W] %04X = %02X (mapper index)\n",
                     CS, cpu_state.pc, port, value);
            pcjx->mapper_index = value;
            pcjx->mapper_phase = 1;
            break;

        case 1:
            pcjx_store_mapper_reg1(pcjx, value);
            pcjx->mapper_phase = 2;
            break;

        default:
            pcjx_store_mapper_reg2(pcjx, value);
            pcjx->mapper_phase = 0;
            break;
    }
}

static uint8_t
pcjx_rtc_in(uint16_t port, void *priv)
{
    const pcjx_t *pcjx   = (const pcjx_t *) priv;
    uint8_t offset = (uint8_t) (port - PCJX_RTC_BASE_PORT);
    uint8_t value = 0;

    if (offset < 0x0d)
        value = pcjx->rtc_latch[offset];

    pcjx_log(pcjx->log, "[%04X:%08X] [R] %04X = %02X (rtc[%02X])\n",
             CS, cpu_state.pc, port, value, offset);

    return value;
}

static void
pcjx_rtc_out(uint16_t port, uint8_t value, void *priv)
{
    pcjx_t  *pcjx   = (pcjx_t *) priv;
    uint8_t offset = (uint8_t) (port - PCJX_RTC_BASE_PORT);

    pcjx_log(pcjx->log, "[%04X:%08X] [W] %04X = %02X (rtc[%02X])\n",
             CS, cpu_state.pc, port, value, offset);

    if (offset < 0x0d) {
        pcjx->rtc_latch[offset] = value;
        return;
    }

    if (offset == 0x0d) {
        if (value) {
            pcjx->rtc_write_mode = 0;
            pcjx_rtc_latch_read(pcjx);
        } else if (pcjx->rtc_write_mode) {
            pcjx->rtc_write_mode = 0;
            pcjx_rtc_commit_write(pcjx);
        }
        return;
    }

    if (offset == 0x0f)
        pcjx->rtc_write_mode = 1;
}

static void *
pcjx_extension_init(UNUSED(const device_t *info))
{
    pcjx_t *pcjx;
    uint32_t program_size;

    pcjx = calloc(1, sizeof(*pcjx));
    if (pcjx == NULL)
        return NULL;

#ifdef ENABLE_PCJX_LOG
    pcjx->log = log_open("PCjx");
#endif

    pcjx->pcjr           = (pcjr_t *) device_get_priv(&pcjx_device);
    pcjx->status_ex_video = 1;
    pcjx->rtc_time       = time(NULL);
    pcjx->rtc_host_time  = pcjx->rtc_time;

    if (pcjx->pcjr == NULL) {
        if (pcjx->log)
            log_close(pcjx->log);
        free(pcjx);
        return NULL;
    }

    pcjx->font_rom = calloc(1, PCJX_FONT_ROM_SIZE);
    if (pcjx->font_rom == NULL) {
        if (pcjx->log)
            log_close(pcjx->log);
        free(pcjx);
        return NULL;
    }
    memset(pcjx->font_rom, 0xff, PCJX_FONT_ROM_SIZE);
    if (!rom_load_linear(PCJX_FONT_ROM_PATH, 0, PCJX_FONT_ROM_SIZE, 0,
                         pcjx->font_rom)) {
        free(pcjx->font_rom);
        if (pcjx->log)
            log_close(pcjx->log);
        free(pcjx);
        return NULL;
    }

    mem_mapping_add(&pcjx->exmem_mapping, 0, 0,
                    mem_read_ram, mem_read_ramw, mem_read_raml,
                    mem_write_ram, mem_write_ramw, mem_write_raml,
                    ram, MEM_MAPPING_INTERNAL, pcjx);
    mem_mapping_disable(&pcjx->exmem_mapping);

    mem_mapping_add(&pcjx->font_mapping, 0, 0,
                    pcjx_font_read, pcjx_font_readw, pcjx_font_readl,
                    pcjx_font_write, pcjx_font_writew, pcjx_font_writel,
                    pcjx->font_rom, MEM_MAPPING_EXTERNAL, pcjx);
    mem_mapping_disable(&pcjx->font_mapping);

    program_size = pcjx_configured_ram_size();
    if (program_size > 0x20000)
        program_size = 0x20000;
    pcjx_video_init(&pcjx->video, program_size);
    pcjx_video_set_font_rom(&pcjx->video, pcjx->font_rom, PCJX_FONT_ROM_SIZE);
    if (pcjx->pcjr != NULL)
        pcjx->pcjr->pcjx_video = &pcjx->video;

    pcjx_log(pcjx->log, "Init program_size=%05X font_size=%05X\n",
             (unsigned int) program_size, (unsigned int) PCJX_FONT_ROM_SIZE);

    io_sethandler(PCJX_MAPPER_PORT, 1,
                  pcjx_mapper_in, NULL, NULL,
                  pcjx_mapper_out, NULL, NULL,
                  pcjx);
    io_sethandler(PCJX_RTC_BASE_PORT, PCJX_RTC_PORTS,
                  pcjx_rtc_in, NULL, NULL,
                  pcjx_rtc_out, NULL, NULL,
                  pcjx);

    return pcjx;
}

static void
pcjx_extension_close(void *priv)
{
    pcjx_t *pcjx = (pcjx_t *) priv;

    if (pcjx == NULL)
        return;

    pcjx_log(pcjx->log, "Close\n");

    if ((pcjx->pcjr != NULL) && (pcjx->pcjr->pcjx_video == &pcjx->video))
        pcjx->pcjr->pcjx_video = NULL;

    pcjx_video_close(&pcjx->video);

    mem_mapping_disable(&pcjx->exmem_mapping);
    mem_mapping_disable(&pcjx->font_mapping);

    io_removehandler(PCJX_MAPPER_PORT, 1,
                     pcjx_mapper_in, NULL, NULL,
                     pcjx_mapper_out, NULL, NULL,
                     pcjx);
    io_removehandler(PCJX_RTC_BASE_PORT, PCJX_RTC_PORTS,
                     pcjx_rtc_in, NULL, NULL,
                     pcjx_rtc_out, NULL, NULL,
                     pcjx);
    free(pcjx->font_rom);
    if (pcjx->log)
        log_close(pcjx->log);
    free(pcjx);
}

int
machine_pcjx_init(UNUSED(const machine_t *model))
{
    int ret;

    ret = machine_pcjr_common_init_with_video(PCJX_BASE_ROM_PATH, 0x000e0000,
                                              131072, PCJX_FONT_ROM_PATH,
                                              &pcjx_device, pcjx_vid_init);
    if (bios_only || !ret)
        return ret;

    device_add(&pcjx_extension_device);
    return ret;
}