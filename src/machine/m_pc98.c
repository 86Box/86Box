/*
 * 86Box experimental NEC PC-9801VX motherboard.
 *
 * This is deliberately a PC-98 motherboard, not an AT with ports moved around.
 * The low I/O decoder follows the PC-9801 split-byte bus: 8259 and 8237 share
 * 00h-1fh on opposite byte lanes, the two 8255s and two 8251s are interleaved,
 * and the PIT lives on odd ports while the display/GRCG registers occupy the
 * even ports in the same 70h-7fh window.
 */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include <86box/86box.h>
#include "cpu.h"
#include <86box/device.h>
#include <86box/timer.h>
#include <86box/dma.h>
#include <86box/fdc.h>
#include <86box/fdc_ext.h>
#include <86box/fdd.h>
#include <86box/hdd.h>
#include <86box/hdc.h>
#include <86box/io.h>
#include <86box/keyboard.h>
#include <86box/lpt.h>
#include <86box/machine.h>
#include <86box/mem.h>
#include <86box/mouse.h>
#include <86box/nvr.h>
#include <86box/pic.h>
#include <86box/pit.h>
#include <86box/postcard.h>
#include <86box/rom.h>
#include <86box/serial.h>
#include <86box/snd_speaker.h>
#include <86box/video.h>
#include <86box/vid_pc98.h>
#include <86box/pc98_ppi8255.h>
#include <86box/pc98_i8251.h>
#include <86box/pc98_mouse.h>
#include <86box/pc98_sasi.h>
#include <86box/plat.h>
#include <86box/plat_unused.h>

#define PC98_PIT_CLOCK 1996800u
#define PC98_FDC_IRQ   11 /* slave 8259 IR3 */
#define PC98_FDC_DMA   2

/* System 8255 port C (35h), read/write, per the PC-9801 I/O map:
 *
 *   D7 SHUT0  0: continue processing after reset
 *   D6 PSTM   printer PSTB mask
 *   D5 SHUT1  0: abort processing on reset
 *   D4 MCKE   store the memory check result
 *   D3 BUZ    buzzer (0: sounding, 1: stopped)
 *   D2 TXRE / D1 TXEE / D0 RXRE   RS-232C interrupt enables
 *
 * The whole register is driven by bit set/reset commands to 37h (00h..0Fh)
 * and read back at 35h.  The firmware never sends port C a mode word, so the
 * 8255 keeps it configured as an input and the read has to come from the
 * board-level latch modelled here. */
#define PC98_SYS_C_SHUT0 0x80u
#define PC98_SYS_C_PSTM  0x40u
#define PC98_SYS_C_SHUT1 0x20u
#define PC98_SYS_C_MCKE  0x10u
#define PC98_SYS_C_BUZ   0x08u

/* Power-on latch: both shutdown flags inactive, buzzer stopped. */
#define PC98_SYS_C_RESET (PC98_SYS_C_SHUT0 | PC98_SYS_C_SHUT1 | PC98_SYS_C_BUZ)

/* DIP switch SW1, in switch order.  SW1-1 (display resolution) is reported
 * through the system PPI as CRTT; SW1-3 and SW1-8 come back through the
 * printer PPI; SW1-5 and SW1-6 through the mouse PPI. */
#define PC98_SW1_1 0x01u
#define PC98_SW1_3 0x04u
#define PC98_SW1_5 0x10u
#define PC98_SW1_6 0x20u
#define PC98_SW1_8 0x80u

/* Printer 8255 port B (42h), the system-type register. */
#define PC98_PRN_B_FIXED 0x80u /* D7 reads as 1, D6 as 0 */
#define PC98_PRN_B_MOD   0x20u /* system clock family: 1 = 8 MHz */
#define PC98_PRN_B_SW13  0x10u
#define PC98_PRN_B_SW18  0x08u
#define PC98_PRN_B_BSY   0x04u /* printer BUSY input, active low */
#define PC98_PRN_B_ACPU  0x02u /* 1 on V30 machines only */

/* 1MB (2HD) interface control register, written at 94h.  Only two bits exist:
 * everything else reads back from the separate switch register. */
#define PC98_FDC_1MB_RST 0x80u /* uPD765 reset, asserted HIGH */
#define PC98_FDC_1MB_RDY 0x40u /* force READY */

/* 640KB (2DD) interface control register, written at CCh. */
#define PC98_FDC_640_RST  0x80u
#define PC98_FDC_640_EAI1 0x40u /* EAI1:EAI0 = 10 enables attention interrupts */
#define PC98_FDC_640_EAI0 0x20u
#define PC98_FDC_640_DMAE 0x10u /* 0: DMA inhibited */
#define PC98_FDC_640_MTON 0x08u /* motor control */
#define PC98_FDC_640_TMSK 0x04u /* motor timer interrupt mask */
#define PC98_FDC_640_TTRG 0x01u /* start the motor timer */

/* Switch register read back at 94h / CCh. */
#define PC98_FDC_SW_FIXED 0x40u /* D6 reads as 1 on both interfaces */
#define PC98_FDC_SW_640   0x20u /* D5 reads as 1 on the 640KB interface */
#define PC98_FDC_SW_RDY   0x10u /* 640KB interface only: drive ready */
#define PC98_FDC_SW_TYP1  0x08u /* FDD #3,#4 type: 1 = 1MB/640KB dual */
#define PC98_FDC_SW_TYP0  0x04u /* FDD #1,#2 type: 1 = 1MB/640KB dual */

/* Depth of the buffered sense-interrupt queue: one entry per drive is what the
 * hardware can have outstanding, plus headroom for the post-reset reports. */
#define PC98_FDC_SIS_QUEUE 8

/* Mode register at BEh, both directions. */
#define PC98_FDC_MODE_SW32 0x08u /* read only: DIP SW3-2 */
#define PC98_FDC_MODE_SW31 0x04u /* read only: DIP SW3-1 */
#define PC98_FDC_MODE_FDE  0x02u /* drive switch:    0 = 640KB, 1 = 1MB */
#define PC98_FDC_MODE_PE   0x01u /* I/O port switch: 0 = 640KB, 1 = 1MB */

/* 86Box CPU core: 80286 software reset that leaves RAM and devices alone. */
extern void softresetx86(void);

/* Start with a small keyboard FIFO and grow it if the guest stops draining
 * the USART. Dropping a queued break byte would leave that guest key held. */
#define PC98_KBD_FIFO_INITIAL 64u

/* NEC uPD4990A-compatible serial RTC as wired in PC-9801VM/VX-class
 * systems.  Port 20h supplies C0/C1/C2, STB, CLK and DATA IN; DATA OUT
 * appears on system PPI port B bit 0.  CS and OE are tied active. */
typedef struct pc98_rtc_t {
    uint8_t c_unlatched;
    uint8_t command;
    uint8_t stb;
    uint8_t clk;
    uint8_t data_in;
    uint8_t data_out;
    uint8_t shift[7];
    int64_t time_offset;
} pc98_rtc_t;

#define PC98_RTC_NVR_FILE "pc9801vx.rtc"
#define PC98_RTC_NVR_SIZE 16u

enum {
    PC98_RTC_HOLD      = 0,
    PC98_RTC_SHIFT     = 1,
    PC98_RTC_TIME_SET  = 2,
    PC98_RTC_TIME_READ = 3
};

/* Boot diagnostics for the extended-memory sizing loop.
 *
 * The VX ITF measures memory above 1 MiB from protected mode by walking two
 * 64 KiB descriptors upward in 128 KiB steps: for each step it fills both
 * windows with a pattern (rep stosw) and verifies them (repz scasw), and the
 * first step that does not read back ends the walk.  It then resets the CPU
 * through F0h, and back in real mode computes
 *
 *     [0000:0401] = (final descriptor base >> 16 - 10h) / 2
 *
 * i.e. the extended-memory size in 128 KiB units.  If nothing answers at
 * 100000h the very first pass fails and the machine reports no extended
 * memory at all - which is also exactly what a 640 KB configuration looks
 * like, since there is then genuinely nothing mapped up there.
 *
 * Logging only new high-water marks turns the walk into one line per step, so
 * the log shows how far the probe got and where the mapping stopped. */
/* Dump bytes in a single pclog call.
 *
 * 86Box's log collapses consecutive identical entries, so emitting one pclog
 * per byte turns every dump into "00 *** 15 repeats ***" and has to be
 * reassembled by hand.  Format the whole line first. */
#ifndef RELEASE_BUILD
static void
pc98_hexdump(const char *tag, uint32_t addr, unsigned count)
{
    char line[3 * 16 + 1];

    if (!ram)
        return;
    while (count) {
        const unsigned n   = (count > 16u) ? 16u : count;
        unsigned       pos = 0;

        for (unsigned i = 0; i < n; i++)
            pos += (unsigned) snprintf(line + pos, sizeof(line) - pos, "%02X ",
                                       ram[addr + i]);
        pclog("[PC98] %s %06X: %s\n", tag, addr, line);
        addr += n;
        count -= n;
    }
}

/* Find the descriptor pair the ITF's memory walk drives.
 *
 * It builds two data descriptors with limit FFFFh, base low word zero and
 * access 93h, at consecutive 8-byte slots, whose base bits 23:16 differ by
 * one.  Searching for that shape locates the GDT wherever it actually is,
 * without needing to guess at the CPU's GDTR. */
static void
pc98_find_walk_descriptors(void)
{
    unsigned found = 0;

    if (!ram)
        return;

    for (uint32_t a = 0; a + 16u <= 0x000a0000u && found < 4u; a += 4u) {
        if (ram[a] != 0xffu || ram[a + 1] != 0xffu || ram[a + 2] != 0x00u || ram[a + 3] != 0x00u || ram[a + 5] != 0x93u)
            continue;
        if (ram[a + 8] != 0xffu || ram[a + 9] != 0xffu || ram[a + 10] != 0x00u || ram[a + 11] != 0x00u || ram[a + 13] != 0x93u)
            continue;
        if (ram[a + 12] != (uint8_t) (ram[a + 4] + 1u))
            continue;

        found++;
        pclog("[PC98] walk descriptor pair at %06X: bases %02X0000 and "
              "%02X0000 (GDT base would be %06X)\n",
              a, ram[a + 4], ram[a + 12], a - 0x40u);
    }

    if (!found)
        pclog("[PC98] no walk descriptor pair found in conventional RAM\n");
}

#endif

static unsigned pc98_dma_logged              = 0;
static int      pc98_firmware_memsize_logged = 0;

static void
pc98_configure_ram_map(void)
{
    /* 86Box already models the conventional A0000h-FFFFFh hole.  PC-98
     * installed-memory sizes exclude that 384 KiB aperture, so recover the
     * hidden backing at the top of installed RAM.  CPU upgrades with a
     * 32-bit address bus can place this backing above 16 MiB. */
    mem_remap_top_extended(384);

    const unsigned ext_kb = (mem_size > 640) ? (unsigned) (mem_size - 640) : 0u;
    if (ext_kb)
        pclog("[PC98] RAM map: configured %dKB total; "
              "%uKB hole remapped at %08X\n",
              mem_size, ram_remapped_mapping.enable ? ram_remapped_mapping.size >> 10 : 0u,
              ram_remapped_mapping.base);
    else
        pclog("[PC98] RAM map: 640KB conventional only, no extended RAM "
              "(configured %dKB)\n",
              mem_size);
}

typedef struct pc98_mb_t {
    pc98_ppi8255_t ppi_sys;
    pc98_ppi8255_t ppi_prn;
    pc98_ppi8255_t ppi_mouse;
    pc98_mouse_t   mouse;
    pc_timer_t     mouse_timer;
    lpt_t         *printer;
    pc98_i8251_t   usart_kbd;
    pc98_i8251_t   usart_rs;

    pit_t        *pit;
    fdc_t        *fdc;
    void         *video_dev;
    pc98_video_t *video;
    pc98_model_t  model;
    uint8_t       sdip[24];
    uint8_t       sdip_bank;
    uint8_t       display_frequency;

    uint8_t dma_autoinc[4];
    uint8_t dma_autoinc_latch;
    uint8_t dma_access_ctrl;
    uint8_t fdc_ctrl_1mb;
    uint8_t fdc_ctrl_640;
    uint8_t fdc_mode;
    uint8_t motor_1mb;
    uint8_t motor_640;
    uint8_t motor_on;
    uint8_t fdc_bank_1mb;
    /* The generic 86Box uPD765 has one mutable IRQ/DMA route, while a PC-98
     * exposes that one controller through two identities.  Media commands are
     * asynchronous, so once one identity starts such a command its physical
     * route must stay pinned until the result phase is drained.  Otherwise a
     * later BEh/control/probe access through the other identity can move an
     * in-flight CAh transfer from DMA3/IRQ10 to DMA2/IRQ11 (or vice versa). */
    uint8_t  fdc_route_lock;
    uint8_t  fdc_route_lock_bank; /* 0=640KB/CAh, 1=1MB/92h */
    uint8_t  dmae_640;
    uint8_t  fdc_xfer_ch;
    uint8_t  fdc_xfer_pending;
    uint32_t fdc_xfer_addr;
    /* The VM/VX exposes one physical uPD765 through two I/O identities, but
     * the two identities terminate on different PIC inputs.  SEEK/RECALIBRATE
     * completions therefore have to remember which bank issued them; a single
     * shared queue/timer lets a later CAh command steal a 92h completion and is
     * exactly what deadlocked the PC-9801-27 SASI ROM at BIOS work byte 055Eh.
     *
     * Index 0 = 640KB (C8h/CAh/CCh, IRQ10)
     * Index 1 = 1MB   (90h/92h/94h, IRQ11) */
    uint8_t sis_q[2][PC98_FDC_SIS_QUEUE][2];
    uint8_t sis_q_n[2];
    uint8_t sis_res[2][2];
    uint8_t sis_res_n[2];
    uint8_t sis_res_i[2];

    /* A result phase which belongs to the identity that issued the command.
     * This is currently used for READ ID on an empty drive.  The generic FDC
     * waits on media polling before it can finish that command; on PC-98 the
     * option ROM has already installed a bank-specific IRQ handler and waits
     * forever at D700:0255 if no completion interrupt arrives. */
    uint8_t    cmd_res[2][7];
    uint8_t    cmd_res_n[2];
    uint8_t    cmd_res_i[2];
    pc_timer_t seek_irq_timer[2];

    /* SEEK and RECALIBRATE are completed by the PC-98 board wrapper rather
     * than by the generic 86Box FDC.  The generic core has only one pending
     * seek interrupt slot; real uPD765 hardware can have one seek outstanding
     * per drive.  Keeping the commands out of that single slot also prevents
     * an asynchronous 92h seek completion from being delivered later on IRQ10
     * after firmware has switched to the CAh identity. */
    uint8_t    wrapper_seek_cmd;
    uint8_t    wrapper_seek_need;
    uint8_t    wrapper_readid_need;
    uint8_t    wrapper_readid_bank;
    uint8_t    fdc_in_params;
    uint8_t    fdc_cmd;
    uint8_t    trace_cmd[10];
    uint8_t    trace_cmd_n;
    uint8_t    trace_res[8];
    uint8_t    trace_res_n;
    uint8_t    fdc_3mode_dev_sel;
    uint8_t    fdc_3mode_144[2];
    uint8_t    dsw3;
    uint8_t    nmi_ff;
    uint8_t    rtc_ctrl;
    pc98_rtc_t rtc;
    uint8_t    rs_irq_mask;
    uint8_t    rs_irq_pending;
    uint8_t    dsw1;
    uint8_t    dsw2;
    /* System PPI port C output latch.  It lives on the board rather than
     * inside the 8255 - the firmware only ever drives it with bit set/reset
     * commands to 37h - so it is tracked from the raw port writes and is
     * unaffected by 8255 mode words and by an 80286 software reset. */
    uint8_t sys_port_c;

    uint8_t *kbd_fifo;
    size_t   kbd_rp;
    size_t   kbd_count;
    size_t   kbd_capacity;
    uint8_t  host_down[512];
    uint8_t  kbd_code_users[128];
} pc98_mb_t;

