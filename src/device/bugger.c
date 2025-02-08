/*
 * 86Box    A hypervisor and IBM PC system emulator that specializes in
 *          running old operating systems and software designed for IBM
 *          PC systems and compatibles from 1981 through fairly recent
 *          system designs based on the PCI bus.
 *
 *          This file is part of the 86Box distribution.
 *
 *          Implementation of the ISA Bus (de)Bugger expansion card
 *          sold as a DIY kit in the late 1980's in The Netherlands.
 *          This card was a assemble-yourself 8bit ISA addon card for
 *          PC and AT systems that had several tools to aid in low-
 *          level debugging (mostly for faulty BIOSes, bootloaders
 *          and system kernels...)
 *
 *          The standard version had a total of 16 LEDs (8 RED, plus
 *          8 GREEN), two 7-segment displays and one 8-position DIP
 *          switch block on board for use as debugging tools.
 *
 *          The "Plus" version, added an extra 2 7-segment displays,
 *          as well as a very simple RS-232 serial interface that
 *          could be used as a mini-console terminal.
 *
 *          Two I/O ports were used; one for control, at offset 0 in
 *          I/O space, and one for data, at offset 1 in I/O space.
 *          Both registers could be read from and written to. Although
 *          the author has a vague memory of a DIP switch to set the
 *          board's I/O address, comments in old software seems to
 *          indicate that it was actually fixed to 0x7A (and 0x7B.)
 *
 *          A READ on the data register always returned the actual
 *          state of the DIP switch. Writing data to the LEDs was done
 *          in two steps.. first, the block number (RED or GREEN) was
 *          written to the CTRL register, and then the actual LED data
 *          was written to the DATA register. Likewise, data for the
 *          7-segment displays was written.
 *
 *          The serial port was a bit different, and its operation is
 *          not verified, but two extra bits in the control register
 *          were used to set up parameters, and also the actual data
 *          input and output.
 *
 * TODO:    Still have to implement the RS232 Serial Port Parameters
 *          configuration register (CTRL_SPCFG bit set) but have to
 *          remember that stuff first...
 *
 *
 *
 * Authors: Fred N. van Kempen, <decwiz@yahoo.com>
 *          Copyright 1989-2018 Fred N. van Kempen.
 */
#include <stdarg.h>
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <wchar.h>
#define HAVE_STDARG_H
#include <86box/86box.h>
#include <86box/io.h>
#include <86box/device.h>
#include <86box/plat.h>
#include <86box/ui.h>
#include <86box/bugger.h>

/* BugBugger registers. */
#define BUG_CTRL   0
#define CTRL_RLED  0x00 /* write to the RED LED block */
#define CTRL_GLED  0x01 /* write to the GREEN LED block */
#define CTRL_SEG1  0x02 /* write to the RIGHT 7SEG displays */
#define CTRL_SEG2  0x04 /* write to the LEFT 7SEG displays */
#define CTRL_SPORT 0x20 /* enable the serial port */
#define CTRL_SPCFG 0x40 /* set up the serial port */
#define CTRL_INIT  0x80 /* enable and reset the card */
#define CTRL_RESET 0xff /* this resets the board */
#define BUG_DATA   1

static uint8_t bug_ctrl;  /* control register */
static uint8_t bug_data;  /* data register */
static uint8_t bug_ledr;  /* RED LEDs */
static uint8_t bug_ledg;  /* GREEN LEDs */
static uint8_t bug_seg1;
static uint8_t bug_seg2;  /* LEFT and RIGHT 7SEG displays */
static uint8_t bug_spcfg; /* serial port configuration */
#define FIFO_LEN 256
static uint8_t  bug_buff[FIFO_LEN]; /* serial port data buffer */
static uint8_t *bug_bptr;
#define UISTR_LEN 24
static char bug_str[UISTR_LEN]; /* UI output string */

extern void ui_sb_bugui(char *__str);

#ifdef ENABLE_BUGGER_LOG
int bugger_do_log = ENABLE_BUGGER_LOG;

static void
bugger_log(const char *fmt, ...)
{
    va_list ap;

    if (bugger_do_log) {
        va_start(ap, fmt);
        pclog_ex(fmt, ap);
        va_end(ap);
    }
}
#else
#    define bugger_log(fmt, ...)
#endif

