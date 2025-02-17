/*
 * 86Box    A hypervisor and IBM PC system emulator that specializes in
 *          running old operating systems and software designed for IBM
 *          PC systems and compatibles from 1981 through fairly recent
 *          system designs based on the PCI bus.
 *
 *          This file is part of the 86Box distribution.
 *
 *          Debug device for assisting in unit testing.
 *          See doc/specifications/86box-unit-tester.md for more info.
 *          If modifying the protocol, you MUST modify the specification
 *          and increment the version number.
 *
 *
 *
 * Authors: GreaseMonkey, <thematrixeatsyou+86b@gmail.com>
 *
 *          Copyright 2024 GreaseMonkey.
 */
#include <stdarg.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>
#define HAVE_STDARG_H
#include <86box/86box.h>
#include <86box/io.h>
#include <86box/plat.h>
#include <86box/unittester.h>
#include <86box/video.h>

enum fsm1_value {
    UT_FSM1_WAIT_8,
    UT_FSM1_WAIT_6,
    UT_FSM1_WAIT_B,
    UT_FSM1_WAIT_o,
    UT_FSM1_WAIT_x,
};
enum fsm2_value {
    UT_FSM2_IDLE,
    UT_FSM2_WAIT_IOBASE_0,
    UT_FSM2_WAIT_IOBASE_1,
};

/* Status bit mask */
#define UT_STATUS_AWAITING_READ   (1 << 0)
#define UT_STATUS_AWAITING_WRITE  (1 << 1)
#define UT_STATUS_IDLE            (1 << 2)
#define UT_STATUS_UNSUPPORTED_CMD (1 << 3)

/* Command list */
enum unittester_cmd {
    UT_CMD_NOOP                             = 0x00,
    UT_CMD_CAPTURE_SCREEN_SNAPSHOT          = 0x01,
    UT_CMD_READ_SCREEN_SNAPSHOT_RECTANGLE   = 0x02,
    UT_CMD_VERIFY_SCREEN_SNAPSHOT_RECTANGLE = 0x03,
    UT_CMD_EXIT                             = 0x04,
};

struct unittester_state {
    /* I/O port settings */
    uint16_t trigger_port;
    uint16_t iobase_port;

    /* Trigger port finite state machines */
    /* FSM1: "86Box" string detection */
    enum fsm1_value fsm1;
    /* FSM2: IOBASE port selection, once trigger is activated */
    enum fsm2_value fsm2;
    uint16_t        fsm2_new_iobase;

    /* Command and data handling state */
    uint8_t             status;
    enum unittester_cmd cmd_id;
    uint32_t            write_offs;
    uint32_t            write_len;
    uint64_t            read_offs;
    uint64_t            read_len;

    /* Screen snapshot state */
    /* Monitor to take snapshot on */
    uint8_t snap_monitor;
    /* Main image width + height */
    uint16_t snap_img_width;
    uint16_t snap_img_height;
    /* Fully overscanned image width + height */
    uint16_t snap_overscan_width;
    uint16_t snap_overscan_height;
    /* Offset of actual image within overscanned area */
    uint16_t snap_img_xoffs;
    uint16_t snap_img_yoffs;

    /* Command-specific state */
    /* 0x02: Read Screen Snapshot Rectangle */
    /* 0x03: Verify Screen Snapshot Rectangle */
    uint16_t read_snap_width;
    uint16_t read_snap_height;
    int16_t  read_snap_xoffs;
    int16_t  read_snap_yoffs;
    uint32_t read_snap_crc;

    /* 0x04: Exit */
    uint8_t exit_code;
};
static struct unittester_state unittester;
static struct unittester_state unittester_defaults = {
    .trigger_port = 0x0080,
    .iobase_port  = 0xFFFF,
    .fsm1         = UT_FSM1_WAIT_8,
    .fsm2         = UT_FSM2_IDLE,
    .status       = UT_STATUS_IDLE,
    .cmd_id       = UT_CMD_NOOP,
};

/* Kept separate, as we will be reusing this object */
static bitmap_t *unittester_screen_buffer = NULL;