/* Translate the normalized 86Box host scan index (Set-1 position for
 * ordinary keys) to a PC-98 keyboard scan value. */
static uint8_t
pc98_translate_scan(uint16_t scan)
{
    if (scan >= 0x02 && scan <= 0x0c)
        return (uint8_t) (scan - 1); /* 1..- */
    if (scan >= 0x10 && scan <= 0x19)
        return scan; /* Q..P */
    if (scan >= 0x1e && scan <= 0x26)
        return (uint8_t) (scan - 1); /* A..L */
    if (scan >= 0x2c && scan <= 0x35)
        return (uint8_t) (scan - 3); /* Z../ */
    if (scan >= 0x3b && scan <= 0x44)
        return (uint8_t) (0x62 + scan - 0x3b); /* F1..F10 */

    switch (scan) {
        case 0x02a: /* left Shift */
        case 0x036: /* right Shift */
            return 0x70;
        case 0x01d: /* left Ctrl */
        case 0x11d: /* right Ctrl */
            return 0x74;
        case 0x038: /* left Alt / PC-98 GRPH */
        case 0x138: /* right Alt / PC-98 GRPH */
            return 0x73;
        case 0x03a: /* Caps Lock */
            return 0x71;
        case 0x045: /* PC-98 VF3 */
            return 0x54;
        case 0x046: /* PC-98 VF4 */
            return 0x55;
        case 0x01:
            return 0x00; /* ESC */
        case 0x0d:
            return 0x0c; /* ^ / = position */
        case 0x0e:
            return 0x0e; /* backspace */
        case 0x0f:
            return 0x0f; /* tab */
        case 0x1a:
            return 0x1a; /* [ */
        case 0x1b:
            return 0x1b; /* ] */
        case 0x1c:
            return 0x1c; /* return */
        case 0x27:
            return 0x26; /* ; */
        case 0x28:
            return 0x27; /* : / quote position */
        case 0x29:
            return 0x0d; /* yen/backslash-ish position */
        case 0x2b:
            return 0x28;
        case 0x37:
            return 0x45; /* keypad * */
        case 0x39:
            return 0x34; /* space */
        case 0x47:
            return 0x42; /* keypad 7 */
        case 0x48:
            return 0x43; /* keypad 8 */
        case 0x49:
            return 0x44; /* keypad 9 */
        case 0x4a:
            return 0x40; /* keypad - */
        case 0x4b:
            return 0x46; /* keypad 4 */
        case 0x4c:
            return 0x47; /* keypad 5 */
        case 0x4d:
            return 0x48; /* keypad 6 */
        case 0x4e:
            return 0x49; /* keypad + */
        case 0x4f:
            return 0x4a; /* keypad 1 */
        case 0x50:
            return 0x4b; /* keypad 2 */
        case 0x51:
            return 0x4c; /* keypad 3 */
        case 0x52:
            return 0x4e; /* keypad 0 */
        case 0x53:
            return 0x50; /* keypad . */
        case 0x56:
            return 0x33; /* PC-98 RO key position */
        case 0x57:
            return 0x52; /* F11 / VF1 */
        case 0x58:
            return 0x53; /* F12 / VF2 */
        case 0x11c:
            return 0x1c; /* keypad Enter shares main Enter */
        case 0x135:
            return 0x41; /* keypad / */
        case 0x100:
            return 0x60; /* Pause / STOP via E1 1D */
        case 0x137:
            return 0x61; /* Print Screen / COPY */
        case 0x145:
            return 0x60; /* Pause / STOP */
        case 0x147:
            return 0x3e; /* Home / HOME CLR */
        case 0x148:
            return 0x3a; /* Up */
        case 0x149:
            return 0x37; /* Page Up / ROLL DOWN */
        case 0x14b:
            return 0x3b; /* Left */
        case 0x14d:
            return 0x3c; /* Right */
        case 0x14f:
            return 0x3f; /* End / HELP */
        case 0x150:
            return 0x3d; /* Down */
        case 0x151:
            return 0x36; /* Page Down / ROLL UP */
        case 0x152:
            return 0x38; /* Insert */
        case 0x153:
            return 0x39; /* Delete */
        default:
            return 0xff;
    }
}

static void
pc98_kbd_pump(pc98_mb_t *d)
{
    if (!d->kbd_count || !(d->usart_kbd.command & 0x04) || (d->usart_kbd.status & PC98_I8251_ST_RXRDY))
        return;
    pc98_i8251_receive(&d->usart_kbd, d->kbd_fifo[d->kbd_rp], 0, 0);
    d->kbd_rp = (d->kbd_rp + 1) % d->kbd_capacity;
    d->kbd_count--;
    if (!d->kbd_count)
        d->kbd_rp = 0;
}

static void
pc98_kbd_queue(pc98_mb_t *d, uint8_t value)
{
    if (d->kbd_count == d->kbd_capacity) {
        size_t   capacity = d->kbd_capacity ? d->kbd_capacity * 2 : PC98_KBD_FIFO_INITIAL;
        uint8_t *fifo;

        if (capacity <= d->kbd_capacity) {
            fatal("PC-98 keyboard FIFO capacity overflow\n");
            return;
        }

        fifo = (uint8_t *) malloc(capacity);
        if (!fifo) {
            fatal("PC-98 keyboard FIFO allocation failed\n");
            return;
        }

        for (size_t i = 0; i < d->kbd_count; i++)
            fifo[i] = d->kbd_fifo[(d->kbd_rp + i) % d->kbd_capacity];
        free(d->kbd_fifo);
        d->kbd_fifo     = fifo;
        d->kbd_capacity = capacity;
        d->kbd_rp       = 0;
    }

    d->kbd_fifo[(d->kbd_rp + d->kbd_count) % d->kbd_capacity] = value;
    d->kbd_count++;
    pc98_kbd_pump(d);
}

static void
pc98_keyboard_host(uint16_t scan, int down, void *priv)
{
    pc98_mb_t *d = (pc98_mb_t *) priv;
    uint8_t    code;

    if (scan >= 512)
        return;
    down = !!down;
    if (d->host_down[scan] == down)
        return;
    d->host_down[scan] = (uint8_t) down;

    code = pc98_translate_scan(scan);
    if (code == 0xff)
        return;

    /* Left/right modifiers share one physical PC-98 key. Keep that guest key
     * held until both corresponding host keys have been released. */
    if (down) {
        if (d->kbd_code_users[code]++ == 0)
            pc98_kbd_queue(d, code);
    } else if (d->kbd_code_users[code] && --d->kbd_code_users[code] == 0) {
        pc98_kbd_queue(d, (uint8_t) (code | 0x80));
    }
}

static void
pc98_kbd_tx(void *opaque, uint8_t value)
{
    pc98_mb_t *d = (pc98_mb_t *) opaque;
    /* The standard pre-119 keyboard meaningfully requires reset only; later
     * 9xh command families are keyboard-model specific. */
    if (value == 0xff) {
        d->kbd_rp = d->kbd_count = 0;
        memset(d->host_down, 0, sizeof(d->host_down));
        memset(d->kbd_code_users, 0, sizeof(d->kbd_code_users));
    }
}

static void
pc98_kbd_rxrdy(void *opaque, int level)
{
    (void) opaque;
    if (level)
        picint(1u << 1);
    else
        picintc(1u << 1);
}

static void
pc98_rs_recalc_irq(pc98_mb_t *d)
{
    uint8_t pending = 0;
    if ((d->rs_irq_mask & 1) && (d->usart_rs.status & PC98_I8251_ST_RXRDY))
        pending = 1;
    if ((d->rs_irq_mask & 2) && (d->usart_rs.status & PC98_I8251_ST_TXRDY))
        pending = 1;
    if ((d->rs_irq_mask & 4) && (d->usart_rs.status & PC98_I8251_ST_TXE))
        pending = 1;
    d->rs_irq_pending = pending;
    if (pending)
        picint(1u << 4);
    else
        picintc(1u << 4);
}

static void
pc98_rs_line(void *opaque, int level)
{
    (void) level;
    pc98_rs_recalc_irq((pc98_mb_t *) opaque);
}

static uint8_t
pc98_bcd(unsigned v)
{
    return (uint8_t) (((v / 10u) << 4) | (v % 10u));
}

static unsigned
pc98_from_bcd(uint8_t v)
{
    return ((unsigned) (v >> 4) * 10u) + (unsigned) (v & 0x0fu);
}

static void
pc98_rtc_save(const pc98_rtc_t *r)
{
    uint8_t image[PC98_RTC_NVR_SIZE] = {
        'P', '9', '8', 'R', 'T', 'C', 1u, 0u
    };
    const uint64_t offset = (uint64_t) r->time_offset;

    for (unsigned i = 0; i < 8u; i++)
        image[8u + i] = (uint8_t) (offset >> (i * 8u));

    FILE *f = nvr_fopen(PC98_RTC_NVR_FILE, "wb");
    if (f) {
        (void) fwrite(image, 1, sizeof(image), f);
        fclose(f);
    }
}

static void
pc98_rtc_load(pc98_rtc_t *r)
{
    uint8_t              image[PC98_RTC_NVR_SIZE];
    static const uint8_t signature[8] = {
        'P', '9', '8', 'R', 'T', 'C', 1u, 0u
    };
    FILE *f = nvr_fopen(PC98_RTC_NVR_FILE, "rb");

    if (!f)
        return;
    const size_t got = fread(image, 1, sizeof(image), f);
    fclose(f);
    if (got != sizeof(image) || memcmp(image, signature, sizeof(signature)))
        return;

    uint64_t offset = 0u;
    for (unsigned i = 0; i < 8u; i++)
        offset |= (uint64_t) image[8u + i] << (i * 8u);
    r->time_offset = (int64_t) offset;
}

static void
pc98_rtc_capture_time(pc98_rtc_t *r)
{
    time_t now     = time(NULL);
    now            = (time_t) ((int64_t) now + r->time_offset);
    struct tm *tmv = localtime(&now);
    if (!tmv) {
        memset(r->shift, 0, 6);
        return;
    }

    r->shift[0] = pc98_bcd((unsigned) tmv->tm_sec);
    r->shift[1] = pc98_bcd((unsigned) tmv->tm_min);
    r->shift[2] = pc98_bcd((unsigned) tmv->tm_hour);
    r->shift[3] = pc98_bcd((unsigned) tmv->tm_mday);
    /* uPD4990A byte 4: high nibble month, low nibble day-of-week (0-6). */
    r->shift[4] = (uint8_t) (((tmv->tm_mon + 1) << 4) | (tmv->tm_wday & 7));
    r->shift[5] = pc98_bcd((unsigned) ((tmv->tm_year + 1900) % 100));
}

static void
pc98_rtc_commit_time(pc98_rtc_t *r)
{
    struct tm tmv;
    memset(&tmv, 0, sizeof(tmv));

    unsigned yy   = pc98_from_bcd(r->shift[5]);
    unsigned mon  = (r->shift[4] >> 4) & 0x0f;
    unsigned mday = pc98_from_bcd(r->shift[3]);
    unsigned hour = pc98_from_bcd(r->shift[2]);
    unsigned min  = pc98_from_bcd(r->shift[1]);
    unsigned sec  = pc98_from_bcd(r->shift[0]);

    if (mon < 1 || mon > 12 || mday < 1 || mday > 31 || hour > 23 || min > 59 || sec > 59)
        return;

    tmv.tm_year  = (int) ((yy < 70 ? 2000 : 1900) + yy - 1900);
    tmv.tm_mon   = (int) mon - 1;
    tmv.tm_mday  = (int) mday;
    tmv.tm_hour  = (int) hour;
    tmv.tm_min   = (int) min;
    tmv.tm_sec   = (int) sec;
    tmv.tm_isdst = -1;

    time_t requested = mktime(&tmv);
    time_t now       = time(NULL);
    if (requested != (time_t) -1 && now != (time_t) -1)
        r->time_offset = (int64_t) requested - (int64_t) now;
    pc98_rtc_save(r);
}

static void
pc98_rtc_reset(pc98_rtc_t *r)
{
    /* Reset the serial interface, not the battery-backed clock. */
    const int64_t time_offset = r->time_offset;
    memset(r, 0, sizeof(*r));
    r->time_offset = time_offset;
}

static int
pc98_rtc_serial_mode(const pc98_rtc_t *r)
{
    /* On the uPD4990A, C2:C0 == 111 selects serial-command mode. */
    return r->c_unlatched == 7;
}

static void
pc98_rtc_strobe(pc98_rtc_t *r)
{
    r->command = pc98_rtc_serial_mode(r) ? (r->shift[6] & 0x0f) : r->c_unlatched;

    switch (r->command) {
        case PC98_RTC_SHIFT:
            r->data_out = r->shift[0] & 1;
            break;
        case PC98_RTC_TIME_SET:
            pc98_rtc_commit_time(r);
            r->data_out = r->shift[0] & 1;
            break;
        case PC98_RTC_TIME_READ:
            pc98_rtc_capture_time(r);
            break;
        default:
            break;
    }
}

static void
pc98_rtc_clock(pc98_rtc_t *r)
{
    int in = r->data_in;

    /* The 4990A has a 4-bit serial command register when C2:C0 == 111. */
    if (pc98_rtc_serial_mode(r)) {
        in = r->shift[6] & 1;
        r->shift[6] >>= 1;
        r->shift[6] |= (uint8_t) ((r->data_in & 1) << 3);
    }

    if (r->command == PC98_RTC_SHIFT) {
        for (unsigned i = 0; i < 6; i++) {
            r->shift[i] >>= 1;
            if (i == 5)
                r->shift[i] |= (uint8_t) ((in & 1) << 7);
            else
                r->shift[i] |= (uint8_t) ((r->shift[i + 1] & 1) << 7);
        }
        r->data_out = r->shift[0] & 1;
    }
}

static void
pc98_rtc_write(pc98_rtc_t *r, uint8_t v)
{
    uint8_t new_stb = (v >> 3) & 1;
    uint8_t new_clk = (v >> 4) & 1;

    r->c_unlatched = v & 7;

    /* Match the PC-98 wiring order used by the real board/MAME: command lines,
     * STB, CLK, then DATA IN.  Software normally establishes DATA IN before
     * raising CLK, so this preserves the hardware edge semantics. */
    if (!r->stb && new_stb)
        pc98_rtc_strobe(r);
    r->stb = new_stb;

    if (!r->clk && new_clk)
        pc98_rtc_clock(r);
    r->clk     = new_clk;
    r->data_in = (v >> 5) & 1;
}

static uint8_t
pc98_rtc_data_out(const pc98_rtc_t *r)
{
    /* HOLD and TIME READ expose the 1 Hz data pulse on uPD4990A.  Model the
     * square wave in emulated CPU time so turbo speed does not affect POST. */
    if ((r->command == PC98_RTC_HOLD || r->command == PC98_RTC_TIME_READ) && cpuclock > 0.0) {
        uint64_t half_seconds = (uint64_t) ((double) tsc * 2.0 / cpuclock);
        return (uint8_t) (half_seconds & 1u);
    }
    return r->data_out & 1u;
}

/* PPI callbacks are bound to a tiny context so one callback implementation can
 * describe system, printer and mouse PPIs without globals. */
typedef struct pc98_ppi_ctx_t {
    pc98_mb_t *mb;
    uint8_t    which;
} pc98_ppi_ctx_t;