/* Update the system's UI with the actual Bugger status. */
static void
bug_setui(void)
{
    /* Format all current info in a string. */
    sprintf(bug_str, "%02X:%02X %c%c%c%c%c%c%c%c-%c%c%c%c%c%c%c%c",
            bug_seg2, bug_seg1,
            (bug_ledg & 0x80) ? 'G' : 'g', (bug_ledg & 0x40) ? 'G' : 'g',
            (bug_ledg & 0x20) ? 'G' : 'g', (bug_ledg & 0x10) ? 'G' : 'g',
            (bug_ledg & 0x08) ? 'G' : 'g', (bug_ledg & 0x04) ? 'G' : 'g',
            (bug_ledg & 0x02) ? 'G' : 'g', (bug_ledg & 0x01) ? 'G' : 'g',
            (bug_ledr & 0x80) ? 'R' : 'r', (bug_ledr & 0x40) ? 'R' : 'r',
            (bug_ledr & 0x20) ? 'R' : 'r', (bug_ledr & 0x10) ? 'R' : 'r',
            (bug_ledr & 0x08) ? 'R' : 'r', (bug_ledr & 0x04) ? 'R' : 'r',
            (bug_ledr & 0x02) ? 'R' : 'r', (bug_ledr & 0x01) ? 'R' : 'r');

    /* Send formatted string to the UI. */
    ui_sb_bugui(bug_str);
}

/* Flush the serial port. */
static void
bug_spflsh(void)
{
    *bug_bptr = '\0';
    bugger_log("BUGGER- serial port [%s]\n", bug_buff);
    bug_bptr = bug_buff;
}

/* Handle a write to the Serial Port Data register. */
static void
bug_wsport(uint8_t val)
{
    uint8_t old = bug_ctrl;

    /* Clear the SPORT bit to indicate we are busy. */
    bug_ctrl &= ~CTRL_SPORT;

    /* Delay while processing byte.. */
    if (bug_bptr == &bug_buff[FIFO_LEN - 1]) {
        /* Buffer full, gotta flush. */
        bug_spflsh();
    }

    /* Write (store) the byte. */
    *bug_bptr++ = val;

    /* Restore the SPORT bit. */
    bug_ctrl |= (old & CTRL_SPORT);

    bugger_log("BUGGER- sport %02x\n", val);
}

/* Handle a write to the Serial Port Configuration register. */
static void
bug_wspcfg(uint8_t val)
{
    bug_spcfg = val;

    bugger_log("BUGGER- spcfg %02x\n", bug_spcfg);
}

/* Handle a write to the control register. */
static void
bug_wctrl(uint8_t val)
{
    if (val == CTRL_RESET) {
        /* User wants us to reset. */
        bug_ctrl  = CTRL_INIT;
        bug_spcfg = 0x00;
        bug_bptr  = NULL;
    } else {
        /* If turning off the serial port, flush it. */
        if ((bug_ctrl & CTRL_SPORT) && !(val & CTRL_SPORT))
            bug_spflsh();

        /* FIXME: did they do this using an XOR of operation bits?  --FvK */

        if (val & CTRL_SPCFG) {
            /* User wants to configure the serial port. */
            bug_ctrl &= ~(CTRL_SPORT | CTRL_SEG2 | CTRL_SEG1 | CTRL_GLED);
            bug_ctrl |= CTRL_SPCFG;
        } else if (val & CTRL_SPORT) {
            /* User wants to talk to the serial port. */
            bug_ctrl &= ~(CTRL_SPCFG | CTRL_SEG2 | CTRL_SEG1 | CTRL_GLED);
            bug_ctrl |= CTRL_SPORT;
            if (bug_bptr == NULL)
                bug_bptr = bug_buff;
        } else if (val & CTRL_SEG2) {
            /* User selected SEG2 (LEFT, Plus only) for output. */
            bug_ctrl &= ~(CTRL_SPCFG | CTRL_SPORT | CTRL_SEG1 | CTRL_GLED);
            bug_ctrl |= CTRL_SEG2;
        } else if (val & CTRL_SEG1) {
            /* User selected SEG1 (RIGHT) for output. */
            bug_ctrl &= ~(CTRL_SPCFG | CTRL_SPORT | CTRL_SEG2 | CTRL_GLED);
            bug_ctrl |= CTRL_SEG1;
        } else if (val & CTRL_GLED) {
            /* User selected the GREEN LEDs for output. */
            bug_ctrl &= ~(CTRL_SPCFG | CTRL_SPORT | CTRL_SEG2 | CTRL_SEG1);
            bug_ctrl |= CTRL_GLED;
        } else {
            /* User selected the RED LEDs for output. */
            bug_ctrl &= ~(CTRL_SPCFG | CTRL_SPORT | CTRL_SEG2 | CTRL_SEG1 | CTRL_GLED);
        }
    }

    /* Update the UI with active settings. */
    bugger_log("BUGGER- ctrl %02x\n", bug_ctrl);
    bug_setui();
}

