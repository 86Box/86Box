/*
 * 86Box       Multi-function PC Cards (src/pcmcia/pccard_mfc.c): a
 *             two-function card shaped like the 3Com/Megahertz 3CXEM556
 *             (pcmcia-cs etc/cis/3CXEM556.cis: a LAN function decoding 16
 *             ports, registers at 800h; a 16550 function decoding 8, at
 *             900h), its CIS read back through the card's attribute memory
 *             and walked by Linux pcmcia-cs cistpl.c's rules for each
 *             function (get_first_tuple's LONGLINK_MFC search, follow_link's
 *             LINKTARGET check), then its configuration registers driven the
 *             way cs.c's RequestConfiguration does, with fake functions.
 *
 *             No test framework; returns non-zero on failure.
 *
 * Authors:    Marcos Alves
 *
 *             Copyright 2026 Marcos Alves.
 */
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <86box/pcmcia.h>

static int checks, failures;
#define CHECK(c, ...)                                   \
    do {                                                \
        checks++;                                       \
        if (!(c)) {                                     \
            failures++;                                 \
            printf("FAIL %s:%d: ", __FILE__, __LINE__); \
            printf(__VA_ARGS__);                        \
            printf("\n");                               \
        }                                               \
    } while (0)

/* The socket's side: the card's IREQ line. */
static int ireq_level = -1, ireq_calls;

void
pcmcia_card_irq(int socket, int level)
{
    (void) socket;
    ireq_level = level;
    ireq_calls++;
}

/* --------------------------------------------------- fake functions --- */

typedef struct {
    uint8_t  regs[16];
    int      last_off;
    int      resets, enables, enabled;
} fake_t;

static uint8_t
fake_read(uint16_t off, void *priv)
{
    fake_t *f   = (fake_t *) priv;
    f->last_off = off;
    return f->regs[off & 15];
}

static void
fake_write(uint16_t off, uint8_t val, void *priv)
{
    fake_t *f      = (fake_t *) priv;
    f->last_off    = off;
    f->regs[off & 15] = val;
}

static void
fake_reset(void *priv)
{
    fake_t *f = (fake_t *) priv;
    f->resets++;
    memset(f->regs, 0, sizeof(f->regs));
}

static void
fake_enable(int on, void *priv)
{
    fake_t *f = (fake_t *) priv;
    f->enables++;
    f->enabled = on;
}

/* ---------------------------------------------------------- the card --- */

#define LAN_CFG  0x800
#define UART_CFG 0x900

static uint8_t      cis[512];
static int          cis_len;
static pccard_mfc_t card;
static fake_t       lan, uart;
static pccard_func_t lan_fn  = { 16, fake_read, fake_write, NULL, NULL, fake_reset, fake_enable, &lan };
static pccard_func_t uart_fn = { 8, fake_read, fake_write, NULL, NULL, fake_reset, fake_enable, &uart };

static void
build_cis(void)
{
    static const char   *vers[]      = { "86Box-Next", "MFC test card", "" };
    static const uint8_t no_device[] = { 0x00, 0x00, 0xff };
    static const uint8_t fn_multi[]  = { 0x00, 0x00 };
    static const uint8_t fn_net[]    = { CISTPL_FUNCID_NETWORK, 0x00 };
    static const uint8_t fn_serial[] = { 0x02, 0x00 };
    pccard_cis_t         c;
    int                  link;

    pccard_cis_init(&c, cis, sizeof(cis));
    pccard_cis_tuple(&c, CISTPL_DEVICE, no_device, 3);
    pccard_cis_vers1(&c, vers, 3);
    pccard_cis_manfid(&c, 0x0101, 0x0035);
    pccard_cis_tuple(&c, CISTPL_FUNCID, fn_multi, 2);
    link = pccard_cis_longlink_mfc(&c, 2);
    pccard_cis_end(&c);

    pccard_cis_mfc_link(&c, link, 0, c.len);
    pccard_cis_linktarget(&c);
    pccard_cis_tuple(&c, CISTPL_FUNCID, fn_net, 2);
    pccard_cis_config(&c, LAN_CFG, 0x63, 0x07);
    pccard_cis_cftable_io(&c, 0x47, 0x0000, 16, 4, 1, 0xffff);
    pccard_cis_end(&c);

    pccard_cis_mfc_link(&c, link, 1, c.len);
    pccard_cis_linktarget(&c);
    pccard_cis_tuple(&c, CISTPL_FUNCID, fn_serial, 2);
    pccard_cis_config(&c, UART_CFG, 0x63, 0x27);
    pccard_cis_cftable_io(&c, 0x67, 0x0000, 8, 3, 0, 0xffff);
    pccard_cis_end(&c);
    cis_len = c.len;
}