static pc98_ppi_ctx_t ppi_ctx_sys, ppi_ctx_prn, ppi_ctx_mouse;

static uint8_t
pc98_ppi_read_cb(void *opaque, unsigned port)
{
    pc98_ppi_ctx_t *c = (pc98_ppi_ctx_t *) opaque;
    pc98_mb_t      *d = c->mb;

    switch (c->which) {
        case 0: /* system */
            if (port == 0)
                return d->dsw2;
            if (port == 1)
                return (uint8_t) (((d->dsw1 & 1) << 3) | pc98_rtc_data_out(&d->rtc));
            /* Port C pin state: the board-level latch, which is what a read of
             * 35h returns on real hardware. */
            if (port == 2)
                return d->sys_port_c;
            break;
        case 1: /* printer */
            /* Port B (42h) is the system-type register:
             *   D7 = 1, D6 = 0, D5 = MOD, D4 = SW13, D3 = SW18,
             *   D2 = BSY, D1 = ACPU, D0 = 0
             * MOD is the system clock family (0: 10 MHz, 1: 8 MHz).  The board
             * clocks its 8253 from 1.9968 MHz, which is the 8 MHz family, so MOD
             * has to say 8 MHz or the firmware's timing constants disagree with
             * the PIT it is actually counting.  BSY is the printer BUSY input and
             * is active low, so no printer reads as not busy.  ACPU is set only
             * on V30 machines. */
            if (port == 1)
                return (uint8_t) (0x80u | PC98_PRN_B_MOD | (d->printer ? ((lpt_read_status(d->printer) & 0x80u) >> 5) : 0x04u) | ((d->dsw1 & PC98_SW1_3) ? PC98_PRN_B_SW13 : 0u) | ((d->dsw1 & PC98_SW1_8) ? PC98_PRN_B_SW18 : 0u));
            return 0xff;
        case 2: /* mouse PPI (7FD9h-7FDFh) */
            if (port == 0)
                return pc98_mouse_data(&d->mouse);
            if (port == 1) {
                /* Port B (7FDBh):
                 * Bit 1: Clock switch (0: HIGH, 1: LOW) -> 0 = HIGH
                 * Bit 6: DIP SW3-6 (internal RAM: 1 = connected).
                 * The DIP byte stores SW3-6 in bit 5, relocated to PB6.
                 */
                return (uint8_t) ((d->dsw3 & 0x20u) << 1);
            }
            if (port == 2) {
                /* Port C (7FDDh):
                 * Bit 0: DIP SW1-5 (RS-232C)
                 * Bit 1: DIP SW1-6 (RS-232C)
                 * Bit 2: DIP SW3-8 (CPU mode: 0 = 80x86, 1 = 70116/V30).
                 *        Must be 0 for 80286 (PC-9801VX) so firmware runs protected-mode RAM sizing!
                 * Bit 3: Machine mode (1 = normal mode)
                 * Bits 4..7: 0
                 */
                uint8_t val = 0x08u; /* normal mode */
                if (d->dsw1 & PC98_SW1_5)
                    val |= 0x01u;
                if (d->dsw1 & PC98_SW1_6)
                    val |= 0x02u;
                if (d->dsw3 & 0x80u)
                    val |= 0x04u; /* SW3-8: 0 for 80286 */
                return val;
            }
            return 0xff;
    }
    return 0xff;
}

static void
pc98_printer_control(pc98_mb_t *d)
{
    if (!d->printer)
        return;
    /* PC7 is the raw active-low PSTB pin; system PC6 forces it inactive.
     * The common host backend uses IBM's inverted STROBE control bit. */
    int     inhibited = d->ppi_prn.dir_c_upper || (d->sys_port_c & PC98_SYS_C_PSTM);
    uint8_t control   = (inhibited || (d->ppi_prn.latch[2] & 0x80u)) ? 0x0c : 0x0d;
    lpt_write(2, control, d->printer);
}

static void
pc98_ppi_write_cb(void *opaque, unsigned port, uint8_t value)
{
    pc98_ppi_ctx_t *c = (pc98_ppi_ctx_t *) opaque;
    pc98_mb_t      *d = c->mb;
    (void) port;
    if (c->which == 1) {
        if (port == 0 && !d->ppi_prn.dir_a && d->printer)
            lpt_write(0, value, d->printer);
        if (port == 2)
            pc98_printer_control(d);
    }
    if (c->which == 2 && port == 2) {
        /* Ignore the latch while its pins are configured as inputs. The
         * board's inhibit input remains pulled high after an 8255 reset. */
        if (d->ppi_mouse.dir_c_upper)
            d->mouse.control = 0x10;
        else
            pc98_mouse_control(&d->mouse, value);
        /* Inhibiting the source stops future clock pulses. The edge-triggered
         * 8259 retains a latched request until acknowledgement; withdrawing
         * IRQ13 here would also erase a pending C-BUS card interrupt. */
    }
    if (c->which == 0 && port == 2) {
        /* Low three bits are the RS-232 interrupt mask on the common PC-98
         * system-PPI wiring. */
        d->rs_irq_mask = value & 7;
        pc98_rs_recalc_irq(d);

        /* PC-98 has no AT-style byte POST-code latch.  The system 8255
         * port C is the documented diagnostic/control register, so feed its
         * output latch to the POST reporter without installing a competing
         * I/O handler on the real 35h port. */
        postcard_pc98_diag_write(value);
    }
}

static void
pc98_ppi_intr_notify(void *opaque, unsigned group, int level)
{
    (void) opaque;
    (void) group;
    (void) level;
}

static uint8_t
ppi_sys_in(uint16_t port, void *priv)
{
    pc98_mb_t *d   = (pc98_mb_t *) priv;
    unsigned   reg = (port - 0x31u) >> 1;

    /* PC-9801VX system PPI port C is not eight ordinary GPIO outputs: PC7 and
     * PC5 are the SHUT0/SHUT1 shutdown-reason flip-flops.  They must read
     * back what the firmware last wrote, because that readback is how both
     * reset entry points decide what to do:
     *
     *   FD80:0000 (BIOS bank)   in al,35h / test al,80h / jz resume
     *   F800:005A (ITF  bank)   in al,35h / test al,80h / jz resume
     *
     * with "resume" restoring SS:SP from 0000:0404 and doing a far RET back
     * into the routine that asked for the reset.  Forcing these bits high
     * makes that path unreachable and turns every return-from-protected-mode
     * into a cold reboot.  pc98_ppi_read_cb() supplies the real pin state. */
    return pc98_ppi8255_read(&d->ppi_sys, reg);
}
static void
pc98_speaker_update(pc98_mb_t *d)
{
    speaker_set_pc98_state(!(d->sys_port_c & PC98_SYS_C_BUZ),
                           d->pit ? pit_devs[0].get_outlevel(d->pit, 1) : 0);
}

static void
ppi_sys_out(uint16_t port, uint8_t val, void *priv)
{
    pc98_mb_t     *d   = (pc98_mb_t *) priv;
    const unsigned reg = (port - 0x31u) >> 1;

    /* Snoop BSR writes to 37h for the two board-level shutdown flip-flops.
     * Doing this here rather than from the 8255 output callback keeps them
     * independent of port C's direction and of mode-set commands, which is
     * how the real wiring behaves.  The VX firmware never sends port C a
     * mode word at all - every write to 37h in the IPL is a BSR write. */
    if (reg == 3u)
        d->sys_port_c = pc98_ppi8255_bsr_latch(d->sys_port_c, val, 0xffu);
    else if (reg == 2u)
        d->sys_port_c = (d->sys_port_c & ~PC98_SYS_C_BUZ) | (val & PC98_SYS_C_BUZ);

    pc98_ppi8255_write(&d->ppi_sys, reg, val);
    pc98_printer_control(d);
    pc98_speaker_update(d);
}
static uint8_t
ppi_prn_in(uint16_t port, void *priv)
{
    return pc98_ppi8255_read(&((pc98_mb_t *) priv)->ppi_prn, (port - 0x40u) >> 1);
}
static void
ppi_prn_out(uint16_t port, uint8_t val, void *priv)
{
    pc98_ppi8255_write(&((pc98_mb_t *) priv)->ppi_prn, (port - 0x40u) >> 1, val);
}
static int pc98_mouse_poll(void *priv);

static uint8_t
ppi_mouse_in(uint16_t port, void *priv)
{
    pc98_mouse_poll(priv);
    return pc98_ppi8255_read(&((pc98_mb_t *) priv)->ppi_mouse, (port - 0x7fd9u) >> 1);
}
static void
ppi_mouse_out(uint16_t port, uint8_t val, void *priv)
{
    /* Account for host motion up to the latch edge, including when a driver
     * polls without mouse interrupts or selects a slower interrupt divisor. */
    pc98_mouse_poll(priv);
    pc98_ppi8255_write(&((pc98_mb_t *) priv)->ppi_mouse, (port - 0x7fd9u) >> 1, val);
}

static int
pc98_mouse_poll(void *priv)
{
    pc98_mb_t *d = (pc98_mb_t *) priv;
    int        dx, dy;

    if (mouse_type != MOUSE_TYPE_INTERNAL || mouse_input_mode != 0 || (!mouse_capture && !(video_fullscreen && !fullscreen_ui_visible)))
        return 1;

    mouse_subtract_coords(&dx, &dy, NULL, NULL, -32768, 32767, 0, 0);
    pc98_mouse_motion(&d->mouse, dx, dy, mouse_get_buttons_ex());
    return 0;
}

static void
pc98_mouse_tick(void *priv)
{
    pc98_mb_t *d = (pc98_mb_t *) priv;
    timer_advance_u64(&d->mouse_timer,
                      (uint64_t) ((1000000.0 / (120u >> d->mouse.divider)) * TIMER_USEC));
    pc98_mouse_poll(d);
    /* INT6 belongs to slave IR5. It is a periodic hardware clock, including
     * when no motion occurs: some games also use it for animation timing. */
    if (!(d->mouse.control & 0x10)) {
        picint(1u << 13);
    }
}

static void
pc98_mouse_rate_out(uint16_t port, uint8_t value, void *priv)
{
    pc98_mb_t *d = (pc98_mb_t *) priv;
    (void) port;
    d->mouse.divider = value & 3u;
    timer_set_delay_u64(&d->mouse_timer,
                        (uint64_t) ((1000000.0 / (120u >> d->mouse.divider)) * TIMER_USEC));
}

static uint8_t
usart_rs_in(uint16_t port, void *priv)
{
    pc98_mb_t *d = (pc98_mb_t *) priv;
    return pc98_i8251_read(&d->usart_rs, (port >> 1) & 1);
}
static void
usart_rs_out(uint16_t port, uint8_t val, void *priv)
{
    pc98_mb_t *d = (pc98_mb_t *) priv;
    pc98_i8251_write(&d->usart_rs, (port >> 1) & 1, val);
    pc98_rs_recalc_irq(d);
}
static uint8_t
usart_kbd_in(uint16_t port, void *priv)
{
    pc98_mb_t *d   = (pc98_mb_t *) priv;
    uint8_t    ret = pc98_i8251_read(&d->usart_kbd, (port >> 1) & 1);
    if (!(port & 2))
        pc98_kbd_pump(d);
    return ret;
}
static void
usart_kbd_out(uint16_t port, uint8_t val, void *priv)
{
    pc98_mb_t *d = (pc98_mb_t *) priv;
    pc98_i8251_write(&d->usart_kbd, (port >> 1) & 1, val);
    if (port & 2)
        pc98_kbd_pump(d);
}

static unsigned
pc98_pit_reg(uint16_t port)
{
    if (port >= 0x3fd9u && port <= 0x3fdfu)
        return (port - 0x3fd9u) >> 1;
    return (port - 0x71u) >> 1;
}
static uint8_t
pit_in(uint16_t port, void *priv)
{
    pc98_mb_t *d = (pc98_mb_t *) priv;
    return pit_devs[0].read((uint16_t) pc98_pit_reg(port), d->pit);
}
static void
pit_out(uint16_t port, uint8_t val, void *priv)
{
    pc98_mb_t *d = (pc98_mb_t *) priv;
    pit_devs[0].write((uint16_t) pc98_pit_reg(port), val, d->pit);
}
static void
pc98_pit_irq(int new_out, int old_out, void *priv)
{
    (void) old_out;
    (void) priv;
    if (new_out)
        picint(1u << 0);
    else
        picintc(1u << 0);
}

static void
pc98_pit_speaker(int new_out, int old_out, void *priv)
{
    pit_t     *pit = (pit_t *) priv;
    pc98_mb_t *d   = (pc98_mb_t *) pit->dev_priv;
    (void) old_out;
    speaker_set_pc98_state(!(d->sys_port_c & PC98_SYS_C_BUZ), new_out);
}

/* PC-9801VX drives all three 8253 CLK inputs from X1 = 31.9488 MHz / 16
 * = 1.9968 MHz, with the GATE inputs tied active.  86Box's pit_set_clock()
 * changes the *global machine/CPU timing basis* and must not be used here.
 * Instead give this PIT instance the CPU-cycles-per-PIT-half-clock constant. */
static void
pc98_pit_reclock(pc98_mb_t *d)
{
    if (!d || !d->pit)
        return;

    const uint64_t pit_const = (uint64_t) ((cpuclock / (double) PC98_PIT_CLOCK) * (double) (1ULL << 32));
    pit_set_pit_const(d->pit, pit_const);
    timer_set_delay_u64(&d->pit->callback_timer, pit_const >> 1);
}

static void
pc98_pit_reset(pc98_mb_t *d)
{
    if (!d || !d->pit)
        return;

    pit_device_reset(d->pit);
    for (int i = 0; i < 3; i++) {
        pit_devs[0].set_using_timer(d->pit, i, 1);
        pit_devs[0].set_gate(d->pit, i, 1);
    }
    pit_devs[0].set_out_func(d->pit, 0, pc98_pit_irq);
    pit_devs[0].set_out_func(d->pit, 1, pc98_pit_speaker);
    pc98_pit_reclock(d);
    pc98_speaker_update(d);
}

static uint32_t
pc98_dma_effective_page(const pc98_mb_t *d, uint8_t page)
{
    uint32_t page_addr = (uint32_t) page << 16;

    /* 0439h bit 2 gates DMA A23-A20 externally.  The page latch itself keeps
     * the complete byte; only the address presented to memory is restricted. */
    if (d->dma_access_ctrl & 0x04u)
        page_addr &= 0x000f0000u;
    return page_addr;
}

static void
pc98_dma_reapply_page(pc98_mb_t *d, unsigned ch)
{
    const uint32_t page_addr = pc98_dma_effective_page(d, dma[ch].page_l);

    /* 86Box folds the external page latch into AB/AC.  Rebuild both combined
     * addresses whenever 0439h changes so enabling the upper address lines can
     * restore bits that dma_set_mask() necessarily discarded while the 20-bit
     * compatibility mask was active. */
    dma[ch].ab = (dma[ch].ab & 0x0000ffffu) | page_addr;
    dma[ch].ac = (dma[ch].ac & 0x0000ffffu) | page_addr;
}