/* Handle a write to the data register. */
static void
bug_wdata(uint8_t val)
{
    bug_data = val;

    if (bug_ctrl & CTRL_SPCFG)
        bug_wspcfg(val);
    else if (bug_ctrl & CTRL_SPORT)
        bug_wsport(val);
    else {
        if (bug_ctrl & CTRL_SEG2)
            bug_seg2 = val;
        else if (bug_ctrl & CTRL_SEG1)
            bug_seg1 = val;
        else if (bug_ctrl & CTRL_GLED)
            bug_ledg = val;
        else
            bug_ledr = val;

        bugger_log("BUGGER- data %02x\n", bug_data);
    }

    /* Update the UI with active settings. */
    bug_setui();
}

/* Reset the ISA BusBugger controller. */
static void
bug_reset(void)
{
    /* Clear the data register. */
    bug_data = 0x00;

    /* Clear the RED and GREEN LEDs. */
    bug_ledr = 0x00;
    bug_ledg = 0x00;

    /* Clear both 7SEG displays. */
    bug_seg1 = 0x00;
    bug_seg2 = 0x00;

    /* Reset the control register (updates UI.) */
    bug_wctrl(CTRL_RESET);
}

/* Handle a WRITE operation to one of our registers. */
static void
bug_write(uint16_t port, uint8_t val, UNUSED(void *priv))
{
    switch (port - BUGGER_ADDR) {
        case BUG_CTRL: /* control register */
            if (val == CTRL_RESET) {
                /* Perform a full reset. */
                bug_reset();
            } else if (bug_ctrl & CTRL_INIT) {
                /* Only allow writes if initialized. */
                bug_wctrl(val);
            }
            break;

        case BUG_DATA: /* data register */
            if (bug_ctrl & CTRL_INIT) {
                bug_wdata(val);
            }
            break;
        default:
            break;
    }
}

/* Handle a READ operation from one of our registers. */
static uint8_t
bug_read(uint16_t port, UNUSED(void *priv))
{
    uint8_t ret = 0xff;

    if (bug_ctrl & CTRL_INIT)
        switch (port - BUGGER_ADDR) {
            case BUG_CTRL: /* control register */
                ret = bug_ctrl;
                break;

            case BUG_DATA: /* data register */
                if (bug_ctrl & CTRL_SPCFG) {
                    ret = bug_spcfg;
                } else if (bug_ctrl & CTRL_SPORT) {
                    ret = 0x00; /* input not supported */
                } else {
                    /* Just read the DIP switch. */
                    ret = bug_data;
                }
                break;

            default:
                break;
        }

    return ret;
}

/* Initialize the ISA BusBugger emulator. */
static void *
bug_init(UNUSED(const device_t *info))
{
    bugger_log("%s, I/O=%04x\n", info->name, BUGGER_ADDR);

    /* Initialize local registers. */
    bug_reset();

    io_sethandler(BUGGER_ADDR, BUGGER_ADDRLEN,
                  bug_read, NULL, NULL, bug_write, NULL, NULL, NULL);

    /* Just so its not NULL. */
    return (&bug_ctrl);
}

/* Remove the ISA BusBugger emulator from the system. */
static void
bug_close(UNUSED(void *priv))
{
    io_removehandler(BUGGER_ADDR, BUGGER_ADDRLEN,
                     bug_read, NULL, NULL, bug_write, NULL, NULL, NULL);
}

const device_t bugger_device = {
    .name          = "ISA/PCI Bus Bugger",
    .internal_name = "bugger",
    .flags         = DEVICE_ISA | DEVICE_AT,
    .local         = 0,
    .init          = bug_init,
    .close         = bug_close,
    .reset         = NULL,
    .available     = NULL,
    .speed_changed = NULL,
    .force_redraw  = NULL,
    .config        = NULL
};
