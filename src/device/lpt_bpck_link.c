/*
 * 86Box    A hypervisor and IBM PC system emulator that specializes in
 *          running old operating systems and software designed for IBM
 *          PC systems and compatibles from 1981 through fairly recent
 *          system designs based on the PCI bus.
 *
 *          This file is part of the 86Box distribution.
 *
 *          The MicroSolutions BackPack wire protocol: the connect knock and
 *          address probe, register addressing, nibble, byte and EPP
 *          transfers. The BackPack CD-ROM and hard disk pods (lpt_bpck.c)
 *          and the Iomega Ditto (lpt_ditto.c) put their register files
 *          behind it.
 *
 * Authors: Dmitry Brant, <me@dmitrybrant.com>
 *          Mike Lycett
 *          Mario Fetka
 *
 *          Copyright 2026 Dmitry Brant.
 *          Copyright 2026 Mike Lycett.
 *          Copyright 2026 Mario Fetka.
 */
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <wchar.h>
#include <86box/86box.h>
#include <86box/device.h>
#include <86box/timer.h>
#include <86box/lpt.h>
#include <86box/lpt_bpck_link.h>

/*
   Port register bits at their literal values; the inversion at the
   connector applies on top, and both ends work in terms of the register.
 */
#define LPT_CTRL_STROBE 0x01
#define LPT_CTRL_AUTOFD 0x02
#define LPT_CTRL_INIT   0x04
#define LPT_CTRL_SELECT 0x08

/*
   The four bits that reach the connector. Compare against them masked:
   the host keeps 0xc0 in its control shadow and writes it back to us.
 */
#define LPT_CTRL_LINES  0x0f

#define LPT_STAT_ACK    0x40
#define LPT_STAT_BUSY   0x80

/* Only the top five bits of the status register belong to the device -
   the parallel port makes the low three up for itself. */
#define LPT_STAT_MASK   0xf8

/*
   What the status lines read as before the pod drives them. Not zero:
   that reads as every line asserted at once, and a host that looks at
   the port before knocking decides there is nothing here.
 */
#define LPT_STAT_IDLE   0xd8

const char *bpck_proto_name[] = { "SPP 4-bit", "PS/2 8-bit", "EPP-8", "EPP-16", "EPP-32" };

#define LINK_LOG(link, ...) \
    do { \
        if ((link)->log != NULL) \
            (link)->log(__VA_ARGS__); \
    } while (0)

const char *
bpck_link_state(const bpck_link_t *link)
{
    static char buf[64];

    snprintf(buf, sizeof(buf), "%s reg=%02X knock=%d%s",
             link->connected ? "CONN" : "----", link->cur_reg, link->knock,
             link->latching ? " latch" : "");

    return buf;
}

static void
link_activity(bpck_link_t *link, int status)
{
    if (link->ops->activity != NULL)
        link->ops->activity(link->priv, status);
}

/* A register is addressed: the byte stream starts over. */
static void
link_address(bpck_link_t *link, int reg)
{
    link->cur_reg = reg;
    link->rd_high = 0;

    if (link->ops->address != NULL)
        link->ops->address(link->priv);
}

static uint8_t
link_encode_nibble(uint8_t val)
{
    return (uint8_t) (((val & 0x07) << 3) | ((val & 0x08) << 4));
}

/* The bridge answers with which family of protocols it is set for. */
static uint8_t
link_connect_response(const bpck_link_t *link)
{
    return (uint8_t) ((link->proto >= BPCK_PROTO_EPP8)
                          ? (LPT_STAT_ACK | LPT_STAT_BUSY)
                          : LPT_STAT_ACK);
}

/*
   The address probe that follows every knock: the unit number, and the
   complement of it when AUTOFD is low. The host requires the two
   readings to be complements - an empty port cannot do that - and only
   then checks the number. It rides alongside the protocol answer
   because another host driver reads the same status and wants only
   that.
 */
static uint8_t
link_ident_response(const bpck_link_t *link)
{
    const uint8_t id = (link->ctrl & LPT_CTRL_AUTOFD)
                           ? link->unit
                           : (uint8_t) ~link->unit;

    return (uint8_t) (link_connect_response(link) | ((id & 0x07) << 3));
}