/* Attribute memory as the guest reads a CIS: byte i at address 2i. */
static uint8_t
cis_at(int i)
{
    return card.card.attr_read(2u * (uint32_t) i, card.card.priv);
}

/* One function's view, by cistpl.c: the primary chain (common tuples),
   then at its END the function's link out of LONGLINK_MFC, which must land
   on a LINKTARGET "CIS" (follow_link tries ofs, then ofs >> 1), then that
   chain to its END. */
typedef struct {
    int      found_mfc, nfn, linked;
    int      funcid;          /* the function chain's */
    uint32_t config_base;
    uint8_t  rmask, last;
    int      io_lines, io_len, io_8only;
    int      cft_index;
    int      manfid;          /* seen in the common chain */
} fn_view_t;

static int
linktarget_at(int ofs)
{
    return (cis_at(ofs) == CISTPL_LINKTARGET) && (cis_at(ofs + 1) >= 3) && (cis_at(ofs + 2) == 'C')
        && (cis_at(ofs + 3) == 'I') && (cis_at(ofs + 4) == 'S');
}

static void
walk_function(int fn, fn_view_t *v)
{
    int ofs = 0, mfc_at = -1;

    memset(v, 0, sizeof(*v));
    v->funcid = -1;
    for (int n = 0; n < 64; n++) {
        const uint8_t code = cis_at(ofs);
        if (code == CISTPL_NULL) {
            ofs++;
            continue;
        }
        if (code == CISTPL_END)
            break;
        if (code == CISTPL_LONGLINK_MFC) {
            v->found_mfc = 1;
            v->nfn       = cis_at(ofs + 2);
            mfc_at       = ofs + 3;
        } else if (code == CISTPL_MANFID)
            v->manfid = cis_at(ofs + 2) | (cis_at(ofs + 3) << 8);
        ofs += 2 + cis_at(ofs + 1);
    }
    if (mfc_at < 0)
        return;

    /* follow_link */
    const int at    = mfc_at + 5 * fn;
    uint32_t  link  = 0;
    for (int k = 0; k < 4; k++)
        link |= (uint32_t) cis_at(at + 1 + k) << (8 * k);
    if (cis_at(at) != 0x00)   /* CISTPL_MFC_ATTR */
        return;
    if (linktarget_at((int) link))
        ofs = (int) link;
    else if (linktarget_at((int) (link >> 1)))
        ofs = (int) (link >> 1);
    else
        return;
    v->linked = 1;

    for (int n = 0; n < 64; n++) {
        const uint8_t code = cis_at(ofs);
        if (code == CISTPL_NULL) {
            ofs++;
            continue;
        }
        if (code == CISTPL_END)
            return;
        const int len = cis_at(ofs + 1);
        if (code == CISTPL_FUNCID)
            v->funcid = cis_at(ofs + 2);
        else if (code == CISTPL_CONFIG) {
            const int rasz = cis_at(ofs + 2) & 3;
            v->last        = cis_at(ofs + 3);
            for (int k = 0; k <= rasz; k++)
                v->config_base |= (uint32_t) cis_at(ofs + 4 + k) << (8 * k);
            v->rmask = cis_at(ofs + 5 + rasz);
        } else if (code == CISTPL_CFTABLE_ENTRY) {
            /* index, interface, features (Vcc, I/O, IRQ), Vcc, I/O */
            v->cft_index = cis_at(ofs + 2) & 0x3f;
            const uint8_t io = cis_at(ofs + 7);
            v->io_lines      = io & 0x1f;
            v->io_8only      = ((io & 0x60) == 0x20);
            v->io_len        = cis_at(ofs + 11) + 1;
        }
        ofs += 2 + len;
    }
}