static bool unittester_exit_enabled = true;

#ifdef ENABLE_UNITTESTER_LOG
int unittester_do_log = ENABLE_UNITTESTER_LOG;

static void
unittester_log(const char *fmt, ...)
{
    va_list ap;

    if (unittester_do_log) {
        va_start(ap, fmt);
        pclog_ex(fmt, ap);
        va_end(ap);
    }
}
#else
#    define unittester_log(fmt, ...)
#endif

static uint8_t
unittester_read_snap_rect_idx(uint64_t offs)
{
    /* WARNING: If the width is somehow 0 and wasn't caught earlier, you'll probably get a divide by zero crash. */
    uint32_t idx = (offs & 0x3);
    int64_t  x   = (offs >> 2) % unittester.read_snap_width;
    int64_t  y   = (offs >> 2) / unittester.read_snap_width;
    x += unittester.read_snap_xoffs;
    y += unittester.read_snap_yoffs;

    if (x < 0 || y < 0 || x >= unittester.snap_overscan_width || y >= unittester.snap_overscan_height) {
        /* Out of range! */
        return (idx == 3 ? 0xFF : 0x00);
    } else {
        /* In range */
        return (unittester_screen_buffer->line[y][x] & 0x00FFFFFF) >> (idx * 8);
    }
}