static void
dma_page_out(uint16_t port, uint8_t val, void *priv)
{
    pc98_mb_t *d   = (pc98_mb_t *) priv;
    unsigned   off = (port - 0x21u) >> 1;
    if (off < 4) {
        unsigned       ch        = (off + 1) & 3;
        const uint32_t page_addr = pc98_dma_effective_page(d, val);

        /* Keep page/page_l as the byte the guest actually wrote (firmware can
         * read it back), but apply the board mask before loading the combined
         * 8237/page-latch address.  Otherwise the first byte after each page
         * programming operation can escape to xxBxxxxx before the generic DMA
         * core applies dma_mask while advancing AC. */
        if (pc98_dma_logged < 200u)
            pclog("[PC98] DMA page write: channel %u <- %02X (effective=%02X, %s)\n",
                  ch, val, (unsigned) ((page_addr >> 16) & 0xffu),
                  (d->dma_access_ctrl & 0x04u) ? "20-bit" : "24-bit");
        dma[ch].page = dma[ch].page_l = val;
        dma[ch].ab                    = (dma[ch].ab & 0x0000ffffu) | page_addr;
        dma[ch].ac                    = (dma[ch].ac & 0x0000ffffu) | page_addr;
    } else if (off == 4) {
        pclog("[PC98] DMA port 29h write: %02X (ch %u mode %u)\n", val, val & 3u, (val >> 2) & 3u);
        d->dma_autoinc_latch    = val & 0x0fu;
        d->dma_autoinc[val & 3] = (val >> 2) & 3;
        dma_set_pc98_autoinc((uint8_t) (val & 3u), (uint8_t) ((val >> 2) & 3u));
    }
}

static uint8_t
dma_page_in(uint16_t port, void *priv)
{
    pc98_mb_t     *d   = (pc98_mb_t *) priv;
    const unsigned off = (port - 0x21u) >> 1;

    if (off < 4u) {
        const unsigned ch = (off + 1u) & 3u;
        return dma[ch].page_l;
    }
    if (off == 4u)
        return d->dma_autoinc_latch;
    return 0xffu;
}

/* 286-class PC-98 DMA access control, port 439h.  Bit 2 selects the
 * 20-bit compatibility mask; clear permits the full 8-bit page register. */
static uint8_t
dma_access_ctrl_in(uint16_t port, void *priv)
{
    (void) port;
    return ((pc98_mb_t *) priv)->dma_access_ctrl;
}

static void
dma_access_ctrl_out(uint16_t port, uint8_t val, void *priv)
{
    pc98_mb_t *d = (pc98_mb_t *) priv;
    (void) port;
    d->dma_access_ctrl = val;
    dma_set_mask((val & 0x04u) ? 0x000fffffu : 0x00ffffffu);

    /* dma_set_mask() can only AND the combined addresses, so a 20->24-bit
     * transition cannot recover the high nibble by itself.  The real board's
     * page latch never lost it.  Reconstitute AB/AC from page_l for every DMA
     * channel, making 0439h a reversible address-line gate rather than a
     * destructive register write. */
    for (unsigned ch = 0; ch < 4; ch++)
        pc98_dma_reapply_page(d, ch);

    if (pc98_dma_logged < 200u)
        pclog("[PC98] DMA access 439h <- %02X (A23-A20 %s)\n", val,
              (val & 0x04u) ? "masked" : "enabled");
}

/* PC-9801VM/VX uses one uPD765 through two legacy I/O identities.  BEh
 * selects which identity is active; VX resets in 2HD mode (mode=3), so the
 * normal 90h/92h/94h interface uses slave-PIC IR3 and DMA channel 2. */
extern void fdc_update_rates(fdc_t *fdc);

static void
pc98_fdc_density_for_drive(pc98_mb_t *d, int is_2hd, unsigned drive)
{
    int densel_polarity = 1;

    if (!d || !d->fdc)
        return;

    /* The VX BIOS does not have to touch the later 04BEh 3-mode selector in
     * order to use an ordinary PC-98 1 MB/1.23 MB disk.  V11 derived DENSEL
     * only from fdc_3mode_dev_sel; that latch resets to "external/no drive",
     * so a configured 3.5-inch PC-98 drive inherited the 5.25-inch polarity.
     * At 500 kbit/s that leaves a 35_2hd_nec mechanism at 300 rpm instead of
     * the required 360 rpm.  READ ID then times out with ST1.MA and the result
     * CHRN bytes are the misleading 00/00/00/00 seen in the BIOS trace.
     *
     * Drive type and transfer mode are independent on PC-98.  Select DENSEL
     * from the physical drive actually addressed by the uPD765.  04BEh's
     * per-drive latch is still honoured when 1.44 MB access is explicitly
     * requested, but normal 1 MB access works even if firmware never probes
     * the 3-mode extension port. */
    if (drive < 2u && fdd_get_flags(d->fdc->fdd[drive]) && !fdd_is_525(d->fdc->fdd[drive]))
        densel_polarity = !!d->fdc_3mode_144[drive];

    d->fdc->rate     = is_2hd ? 0 : 2; /* 500 kbit/s : 250 kbit/s */
    d->fdc->bit_rate = is_2hd ? 500 : 250;
    fdc_update_densel_polarity(d->fdc, densel_polarity);
}

static void
pc98_fdc_density(pc98_mb_t *d, int is_2hd)
{
    unsigned drive;

    if (!d || !d->fdc)
        return;

    /* 04BEh explicitly selects an internal drive while programming 1.44 MB
     * mode.  Otherwise use the drive currently selected by the shared 765,
     * which is what old VX firmware does for normal 1 MB operation. */
    drive = (d->fdc_3mode_dev_sel < 2u && fdd_get_flags(d->fdc->fdd[d->fdc_3mode_dev_sel]))
        ? d->fdc_3mode_dev_sel
        : (unsigned) (d->fdc->drive & 3u);
    pc98_fdc_density_for_drive(d, is_2hd, drive);
}

static int
pc98_fdc_cmd_has_drivesel(uint8_t cmd)
{
    switch (cmd & 0x1fu) {
        case 0x02u: /* READ TRACK */
        case 0x04u: /* SENSE DRIVE STATUS */
        case 0x05u: /* WRITE DATA */
        case 0x06u: /* READ DATA */
        case 0x07u: /* RECALIBRATE */
        case 0x09u: /* WRITE DELETED DATA */
        case 0x0au: /* READ ID */
        case 0x0cu: /* READ DELETED DATA */
        case 0x0du: /* FORMAT TRACK */
        case 0x0fu: /* SEEK */
        case 0x11u: /* SCAN EQUAL */
        case 0x19u: /* SCAN LOW OR EQUAL */
        case 0x1du: /* SCAN HIGH OR EQUAL */
            return 1;
        default:
            return 0;
    }
}

static unsigned pc98_fdc_logged;

/* PC-9801VM/VX normal-mode floppy routing, from NEC/ASCII PC-9800
 * hardware documentation: the 1 MB interface uses slave IR3 (IRQ 11 in
 * 86Box's flattened numbering) and DMA channel 2; the 640 KB interface uses
 * slave IR2 (IRQ 10) and DMA channel 3.  High-resolution machines use a
 * different routing and are not emulated by this VX motherboard.
 *
 * There is one shared uPD765, but there are two documented DMA request
 * routes.  Route strictly from the active normal-mode I/O identity.  Do not
 * infer the channel from whichever 8237 channel happens to contain a nonzero
 * address: the IPL legitimately programs/probes both interfaces during POST. */
static unsigned
pc98_fdc_core_bank(const pc98_mb_t *d)
{
    return d->fdc_route_lock ? (unsigned) d->fdc_route_lock_bank
                             : (unsigned) d->fdc_bank_1mb;
}

static void
pc98_fdc_apply_core_route(pc98_mb_t *d)
{
    const unsigned bank = pc98_fdc_core_bank(d);

    if (!d->fdc)
        return;
    fdc_set_irq(d->fdc, bank ? 11 : 10);
    fdc_set_dma_ch(d->fdc, bank ? 2 : 3);
}

static void
pc98_fdc_route(pc98_mb_t *d, int port_1mb)
{
    /* Record which identity the guest most recently selected, but do not let
     * that selection steal the physical IRQ/DMA lines from an asynchronous
     * command which is already in flight on the other identity. */
    d->fdc_bank_1mb = (uint8_t) !!port_1mb;
    pc98_fdc_apply_core_route(d);
}

/* Switch register read back at 94h (1MB) or CCh (640KB).  TYP0/TYP1 describe
 * the installed drive pairs; the VX's internal pair is 1MB/640KB dual-mode and
 * there is no external pair.  Only the 640KB identity exposes READY.  The two
 * internal drives share that READY input, wired so either ready drive asserts
 * it; it is not an installed-drive or media-change bit. */
static uint8_t
pc98_fdc_switch_value(pc98_mb_t *d, int port_640)
{
    uint8_t v = PC98_FDC_SW_FIXED | PC98_FDC_SW_TYP0;

    if (port_640) {
        v |= PC98_FDC_SW_640;
        if (d && d->fdc) {
            for (unsigned drive = 0; drive < 2u; drive++) {
                if (fdd_is_ready(d->fdc->fdd[drive])) {
                    v |= PC98_FDC_SW_RDY;
                    break;
                }
            }
        }
    }
    return v;
}

static void pc98_fdc_forget_results(pc98_mb_t *d);

/* Motor state.
 *
 * The 1MB control register has no motor bit - the drive spins under uPD765
 * control - while the 640KB register drives it explicitly through MTON.  One
 * pair of physical drives is shared between the two banks here, so the motor
 * follows whichever bank is asking for it; otherwise a CCh write with MTON
 * clear would stop a drive the 1MB path is in the middle of using.
 *
 * Nothing enables the motor during machine initialisation.  86Box rebuilds the
 * per-drive media poll timers in fdd_reset(), which runs *after* machine_init()
 * and clears the timer objects, so a motor asserted during init would be left
 * marked on with no poll scheduled - and a READ DATA would then wait forever
 * for media polling that never happens.  The motor is therefore raised lazily,
 * from the first control-register write or command the firmware issues, both
 * of which happen long after fdd_reset(). */
/* DMA enable belongs to the interface, not to the chip.
 *
 * The 640KB control register at CCh has a DMAE bit; the 1MB register at 94h
 * has no such bit, because that interface always transfers through the 8237.
 * Both registers drive one shared uPD765 here, so applying CCh's DMAE to the
 * core unconditionally lets a 640KB-side write disable DMA for a transfer that
 * is about to run on the 1MB side.  The firmware does exactly that - it leaves
 * DMAE clear in CCh while the booted code reads through 92h - and every read
 * then came back with ST1 bit 4 set, overrun, having moved no bytes at all.
 *
 * The gate therefore follows the physical route which owns the active command.
 * When no asynchronous command is in flight this is just the currently
 * selected identity. */
static void
pc98_fdc_apply_dma_gate(pc98_mb_t *d)
{
    if (d->fdc) {
        const unsigned bank = pc98_fdc_core_bank(d);
        /* The interface gate permits DMA; it cannot undo ND=1 programmed
         * by the uPD765 SPECIFY command. */
        d->fdc->dma = (bank || d->dmae_640) && !(d->fdc->specify[1] & 1u);
    }
}

static void
pc98_fdc_lock_route(pc98_mb_t *d, unsigned bank)
{
    d->fdc_route_lock      = 1u;
    d->fdc_route_lock_bank = (uint8_t) !!bank;
    pc98_fdc_apply_core_route(d);
    pc98_fdc_apply_dma_gate(d);

    if (pc98_fdc_logged < 200u)
        pclog("[PC98] FDC route lock: %s -> IRQ%d/DMA%d\n",
              bank ? "1MB/92h" : "640KB/CAh",
              bank ? 11 : 10, bank ? 2 : 3);
}

static void
pc98_fdc_unlock_route(pc98_mb_t *d)
{
    if (!d->fdc_route_lock)
        return;

    d->fdc_route_lock = 0u;
    pc98_fdc_apply_core_route(d);
    pc98_fdc_apply_dma_gate(d);

    if (pc98_fdc_logged < 200u)
        pclog("[PC98] FDC route unlock: selected=%s -> IRQ%d/DMA%d\n",
              d->fdc_bank_1mb ? "1MB/92h" : "640KB/CAh",
              d->fdc_bank_1mb ? 11 : 10, d->fdc_bank_1mb ? 2 : 3);
}

static void
pc98_fdc_motor_update(pc98_mb_t *d)
{
    const int on = d->motor_1mb || d->motor_640;

    /* fdd_reset() rebuilds the drive timers independently of this latch.
     * Reconcile the physical drives even if the requested level is unchanged;
     * fdd_set_motor_enable() itself only schedules on an actual transition. */
    d->motor_on = (uint8_t) on;
    for (unsigned drive = 0; drive < 2u; drive++)
        fdd_set_motor_enable(d->fdc->fdd[drive],
                             on && fdd_get_flags(d->fdc->fdd[drive]));
}

/* Both interfaces reset the uPD765 through bit 7 of their control register,
 * asserted HIGH.  The controller leaves reset on the falling edge and raises
 * its post-reset interrupt there, which is what the disk BIOS waits for, so
 * the transition has to be driven through the core's own DOR path (bit 2,
 * /RESET, asserted low) rather than by poking fdc->dor.
 *
 * The 1MB control register has no motor bit at all - the drive spins under
 * uPD765 control - so its motors are simply held enabled.  The 640KB register
 * drives the motor explicitly through MTON and can inhibit DMA through DMAE. */
static void
pc98_fdc_apply_ctrl(pc98_mb_t *d, uint8_t val,
                    int port_640)
{
    const int in_reset = !!(val & PC98_FDC_1MB_RST);
    /* Both address identities drive one physical RESET input. Firmware may
     * assert it through one address, exchange the interface, then release it
     * through the other. Per-port write history is not the old pin level. */
    const int was_in_reset = !(d->fdc->dor & 0x04u);

    if (port_640) {
        d->motor_640 = !!(val & PC98_FDC_640_MTON);
        /* DMAE is a level-controlled permission bit, not part of the reset
         * edge: the firmware enables and disables DMA with plain CCh writes
         * between commands. */
        d->dmae_640 = !!(val & PC98_FDC_640_DMAE);
    } else {
        d->motor_1mb = 1; /* no motor bit: the drive spins under 765 control */
    }

    pc98_fdc_motor_update(d);

    uint8_t dor = 0x08u; /* DMA and interrupt requests enabled */
    if (d->motor_on)
        dor |= 0x30u;

    /* Cancel the physical command on its OLD IRQ/DMA route before dropping
     * the route pin.  Retargeting first leaves the old channel's DRQ asserted. */
    if (!was_in_reset && in_reset) {
        fdc_write(2, dor, d->fdc);
        pc98_fdc_forget_results(d);
        picintc((1u << 10) | (1u << 11));
    }

    /* Select the bank before touching /RESET: releasing it can schedule the
     * post-reset interrupt straight away, so routing afterwards is too late. */
    pc98_fdc_route(d, !port_640);
    pc98_fdc_apply_dma_gate(d);

    if (was_in_reset == in_reset)
        return;

    if (!in_reset) {
        /* Route is now established before the post-reset IRQ is scheduled. */
        fdc_write(2, dor | 0x04u, d->fdc);
        pc98_fdc_density(d, !!(d->fdc_mode & PC98_FDC_MODE_FDE));
        pc98_fdc_apply_dma_gate(d);
    }
}

/* Concurrent seeks and the dual PC-98 FDC identities.
 *
 * A real uPD765 can have a seek/recalibrate outstanding for more than one
 * drive.  The PC-9801-27 SASI ROM relies on this during its own init: through
 * the 1MB identity it issues RECALIBRATE to drives 3,2,1,0, installs an IRQ11
 * handler which marks BIOS work byte 055Eh, and its second option-ROM entry
 * then waits forever for 055Eh == 0Fh.
 *
 * The generic 86Box FDC has one pending seek status (`fintr` + ST0), and this
 * motherboard also used to have one *shared* delayed IRQ timer.  A following
 * CAh/640KB recalibrate therefore replaced the pending 92h/1MB timer and, when
 * it fired, used the now-current bank to raise IRQ10.  The 1MB handler never
 * ran and the SASI ROM deadlocked before it ever touched ports 80h/82h.
 *
 * SEEK/RECALIBRATE are consequently owned by this board wrapper.  We apply the
 * mechanical position change synchronously and queue the two-byte SIS result
 * against the identity that issued the command.  Other uPD765 commands still
 * go through the generic core unchanged.
 */