static void
link_connect(bpck_link_t *link)
{
    link->connected = 1;
    link->knock     = 0;
    link->ident     = 1;
    link->rd_high   = 0;
    link->rd_out    = 0x00;

    /* A pod may switch protocols here, so the answer comes after it. */
    if (link->ops->connect != NULL)
        link->ops->connect(link->priv);

    link->rd_out = link_connect_response(link);

    LINK_LOG(link, "%s: connected in %s mode\n", link->name, bpck_proto_name[link->proto]);
}

static void
link_disconnect(bpck_link_t *link)
{
    if (!link->connected)
        return;

    link->connected = 0;
    link->knock     = 0;
    link->ident     = 0;
    link->latching  = 0;
    link->rd_out    = 0x00;

    if (link->ops->disconnect != NULL)
        link->ops->disconnect(link->priv);

    LINK_LOG(link, "%s: disconnected\n", link->name);
}

/*
   Whether the bridge will talk in the mode it is programmed for. Held
   back by the ceiling it answers all ones, which breaks the host's self
   test - which is how it is meant to find out and settle on less.
 */
int
bpck_link_usable(const bpck_link_t *link)
{
    return link->connected && (link->proto <= link->max_proto);
}

/* The bridge drives the data lines, during a PS/2 or EPP read. */
static void
link_drive_data(bpck_link_t *link, uint8_t val)
{
    link->dat_out = val;
    if (link->lpt != NULL)
        lpt_write_to_dat(link->lpt, val);
}

/*
   The next nibble of the addressed register. Every second toggle starts
   a fresh byte, so a host that keeps toggling streams the register out.
 */
static void
link_advance_read(bpck_link_t *link)
{
    uint8_t val;

    if (!bpck_link_usable(link)) {
        LINK_LOG(link, "%s: read refused in %s mode\n", link->name, bpck_proto_name[link->proto]);
        link->rd_out  = link_encode_nibble(0x0f);
        link->rd_high = !link->rd_high;
        link_drive_data(link, 0xff);
        return;
    }

    /*
       In byte mode the bridge drives the data lines rather than the status
       bits, so a whole byte comes back per toggle.
     */
    if (link->proto == BPCK_PROTO_PS2) {
        val          = link->ops->read_reg(link->priv, link->cur_reg);
        link->rd_out = 0x00;
        link_drive_data(link, val);
        return;
    }

    if (link->rd_high) {
        link->rd_out  = link_encode_nibble(link->rd_val >> 4);
        link->rd_high = 0;
    } else {
        link->rd_val  = link->ops->read_reg(link->priv, link->cur_reg);
        link->rd_out  = link_encode_nibble(link->rd_val & 0x0f);
        link->rd_high = 1;
    }
}

/*
   EPP moves addressing and data onto the port's own registers, so none
   of the toggling applies. A wide read arrives as consecutive byte
   reads, which is what the bridge would see too.
 */
static void
link_epp_write_data(uint8_t is_addr, uint8_t val, void *priv)
{
    bpck_link_t *link = (bpck_link_t *) priv;

    if (!bpck_link_usable(link) || (link->proto < BPCK_PROTO_EPP8)) {
        LINK_LOG(link, "%s: EPP write ignored in %s mode\n", link->name,
                 bpck_proto_name[link->proto]);
        return;
    }

    if (is_addr) {
        LINK_LOG(link, "%s: W3 %02X (EPP address)\n", link->name, val);
        link_address(link, val);
    } else {
        LINK_LOG(link, "%s: W4 %02X (EPP data)\n", link->name, val);
        link->ops->write_reg(link->priv, link->cur_reg, val);
    }
}

static void
link_epp_request_read(uint8_t is_addr, void *priv)
{
    bpck_link_t *link = (bpck_link_t *) priv;
    uint8_t      val  = 0xff;

    LINK_LOG(link, "%s: R%d (EPP %s)\n", link->name, is_addr ? 3 : 4,
             is_addr ? "address" : "data");

    if (!is_addr && bpck_link_usable(link) && (link->proto >= BPCK_PROTO_EPP8))
        val = link->ops->read_reg(link->priv, link->cur_reg);

    link_drive_data(link, val);
}