static void
unittester_write(uint16_t port, uint8_t val, UNUSED(void *priv))
{
    if (port == unittester.iobase_port + 0x00) {
        /* Command port */
        /* unittester_log("[UT] W %02X Command\n", val); */

        unittester.write_offs = 0;
        unittester.write_len  = 0;
        unittester.read_offs  = 0;
        unittester.read_len   = 0;

        switch (val) {
            /* 0x00: No-op */
            case UT_CMD_NOOP:
                unittester.cmd_id = UT_CMD_NOOP;
                unittester.status = UT_STATUS_IDLE;
                break;

            /* 0x01: Capture Screen Snapshot */
            case UT_CMD_CAPTURE_SCREEN_SNAPSHOT:
                unittester.cmd_id    = UT_CMD_CAPTURE_SCREEN_SNAPSHOT;
                unittester.status    = UT_STATUS_AWAITING_WRITE;
                unittester.write_len = 1;
                break;

            /* 0x02: Read Screen Snapshot Rectangle */
            case UT_CMD_READ_SCREEN_SNAPSHOT_RECTANGLE:
                unittester.cmd_id    = UT_CMD_READ_SCREEN_SNAPSHOT_RECTANGLE;
                unittester.status    = UT_STATUS_AWAITING_WRITE;
                unittester.write_len = 8;
                break;

            /* 0x03: Verify Screen Snapshot Rectangle */
            case UT_CMD_VERIFY_SCREEN_SNAPSHOT_RECTANGLE:
                unittester.cmd_id    = UT_CMD_VERIFY_SCREEN_SNAPSHOT_RECTANGLE;
                unittester.status    = UT_STATUS_AWAITING_WRITE;
                unittester.write_len = 8;
                break;

            /* 0x04: Exit */
            case UT_CMD_EXIT:
                unittester.cmd_id    = UT_CMD_EXIT;
                unittester.status    = UT_STATUS_AWAITING_WRITE;
                unittester.write_len = 1;
                break;

            /* Unsupported command - terminate here */
            default:
                unittester.cmd_id = UT_CMD_NOOP;
                unittester.status = UT_STATUS_IDLE | UT_STATUS_UNSUPPORTED_CMD;
                break;
        }

    } else if (port == unittester.iobase_port + 0x01) {
        /* Data port */
        /* unittester_log("[UT] W %02X Data\n", val); */

        /* Skip if not awaiting */
        if ((unittester.status & UT_STATUS_AWAITING_WRITE) == 0)
            return;

        switch (unittester.cmd_id) {
            case UT_CMD_EXIT:
                switch (unittester.write_offs) {
                    case 0:
                        unittester.exit_code = val;
                        break;
                    default:
                        break;
                }
                break;

            case UT_CMD_CAPTURE_SCREEN_SNAPSHOT:
                switch (unittester.write_offs) {
                    case 0:
                        unittester.snap_monitor = val;
                        break;
                    default:
                        break;
                }
                break;

            case UT_CMD_READ_SCREEN_SNAPSHOT_RECTANGLE:
            case UT_CMD_VERIFY_SCREEN_SNAPSHOT_RECTANGLE:
                switch (unittester.write_offs) {
                    case 0:
                        unittester.read_snap_width = (uint16_t) val;
                        break;
                    case 1:
                        unittester.read_snap_width |= ((uint16_t) val) << 8;
                        break;
                    case 2:
                        unittester.read_snap_height = (uint16_t) val;
                        break;
                    case 3:
                        unittester.read_snap_height |= ((uint16_t) val) << 8;
                        break;
                    case 4:
                        unittester.read_snap_xoffs = (uint16_t) val;
                        break;
                    case 5:
                        unittester.read_snap_xoffs |= ((uint16_t) val) << 8;
                        break;
                    case 6:
                        unittester.read_snap_yoffs = (uint16_t) val;
                        break;
                    case 7:
                        unittester.read_snap_yoffs |= ((uint16_t) val) << 8;
                        break;
                    default:
                        break;
                }
                break;

            /* This should not be reachable, but just in case... */
            default:
                break;
        }

        /* Advance write buffer */
        unittester.write_offs += 1;
        if (unittester.write_offs >= unittester.write_len) {
            unittester.status &= ~UT_STATUS_AWAITING_WRITE;
            /* Determine what we're doing here based on the command. */
            switch (unittester.cmd_id) {
                case UT_CMD_EXIT:
                    unittester_log("[UT] Exit received - code = %02X\n", unittester.exit_code);

                    /* CHECK: Do we actually exit? */
                    if (unittester_exit_enabled) {
                        /* Yes - call exit! */
                        /* Clamp exit code */
                        if (unittester.exit_code > 0x7F)
                            unittester.exit_code = 0x7F;

                        /* Exit somewhat quickly! */
                        unittester_log("[UT] Exit enabled, exiting with code %02X\n", unittester.exit_code);
                        exit(unittester.exit_code);

                    } else {
                        /* No - report successful command completion and continue program execution */
                        unittester_log("[UT] Exit disabled, continuing execution\n");
                    }
                    unittester.cmd_id = UT_CMD_NOOP;
                    unittester.status = UT_STATUS_IDLE;
                    break;

                case UT_CMD_CAPTURE_SCREEN_SNAPSHOT:
                    /* Recompute screen */
                    unittester.snap_img_width       = 0;
                    unittester.snap_img_height      = 0;
                    unittester.snap_img_xoffs       = 0;
                    unittester.snap_img_yoffs       = 0;
                    unittester.snap_overscan_width  = 0;
                    unittester.snap_overscan_height = 0;
                    if (unittester.snap_monitor < 0x01 || (unittester.snap_monitor - 1) > MONITORS_NUM) {
                        /* No monitor here - clear snapshot */
                        unittester.snap_monitor = 0x00;
                    } else if (video_get_type_monitor(unittester.snap_monitor - 1) == VIDEO_FLAG_TYPE_NONE) {
                        /* Monitor disabled - clear snapshot */
                        unittester.snap_monitor = 0x00;
                    } else {
                        /* Compute bounds for snapshot */
                        const monitor_t *m              = &monitors[unittester.snap_monitor - 1];
                        unittester.snap_img_width       = m->mon_xsize;
                        unittester.snap_img_height      = m->mon_ysize;
                        unittester.snap_overscan_width  = m->mon_xsize + m->mon_overscan_x;
                        unittester.snap_overscan_height = m->mon_ysize + m->mon_overscan_y;
                        unittester.snap_img_xoffs       = (m->mon_overscan_x >> 1);
                        unittester.snap_img_yoffs       = (m->mon_overscan_y >> 1);
                        /* Take snapshot */
                        for (size_t y = 0; y < unittester.snap_overscan_height; y++) {
                            for (size_t x = 0; x < unittester.snap_overscan_width; x++) {
                                unittester_screen_buffer->line[y][x] = m->target_buffer->line[y][x];
                            }
                        }
                    }

                    /* We have 12 bytes to read. */
                    unittester_log("[UT] Screen snapshot - image %d x %d @ (%d, %d) in overscan %d x %d\n",
                                   unittester.snap_img_width,
                                   unittester.snap_img_height,
                                   unittester.snap_img_xoffs,
                                   unittester.snap_img_yoffs,
                                   unittester.snap_overscan_width,
                                   unittester.snap_overscan_height);
                    unittester.status   = UT_STATUS_AWAITING_READ;
                    unittester.read_len = 12;
                    break;

                case UT_CMD_READ_SCREEN_SNAPSHOT_RECTANGLE:
                case UT_CMD_VERIFY_SCREEN_SNAPSHOT_RECTANGLE:
                    /* Offset the X,Y offsets by the overscan offsets. */
                    unittester.read_snap_xoffs += (int16_t) unittester.snap_img_xoffs;
                    unittester.read_snap_yoffs += (int16_t) unittester.snap_img_yoffs;
                    /* NOTE: Width * Height * 4 can potentially exceed a 32-bit number.
                       So, we use 64-bit numbers instead.
                       In practice, this will only happen if someone decides to request e.g. a 65535 x 65535 image,
                       of which most of the pixels will be out of range anyway.
                      */
                    unittester.read_len      = ((uint64_t) unittester.read_snap_width) * ((uint64_t) unittester.read_snap_height) * 4;
                    unittester.read_snap_crc = 0xFFFFFFFF;

                    unittester_log("[UT] Screen rectangle analysis - %d x %d @ (%d, %d)\n",
                                   unittester.read_snap_width,
                                   unittester.read_snap_height,
                                   unittester.read_snap_xoffs - (int16_t) unittester.snap_img_xoffs,
                                   unittester.read_snap_yoffs - (int16_t) unittester.snap_img_yoffs);

                    if (unittester.cmd_id == UT_CMD_VERIFY_SCREEN_SNAPSHOT_RECTANGLE) {
                        /* Read everything and compute CRC */
                        uint32_t crc = 0xFFFFFFFF;
                        for (uint64_t i = 0; i < unittester.read_len; i++) {
                            crc ^= 0xFF & (uint32_t) unittester_read_snap_rect_idx(i);
                            /* Use some bit twiddling until we have a table-based fast CRC-32 implementation */
                            for (uint32_t j = 0; j < 8; j++) {
                                crc = (crc >> 1) ^ ((-(crc & 0x1)) & 0xEDB88320);
                            }
                        }
                        unittester.read_snap_crc = crc ^ 0xFFFFFFFF;

                        unittester_log("[UT] Screen rectangle analysis CRC = %08X\n",
                                       unittester.read_snap_crc);

                        /* Set actual read length for CRC result */
                        unittester.read_len = 4;
                        unittester.status   = UT_STATUS_AWAITING_READ;

                    } else {
                        /* Do we have anything to read? */
                        if (unittester.read_len >= 1) {
                            /* Yes - start reads! */
                            unittester.status = UT_STATUS_AWAITING_READ;
                        } else {
                            /* No - stop here. */
                            unittester.cmd_id = UT_CMD_NOOP;
                            unittester.status = UT_STATUS_IDLE;
                        }
                    }
                    break;

                default:
                    /* Nothing to write? Stop here. */
                    unittester.cmd_id = UT_CMD_NOOP;
                    unittester.status = UT_STATUS_IDLE;
                    break;
            }
        }

    } else {
        /* Not handled here - possibly open bus! */
    }
}