static void
test_cis(void)
{
    fn_view_t v;

    walk_function(0, &v);
    CHECK(v.found_mfc && v.nfn == 2, "the primary chain has LONGLINK_MFC for 2 functions (%d, %d)", v.found_mfc, v.nfn);
    CHECK(v.manfid == 0x0101, "the common chain has the card's MANFID (%04X)", v.manfid);
    CHECK(v.linked, "function 0's link lands on a LINKTARGET");
    CHECK(v.funcid == CISTPL_FUNCID_NETWORK, "function 0 is a network adapter (%d)", v.funcid);
    CHECK(v.config_base == LAN_CFG && v.rmask == 0x63 && v.last == 0x07, "function 0: registers at %X mask %02X last %02X",
          v.config_base, v.rmask, v.last);
    CHECK(v.cft_index == 7 && v.io_len == 16 && v.io_lines == 4 && !v.io_8only, "function 0: index %d, %d ports on %d lines",
          v.cft_index, v.io_len, v.io_lines);

    walk_function(1, &v);
    CHECK(v.linked, "function 1's link lands on a LINKTARGET");
    CHECK(v.funcid == 0x02, "function 1 is a serial port (%d)", v.funcid);
    CHECK(v.config_base == UART_CFG && v.last == 0x27, "function 1: registers at %X, last index %02X", v.config_base, v.last);
    CHECK(v.cft_index == 0x27 && v.io_len == 8 && v.io_lines == 3 && v.io_8only, "function 1: index %02X, %d ports, 8-bit",
          v.cft_index, v.io_len);

    CHECK(2 * cis_len <= LAN_CFG, "the CIS (to %X) is clear of the registers at %X", 2 * cis_len, LAN_CFG);

    /* The Windows 9x ID is made from the primary chain. */
    char id[128];
    pccard_cis_win9x_id(cis, cis_len, id, sizeof(id));
    CHECK(!strncmp(id, "PCMCIA\\86Box-Next-MFC_test_card-", 32), "Windows 9x ID %s", id);
}

/* RequestConfiguration for one function: COR = index bits 5:3 | function
   enable | IREQ enable | level, decode with the I/O base registers. */
static void
configure(uint32_t base, uint8_t index, uint16_t io, uint8_t nports)
{
    const uint8_t cor = (uint8_t) ((index & 0x38) | 0x01 | 0x04 | 0x02 | 0x40);

    card.card.attr_write(base + 0x0a, io & 0xff, card.card.priv);
    card.card.attr_write(base + 0x0c, io >> 8, card.card.priv);
    card.card.attr_write(base + 0x12, (uint8_t) (nports - 1), card.card.priv);
    card.card.attr_write(base + 0x00, cor, card.card.priv);
}

static uint8_t
attr(uint32_t addr)
{
    return card.card.attr_read(addr, card.card.priv);
}

