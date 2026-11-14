/*
 * 86Box       A hypervisor and IBM PC system emulator.
 *
 *             Card Information Structures for the emulated PC Cards: a
 *             builder, the rules a CIS must follow to be read the way the
 *             guests read it, and the Plug and Play ID Windows 9x makes of
 *             one.
 *
 *             How the guests walk a CIS (Linux pcmcia-cs modules/cistpl.c,
 *             and Windows 98's PCCARD.VXD the same way): tuples from
 *             attribute memory 0, NULL tuples skipped, to CISTPL_END; with
 *             no CISTPL_NO_LINK they then assume a long link to common
 *             memory 0 and look there for a CISTPL_LINKTARGET.  A card
 *             without common memory therefore carries NO_LINK.
 *
 *             The Windows 9x ID of a single-function card is
 *             PCMCIA\<string 1>-<string 2>-<CRC>: the first two VERS_1
 *             strings, printable characters only, spaces and commas made
 *             underscores (SCSI.INF's "Adaptec__Inc." is "Adaptec, Inc."),
 *             and a 16-bit checksum PCCARD.VXD takes through CONFIGMG's
 *             CM_Get_CRC_CheckSum over the bodies of the DEVICE, VERS_1
 *             (through its second string), CONFIG, CFTABLE_ENTRY and MANFID
 *             tuples in CIS order, from a seed of 0.  The checksum is
 *             CRC-16/ARC computed a nibble at a time from two 16-entry
 *             tables -- except that the high-nibble table's last entry is
 *             0x4600 where ARC has 0x4400, so it is not quite ARC.  (Taken
 *             from the two VxDs and checked by running them under an x86
 *             emulator against IDs Windows 98 gave three CIS variants.)
 *
 *             An INF lists the IDs of the real cards, checksum included;
 *             an emulated CIS is not the real one byte for byte, so its
 *             CONFIG tuple ends in a two-byte vendor subtuple (code 0xC1,
 *             which every parser skips) chosen to make the checksum the
 *             one the INF names (pccard_cis_match_id()).
 *
 * Authors:    Marcos Alves
 *
 *             Copyright 2026 Marcos Alves.
 *
 *             Released under the GNU General Public License version 2 or
 *             later.  See COPYING for more information.
 */
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <86box/pcmcia.h>

/* ---------------------------------------------------------- building --- */

void
pccard_cis_init(pccard_cis_t *c, uint8_t *buf, int max)
{
    c->buf    = buf;
    c->len    = 0;
    c->max    = max;
    c->filler = -1;
    memset(buf, 0xff, max);
}

static void
put(pccard_cis_t *c, uint8_t b)
{
    if (c->len < c->max)
        c->buf[c->len] = b;
    c->len++;
}

void
pccard_cis_tuple(pccard_cis_t *c, uint8_t code, const uint8_t *data, int len)
{
    put(c, code);
    put(c, (uint8_t) len);
    for (int i = 0; i < len; i++)
        put(c, data[i]);
}

/* VERS_1, PC Card 2.1 and later (4.1): the strings, then 0xFF. */
void
pccard_cis_vers1(pccard_cis_t *c, const char *const *s, int n)
{
    int at;

    put(c, CISTPL_VERS_1);
    at = c->len;
    put(c, 0);
    put(c, 0x04);
    put(c, 0x01);
    for (int i = 0; i < n; i++) {
        for (const char *p = s[i]; *p; p++)
            put(c, (uint8_t) *p);
        put(c, 0x00);
    }
    put(c, 0xff);
    if (at < c->max)
        c->buf[at] = (uint8_t) (c->len - at - 1);
}

void
pccard_cis_manfid(pccard_cis_t *c, uint16_t manf, uint16_t card)
{
    const uint8_t d[4] = { manf & 0xff, manf >> 8, card & 0xff, card >> 8 };
    pccard_cis_tuple(c, CISTPL_MANFID, d, 4);
}

/* CONFIG: registers at base (a two-byte address), present mask rmask, last
   configuration index last, and the two-byte filler subtuple, set later by
   pccard_cis_match_id(). */