static uint8_t
unittester_read(uint16_t port, UNUSED(void *priv))
{
    uint8_t outval = 0xFF;

    if (port == unittester.iobase_port + 0x00) {
        /* Status port */
        /* unittester_log("[UT] R -- Status = %02X\n", unittester.status); */
        return unittester.status;
    } else if (port == unittester.iobase_port + 0x01) {
        /* Data port */
        /* unittester_log("[UT] R -- Data\n"); */

        /* Skip if not awaiting */
        if ((unittester.status & UT_STATUS_AWAITING_READ) == 0)
            return 0xFF;

        switch (unittester.cmd_id) {
            case UT_CMD_CAPTURE_SCREEN_SNAPSHOT:
                switch (unittester.read_offs) {
                    case 0:
                        outval = (uint8_t) (unittester.snap_img_width);
                        break;
                    case 1:
                        outval = (uint8_t) (unittester.snap_img_width >> 8);
                        break;
                    case 2:
                        outval = (uint8_t) (unittester.snap_img_height);
                        break;
                    case 3:
                        outval = (uint8_t) (unittester.snap_img_height >> 8);
                        break;
                    case 4:
                        outval = (uint8_t) (unittester.snap_overscan_width);
                        break;
                    case 5:
                        outval = (uint8_t) (unittester.snap_overscan_width >> 8);
                        break;
                    case 6:
                        outval = (uint8_t) (unittester.snap_overscan_height);
                        break;
                    case 7:
                        outval = (uint8_t) (unittester.snap_overscan_height >> 8);
                        break;
                    case 8:
                        outval = (uint8_t) (unittester.snap_img_xoffs);
                        break;
                    case 9:
                        outval = (uint8_t) (unittester.snap_img_xoffs >> 8);
                        break;
                    case 10:
                        outval = (uint8_t) (unittester.snap_img_yoffs);
                        break;
                    case 11:
                        outval = (uint8_t) (unittester.snap_img_yoffs >> 8);
                        break;
                    default:
                        break;
                }
                break;

            case UT_CMD_READ_SCREEN_SNAPSHOT_RECTANGLE:
                outval = unittester_read_snap_rect_idx(unittester.read_offs);
                break;

            case UT_CMD_VERIFY_SCREEN_SNAPSHOT_RECTANGLE:
                outval = (uint8_t) (unittester.read_snap_crc >> (8 * unittester.read_offs));
                break;

            /* This should not be reachable, but just in case... */
            default:
                break;
        }

        /* Advance read buffer */
        unittester.read_offs += 1;
        if (unittester.read_offs >= unittester.read_len) {
            /* Once fully read, we stop here. */
            unittester.cmd_id = UT_CMD_NOOP;
            unittester.status = UT_STATUS_IDLE;
        }

        return outval;
    } else {
        /* Not handled here - possibly open bus! */
        return 0xFF;
    }
}