static int
pc98_fdc_cmd_owns_interrupt(uint8_t cmd)
{
    switch (cmd) {
        case 0x02u: /* READ TRACK       */
        case 0x05u: /* WRITE DATA       */
        case 0x06u: /* READ DATA        */
        case 0x09u: /* WRITE DELETED    */
        case 0x0au: /* READ ID          */
        case 0x0cu: /* READ DELETED     */
        case 0x0du: /* FORMAT TRACK     */
        case 0x11u: /* SCAN EQUAL       */
        case 0x19u: /* SCAN LOW OR EQ   */
        case 0x1du: /* SCAN HIGH OR EQ  */
            return 1;
        default:
            return 0;
    }
}

static void
pc98_fdc_queue_sis(pc98_mb_t *d, unsigned bank,
                   uint8_t st0, uint8_t pcn)
{
    if (bank > 1u)
        return;

    if (d->sis_q_n[bank] < PC98_FDC_SIS_QUEUE) {
        const unsigned n     = d->sis_q_n[bank]++;
        d->sis_q[bank][n][0] = st0;
        d->sis_q[bank][n][1] = pcn;
    }

    /* One level interrupt is sufficient: the NEC ROM handler drains SENSE
     * INTERRUPT STATUS until no completion remains.  Each identity gets an
     * independent timer so CAh activity cannot cancel/retarget a 92h IRQ. */
    timer_set_delay_u64(&d->seek_irq_timer[bank], 500ULL * TIMER_USEC);
}

static void
pc98_fdc_queue_cmd_result(pc98_mb_t *d, unsigned bank,
                          const uint8_t *res, unsigned n)
{
    if (bank > 1u || !res || !n || n > sizeof(d->cmd_res[bank]))
        return;

    memcpy(d->cmd_res[bank], res, n);
    d->cmd_res_n[bank] = (uint8_t) n;
    d->cmd_res_i[bank] = 0u;

    /* The callback is bank-specific, so switching 92h <-> CAh after the
     * command cannot retarget its completion to the other PIC input. */
    timer_set_delay_u64(&d->seek_irq_timer[bank], 500ULL * TIMER_USEC);
}

static int
pc98_fdc_serve_sis(pc98_mb_t *d, unsigned bank)
{
    if (bank > 1u || !d->sis_q_n[bank])
        return 0;

    d->sis_res[bank][0] = d->sis_q[bank][0][0];
    d->sis_res[bank][1] = d->sis_q[bank][0][1];
    memmove(d->sis_q[bank], d->sis_q[bank] + 1,
            (size_t) (d->sis_q_n[bank] - 1u) * sizeof(d->sis_q[bank][0]));
    d->sis_q_n[bank]--;
    d->sis_res_n[bank] = 2;
    d->sis_res_i[bank] = 0;

    /* Some firmware drains every completion in one ISR; other code senses
     * only one per interrupt.  Keep level-triggered progress for both. */
    if (d->sis_q_n[bank])
        timer_set_delay_u64(&d->seek_irq_timer[bank], 500ULL * TIMER_USEC);
    return 1;
}

static void
pc98_seek_irq_640_tick(void *priv)
{
    pc98_mb_t *d = (pc98_mb_t *) priv;

    /* A transfer command can supersede an undrained synthetic seek queue.
     * Do not let its old timer turn into a ghost IRQ after the queue has been
     * discarded. */
    if (d && (d->sis_q_n[0] || d->cmd_res_n[0]))
        picint(1u << 10);
}

static void
pc98_seek_irq_1mb_tick(void *priv)
{
    pc98_mb_t *d = (pc98_mb_t *) priv;

    if (d && (d->sis_q_n[1] || d->cmd_res_n[1]))
        picint(1u << 11);
}

static void
pc98_fdc_complete_seek(pc98_mb_t *d, unsigned bank)
{
    const uint8_t  cmd     = d->wrapper_seek_cmd;
    const uint8_t  drvsel  = d->trace_cmd[1];
    const unsigned drive   = drvsel & 3u;
    const uint8_t  head    = (drvsel >> 2) & 1u;
    const uint8_t  old_pcn = d->fdc->pcn[drive];
    const uint8_t  pcn     = (cmd == 0x07u) ? 0u : d->trace_cmd[2];
    /* RESET clears PCN but cannot move a mechanism's head. RECALIBRATE
     * steps to physical TRACK0, whereas SEEK uses the controller's PCN. */
    const int delta = (cmd == 0x07u) ? -fdd_current_track(d->fdc->fdd[drive]) : (int) pcn - (int) old_pcn;
    uint8_t   st0;

    /* Match the generic 86Box uPD765's failure semantics for a drive which
     * is not physically configured.  The old wrapper always returned SEEK
     * END / normal termination (20h|drive), so the NEC BIOS kept treating
     * nonexistent drives as candidates and then retried READ ID on them.
     * A real absent mechanism cannot step and reports equipment-check style
     * failure; 86Box's own recalibrate path uses 70h|drive for this case.
     * An installed but merely empty drive is different: it can recalibrate
     * normally because TRACK0 does not require a diskette. */
    if (!fdd_get_flags(d->fdc->fdd[drive])) {
        st0 = (uint8_t) (0x70u | (head << 2) | drive);
    } else {
        st0 = (uint8_t) (0x20u | (head << 2) | drive);
        fdd_forced_seek(d->fdc->fdd[drive], delta);
    }

    d->fdc->drive      = (uint8_t) drive;
    d->fdc->rw_drive   = (uint8_t) drive;
    d->fdc->head       = head;
    d->fdc->pcn[drive] = pcn;

    pc98_fdc_queue_sis(d, bank, st0, pcn);

    pclog("[PC98] %s completion queued on %s bank: "
          "ST0=%02X PCN=%u -> IRQ%d\n",
          (cmd == 0x07u) ? "RECALIBRATE" : "SEEK",
          bank ? "1MB/92h" : "640KB/CAh",
          st0, pcn, bank ? 11 : 10);
}

static void
pc98_fdc_complete_not_ready_read_id(pc98_mb_t *d, unsigned bank,
                                    uint8_t drvsel)
{
    const unsigned drive  = drvsel & 3u;
    const uint8_t  head   = (drvsel >> 2) & 1u;
    const uint8_t  res[7] = {
        (uint8_t) (0x48u | (head << 2) | drive), /* abnormal + NOT READY */
        0x00u,                                   /* ST1: no media, not MA */
        0x00u,                                   /* ST2 */
        (uint8_t) d->fdc->pcn[drive],
        head,
        0x00u, /* no sector ID exists */
        0x00u
    };

    d->fdc->drive    = (uint8_t) drive;
    d->fdc->rw_drive = (uint8_t) drive;
    d->fdc->head     = head;
    pc98_fdc_queue_cmd_result(d, bank, res, sizeof(res));

    pclog("[PC98] READ ID not-ready drive %u on %s bank: "
          "ST0=%02X (NOT READY) -> IRQ%d\n",
          drive, bank ? "1MB/92h" : "640KB/CAh", res[0],
          bank ? 11 : 10);
}

static void
pc98_fdc_forget_results(pc98_mb_t *d)
{
    timer_disable(&d->seek_irq_timer[0]);
    timer_disable(&d->seek_irq_timer[1]);
    memset(d->sis_q, 0, sizeof(d->sis_q));
    memset(d->sis_q_n, 0, sizeof(d->sis_q_n));
    memset(d->sis_res, 0, sizeof(d->sis_res));
    memset(d->sis_res_n, 0, sizeof(d->sis_res_n));
    memset(d->sis_res_i, 0, sizeof(d->sis_res_i));
    memset(d->cmd_res, 0, sizeof(d->cmd_res));
    memset(d->cmd_res_n, 0, sizeof(d->cmd_res_n));
    memset(d->cmd_res_i, 0, sizeof(d->cmd_res_i));
    d->wrapper_seek_cmd    = 0;
    d->wrapper_seek_need   = 0;
    d->wrapper_readid_need = 0;
    d->wrapper_readid_bank = 0;
    d->fdc_in_params       = 0;
    d->fdc_cmd             = 0;
    d->fdc_route_lock      = 0;
    d->fdc_route_lock_bank = 0;
    d->fdc_xfer_ch         = 0;
    d->fdc_xfer_pending    = 0;
    d->fdc_xfer_addr       = 0;
    d->trace_cmd_n         = 0;
    d->trace_res_n         = 0;
}

/* Boot diagnostics.  The disk BIOS issues a command by polling the main status
 * register at 90h for CB=0 then RQM=1/DIO=0, writing the bytes to 92h, waiting
 * for its interrupt handler to set a flag in the BIOS work area, and reading
 * the result bytes back from 92h.  Tracing only the data register and the
 * control ports - never the status polls, which spin - shows exactly how far
 * that sequence gets: no command at all, a command with no result phase (the
 * interrupt never arrived), or a full exchange (in which case the problem is
 * DMA or the disk image, not the controller). */
static unsigned pc98_fdc_logged = 0;

/* Decoded command/result tracing.
 *
 * Byte-at-a-time tracing is unreadable: a uPD765 command is a command byte
 * plus up to eight parameters and up to seven result bytes, all through the
 * same port, and mistaking a parameter for a command byte - or reading ST0
 * bit 2, the head address, as a fault - sends you chasing the wrong thing.
 * Collect a whole phase and print it decoded. */
#ifndef RELEASE_BUILD
static const char *
pc98_fdc_cmd_name(uint8_t cmd)
{
    switch (cmd & 0x1fu) {
        case 0x02:
            return "READ TRACK";
        case 0x03:
            return "SPECIFY";
        case 0x04:
            return "SENSE DRIVE";
        case 0x05:
            return "WRITE DATA";
        case 0x06:
            return "READ DATA";
        case 0x07:
            return "RECALIBRATE";
        case 0x08:
            return "SENSE INT";
        case 0x09:
            return "WRITE DELETED";
        case 0x0a:
            return "READ ID";
        case 0x0c:
            return "READ DELETED";
        case 0x0d:
            return "FORMAT TRACK";
        case 0x0f:
            return "SEEK";
        default:
            return "?";
    }
}

static void
pc98_fdc_log_cmd(pc98_mb_t *d, uint16_t port)
{
    const uint8_t  cmd = d->trace_cmd[0];
    const unsigned n   = d->trace_cmd_n;

    if (pc98_fdc_logged >= 600u)
        return;
    pc98_fdc_logged++;

    if ((cmd & 0x1fu) == 0x06u || (cmd & 0x1fu) == 0x05u || (cmd & 0x1fu) == 0x09u || (cmd & 0x1fu) == 0x0cu) {
        if (n >= 9u) {
            pclog("[PC98] %03X %-13s drive %u head %u  C=%u H=%u R=%u "
                  "N=%u (%u bytes) EOT=%u%s\n",
                  port, pc98_fdc_cmd_name(cmd),
                  d->trace_cmd[1] & 3u, (d->trace_cmd[1] >> 2) & 1u,
                  d->trace_cmd[2], d->trace_cmd[3], d->trace_cmd[4],
                  d->trace_cmd[5], 128u << (d->trace_cmd[5] & 7u),
                  d->trace_cmd[6], (cmd & 0x80u) ? " MT" : "");
            return;
        }
    } else if ((cmd & 0x1fu) == 0x0fu && n >= 3u) {
        pclog("[PC98] %03X %-13s drive %u -> cylinder %u\n",
              port, pc98_fdc_cmd_name(cmd), d->trace_cmd[1] & 3u,
              d->trace_cmd[2]);
        return;
    } else if (((cmd & 0x1fu) == 0x07u || (cmd & 0x1fu) == 0x04u || (cmd & 0x1fu) == 0x0au) && n >= 2u) {
        pclog("[PC98] %03X %-13s drive %u head %u\n",
              port, pc98_fdc_cmd_name(cmd), d->trace_cmd[1] & 3u,
              (d->trace_cmd[1] >> 2) & 1u);
        return;
    }

    pclog("[PC98] %03X %-13s (%u bytes)\n", port, pc98_fdc_cmd_name(cmd), n);
}

/* ST0 bit 2 is the head address, not an error.  Only bits 7:6 say whether the
 * command terminated normally. */
static void
pc98_fdc_log_result(pc98_mb_t *d, uint16_t port)
{
    static const char *const ic[4] = { "ok", "ABNORMAL", "INVALID", "NOT READY" };
    const unsigned           n     = d->trace_res_n;

    if (pc98_fdc_logged >= 600u)
        return;
    pc98_fdc_logged++;

    if (n >= 7u) {
        const uint8_t st0 = d->trace_res[0];
        const uint8_t st1 = d->trace_res[1];
        const uint8_t st2 = d->trace_res[2];

        pclog("[PC98] %03X   -> %s  ST0=%02X ST1=%02X ST2=%02X  "
              "C=%u H=%u R=%u N=%u%s%s%s%s%s\n",
              port, ic[(st0 >> 6) & 3u], st0, st1, st2,
              d->trace_res[3], d->trace_res[4], d->trace_res[5],
              d->trace_res[6],
              (st1 & 0x01u) ? " MA" : "",  /* missing address mark   */
              (st1 & 0x04u) ? " ND" : "",  /* no data / not found    */
              (st1 & 0x10u) ? " OR" : "",  /* overrun - DMA starved  */
              (st1 & 0x20u) ? " DE" : "",  /* CRC error              */
              (st1 & 0x80u) ? " EN" : ""); /* end of cylinder        */
        return;
    }
    if (n == 2u) {
        pclog("[PC98] %03X   -> ST0=%02X PCN=%u%s\n", port,
              d->trace_res[0], d->trace_res[1],
              (d->trace_res[0] & 0x20u) ? " seek end" : "");
        return;
    }
    if (n == 1u) {
        pclog("[PC98] %03X   -> %02X%s\n", port, d->trace_res[0],
              (d->trace_res[0] == 0x80u) ? " (no interrupt pending)" : "");
        return;
    }
    pclog("[PC98] %03X   -> (%u result bytes)\n", port, n);
}

#else
#    define pc98_fdc_log_cmd(...)    ((void) 0)
#    define pc98_fdc_log_result(...) ((void) 0)
#endif

static void
pc98_fdc_trace_flush(pc98_mb_t *d, uint16_t port)
{
    (void) port;
    if (d->trace_res_n) {
        pc98_fdc_log_result(d, port);
        d->trace_res_n = 0;
    }
    if (d->trace_cmd_n) {
        pc98_fdc_log_cmd(d, port);
        d->trace_cmd_n = 0;
    }
}

/* Record where a transfer is aimed, and dump what the previous one delivered.
 * A boot sector that reads back as all zeroes, or that lands nowhere near the
 * address the firmware programmed, says the data path is wrong even though the
 * controller reported success. */
static void pc98_fdc_report_transfer(pc98_mb_t *d);

static void
pc98_fdc_trace(const char *what, uint16_t port, uint8_t val)
{
#ifndef RELEASE_BUILD
    if (pc98_fdc_logged >= 600u)
        return;
    pc98_fdc_logged++;
    pclog("[PC98] FDC %s %02X: %02X\n", what, port, val);
#else
    (void) what;
    (void) port;
    (void) val;
#endif
}