void
pccard_cis_config(pccard_cis_t *c, uint16_t base, uint8_t rmask, uint8_t last)
{
    const uint8_t d[9] = { 0x01, last, base & 0xff, base >> 8, rmask, 0xc1, 0x02, 0x00, 0x00 };
    pccard_cis_tuple(c, CISTPL_CONFIG, d, 9);
    c->filler = c->len - 2;
}

void
pccard_cis_end(pccard_cis_t *c)
{
    put(c, CISTPL_END);
}

/* A multi-function card's link to its n functions' CIS chains, all in
   attribute memory; their addresses are filled in by pccard_cis_mfc_link()
   once the chains are placed.  Returns where the first address is. */
int
pccard_cis_longlink_mfc(pccard_cis_t *c, int n)
{
    uint8_t d[1 + 5 * PCCARD_MFC_MAX] = { (uint8_t) n };

    pccard_cis_tuple(c, CISTPL_LONGLINK_MFC, d, 1 + 5 * n);   /* space 0: attribute */
    return c->len - 5 * n;
}

/* Function f's chain starts at byte at of the CIS buffer (attribute address
   2 * at). */
void
pccard_cis_mfc_link(pccard_cis_t *c, int link, int f, int at)
{
    const uint32_t addr = 2u * (uint32_t) at;

    for (int k = 0; k < 4; k++)
        if (link + 5 * f + 1 + k < c->max)
            c->buf[link + 5 * f + 1 + k] = (uint8_t) (addr >> (8 * k));
}

/* What a linked chain starts with. */
void
pccard_cis_linktarget(pccard_cis_t *c)
{
    static const uint8_t cis[3] = { 'C', 'I', 'S' };

    pccard_cis_tuple(c, CISTPL_LINKTARGET, cis, 3);
}

/* An I/O configuration: index (with 0x40 set, the default), I/O interface,
   5 V, len ports at base (0: anywhere) decoding lines address lines, 8-bit
   only or 8 and 16 bit, any of the IRQs in irqs, level-triggered. */
void
pccard_cis_cftable_io(pccard_cis_t *c, uint8_t index, uint16_t base, uint8_t len, uint8_t lines, int bits16, uint16_t irqs)
{
    const uint8_t d[13] = {
        (uint8_t) (0x80 | index), 0x01, /* index, I/O interface               */
        0x19,                           /* features: Vcc, I/O, IRQ            */
        0x01, 0x55,                     /* Vcc: nominal 5 V                   */
        (uint8_t) ((bits16 ? 0x60 : 0x20) | 0x80 | lines), /* range follows, widths, lines */
        0x60, base & 0xff, base >> 8,   /* one range, 2-byte base, 1-byte len */
        (uint8_t) (len - 1),
        0x30, irqs & 0xff, irqs >> 8    /* IRQ mask follows, level            */
    };
    pccard_cis_tuple(c, CISTPL_CFTABLE_ENTRY, d, 13);
}

/* -------------------------------------------------------- the Win9x ID --- */

static const uint16_t crc_lo[16] = {
    0x0000, 0xc0c1, 0xc181, 0x0140, 0xc301, 0x03c0, 0x0280, 0xc241,
    0xc601, 0x06c0, 0x0780, 0xc741, 0x0500, 0xc5c1, 0xc481, 0x0440
};
static const uint16_t crc_hi[16] = {
    0x0000, 0xcc01, 0xd801, 0x1400, 0xf001, 0x3c00, 0x2800, 0xe401,
    0xa001, 0x6c00, 0x7800, 0xb401, 0x5000, 0x9c01, 0x8801, 0x4600   /* sic: 0x4400 in ARC */
};

static uint16_t
win9x_crc(uint16_t c, const uint8_t *p, int n)
{
    while (n--) {
        const uint8_t x = (uint8_t) (c ^ *p++);
        c = (uint16_t) ((c >> 8) ^ crc_lo[x & 15] ^ crc_hi[x >> 4]);
    }
    return c;
}