static void
link_write_data(uint8_t val, void *priv)
{
    bpck_link_t  *link = (bpck_link_t *) priv;
    const uint8_t old  = link->dat;

    link_activity(link, 0);

    LINK_LOG(link, "%s: W0 %02X            [%s]\n", link->name, val, bpck_link_state(link));

    link->dat = val;

    /*
       An address on the data lines starts a knock, so toggles counted before
       it were not part of one. Counting from a data write is safe where
       arming on one is not: the port calls us for every write, changed or
       not. Without this the knock completes an edge early.
     */
    link->knock = 0;

    if (link->ops->data != NULL)
        link->ops->data(link->priv, val, old);

    /*
       The bridge drives the data lines only during a PS/2 or EPP read;
       otherwise the host reads its own latch back, so keep the port's input
       register tracking what was written or it sees zeroes and misjudges
       what the port can do.
     */
    if (link->lpt != NULL)
        lpt_write_to_dat(link->lpt, val);
}

static void
link_write_ctrl(uint8_t val, void *priv)
{
    bpck_link_t  *link      = (bpck_link_t *) priv;
    const uint8_t old       = link->ctrl;
    const uint8_t chg       = (uint8_t) ((old ^ val) & LPT_CTRL_LINES);
    const uint8_t lines     = (uint8_t) (val & LPT_CTRL_LINES);
    const uint8_t old_lines = (uint8_t) (old & LPT_CTRL_LINES);

    link_activity(link, 0);

    LINK_LOG(link, "%s: W2 %02X (was %02X)  [%s]\n", link->name, val, old, bpck_link_state(link));

    link->ctrl = val;

    /* The bridge latches on edges. Rewriting the same value is nothing. */
    if (chg == 0x00)
        return;

    /*
       An out-of-band register write, addressed off the data lines with
       SELECT raised. A host forces a protocol and seeds the EPP self test
       this way, and they are one mechanism: SELECT latches the register
       number, each AUTOFD toggle commits the data lines, STROBE dropping
       ends it and leaves that register addressed. A disconnect opens
       identically but commits nothing. Test this before anything else
       looks at SELECT.
     */
    if (link->latching) {
        if (chg & LPT_CTRL_AUTOFD) {
            link->ops->write_reg(link->priv, link->cur_reg, link->dat);
            link->latch_commits++;
        }

        if (!(lines & LPT_CTRL_STROBE)) {
            link->latching = 0;
            if (link->latch_commits == 0)
                link_disconnect(link);
        }

        return;
    }

    if (link->connected && (lines == (LPT_CTRL_STROBE | LPT_CTRL_SELECT))) {
        link->latching      = 1;
        link->latch_commits = 0;
        link_address(link, link->dat);
        return;
    }

    /*
       Letting go: INIT dropped leaving AUTOFD alone, then INIT and SELECT
       raised together. Only the pair in that order ends the link - one
       driver toggles SELECT constantly, and another's nibble reads pass
       through AUTOFD-alone twice a byte, so neither half means anything on
       its own.
     */
    if (link->connected && (old_lines == LPT_CTRL_AUTOFD) &&
        (lines & LPT_CTRL_SELECT)) {
        link_disconnect(link);
        return;
    }

    /*
       The address probe that closes the knock. Nothing else may look at
       these two AUTOFD edges: once the link is up an AUTOFD edge latches a
       register number off the data lines, which still carry the unit number
       - so without this the probe addresses register zero and the host
       reads a nibble where it expected an address, and gives up.
     */
    if (link->ident) {
        if (chg & LPT_CTRL_SELECT)
            link->ident = 0;
        return;
    }

    /*
       The connect knock: three toggles of SELECT while the other lines hold
       INIT alone, with the unit address on the data lines. What makes an
       edge count is the state of the other lines, not an earlier write that
       armed it - a host may arrive in the knocking state without writing
       anything at all. As a level it also throws out stray counts.
     */
    if ((lines & ~LPT_CTRL_SELECT) != LPT_CTRL_INIT)
        link->knock = 0;
    else if (chg & LPT_CTRL_SELECT) {
        if (++link->knock >= 3) {
            link->knock = 0;

            /*
               The knock is addressed: these chain, so only the pod named on
               the data lines may take the link. Answering to every address is
               not harmless - the host decides a unit is present by the status
               changing across the knock, so a pod that answers everywhere is
               found nowhere. A knock for another pod also lets go of the
               link this one held: a host that walks the chain without
               letting go first must see the others' answers, not ours.
             */
            if (link->dat == link->unit)
                link_connect(link);
            else {
                link_disconnect(link);
                LINK_LOG(link, "%s: knock for unit %02X, not ours (%02X)\n",
                         link->name, link->dat, link->unit);
            }
        }

        return;
    }

    if (!link->connected)
        return;

    if (link->ops->ctrl != NULL)
        link->ops->ctrl(link->priv, chg, lines);

    /*
       With the link up three edges carry everything: AUTOFD latches the
       register number, and INIT clocks a byte - in when STROBE is set, out
       when it is clear.
     */
    if (chg & LPT_CTRL_AUTOFD) {
        link_address(link, link->dat);

        /*
           Addressing a register also puts the connect answer back on the
           status lines, and it has to be a level the bridge holds: a host
           that reads the status without clocking anything must keep seeing
           it.
         */
        link->rd_out = link_connect_response(link);
        return;
    }

    if (chg & LPT_CTRL_INIT) {
        if (lines & LPT_CTRL_STROBE)
            link->ops->write_reg(link->priv, link->cur_reg, link->dat);
        else
            link_advance_read(link);
    }
}