static void
pc98_fdc_report_transfer(pc98_mb_t *d)
{
#ifndef RELEASE_BUILD
    const int ch = pc98_fdc_core_bank(d) ? 2 : 3;

    /* By the time the IPL starts its first floppy transfer it has returned
     * from the protected-mode sizing pass and committed the result to the
     * BIOS work area.  Logging here avoids interpreting the temporary GDT
     * while the CPU-reset transition is still in progress. */
    if (!pc98_firmware_memsize_logged && ram != NULL) {
        const unsigned units = ram[0x401u];
        pclog("[PC98] firmware extended memory: byte 0401=%02X (%uKB)\n",
              units, units * 128u);
        pc98_firmware_memsize_logged = 1;
    }

    if (d->fdc_xfer_pending && pc98_dma_logged < 200u) {
        const uint32_t end = dma[d->fdc_xfer_ch].ac;

        /* The firmware reprograms the channel before the next command, so a
         * backwards "end" just means we are looking after that happened. */
        if (end >= d->fdc_xfer_addr)
            pclog("[PC98] DMA ch%u: %06X -> %06X (%u bytes)\n",
                  d->fdc_xfer_ch, d->fdc_xfer_addr, end,
                  (unsigned) (end - d->fdc_xfer_addr));
        else
            pclog("[PC98] DMA ch%u: transfer started at %06X\n",
                  d->fdc_xfer_ch, d->fdc_xfer_addr);
        if (d->fdc_xfer_addr < 0x000a0000u)
            pc98_hexdump("data", d->fdc_xfer_addr, 16u);
        pc98_dma_logged++;
    }

    d->fdc_xfer_ch      = (uint8_t) ch;
    d->fdc_xfer_addr    = dma[ch].ac;
    d->fdc_xfer_pending = 1;

    if (pc98_dma_logged < 200u)
        pclog("[PC98] DMA setup: ch2 addr %06X, ch3 addr %06X, using ch%d "
              "mode=%02X count=%04X masked=%u enabled=%u fdc_dma=%u "
              "writable=%u\n",
              (unsigned) dma[2].ac, (unsigned) dma[3].ac, ch,
              dma_mode(ch), (unsigned) dma[ch].cc,
              (unsigned) !!(dma_m & (1u << ch)),
              (unsigned) !!(dma_e & (1u << ch)),
              (unsigned) d->fdc->dma,
              (unsigned) dma_channel_writable(ch));
#else
    (void) d;
#endif
}

static uint8_t
fdc_in(uint16_t port, void *priv)
{
    pc98_mb_t *d = (pc98_mb_t *) priv;
    switch (port) {
        case 0x90:
        case 0xc8:
            {
                const unsigned bank = (port == 0x90u) ? 1u : 0u;
                /* While this identity owns a buffered SIS result, report a result
                 * phase independently of the other identity. */
                if (d->sis_res_n[bank] || d->cmd_res_n[bank])
                    return 0xd0u;
                /* A wrapper-owned seek/empty-media READ ID is parameter-driven even
                 * though the generic core is idle. */
                if (d->wrapper_readid_need && d->wrapper_readid_bank == bank)
                    return 0x90u;
                /* A wrapper-owned seek is parameter-driven even though the generic
                 * core is idle.  Expose CB while its parameters are being accepted. */
                if (d->wrapper_seek_need && d->fdc_bank_1mb == bank)
                    return 0x90u;
                return fdc_read(4, d->fdc);
            }
        case 0x92:
        case 0xca:
            {
                const unsigned bank = (port == 0x92u) ? 1u : 0u;
                uint8_t        v;
                int            core_result_completed = 0;

                if (d->sis_res_n[bank]) {
                    v = d->sis_res[bank][d->sis_res_i[bank]++];
                    if (d->sis_res_i[bank] >= d->sis_res_n[bank]) {
                        d->sis_res_n[bank] = d->sis_res_i[bank] = 0;
                        if (d->trace_res_n < sizeof(d->trace_res))
                            d->trace_res[d->trace_res_n++] = v;
                        pc98_fdc_log_result(d, port);
                        d->trace_res_n = 0;
                        return v;
                    }
                } else if (d->cmd_res_n[bank]) {
                    v = d->cmd_res[bank][d->cmd_res_i[bank]++];
                    if (d->cmd_res_i[bank] >= d->cmd_res_n[bank]) {
                        d->cmd_res_n[bank] = d->cmd_res_i[bank] = 0;
                        picintc(1u << (bank ? 11 : 10));
                    }
                } else {
                    const int      in_result_phase = (fdc_read(4, d->fdc) & 0xf0u) == 0xd0u;
                    const unsigned results_before  = d->fdc->paramstogo;
                    v                              = fdc_read(5, d->fdc);
                    core_result_completed          = in_result_phase && results_before && !d->fdc->paramstogo;
                }
                if (d->trace_res_n < sizeof(d->trace_res))
                    d->trace_res[d->trace_res_n++] = v;

                /* The phase is over once the controller leaves it. */
                if ((fdc_read(4, d->fdc) & 0xc0u) != 0xc0u && !d->sis_res_n[bank] && !d->cmd_res_n[bank]) {
                    pc98_fdc_log_result(d, port);
                    d->trace_res_n = 0;
                }
                /* A programmed-I/O data byte also leaves RQM/DIO clear while the
                 * decoder supplies the next byte. Only draining the final actual
                 * 765 result byte completes the asynchronous command and releases
                 * its route; FIFO probes or private results from the other identity
                 * must not retarget the command's IRQ/DMA lines. */
                if (core_result_completed)
                    pc98_fdc_unlock_route(d);
                return v;
            }
        case 0x94:
            return pc98_fdc_switch_value(d, 0);
        case 0xcc:
            return pc98_fdc_switch_value(d, 1);
        case 0xbe:
            /* D7..D4 undefined, D3 = SW3-2, D2 = SW3-1, D1 = FDE, D0 = PE. */
            return (uint8_t) (0xf0u | ((d->dsw3 & 3u) << 2) | /* D3 = SW3-2, D2 = SW3-1 */
                              (d->fdc_mode & (PC98_FDC_MODE_FDE | PC98_FDC_MODE_PE)));
        case 0x04be:
            /* NEC 3-mode ID: only the two internal 3.5-inch drives respond.
             * D4 identifies the 1MB/1.44MB-capable drive and D0 reports the
             * currently selected 1.44MB access mode. */
            if (d->fdc_3mode_dev_sel >= 2 || !fdd_get_flags(d->fdc->fdd[d->fdc_3mode_dev_sel]) || fdd_is_525(d->fdc->fdd[d->fdc_3mode_dev_sel]))
                return 0x00;
            return (uint8_t) (0xeeu | (!!(d->fdc_mode & PC98_FDC_MODE_FDE) << 4) | !!d->fdc_3mode_144[d->fdc_3mode_dev_sel]);
        default:
            return 0xff;
    }
}

static void
pc98_fdc_ctrl_write(pc98_mb_t *d, uint16_t port, uint8_t val)
{
    const int port_640 = (port == 0xccu);
    uint8_t  *slot     = port_640 ? &d->fdc_ctrl_640 : &d->fdc_ctrl_1mb;
    *slot              = val;
    pc98_fdc_apply_ctrl(d, val, port_640);
}

static void
fdc_out(uint16_t port, uint8_t val, void *priv)
{
    pc98_mb_t *d = (pc98_mb_t *) priv;
    switch (port) {
        case 0x92:
        case 0xca:
            {
                const unsigned bank = (port == 0x92u) ? 1u : 0u;

                /* RQM/NDM/CB with DIO clear is a programmed-I/O execution byte,
                 * not a new command. Sector data may have the same value as SEEK,
                 * READ ID or any other opcode; bypass wrapper command framing. */
                if ((fdc_read(4, d->fdc) & 0xf0u) == 0xb0u) {
                    fdc_write(5, val, d->fdc);
                    return;
                }
                /* A busy or reset 765 does not accept a new command. Apply that
                 * hardware rule before changing wrapper queues, command framing or
                 * routes; the generic core's own rejection happens too late for
                 * wrapper-owned SEEK/READ ID and their side effects. */
                if (!d->fdc_in_params && (fdc_read(4, d->fdc) & 0xf0u) != 0x80u)
                    return;

                if (!d->fdc_in_params)
                    pc98_fdc_trace_flush(d, port);
                if (d->trace_cmd_n < sizeof(d->trace_cmd))
                    d->trace_cmd[d->trace_cmd_n++] = val;

                if (!d->fdc_in_params) {
                    /* Start of a command.  Abandon only this identity's private
                     * wrapper result phase; the other identity may still be waiting
                     * for its own interrupt handler to drain it. */
                    d->sis_res_n[bank] = d->sis_res_i[bank] = 0;
                    d->cmd_res_n[bank] = d->cmd_res_i[bank] = 0;

                    if (pc98_fdc_cmd_owns_interrupt(val & 0x1fu)) {
                        /* Transfer/result commands own their completion.  If an old
                         * probe left synthetic seek completions undrained, discard
                         * both the queue and the delayed IRQ which advertises it. */
                        d->sis_q_n[bank] = 0;
                        timer_disable(&d->seek_irq_timer[bank]);
                        picintc(1u << (bank ? 11 : 10));
                    }

                    pc98_fdc_route(d, bank != 0u);
                    pc98_fdc_apply_dma_gate(d);
                    if (bank)
                        d->motor_1mb = 1;
                    pc98_fdc_motor_update(d);

                    d->fdc_cmd = val & 0x1fu;

                    /* Generic media commands finish asynchronously.  Pin the shared
                     * 765's physical IRQ/DMA route to the identity which issued the
                     * command until its result phase is completely drained.  Without
                     * this, a BEh switch, an opposite-bank control access, or the
                     * PC-9801-27 SASI ROM probing 92h can retarget a CAh command from
                     * DMA3/IRQ10 to DMA2/IRQ11 while the floppy image layer is still
                     * working.  READ ID is delayed until its drive parameter tells us
                     * whether the wrapper will synthesize NOT READY instead. */
                    if (d->fdc_cmd != 0x0au && pc98_fdc_cmd_owns_interrupt(d->fdc_cmd))
                        pc98_fdc_lock_route(d, bank);

                    /* READ DATA / WRITE DATA / READ DELETED / WRITE DELETED all move
                     * bytes through the 8237.  The DMA channel is fixed by the active
                     * PC-98 FDD interface identity (1 MB -> DMA2, 640 KB -> DMA3);
                     * never infer it from whichever 8237 address register happens to
                     * look programmed. */
                    if (d->fdc_cmd == 0x06u || d->fdc_cmd == 0x05u || d->fdc_cmd == 0x0cu || d->fdc_cmd == 0x09u)
                        pc98_fdc_report_transfer(d);

                    if (d->fdc_cmd == 0x08u) {
                        const int irq = bank ? 11 : 10;
                        picintc(1u << irq);
                        timer_disable(&d->seek_irq_timer[bank]);
                    }

                    if (d->fdc_cmd == 0x08u && pc98_fdc_serve_sis(d, bank)) {
                        pc98_fdc_log_cmd(d, port);
                        d->trace_cmd_n = 0;
                        return; /* generic core remains idle */
                    }

                    /* Hold READ ID for its one drive/head parameter.  If there is
                     * no mounted medium, completing it in the board wrapper avoids a
                     * generic-FDD media-poll wait which can lose the PC-98 bank IRQ.
                     * With media present we replay the original command and parameter
                     * into the generic uPD765 unchanged. */
                    if (d->fdc_cmd == 0x0au) {
                        d->wrapper_readid_need = 1u;
                        d->wrapper_readid_bank = (uint8_t) bank;
                        d->fdc_in_params       = 1u;
                        return;
                    }

                    /* The wrapper owns SEEK and RECALIBRATE.  Do not feed their
                     * command byte into the generic FDC, otherwise the asynchronous
                     * FDD completion later uses whatever IRQ route happens to be
                     * selected at that future instant. */
                    if (d->fdc_cmd == 0x07u || d->fdc_cmd == 0x0fu) {
                        d->wrapper_seek_cmd  = d->fdc_cmd;
                        d->wrapper_seek_need = (d->fdc_cmd == 0x07u) ? 1u : 2u;
                        d->fdc_in_params     = 1;
                        return;
                    }

                    d->wrapper_seek_cmd  = 0;
                    d->wrapper_seek_need = 0;
                } else if (d->wrapper_readid_need) {
                    const uint8_t  drvsel       = val;
                    const unsigned drive        = drvsel & 3u;
                    const uint8_t  original_cmd = d->trace_cmd[0];

                    d->wrapper_readid_need = 0u;
                    d->fdc_in_params       = 0u;

                    pc98_fdc_density_for_drive(
                        d, !!(d->fdc_mode & PC98_FDC_MODE_FDE), drive);

                    if (drive >= 4 || !fdd_is_ready(d->fdc->fdd[drive])) {
                        pc98_fdc_complete_not_ready_read_id(d, bank, drvsel);
                        pc98_fdc_log_cmd(d, port);
                        d->trace_cmd_n = 0;
                        return;
                    }

                    /* There is media: let 86Box's real floppy image layer discover
                     * the actual C/H/R/N ID.  We delayed the command byte only long
                     * enough to learn which drive the parameter selected.  From here
                     * on READ ID is asynchronous too, so pin its originating route. */
                    pc98_fdc_lock_route(d, bank);
                    fdc_write(5, original_cmd, d->fdc);
                    fdc_write(5, drvsel, d->fdc);
                    d->fdc_in_params = ((fdc_read(4, d->fdc) & 0xf0u) == 0x90u);
                    if (!d->fdc_in_params) {
                        pc98_fdc_log_cmd(d, port);
                        d->trace_cmd_n = 0;
                    }
                    return;
                } else if (d->wrapper_seek_need) {
                    /* Parameter byte for a wrapper-owned seek command.  The first
                     * parameter carries drive/head, so make the physical spindle mode
                     * follow that drive before any later media command uses it. */
                    if (d->trace_cmd_n == 2u)
                        pc98_fdc_density_for_drive(
                            d, !!(d->fdc_mode & PC98_FDC_MODE_FDE), val & 3u);
                    d->wrapper_seek_need--;
                    if (!d->wrapper_seek_need) {
                        d->fdc_in_params = 0;
                        pc98_fdc_complete_seek(d, bank);
                        pc98_fdc_log_cmd(d, port);
                        d->trace_cmd_n      = 0;
                        d->wrapper_seek_cmd = 0;
                    }
                    return;
                }

                /* All media commands carry drive/head in their first parameter.
                 * Refresh DENSEL at that point, not merely when BEh/04BEh changes, so
                 * a mixed 5.25/3.5 pair and old BIOSes that never use 04BEh get the
                 * correct spindle speed on every command. */
                if (d->trace_cmd_n == 2u && pc98_fdc_cmd_has_drivesel(d->fdc_cmd))
                    pc98_fdc_density_for_drive(
                        d, !!(d->fdc_mode & PC98_FDC_MODE_FDE), val & 3u);

                fdc_write(5, val, d->fdc);

                /* RQM/CB set with DIO/NDM clear means more command parameters.
                 * NDM distinguishes programmed-I/O execution bytes from parameters. */
                d->fdc_in_params = ((fdc_read(4, d->fdc) & 0xf0u) == 0x90u);
                if (!d->fdc_in_params) {
                    /* SPECIFY can update the controller's ND bit as its last
                     * parameter is accepted. Reconcile it with the interface gate. */
                    pc98_fdc_apply_dma_gate(d);
                    pc98_fdc_log_cmd(d, port);
                    d->trace_cmd_n = 0;
                }
                return;
            }
        case 0x94:
        case 0xcc:
            pc98_fdc_trace("ctrl  ", port, val);
            pc98_fdc_ctrl_write(d, port, val);
            return;
        case 0xbe:
            {
                pc98_fdc_trace("mode  ", port, val);
                /* D3:D2 are the SW3-2/SW3-1 DIP inputs and are read-only; only FDE
                 * and PE are writable.  Accepting D2 as well used to invent a
                 * "force motors on" mode out of a switch position. */
                const uint8_t new_mode = val & (PC98_FDC_MODE_FDE | PC98_FDC_MODE_PE);
                const int     old_1mb  = !!(d->fdc_mode & PC98_FDC_MODE_FDE);
                const int     new_1mb  = !!(new_mode & PC98_FDC_MODE_FDE);

                d->fdc_mode = new_mode;
                if (old_1mb != new_1mb)
                    pc98_fdc_density(d, new_1mb);
                pc98_fdc_route(d, !!(new_mode & PC98_FDC_MODE_PE));
                return;
            }
        case 0x04be:
            d->fdc_3mode_dev_sel = (uint8_t) ((val & 0x60) >> 5);
            if ((val & 0x10) && d->fdc_3mode_dev_sel < 2 && fdd_get_flags(d->fdc->fdd[d->fdc_3mode_dev_sel]) && !fdd_is_525(d->fdc->fdd[d->fdc_3mode_dev_sel]))
                d->fdc_3mode_144[d->fdc_3mode_dev_sel] = val & 1;
            /* DENSEL means opposite spindle speeds to 5.25-inch and 3.5-inch
             * dual-RPM mechanisms.  Refresh it even for a select-only write so
             * mixed-form-factor drive pairs cannot inherit each other's RPM. */
            pc98_fdc_density(d, !!(d->fdc_mode & PC98_FDC_MODE_FDE));
            return;
        default:
            return;
    }
}