/* The tuples PCCARD.VXD's checksum walk sees, in order: the primary chain
   to its END, then -- on a multi-function card -- the chain of function 0
   (the parent devnode's), found the way it follows CISTPL_LONGLINK_MFC: the
   link address taken as a byte index first, then halved (as an attribute
   memory address), wherever a LINKTARGET "CIS" is.  Offsets into out, the
   count returned. */
static int
linktarget_at(const uint8_t *cis, int len, uint32_t i)
{
    return (i + 5 <= (uint32_t) len) && (cis[i] == CISTPL_LINKTARGET) && (cis[i + 1] >= 3) && (cis[i + 2] == 'C')
        && (cis[i + 3] == 'I') && (cis[i + 4] == 'S');
}

static int
crc_walk(const uint8_t *cis, int len, int *out, int max)
{
    int      n = 0, i = 0, mfc = -1, chains = 0;

    while (chains < 2) {
        if ((i + 1 >= len) || (cis[i] == CISTPL_END)) {
            uint32_t link = 0;

            if ((mfc < 0) || chains)
                break;
            for (int k = 0; k < 4; k++)
                link |= (uint32_t) cis[mfc + 4 + k] << (8 * k);   /* function 0's entry */
            if (linktarget_at(cis, len, link))
                i = (int) link;
            else if (linktarget_at(cis, len, link >> 1))
                i = (int) (link >> 1);
            else
                break;
            chains++;
            continue;
        }
        if (cis[i] == CISTPL_NULL) {
            i++;
            continue;
        }
        if ((cis[i] == CISTPL_LONGLINK_MFC) && !chains && (cis[i + 1] >= 6) && (cis[i + 2] >= 1) && (cis[i + 3] == 0x00))
            mfc = i;
        if (n < max)
            out[n++] = i;
        i += 2 + cis[i + 1];
    }
    return n;
}

uint16_t
pccard_cis_win9x_crc(const uint8_t *cis, int len)
{
    int      t[128];
    const int nt       = crc_walk(cis, len, t, 128);
    uint32_t cfg_base = 0;
    uint16_t c        = 0;

    /* The configuration registers' address, first: a tuple running over
       them is cut short where they start. */
    for (int k = 0; k < nt; k++) {
        const int i = t[k];
        if ((cis[i] == CISTPL_CONFIG) && (cis[i + 1] >= 4)) {
            const int rasz = cis[i + 2] & 3;
            for (int b = 0; b <= rasz; b++)
                cfg_base |= (uint32_t) cis[i + 4 + b] << (8 * b);
            break;
        }
    }

    for (int k = 0; k < nt; k++) {
        const int     i    = t[k];
        const uint8_t code = cis[i];
        const int     n    = cis[i + 1];

        if ((code == CISTPL_DEVICE) || (code == CISTPL_VERS_1) || (code == CISTPL_CONFIG) || (code == CISTPL_CFTABLE_ENTRY)
            || (code == CISTPL_MANFID)) {
            const uint8_t *d   = &cis[i + 2];
            const uint32_t off = (uint32_t) i * 2;   /* attribute memory address */
            int            m   = n;

            if ((cfg_base >= off + 4) && (cfg_base < off + 4 + 2 * (uint32_t) n))
                m = (int) (cfg_base - off - 4) >> 1;
            if (code == CISTPL_VERS_1) {
                int j = 2;
                while ((j < n) && d[j])
                    j++;
                j++;
                while ((j < n) && d[j])
                    j++;
                j++;
                if (m > j)
                    m = j;
            }
            c = win9x_crc(c, d, m);
        }
    }
    return c;
}