static uint8_t
link_read_status(void *priv)
{
    bpck_link_t *link = (bpck_link_t *) priv;
    uint8_t      ret;

    link_activity(link, 1);

    if (link->connected && link->ident)
        ret = link_ident_response(link);
    else if (link->connected)
        ret = link->rd_out;
    else
        /* Not connected: the bridge leaves the status lines alone. */
        ret = LPT_STAT_IDLE;

    ret &= LPT_STAT_MASK;

    /*
       Idle status reads are how a host spins, so trace them only when the
       answer changes.
     */
    if ((ret != link->last_status) || link->connected) {
        LINK_LOG(link, "%s: R1 -> %02X          [%s]\n", link->name, ret, bpck_link_state(link));
        link->last_status = ret;
    }

    return ret;
}

/*
   The 93C46. CS falling ends a frame; every rising CLK clocks DI in until
   a command is recognised, after which each rising edge shifts the
   addressed word out MSB first. Only READ is implemented: a driver issues
   EWEN and WRITE as well, but a pod whose identity changed under it would
   be a fault, not a feature.
 */
void
bpck_ee_write(bpck_ee_t *ee, uint8_t val, const char *name,
              void (*log)(const char *fmt, ...))
{
    const uint8_t old = ee->lines;

    ee->lines = val;

    if (!(val & BPCK_EE_CS)) {
        ee->bits    = 0;
        ee->shift   = 0;
        ee->reading = 0;
        ee->dout    = 0;
        return;
    }

    /* Everything happens on the rising edge of the clock. */
    if (!((val & BPCK_EE_CLK) && !(old & BPCK_EE_CLK)))
        return;

    if (ee->reading) {
        ee->dout = (ee->out & 0x8000) ? 1 : 0;
        ee->out  = (uint16_t) (ee->out << 1);
        return;
    }

    ee->shift = (uint16_t) ((ee->shift << 1) | ((val & BPCK_EE_DI) ? 1 : 0));
    ee->bits++;

    /* START, opcode and six address bits: nine in all. */
    if (ee->bits >= 9) {
        const uint8_t addr = ee->shift & 0x3f;

        if ((ee->shift & 0x180) == 0x180) {
            ee->out     = ee->data[addr];
            ee->reading = 1;
            if (log != NULL)
                log("%s: EEPROM read word %02X -> %04X\n", name, addr, ee->out);
        } else {
            if (log != NULL)
                log("%s: EEPROM command %03X ignored\n", name, ee->shift & 0x1ff);
            ee->bits  = 0;
            ee->shift = 0;
        }
    }
}

int
bpck_link_attach(bpck_link_t *link, const bpck_link_ops_t *ops, void *priv,
                 const char *name, void (*log)(const char *fmt, ...))
{
    link->ops  = ops;
    link->priv = priv;
    link->name = name;
    link->log  = log;

    link->lpt = lpt_attach(link_write_data, link_write_ctrl, NULL,
                           link_read_status, NULL,
                           link_epp_write_data, link_epp_request_read, link);

    return link->lpt != NULL;
}