static void
unittester_trigger_write(UNUSED(uint16_t port), uint8_t val, UNUSED(void *priv))
{
    /* This one gets quite spammy. */
    /* unittester_log("[UT] Trigger value %02X -> FSM1 = %02X, FSM2 = %02X, IOBASE = %04X\n", val, unittester.fsm1, unittester.fsm2, unittester.iobase_port); */

    /* Update FSM2 */
    switch (unittester.fsm2) {
        /* IDLE: Do nothing - FSM1 will put us in the right state. */
        case UT_FSM2_IDLE:
            unittester.fsm2 = UT_FSM2_IDLE;
            break;

        /* WAIT IOBASE 0: Set low byte of temporary IOBASE. */
        case UT_FSM2_WAIT_IOBASE_0:
            unittester.fsm2_new_iobase = ((uint16_t) val);
            unittester.fsm2            = UT_FSM2_WAIT_IOBASE_1;
            break;

        /* WAIT IOBASE 0: Set high byte of temporary IOBASE and commit to the real IOBASE. */
        case UT_FSM2_WAIT_IOBASE_1:
            unittester.fsm2_new_iobase |= ((uint16_t) val) << 8;

            unittester_log("[UT] Remapping IOBASE: %04X -> %04X\n", unittester.iobase_port, unittester.fsm2_new_iobase);

            /* Unmap old IOBASE */
            if (unittester.iobase_port != 0xFFFF)
                io_removehandler(unittester.iobase_port, 2, unittester_read, NULL, NULL, unittester_write, NULL, NULL, NULL);
            unittester.iobase_port = 0xFFFF;

            /* Map new IOBASE */
            unittester.iobase_port = unittester.fsm2_new_iobase;
            if (unittester.iobase_port != 0xFFFF)
                io_sethandler(unittester.iobase_port, 2, unittester_read, NULL, NULL, unittester_write, NULL, NULL, NULL);

            /* Reset FSM2 to IDLE */
            unittester.fsm2 = UT_FSM2_IDLE;
            break;
    }

    /* Update FSM1 */
    switch (val) {
        case '8':
            unittester.fsm1 = UT_FSM1_WAIT_6;
            break;
        case '6':
            if (unittester.fsm1 == UT_FSM1_WAIT_6)
                unittester.fsm1 = UT_FSM1_WAIT_B;
            else
                unittester.fsm1 = UT_FSM1_WAIT_8;
            break;
        case 'B':
            if (unittester.fsm1 == UT_FSM1_WAIT_B)
                unittester.fsm1 = UT_FSM1_WAIT_o;
            else
                unittester.fsm1 = UT_FSM1_WAIT_8;
            break;
        case 'o':
            if (unittester.fsm1 == UT_FSM1_WAIT_o)
                unittester.fsm1 = UT_FSM1_WAIT_x;
            else
                unittester.fsm1 = UT_FSM1_WAIT_8;
            break;
        case 'x':
            if (unittester.fsm1 == UT_FSM1_WAIT_x) {
                unittester_log("[UT] Config activated, awaiting new IOBASE\n");
                unittester.fsm2 = UT_FSM2_WAIT_IOBASE_0;
            }
            unittester.fsm1 = UT_FSM1_WAIT_8;
            break;

        default:
            unittester.fsm1 = UT_FSM1_WAIT_8;
            break;
    }
}