static void
test_registers(void)
{
    /* Unconfigured: nobody answers I/O. */
    CHECK(card.card.io_read(0x300, card.card.priv) == 0xff, "unconfigured, the card answers no I/O");
    CHECK(attr(LAN_CFG) == 0x00 && attr(UART_CFG) == 0x00, "both CORs read 0 after reset");

    configure(LAN_CFG, 0x07, 0x300, 16);
    configure(UART_CFG, 0x27, 0x2f8, 8);
    CHECK(lan.enabled && uart.enabled, "COR function enable reaches both functions");
    CHECK(attr(LAN_CFG) == 0x47 && attr(UART_CFG) == 0x67, "CORs read back (%02X %02X)", attr(LAN_CFG), attr(UART_CFG));
    CHECK(attr(UART_CFG + 0x0a) == 0xf8 && attr(UART_CFG + 0x0c) == 0x02 && attr(UART_CFG + 0x12) == 7,
          "I/O base and limit read back");

    /* I/O decode by the I/O base registers. */
    card.card.io_write(0x305, 0x5a, card.card.priv);
    CHECK(lan.last_off == 5 && lan.regs[5] == 0x5a, "port 305h is the LAN function's register 5");
    card.card.io_write(0x2fb, 0x83, card.card.priv);
    CHECK(uart.last_off == 3 && uart.regs[3] == 0x83, "port 2FBh is the UART's register 3 (LCR)");
    CHECK(card.card.io_read(0x2fb, card.card.priv) == 0x83, "and reads back");
    CHECK(card.card.io_read(0x310, card.card.priv) == 0xff, "past the LAN's 16 ports, nobody");
    CHECK(card.card.io_readw(0x304, card.card.priv) == 0x5a00, "a word read without a word handler is two byte reads");

    /* Interrupts: the function's request shows in its CCSR and on IREQ. */
    CHECK(ireq_level <= 0, "no IREQ yet");
    pccard_mfc_irq(&card, 1, 1);
    CHECK(ireq_level == 1, "the UART's interrupt requests IREQ");
    CHECK(attr(UART_CFG + 2) & 0x02, "and is pending in its CCSR");
    CHECK(!(attr(LAN_CFG + 2) & 0x02), "not in the LAN function's");
    pccard_mfc_irq(&card, 0, 1);
    pccard_mfc_irq(&card, 1, 0);
    CHECK(ireq_level == 1, "the LAN's keeps IREQ up when the UART's goes");
    pccard_mfc_irq(&card, 0, 0);
    CHECK(ireq_level == 0, "IREQ drops with the last request");

    /* IREQ enable off: pending in the CCSR, but no IREQ. */
    card.card.attr_write(UART_CFG, 0x63, card.card.priv);   /* COR without IREQ enable */
    pccard_mfc_irq(&card, 1, 1);
    CHECK(ireq_level == 0 && (attr(UART_CFG + 2) & 0x02), "with IREQ disabled, pending but no IREQ");
    card.card.attr_write(UART_CFG + 2, 0x00, card.card.priv);
    CHECK(attr(UART_CFG + 2) & 0x02, "writing the CCSR doesn't drop the function's request");
    card.card.attr_write(UART_CFG, 0x67, card.card.priv);
    CHECK(ireq_level == 1, "IREQ enable on again: IREQ");
    pccard_mfc_irq(&card, 1, 0);

    /* No address decode: the low lines select the register. */
    card.card.attr_write(LAN_CFG, 0x45, card.card.priv);   /* enable + IREQ, no decode */
    card.card.io_write(0x2fc, 0x11, card.card.priv);        /* function 0 is asked first */
    CHECK(lan.last_off == 0x0c && lan.regs[12] == 0x11, "without decode, the LAN takes any port by its low 4 lines");
    card.card.attr_write(LAN_CFG, 0x47, card.card.priv);

    /* SRESET on one function. */
    const int r = uart.resets;
    card.card.attr_write(UART_CFG, 0x80, card.card.priv);
    CHECK(uart.resets == r + 1 && !uart.enabled, "COR SRESET resets the UART and disables it");
    CHECK(attr(UART_CFG) == 0x80 && attr(UART_CFG + 0x0a) == 0, "it reads SRESET, its I/O base is cleared");
    CHECK(card.card.io_read(0x2fb, card.card.priv) == 0xff, "and it answers no I/O");
    CHECK(card.card.io_read(0x305, card.card.priv) == 0x5a, "the LAN goes on");

    /* Card reset: everything back to unconfigured. */
    pccard_mfc_irq(&card, 0, 1);
    card.card.reset(card.card.priv);
    CHECK(attr(LAN_CFG) == 0 && !lan.enabled && ireq_level == 0, "card RESET: unconfigured, no IREQ");
    CHECK(card.card.io_read(0x305, card.card.priv) == 0xff, "and no I/O");
}

/* The 3C562D's CIS (pccard_cis_3c562d()) on a card of fake functions: the
   layout Windows 98's INFs expect, and its LAN's A7-A0 decode. */