/* ARTIC free-running timestamp device at 5Ch-5Fh.  The hardware exposes
 * overlapping 16-bit windows: offset 0 = cycle bits 15:0, offset 1 = 23:8.
 * Byte reads are supplied too because some diagnostics probe them directly. */
static uint8_t
pc98_artic_inb(uint16_t port, void *priv)
{
    (void) priv;
    const unsigned sh = (unsigned) (port - 0x5cu) * 8u;
    return (uint8_t) (tsc >> sh);
}

static uint16_t
pc98_artic_inw(uint16_t port, void *priv)
{
    (void) priv;
    const unsigned off = (unsigned) ((port - 0x5cu) >> 1);
    return (uint16_t) (tsc >> (off * 8u));
}

static void
pc98_artic_wait(uint16_t port, uint8_t val, void *priv)
{
    (void) port;
    (void) val;
    (void) priv;
    /* Real ARTIC inserts about 0.6 us.  Functional firmware only requires
     * the port to terminate; I/O timing already consumes guest cycles. */
}

static void
rtc_out(uint16_t port, uint8_t val, void *priv)
{
    pc98_mb_t *d = (pc98_mb_t *) priv;
    (void) port;
    d->rtc_ctrl = val;
    pc98_rtc_write(&d->rtc, val);
}

static void
vrtc_clear_out(uint16_t port, uint8_t val, void *priv)
{
    pc98_mb_t *d = (pc98_mb_t *) priv;
    (void) port;
    (void) val;
    pc9801_video_vrtc_reset(d->video_dev);

#ifndef RELEASE_BUILD
    static int vrtc_count = 0;
    if (++vrtc_count >= 60) {
        vrtc_count = 0;
        pclog("[PC98-CPU] %04X:%04X AX=%04X BX=%04X CX=%04X DX=%04X SP=%04X BP=%04X SI=%04X DI=%04X DS=%04X ES=%04X SS=%04X\n",
              CS, (uint16_t) cpu_state.pc, AX, BX, CX, DX, SP, BP, SI, DI, DS, ES, SS);
    }
#endif
}

static void
nmi_ff_out(uint16_t port, uint8_t val, void *priv)
{
    (void) val;
    ((pc98_mb_t *) priv)->nmi_ff = (uint8_t) ((port - 0x50u) >> 1);
}

static int
pc98_a20_enabled(void)
{
    /* rammask is the authoritative address-line state.  In particular, the
     * generic 286 reset path can transiently leave mem_a20_state at zero
     * while the physical mask is still open. */
    return (rammask & 0x00100000u) != 0;
}

static void
pc98_sdip_translate(pc98_mb_t *d)
{
    d->dsw1 = d->sdip[0] | 1u;
    d->dsw2 = (d->sdip[1] & 0xefu) | ((d->sdip[3] & 0x20u) >> 1);
    d->dsw3 = ((d->sdip[2] & 3u) ^ 2u) | (d->sdip[2] & 0x7cu);
    if (d->video)
        d->video->graph_clock_fast = !(d->dsw2 & 0x80u);
}

static void
pc98_sdip_load(pc98_mb_t *d)
{
    uint8_t image[sizeof(d->sdip)];
    memset(d->sdip, 0xff, sizeof(d->sdip));
    FILE *f = nvr_fopen("pc9801bx4.sdip", "rb");
    if (f) {
        if (fread(image, 1, sizeof(image), f) == sizeof(image) && fgetc(f) == EOF)
            memcpy(d->sdip, image, sizeof(image));
        fclose(f);
    }
    pc98_sdip_translate(d);
}

static void
pc98_sdip_save(const pc98_mb_t *d)
{
    FILE *f = nvr_fopen("pc9801bx4.sdip", "wb");
    if (f) {
        (void) fwrite(d->sdip, 1, sizeof(d->sdip), f);
        fclose(f);
    }
}

static uint8_t
pc98_sdip_in(uint16_t port, void *priv)
{
    pc98_mb_t *d = priv;
    return d->sdip[d->sdip_bank * 12u + ((port >> 8) - 0x84u)];
}

static void
pc98_sdip_out(uint16_t port, uint8_t val, void *priv)
{
    pc98_mb_t *d = priv;
    if (port == 0x8f1f) {
        if (val == 0x80u || val == 0xc0u)
            d->sdip_bank = (val >> 6) & 1u;
    } else {
        const unsigned offset = d->sdip_bank * 12u + ((port >> 8) - 0x84u);
        if (d->sdip[offset] == val)
            return;
        d->sdip[offset] = val;
        pc98_sdip_translate(d);
        /* Persist guest writes, including setup followed by an immediate
         * reset. No firmware bytes or synthesized parity are supplied. */
        pc98_sdip_save(d);
    }
}

static uint8_t
pc98_display_frequency_in(uint16_t port, void *priv)
{
    (void) port;
    return ((pc98_mb_t *) priv)->display_frequency;
}

static void
pc98_display_frequency_out(uint16_t port, uint8_t val, void *priv)
{
    (void) port;
    /* Later PC-98 monitor-frequency selector. Bits 0..1 are writable;
     * bit 7 stays low. The legacy renderer still uses its 24 kHz timing. */
    ((pc98_mb_t *) priv)->display_frequency = val & 3u;
}

static uint8_t
a20_in(uint16_t port, void *priv)
{
    pc98_mb_t    *d      = (pc98_mb_t *) priv;
    const uint8_t masked = pc98_a20_enabled() ? 0u : 1u;

    /* PC-98 A20 status is active-low: bit 0 == 1 means A20 is masked.
     * F2h has the remaining bits pulled high. Later 32-bit boards also
     * expose the NMI FF in F6h bit 1. VX leaves F6h undecoded. */
    if (port == 0xf2)
        return (uint8_t) (0xfeu | masked);
    if (port == 0xf6 && d->model == PC98_MODEL_BX4)
        return masked | (d->nmi_ff << 1);
    return 0xff;
}

static void
a20_out(uint16_t port, uint8_t val, void *priv)
{
    pc98_mb_t *d = priv;

    /* F2h opens A20 on both models. The BX4's later F6h gate additionally
     * accepts 02h (open) / 03h (mask); other values have no effect. */
    if (port == 0xf2) {
        mem_a20_alt = 2;
        mem_a20_recalc();
    } else if (port == 0xf6 && d->model == PC98_MODEL_BX4 && (val == 2u || val == 3u)) {
        mem_a20_key = mem_a20_chipset = 0;
        mem_a20_alt                   = val == 2u ? 2 : 0;
        mem_a20_recalc();
    }
}

/* PC-98 CPU reset port F0h.
 *
 * An 80286 cannot leave protected mode by itself, so the VX firmware returns
 * to real mode the documented way: it parks its resume address on the stack,
 * saves SS:SP at 0000:0404, clears SHUT0/SHUT1 through the system 8255, then
 *
 *     mov al,0
 *     out 0F0h,al
 *     jmp $
 *
 * and waits to be reset out of that spin loop.  Without this handler the OUT
 * is swallowed and the machine hangs on the EB FE forever, which is exactly
 * where the IPL stops just as it starts exercising memory above 1 MiB.
 *
 * This is a CPU-only reset.  Conventional RAM, the 8255 SHUT latch and the
 * 043Dh IPL bank must all survive it: the firmware's shutdown handler reads
 * 0000:0404 for its stack, 35h for the shutdown reason, and (on the ITF path)
 * builds a bank-switch trampoline at 0000:04F8 and jumps to it.  Running the
 * motherboard or machine reset here would destroy all three. */
static void
pc98_cpu_reset_out(uint16_t port, uint8_t val, void *priv)
{
#ifndef RELEASE_BUILD
    pc98_mb_t *d = (pc98_mb_t *) priv;
#endif
    (void) priv;
    (void) port;
    (void) val;

    pclog("[PC98] CPU reset via F0h (SHUT0=%u SHUT1=%u)\n",
          !!(d->sys_port_c & PC98_SYS_C_SHUT0),
          !!(d->sys_port_c & PC98_SYS_C_SHUT1));

#ifndef RELEASE_BUILD
    /* The ITF sizes extended memory by walking a protected-mode descriptor
     * upward in 128 KiB steps, then resets the CPU through this port and, back
     * in real mode, converts the descriptor's final base into the answer:
     *
     *     mov al, ds:8044h     ; base bits 23:16 the walk stopped at
     *     sub al, 10h          ; minus 1 MiB
     *     shr al, 1            ; -> 128 KiB units
     *     mov ds:0401h, al     ; extended memory size
     *
     * Reporting it here says whether the walk found the memory at all, which
     * is a different question from whether the firmware then displays it. */
    if (ram != NULL) {
        const unsigned base_high = ram[0x8044u];
        const unsigned kb        = (base_high >= 0x10u)
            ? ((base_high - 0x10u) / 2u) * 128u
            : 0u;

        /* These are PHYSICAL addresses, not offsets in the host `ram`
         * allocation.  With the generic mem_remap_top() layout a perfectly
         * valid guest address at/above 1 MiB can be backed by ram+A0000h, and
         * with a 640 KiB configuration ram[100000h] is outright out of
         * bounds.  The old diagnostic memcpy()s therefore caused a host
         * access violation exactly when the IPL issued its first F0h reset
         * while probing high memory.  Read through the memory bus instead. */
        const uint16_t first_begin  = mem_readw_phys(0x100000u);
        const uint16_t first_end    = mem_readw_phys(0x10fffeu);
        const uint16_t second_begin = mem_readw_phys(0x110000u);
        const uint16_t second_end   = mem_readw_phys(0x11fffeu);
        const uint16_t low_begin    = mem_readw_phys(0x000000u);
        const uint16_t low_second   = mem_readw_phys(0x010000u);

        if (base_high == 0x11u) {
            pclog("[PC98] CPU reset 1 (A20 wrap test)\n");
        } else {
            pclog("[PC98] memory walk stopped at descriptor base %02Xh "
                  "-> %u KB extended\n",
                  base_high, kb);
        }
        pclog("[PC98] memory probe windows: 100000=%04X 10FFFE=%04X "
              "110000=%04X 11FFFE=%04X; aliases 000000=%04X 010000=%04X; "
              "rammask=%08X\n",
              first_begin, first_end, second_begin, second_end,
              low_begin, low_second, rammask);

        /* The walk drives two descriptors, at GDT offsets 40h and 48h, whose
         * base bits 23:16 live at 8044h and 804Ch.  It starts them at 10h and
         * 11h and adds two to each per 128 KiB pass, so 8044h should only ever
         * hold an even value.  Dumping both, with the rest of the pair, shows
         * whether the loop ran at all and whether the GDT really is where the
         * firmware's real-mode read assumes it is. */
        pc98_hexdump("GDT?", 0x8000u, 128u);
        pc98_find_walk_descriptors();
    }

#endif

    /* F0h resets only the CPU (including protected-mode return through RESET).
     * Preserve both the physical FDC command and its wrapper/route until the
     * guest resets the controller at 94h/CCh. Dropping just the wrapper here
     * orphaned a live DMA request and its media callback on a CPU-only reset. */

    /* F0h starts a new firmware pass without rebuilding the motherboard.
     * Reset the bounded trace counters so a warm boot still captures the
     * command and result phases which follow the reset. */
    pc98_fdc_logged              = 0;
    pc98_dma_logged              = 0;
    pc98_firmware_memsize_logged = 0;

    /* F0h resets the CPU and returns the PC-98 address bus to its reset
     * compatibility state.  Reassert the board state here as well as in the
     * common 286 reset path, because softresetx86() may be suppressed by the
     * generic soft-reset guard.  F2h later unmasks addresses at/above 1 MiB. */
    softresetx86();

    mem_a20_key     = 0;
    mem_a20_alt     = 0;
    mem_a20_chipset = 0;
    mem_a20_recalc();
    rammask = 0x000fffffU;

    /* The 286 reset-vector fetch still has to reach FFFFF0h with the external
     * address bus masked.  This one-shot bypass affects only that fetch; the
     * 20-bit compatibility mask remains active for firmware execution. */
    mem_a20_reset_vector_bypass_once();
    flushmmucache();
}