/* PCMCIA\<string 1>-<string 2>-<CRC>, as PCCARD.VXD makes it. */
void
pccard_cis_win9x_id(const uint8_t *cis, int len, char *out, int outlen)
{
    char ids[2][64] = { "", "" };

    for (int i = 0; (i + 1 < len) && (cis[i] != CISTPL_END);) {
        if (cis[i] == CISTPL_NULL) {
            i++;
            continue;
        }
        if (cis[i] != CISTPL_VERS_1) {
            i += 2 + cis[i + 1];
            continue;
        }
        const uint8_t *d = &cis[i + 4];
        const uint8_t *e = &cis[i + 2 + cis[i + 1]];
        for (int s = 0; (s < 2) && (d < e) && (*d != 0xff); s++) {
            int k = 0;
            for (; (d < e) && *d && (*d != 0xff); d++)
                if ((*d >= 0x20) && (*d < 0x7f) && (k < 63))
                    ids[s][k++] = ((*d == ' ') || (*d == ',')) ? '_' : (char) *d;
            ids[s][k] = '\0';
            if ((d < e) && (*d == 0))
                d++;
        }
        break;
    }
    snprintf(out, outlen, "PCMCIA\\%s-%s-%04X", ids[0], ids[1], pccard_cis_win9x_crc(cis, len));
}

/* Set the CONFIG tuple's filler bytes so Windows 9x gives the card the ID
   ending in crc.  Returns 1 when it does. */
int
pccard_cis_match_id(pccard_cis_t *c, uint16_t crc)
{
    if (c->filler < 0)
        return 0;
    for (int v = 0; v < 0x10000; v++) {
        c->buf[c->filler]     = (uint8_t) v;
        c->buf[c->filler + 1] = (uint8_t) (v >> 8);
        if (pccard_cis_win9x_crc(c->buf, c->len) == crc)
            return 1;
    }
    c->buf[c->filler] = c->buf[c->filler + 1] = 0;
    return 0;
}

/* ------------------------------------------------------- the cards' CIS --- */

/* An I/O card's one configuration: index 1 (the default), I/O interface,
   5 V, len ports at base on lines address lines, 8 and 16 bit, any of the
   IRQs in irqs, level-triggered. */
static void
cftable_io(pccard_cis_t *c, uint16_t base, uint8_t len, uint8_t lines, uint16_t irqs)
{
    pccard_cis_cftable_io(c, 0x41, base, len, lines, 1, irqs);
}

static const uint8_t no_device[3] = { 0x00, 0x00, 0xff };   /* DEVICE: no common memory */
static const uint8_t fn_network[2] = { CISTPL_FUNCID_NETWORK, 0x00 };

/* TRENDnet TE100-PC16 (net_te100pc16.c).  Its driver's INF knows it as
   PCMCIA\Fast_Ethernet-16-bit_PC_Card-8B43. */
int
pccard_cis_te100pc16(uint8_t *buf, const uint8_t mac[6])
{
    static const char *vers[] = { "Fast Ethernet", "16-bit PC Card", "", "AX88190" };
    pccard_cis_t       c;
    uint8_t            node[8] = { 0x04, 0x06 };

    memcpy(node + 2, mac, 6);
    pccard_cis_init(&c, buf, 256);
    pccard_cis_tuple(&c, CISTPL_DEVICE, no_device, 3);
    pccard_cis_vers1(&c, vers, 4);
    pccard_cis_manfid(&c, 0x0149, 0xc1ab);
    pccard_cis_tuple(&c, CISTPL_FUNCID, fn_network, 2);
    pccard_cis_tuple(&c, CISTPL_FUNCE, node, 8);                /* LAN node ID */
    pccard_cis_config(&c, 0x03c0, 0x03, 0x01);                  /* COR, CCSR */
    cftable_io(&c, 0x0300, 32, 5, 0xdeb8);
    pccard_cis_tuple(&c, CISTPL_NO_LINK, NULL, 0);
    pccard_cis_end(&c);
    pccard_cis_match_id(&c, 0x8b43);
    return c.len;
}

/* 3Com 3C589D EtherLink III LAN PC Card (net_3c509b.c's PC Card model):
   sixteen ports decoding all sixteen address lines, the station address in
   its EEPROM.  Windows 98's own NET3C589.INF knows it as
   PCMCIA\3Com_Corporation-3C589D-9CA6. */