static void
test_3c562d(void)
{
    static const uint8_t mac[6] = { 0x00, 0x20, 0xaf, 0x12, 0x34, 0x56 };
    static uint8_t       c562[512];
    uint32_t             lan_cfg, modem_cfg;
    const int            len = pccard_cis_3c562d(c562, mac, &lan_cfg, &modem_cfg);
    fn_view_t            v;
    char                 id[128];

    CHECK(lan_cfg == 0x1800 && modem_cfg == 0x1900, "3C562D: registers at %X and %X", lan_cfg, modem_cfg);
    CHECK(len <= 512 && 2 * len <= (int) lan_cfg, "3C562D: the CIS (%d bytes) fits and is clear of the registers", len);

    lan  = (fake_t) { 0 };
    uart = (fake_t) { 0 };
    pccard_mfc_init(&card, 0, "3C562D", c562, len);
    pccard_mfc_add(&card, &lan_fn, lan_cfg, 0x23);
    pccard_mfc_add(&card, &uart_fn, modem_cfg, 0x23);
    card.card.reset(card.card.priv);

    walk_function(0, &v);
    CHECK(v.found_mfc && v.nfn == 2 && v.manfid == 0x0101, "3C562D: 3Com, two functions");
    CHECK(v.linked && v.funcid == CISTPL_FUNCID_NETWORK, "3C562D: function 0 is the LAN");
    CHECK(v.config_base == 0x1800 && v.rmask == 0x23 && v.last == 0x07,
          "3C562D: LAN registers as NET3C562.INF's override has them (%X %02X %02X)", v.config_base, v.rmask, v.last);
    walk_function(1, &v);
    CHECK(v.linked && v.funcid == CISTPL_FUNCID_SERIAL && v.config_base == 0x1900 && v.io_len == 8,
          "3C562D: function 1 is a serial port, 8 ports, registers at 1900h");

    /* 3Com's station address tuple, byte pairs swapped (NetBSD, Linux). */
    int found = 0;
    for (int i = 0; i + 8 <= len; i++)
        if ((c562[i] == 0x88) && (c562[i + 1] == 6)) {
            found = (c562[i + 2] == mac[1]) && (c562[i + 3] == mac[0]) && (c562[i + 4] == mac[3])
                 && (c562[i + 5] == mac[2]) && (c562[i + 6] == mac[5]) && (c562[i + 7] == mac[4]);
            break;
        }
    CHECK(found, "3C562D: tuple 88h holds the station address, pairs swapped");

    pccard_cis_win9x_id(c562, len, id, sizeof(id));
    CHECK(!strcmp(id, "PCMCIA\\3Com_Corporation-3C562D/3C563D-E4C0"),
          "3C562D: Windows 9x parent ID %s (its functions: -DEV0-/-DEV1-E4C0, NET3C562.INF / MDMGATEW.INF)", id);

    /* The LAN compares A7-A0 only: base 0x00 at port 300h. */
    configure(0x1800, 0x07, 0x300, 16);   /* writes I/O base 1 too, which the LAN hasn't */
    card.card.io_write(0x30e, 0x01, card.card.priv);
    CHECK(lan.last_off == 0x0e && lan.regs[14] == 0x01, "3C562D: LAN at 300h by A7-A0");
    card.card.io_write(0x10e, 0x02, card.card.priv);
    CHECK(lan.regs[14] == 0x02, "3C562D: and its alias at 10Eh (only A7-A0 compared)");
    /* MDMGATEW.INF's override: COR 47h, I/O base 0 only -- the modem compares
       A7-A0 too. */
    card.card.attr_write(0x1900 + 0x0a, 0xe8, card.card.priv);
    card.card.attr_write(0x1900, 0x47, card.card.priv);
    card.card.io_write(0x3eb, 0x03, card.card.priv);
    CHECK(uart.last_off == 3 && uart.regs[3] == 0x03, "3C562D: modem at 3E8h with only I/O base 0 written");
    CHECK(card.card.io_read(0x2eb, card.card.priv) == 0x03, "3C562D: the modem compares A7-A0 (2EBh aliases it)");
}

int
main(void)
{
    build_cis();
    pccard_mfc_init(&card, 1, "MFC test card", cis, cis_len);
    CHECK(pccard_mfc_add(&card, &lan_fn, LAN_CFG, 0x63) == 0, "LAN is function 0");
    CHECK(pccard_mfc_add(&card, &uart_fn, UART_CFG, 0x63) == 1, "UART is function 1");
    card.card.reset(card.card.priv);

    test_cis();
    test_registers();
    test_3c562d();

    printf("%d checks, %d failures\n", checks, failures);
    return failures != 0;
}