static void *
unittester_init(UNUSED(const device_t *info))
{
    unittester = unittester_defaults;

    unittester_exit_enabled = !!device_get_config_int("exit_enabled");

    if (unittester_screen_buffer == NULL)
        unittester_screen_buffer = create_bitmap(2048, 2048);

    io_sethandler(unittester.trigger_port, 1, NULL, NULL, NULL, unittester_trigger_write, NULL, NULL, NULL);

    unittester_log("[UT] 86Box Unit Tester initialised\n");

    return &unittester; /* Dummy non-NULL value */
}

static void
unittester_close(UNUSED(void *priv))
{
    io_removehandler(unittester.trigger_port, 1, NULL, NULL, NULL, unittester_trigger_write, NULL, NULL, NULL);

    if (unittester.iobase_port != 0xFFFF)
        io_removehandler(unittester.iobase_port, 2, unittester_read, NULL, NULL, unittester_write, NULL, NULL, NULL);
    unittester.iobase_port = 0xFFFF;

    if (unittester_screen_buffer != NULL) {
        destroy_bitmap(unittester_screen_buffer);
        unittester_screen_buffer = NULL;
    }

    unittester_log("[UT] 86Box Unit Tester closed\n");
}

static const device_config_t unittester_config[] = {
  // clang-format off
    {
        .name           = "exit_enabled",
        .description    = "Enable 0x04 \"Exit 86Box\" command",
        .type           = CONFIG_BINARY,
        .default_int    = 1,
        .default_string = NULL,
        .file_filter    = NULL,
        .spinner        = { 0 },
        .selection      = { { 0 } },
        .bios           = { { 0 } }
    },
    { .name = "", .description = "", .type = CONFIG_END }
  // clang-format on
};

const device_t unittester_device = {
    .name          = "86Box Unit Tester",
    .internal_name = "unittester",
    .flags         = DEVICE_ISA,
    .local         = 0,
    .init          = unittester_init,
    .close         = unittester_close,
    .reset         = NULL,
    .available     = NULL,
    .speed_changed = NULL,
    .force_redraw  = NULL,
    .config        = unittester_config,
};