int
pccard_cis_3c589d(uint8_t *buf)
{
    static const char *vers[] = { "3Com Corporation", "3C589D", "TP/BNC LAN Card Ver. 2a", "000002" };
    pccard_cis_t       c;

    pccard_cis_init(&c, buf, 256);
    pccard_cis_tuple(&c, CISTPL_DEVICE, no_device, 3);
    pccard_cis_vers1(&c, vers, 4);
    pccard_cis_manfid(&c, 0x0101, 0x0589);
    pccard_cis_tuple(&c, CISTPL_FUNCID, fn_network, 2);
    pccard_cis_config(&c, 0x0200, 0x01, 0x01);                  /* COR */
    cftable_io(&c, 0x0300, 16, 16, 0xdeb8);
    pccard_cis_tuple(&c, CISTPL_NO_LINK, NULL, 0);
    pccard_cis_end(&c);
    pccard_cis_match_id(&c, 0x9ca6);
    return c.len;
}

/* 3Com 3C562D/3C563D EtherLink III LAN+33.6 Modem PC Card
   (pccard_3c562.c): a multi-function card.  The primary chain names the
   card and links to two function chains:

     function 0, the LAN (net_3c509b.c's EtherLink III): registers at 1800h
       -- where NET3C562.INF's override says they are -- COR, CCSR and I/O
       base 0 (0x23, the override's too: the LAN compares A7-A0, which is why
       Linux puts it at xx00-xx7Fh), configuration 7 = function enable,
       address decode and IREQ enable as a raw index too; sixteen ports; the
       station address in 3Com's tuple 88h, each byte pair swapped;
     function 1, the modem's 16550: registers at 1900h, COR, CCSR and I/O base
       0 like the LAN's (MDMGATEW.INF's override for it says so), configuration
       7, eight ports, 8-bit.

   Windows 98 names the functions PCMCIA\3COM_CORPORATION-3C562D/3C563D-DEV0-
   and -DEV1-E4C0 (NET3C562.INF, MDMGATEW.INF): the checksum covers the
   primary chain and function 0's, so the filler is function 0's CONFIG's.
   No dump of a real 3C562 CIS was found; this is built from the INFs and the
   Linux/BSD drivers. */
int
pccard_cis_3c562d(uint8_t *buf, const uint8_t mac[6], uint32_t *lan_cfg, uint32_t *modem_cfg)
{
    static const char   *vers[]     = { "3Com Corporation", "3C562D/3C563D", "EtherLink III LAN+336 Modem PC Card", "Ver 1.0" };
    static const uint8_t fn_multi[] = { 0x00, 0x00 };
    static const uint8_t fn_serial[] = { 0x02, 0x00 };
    const uint8_t        node[6]    = { mac[1], mac[0], mac[3], mac[2], mac[5], mac[4] };
    pccard_cis_t         c;
    int                  link;

    pccard_cis_init(&c, buf, 512);
    pccard_cis_tuple(&c, CISTPL_DEVICE, no_device, 3);
    pccard_cis_vers1(&c, vers, 4);
    pccard_cis_manfid(&c, 0x0101, 0x0562);
    pccard_cis_tuple(&c, CISTPL_FUNCID, fn_multi, 2);
    link = pccard_cis_longlink_mfc(&c, 2);
    pccard_cis_end(&c);

    pccard_cis_mfc_link(&c, link, 0, c.len);
    pccard_cis_linktarget(&c);
    pccard_cis_tuple(&c, CISTPL_FUNCID, fn_network, 2);
    pccard_cis_config(&c, 0x1800, 0x23, 0x07);
    pccard_cis_cftable_io(&c, 0x47, 0x0000, 16, 4, 1, 0xdeb8);
    pccard_cis_tuple(&c, 0x88, node, 6);
    pccard_cis_end(&c);
    const int filler = c.filler;

    pccard_cis_mfc_link(&c, link, 1, c.len);
    pccard_cis_linktarget(&c);
    pccard_cis_tuple(&c, CISTPL_FUNCID, fn_serial, 2);
    pccard_cis_config(&c, 0x1900, 0x23, 0x07);
    pccard_cis_cftable_io(&c, 0x47, 0x0000, 8, 3, 0, 0xdeb8);
    pccard_cis_end(&c);

    c.filler = filler;
    pccard_cis_match_id(&c, 0xe4c0);
    if (lan_cfg)
        *lan_cfg = 0x1800;
    if (modem_cfg)
        *modem_cfg = 0x1900;
    return c.len;
}