static void
pc98_install_io(pc98_mb_t *d)
{
    /* System PPI: odd byte lane */
    for (uint16_t p = 0x31; p <= 0x37; p += 2)
        io_sethandler(p, 1, ppi_sys_in, NULL, NULL, ppi_sys_out, NULL, NULL, d);

    /* RS-232 8251: even byte lane */
    io_sethandler(0x30, 1, usart_rs_in, NULL, NULL, usart_rs_out, NULL, NULL, d);
    io_sethandler(0x32, 1, usart_rs_in, NULL, NULL, usart_rs_out, NULL, NULL, d);

    /* Printer PPI even, keyboard USART odd. */
    for (uint16_t p = 0x40; p <= 0x46; p += 2)
        io_sethandler(p, 1, ppi_prn_in, NULL, NULL, ppi_prn_out, NULL, NULL, d);
    io_sethandler(0x41, 1, usart_kbd_in, NULL, NULL, usart_kbd_out, NULL, NULL, d);
    io_sethandler(0x43, 1, usart_kbd_in, NULL, NULL, usart_kbd_out, NULL, NULL, d);

    io_sethandler(0x20, 1, NULL, NULL, NULL, rtc_out, NULL, NULL, d);
    for (uint16_t p = 0x21; p <= 0x29; p += 2)
        io_sethandler(p, 1, dma_page_in, NULL, NULL, dma_page_out, NULL, NULL, d);
    for (uint16_t p = 0x50; p <= 0x56; p += 2)
        io_sethandler(p, 1, NULL, NULL, NULL, nmi_ff_out, NULL, NULL, d);
    io_sethandler(0x5c, 4, pc98_artic_inb, pc98_artic_inw, NULL, NULL, NULL, NULL, d);
    io_sethandler(0x5f, 1, pc98_artic_inb, NULL, NULL, pc98_artic_wait, NULL, NULL, d);
    io_sethandler(0x64, 1, NULL, NULL, NULL, vrtc_clear_out, NULL, NULL, d);

    /* PIT shares 70h-7fh with display/GRCG on the opposite byte lane. */
    for (uint16_t p = 0x71; p <= 0x77; p += 2)
        io_sethandler(p, 1, pit_in, NULL, NULL, pit_out, NULL, NULL, d);
    for (uint16_t p = 0x3fd9; p <= 0x3fdf; p += 2)
        io_sethandler(p, 1, pit_in, NULL, NULL, pit_out, NULL, NULL, d);

    io_sethandler(0x90, 1, fdc_in, NULL, NULL, NULL, NULL, NULL, d);
    io_sethandler(0x92, 1, fdc_in, NULL, NULL, fdc_out, NULL, NULL, d);
    io_sethandler(0x94, 1, fdc_in, NULL, NULL, fdc_out, NULL, NULL, d);
    io_sethandler(0xbe, 1, fdc_in, NULL, NULL, fdc_out, NULL, NULL, d);
    io_sethandler(0xc8, 1, fdc_in, NULL, NULL, NULL, NULL, NULL, d);
    io_sethandler(0xca, 1, fdc_in, NULL, NULL, fdc_out, NULL, NULL, d);
    io_sethandler(0xcc, 1, fdc_in, NULL, NULL, fdc_out, NULL, NULL, d);
    io_sethandler(0x04be, 1, fdc_in, NULL, NULL, fdc_out, NULL, NULL, d);

    io_sethandler(0xf0, 1, NULL, NULL, NULL, pc98_cpu_reset_out, NULL, NULL, d);
    io_sethandler(0xf2, 1, a20_in, NULL, NULL, a20_out, NULL, NULL, d);
    if (d->model == PC98_MODEL_BX4)
        io_sethandler(0xf6, 1, a20_in, NULL, NULL, a20_out, NULL, NULL, d);
    if (d->model == PC98_MODEL_BX4) {
        for (uint16_t p = 0x841e; p <= 0x8f1e; p += 0x100)
            io_sethandler(p, 1, pc98_sdip_in, NULL, NULL, pc98_sdip_out, NULL, NULL, d);
        io_sethandler(0x8f1f, 1, NULL, NULL, NULL, pc98_sdip_out, NULL, NULL, d);
        io_sethandler(0x09a8, 1, pc98_display_frequency_in, NULL, NULL,
                      pc98_display_frequency_out, NULL, NULL, d);
    }
    io_sethandler(0x0439, 1, dma_access_ctrl_in, NULL, NULL,
                  dma_access_ctrl_out, NULL, NULL, d);

    for (uint16_t p = 0x7fd9; p <= 0x7fdf; p += 2)
        io_sethandler(p, 1, ppi_mouse_in, NULL, NULL, ppi_mouse_out, NULL, NULL, d);
    io_sethandler(0xbfdb, 1, NULL, NULL, NULL, pc98_mouse_rate_out, NULL, NULL, d);
}

static void
pc98_mb_reset(void *priv)
{
    pc98_mb_t *d = (pc98_mb_t *) priv;
    d->sdip_bank = d->display_frequency = 0;

    /* Abort on the old physical route before the wrapper loses its route pin.
     * Motors remain off until firmware starts them after fdd_reset(). */
    if (d->fdc)
        fdc_write(2, 0x08u, d->fdc);

    /* Reset leaves both control registers with RESET deasserted, and BEh
     * selecting the 1MB port identity and 1MB drive mode, which is how a VX
     * comes up. */
    d->fdc_ctrl_1mb = d->fdc_ctrl_640 = 0;
    /* Motors stay down until the firmware asks: see pc98_fdc_motor_update(). */
    d->motor_1mb = d->motor_640 = d->motor_on = 0;
    d->dmae_640                               = 1;
    pc98_fdc_forget_results(d);
    d->fdc_mode          = PC98_FDC_MODE_FDE | PC98_FDC_MODE_PE;
    d->fdc_3mode_dev_sel = 2;
    d->fdc_3mode_144[0]  = 0;
    d->fdc_3mode_144[1]  = 0;
    d->nmi_ff            = 0;
    picintc((1u << 0) | (1u << 1) | (1u << 2) | (1u << 4) | (1u << 10) | (1u << 11));
    /* Power-on state: both shutdown flags inactive, so the reset entry point
     * takes the cold-boot path instead of trying to resume through a stack
     * that does not exist yet. */
    d->sys_port_c = PC98_SYS_C_RESET;
    d->rtc_ctrl   = 0;
    pc98_rtc_reset(&d->rtc);
    d->rs_irq_mask = d->rs_irq_pending = 0;
    d->kbd_rp = d->kbd_count = 0;
    memset(d->host_down, 0, sizeof(d->host_down));
    memset(d->kbd_code_users, 0, sizeof(d->kbd_code_users));
    memset(d->dma_autoinc, 0, sizeof(d->dma_autoinc));
    d->dma_autoinc_latch = 0;
    for (uint8_t ch = 0; ch < 4; ch++)
        dma_set_pc98_autoinc(ch, 0);
    /* NEC's normal DMA mode masks A23-A20 (0439h MSK=1).  Legacy disk
     * ROMs form the page byte with ROL segment,4 and can leave unrelated
     * segment bits in its upper nibble; those bits must not reach memory.
     * Software can clear MSK explicitly when it needs DMA above 1 MiB. */
    d->dma_access_ctrl = 0x04;
    dma_set_mask(0x000fffffu);
    pc98_pit_reset(d);
    if (d->fdc) {
        /* PC-98 has no IBM DOR at 3F2h: the DOR exists only as the core's
         * internal view of /RESET, the DMA/interrupt gate and the motors, all
         * of which the board drives from 94h/CCh.  Come up with reset released
         * and the motors off so the first command starts media polling even if
         * the firmware never pulses RESET. */
        pc98_fdc_route(d, 1);
        pc98_fdc_apply_dma_gate(d);
        pc98_fdc_density(d, 1);
        fdc_write(2, 0x0cu, d->fdc);
    }
    pc98_ppi8255_reset(&d->ppi_sys);
    pc98_ppi8255_reset(&d->ppi_prn);
    pc98_printer_control(d);
    pc98_ppi8255_reset(&d->ppi_mouse);
    pc98_mouse_reset(&d->mouse);
    picintc(1u << 13);
    timer_set_delay_u64(&d->mouse_timer, (uint64_t) ((1000000.0 / 120.0) * TIMER_USEC));
    pc98_i8251_reset(&d->usart_kbd);
    pc98_i8251_reset(&d->usart_rs);
    pc98_i8251_set_cts(&d->usart_kbd, 1);
    pc98_i8251_set_dsr(&d->usart_kbd, 1);
    pc98_i8251_set_cts(&d->usart_rs, 1);
    pc98_i8251_set_dsr(&d->usart_rs, 1);
}

static void
pc98_mb_speed_changed(void *priv)
{
    pc98_mb_t *d = (pc98_mb_t *) priv;
    pc98_pit_reclock(d);
}

static void
pc98_mb_close(void *priv)
{
    pc98_mb_t *d = (pc98_mb_t *) priv;
    speaker_set_pc98_state(0, 0);
    pc98_rtc_save(&d->rtc);
    if (d->model == PC98_MODEL_BX4)
        pc98_sdip_save(d);
    pclog("[PC98-SHUTDOWN-CPU] CS:IP=%04X:%04X AX=%04X BX=%04X CX=%04X DX=%04X SP=%04X BP=%04X SI=%04X DI=%04X DS=%04X ES=%04X SS=%04X\n",
          CS, (uint16_t) cpu_state.pc, AX, BX, CX, DX, SP, BP, SI, DI, DS, ES, SS);
#ifndef RELEASE_BUILD
    uint32_t phys_ip = (CS << 4) + (uint16_t) cpu_state.pc;
    if (ram != NULL && phys_ip < (uint32_t) mem_size * 1024u)
        pc98_hexdump("CS:IP bytes", phys_ip, 16u);
#endif
    keyboard_set_input_handler(NULL, NULL);
    timer_disable(&d->seek_irq_timer[0]);
    timer_disable(&d->seek_irq_timer[1]);
    timer_disable(&d->mouse_timer);
    if (mouse_type == MOUSE_TYPE_INTERNAL)
        mouse_set_poll(NULL, NULL);
    free(d->kbd_fifo);
    free(d);
}

static void *
pc98_mb_init(const device_t *info)
{
    pc98_mb_t *d = (pc98_mb_t *) calloc(1, sizeof(*d));
    if (!d)
        return NULL;

    pc98_rtc_load(&d->rtc);

    /* Claim the host LPT slots without installing IBM 378h/278h/3BCh ports.
     * LPT1 is the native printer; further backends have no guest decoder. */
    for (int i = 0; i < PARALLEL_MAX; i++) {
        lpt_t *backend = device_add_inst(&lpt_port_native_device, i + 1);
        if (i == 0)
            d->printer = backend;
    }
    lpt_set_next_inst(PARALLEL_MAX);
    /* RS-232C belongs to the native 8251 at 30h/32h. Until its host bridge is
     * implemented, don't create unrelated 8250 ports in the later generic
     * serial setup pass. These would install AT decoders on a PC-98. */
    serial_set_next_inst(SERIAL_MAX);

    timer_add(&d->seek_irq_timer[0], pc98_seek_irq_640_tick, d, 0);
    timer_add(&d->seek_irq_timer[1], pc98_seek_irq_1mb_tick, d, 0);
    timer_add(&d->mouse_timer, pc98_mouse_tick, d, 0);
    if (mouse_type == MOUSE_TYPE_INTERNAL) {
        mouse_set_buttons(2);
        mouse_set_poll(pc98_mouse_poll, d);
    }

    /* Diagnostic limits are per machine instance.  Keeping the static count
     * from a previous reset/recreate hid the useful READ/READ-ID trace exactly
     * when a boot fell through to the next device. */
    pc98_fdc_logged              = 0;
    pc98_dma_logged              = 0;
    pc98_firmware_memsize_logged = 0;
    pc98_configure_ram_map();

    d->video_dev = (void *) info->local;
    d->video     = pc9801_video_get_core(d->video_dev);
    d->model     = pc9801_video_get_model(d->video_dev);
    /* VX factory DIP defaults.  In particular SW2 bit 1 must be 1: a zero
     * selects Terminal Mode and the firmware intentionally avoids the normal
     * local display path.  SW1 bit 0 selects the normal PC-98 24 kHz display. */
    d->dsw1 = 0x01;
    d->dsw2 = 0xe3;
    if (d->video)
        d->video->graph_clock_fast = !(d->dsw2 & 0x80u);
    /* DIP switch SW3.  Only SW3-1 and SW3-2 are read through BEh:
     *
     *   SW3-1  internal FD operating mode   ON: fixed      OFF: auto-switching
     *   SW3-2  internal FD format           ON: 2DD mode   OFF: 2HD mode
     *
     * Both were being reported as set, and the firmware read that as "fixed,
     * 2DD": it locked onto the 640KB interface, did all its work through
     * CAh/CCh, and handed the booted code a 2DD device address.  The boot
     * loader then asked for 512-byte sectors on 1024-byte media and every read
     * came back as ND.  The IPL never even issued a READ ID on the 1MB bank.
     *
     * Clearing them selects 2HD with auto-switching, which is how a VX with
     * 1.25MB media is meant to come up. */
    d->dsw3 = 0x7c;
    if (d->model == PC98_MODEL_BX4)
        pc98_sdip_load(d);

    ppi_ctx_sys.mb      = d;
    ppi_ctx_sys.which   = 0;
    ppi_ctx_prn.mb      = d;
    ppi_ctx_prn.which   = 1;
    ppi_ctx_mouse.mb    = d;
    ppi_ctx_mouse.which = 2;
    pc98_ppi8255_init(&d->ppi_sys, &ppi_ctx_sys, pc98_ppi_read_cb, pc98_ppi_write_cb, pc98_ppi_intr_notify);
    pc98_ppi8255_init(&d->ppi_prn, &ppi_ctx_prn, pc98_ppi_read_cb, pc98_ppi_write_cb, pc98_ppi_intr_notify);
    pc98_ppi8255_init(&d->ppi_mouse, &ppi_ctx_mouse, pc98_ppi_read_cb, pc98_ppi_write_cb, pc98_ppi_intr_notify);

    pc98_i8251_init(&d->usart_kbd, d, pc98_kbd_tx, pc98_kbd_rxrdy, NULL, NULL);
    pc98_i8251_init(&d->usart_rs, d, NULL, pc98_rs_line, pc98_rs_line, pc98_rs_line);

    /* Genuine PC-98 interrupt/DMA decoders.  These functions are supplied by
     * the small core patches installed by apply_to_upd7220_code.py. */
    pic_init_pc98();
    dma_init_pc98();

    d->pit           = (pit_t *) device_add(&i8253_ext_io_device);
    d->pit->dev_priv = d;
    pit_devs[0]      = pit_classic_intf;
    pit_devs[0].data = d->pit;

    d->fdc = (fdc_t *) device_add(&fdc_pc98_device);
    fdc_set_irq(d->fdc, PC98_FDC_IRQ);
    fdc_set_dma_ch(d->fdc, PC98_FDC_DMA);
    /* pc98_mb_reset() below establishes the DOR through the core's own path. */

    pc98_install_io(d);
    keyboard_scan = 1;
    keyboard_set_input_handler(pc98_keyboard_host, d);
    pc98_mb_reset(d);
    return d;
}

static const device_config_t pc9801vx_config[] = {
    { .name        = "skip_timer_checks",
     .description = "Skip BIOS timer checks (CPU upgrades)",
     .type        = CONFIG_BINARY,
     .default_int = 0 },
    { .name = "", .type = CONFIG_END }
};

/* Configuration only: the video-backed motherboard is created separately. */
const device_t pc9801vx_device = {
    .name          = "NEC PC-9801VX",
    .internal_name = "pc9801vx",
    .config        = pc9801vx_config
};

static const device_t pc9801vx_motherboard_device = {
    .name          = "NEC PC-9801VX motherboard",
    .internal_name = "pc9801vx_motherboard",
    .flags         = DEVICE_ONBOARD | DEVICE_CBUS,
    .local         = 0,
    .init          = pc98_mb_init,
    .close         = pc98_mb_close,
    .reset         = pc98_mb_reset,
    .available     = NULL,
    .speed_changed = pc98_mb_speed_changed,
    .force_redraw  = NULL,
    .config        = NULL
};

static const device_t pc9801bx4_motherboard_device = {
    .name          = "NEC PC-9801BX4 legacy motherboard (experimental)",
    .internal_name = "pc9801bx4_motherboard",
    .flags         = DEVICE_ONBOARD | DEVICE_CBUS,
    .init          = pc98_mb_init,
    .close         = pc98_mb_close,
    .reset         = pc98_mb_reset,
    .speed_changed = pc98_mb_speed_changed
};

static int
pc98_machine_init(const machine_t *model, pc98_model_t firmware_model)
{
    if (!pc9801_video_firmware_available_for_model(firmware_model))
        return 0;
    if (bios_only)
        return 1;

    /* Video owns the low/high IPL mappings as well as all PC-98 VRAM. */
    void *video = device_add(model->vid_device);
    if (!video)
        return 0;
    device_add_params(firmware_model == PC98_MODEL_BX4 ? &pc9801bx4_motherboard_device : &pc9801vx_motherboard_device, video);

    /* hdc_reset() installs explicitly selected cards. A disk image does not
     * imply a controller: the UI's None selection must leave SASI absent. */

    return 1;
}

int
machine_pc9801vx_init(const machine_t *model)
{
    return pc98_machine_init(model, PC98_MODEL_VX);
}

int
machine_pc9801bx4_init(const machine_t *model)
{
    if (!bios_only)
        pclog("[PC98] BX4 experimental: running supplied BX4 firmware on legacy "
              "PC-98 peripherals; GD5430, PMC and onboard IDE are not implemented\n");
    return pc98_machine_init(model, PC98_MODEL_BX4);
}