/* Adaptec APA-1460 SlimSCSI (scsi_apa1460.c), from its Technical Reference
   (Adaptec 510671-00 rev. A, chapter 5): "Adaptec, Inc.", "APA-1460 SCSI
   Host Adapter", "Version 0.01"; the Configuration Option Register at 2000h;
   32 ports, 16-bit, on ten address lines at 340h or 140h; IRQ 9 to 12,
   level.  The card's register is not a configuration index but SRESET, IOEN
   (bit 3) and PRIMARY (bit 0, 340h), so the entries' indexes are the values
   that enable each range: 09h for 340h (the default), 08h for 140h.
   Windows 98's SCSI.INF knows it as
   PCMCIA\Adaptec__Inc.-APA-1460_SCSI_Host_Adapter-BE89 (SPARROW.MPD). */
int
pccard_cis_apa1460(uint8_t *buf)
{
    static const char *vers[] = { "Adaptec, Inc.", "APA-1460 SCSI Host Adapter", "Version 0.01", "" };
    pccard_cis_t       c;

    pccard_cis_init(&c, buf, 256);
    pccard_cis_tuple(&c, CISTPL_DEVICE, no_device, 3);
    pccard_cis_vers1(&c, vers, 4);
    pccard_cis_config(&c, 0x2000, 0x01, 0x09);                  /* COR */
    pccard_cis_cftable_io(&c, 0x49, 0x0340, 32, 10, 1, 0x1e00);
    pccard_cis_cftable_io(&c, 0x08, 0x0140, 32, 10, 1, 0x1e00);
    pccard_cis_tuple(&c, CISTPL_NO_LINK, NULL, 0);
    pccard_cis_end(&c);
    pccard_cis_match_id(&c, 0xbe89);
    return c.len;
}

/* Hayes Accura 56K PC Card (pccard_accura56k.c): a data/fax modem, one
   function -- a 16550 behind the standard configuration registers.  No dump
   of the real card's CIS was found; this is the shape the modem cards of its
   day share (pcmcia-cs's serial_cs binds any of them by FUNCID serial): the
   COR and CCSR at 200h; eight ports at COM1-COM4's addresses on ten address
   lines, configuration indexes 1 to 4, then anywhere on three lines, index 5;
   8-bit, any of the usual IRQs.  Windows 98's MDMHAY2.INF knows it as
   PCMCIA\Hayes-Accura_56K_PC_CARD-EBBB. */
int
pccard_cis_accura56k(uint8_t *buf, uint32_t *cfg_base)
{
    static const char   *vers[]      = { "Hayes", "Accura 56K PC CARD" };
    static const uint8_t fn_serial[] = { CISTPL_FUNCID_SERIAL, 0x00 };
    static const uint16_t com[4]     = { 0x03f8, 0x02f8, 0x03e8, 0x02e8 };
    pccard_cis_t         c;

    pccard_cis_init(&c, buf, 256);
    pccard_cis_tuple(&c, CISTPL_DEVICE, no_device, 3);
    pccard_cis_vers1(&c, vers, 2);
    pccard_cis_tuple(&c, CISTPL_FUNCID, fn_serial, 2);
    pccard_cis_config(&c, 0x0200, 0x03, 0x05);                  /* COR, CCSR */
    for (int i = 0; i < 4; i++)
        pccard_cis_cftable_io(&c, (uint8_t) ((i ? 0x00 : 0x40) | (i + 1)), com[i], 8, 10, 0, 0xdeb8);
    pccard_cis_cftable_io(&c, 0x05, 0x0000, 8, 3, 0, 0xdeb8);
    pccard_cis_tuple(&c, CISTPL_NO_LINK, NULL, 0);
    pccard_cis_end(&c);
    pccard_cis_match_id(&c, 0xebbb);
    if (cfg_base)
        *cfg_base = 0x0200;
    return c.len;
}
