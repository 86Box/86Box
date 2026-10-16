/*
 * 86Box       The emulated PC Cards' Card Information Structures
 *             (src/pcmcia/pccard_cis.c), checked the way the guests read
 *             them: walked and parsed by the rules of Linux pcmcia-cs's
 *             modules/cistpl.c (get_next_tuple, parse_device, parse_vers_1,
 *             parse_manfid, parse_config, parse_cftable_entry), plus what
 *             makes a CIS work in practice -- NO_LINK on a card whose common
 *             memory holds no CIS, the configuration registers clear of the
 *             CIS, a CFTABLE entry for the CONFIG tuple's last index -- and
 *             the Plug and Play ID Windows 9x makes of it.
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

/* What the parsers found. */
typedef struct {
    int      ok;               /* every tuple parsed */
    int      ntuples;
    int      first_device;     /* the first tuple is CISTPL_DEVICE */
    int      no_link;
    int      vers_ns;
    char     vers[4][64];
    int      has_manfid;
    uint16_t manf, card;
    int      funcid;           /* -1: none */
    int      has_config;
    uint32_t config_base;
    uint8_t  rmask, last_idx;
    int      cft_has_last;     /* a CFTABLE entry with the last index */
    int      io_lines, io_len; /* the I/O entry's */
    uint32_t dev_size;         /* DEVICE: common memory size */
    int      dev_type;
    int      end;              /* where CISTPL_END is */
} cis_info_t;

static const char *bad;   /* why a tuple did not parse */

/* parse_device: entries of type/speed and size, to 0xFF. */
static int
parse_device(const uint8_t *p, const uint8_t *q, cis_info_t *ci)
{
    for (int i = 0; i < 4; i++) {
        if (*p == 0xff)
            break;
        const int type  = *p >> 4;
        const int speed = *p & 7;
        if (speed == 7) {
            if (++p == q)
                return 0;
            while (*p & 0x80)
                if (++p == q)
                    return 0;
        } else if (speed > 4)
            return 0;
        if (++p == q)
            return 0;
        if (*p == 0xff)
            break;
        const int scale = *p & 7;
        if (scale == 7)
            return 0;
        if (i == 0) {
            ci->dev_type = type;
            ci->dev_size = (uint32_t) ((*p >> 3) + 1) * (512u << (scale * 2));
            if (type == 0)
                ci->dev_size = 0;
        }
        if (++p == q)
            break;
    }
    return 1;
}

/* parse_vers_1 / parse_strings. */
static int
parse_vers1(const uint8_t *p, const uint8_t *q, cis_info_t *ci)
{
    p += 2;
    if (p >= q)
        return 0;
    ci->vers_ns = 0;
    for (int i = 0; i < 4; i++) {
        if (*p == 0xff)
            break;
        int j = 0;
        for (;;) {
            if ((*p == 0) || (*p == 0xff))
                break;
            if (j < 63)
                ci->vers[i][j++] = (char) *p;
            if (++p == q)
                return 0;
        }
        ci->vers[i][j] = '\0';
        ci->vers_ns++;
        if ((*p == 0xff) || (++p == q))
            break;
    }
    return 1;
}

static const uint8_t *
parse_power(const uint8_t *p, const uint8_t *q)
{
    if (p == q)
        return NULL;
    const uint8_t present = *p++;
    for (int i = 0; i < 7; i++)
        if (present & (1 << i)) {
            if (p == q)
                return NULL;
            while (*p & 0x80)
                if (++p == q)
                    return NULL;
            p++;
        }
    return p;
}

static const uint8_t *
parse_timing(const uint8_t *p, const uint8_t *q)
{
    if (p == q)
        return NULL;
    uint8_t scale = *p;
    if ((scale & 3) != 3 && (++p == q))
        return NULL;
    scale >>= 2;
    if ((scale & 7) != 7 && (++p == q))
        return NULL;
    scale >>= 3;
    if ((scale != 7) && (++p == q))
        return NULL;
    return p + 1;
}

static const uint8_t *
parse_io(const uint8_t *p, const uint8_t *q, cis_info_t *ci)
{
    if (p == q)
        return NULL;
    ci->io_lines = *p & 0x1f;
    ci->io_len   = 1 << ci->io_lines;
    if (!(*p & 0x80))
        return p + 1;
    if (++p == q)
        return NULL;
    const int nwin = (*p & 0x0f) + 1;
    int       bsz  = (*p & 0x30) >> 4;
    int       lsz  = (*p & 0xc0) >> 6;
    if (bsz == 3)
        bsz++;
    if (lsz == 3)
        lsz++;
    p++;
    for (int i = 0; i < nwin; i++) {
        int len = 1;
        for (int j = 0; j < bsz; j++, p++)
            if (p == q)
                return NULL;
        for (int j = 0; j < lsz; j++, p++) {
            if (p == q)
                return NULL;
            len += *p << (j * 8);
        }
        if (i == 0)
            ci->io_len = len;
    }
    return p;
}

/* parse_cftable_entry. */
static int
parse_cftable(const uint8_t *p, const uint8_t *q, cis_info_t *ci)
{
    const int index = *p & 0x3f;
    if (*p & 0x80)
        if (++p == q)
            return 0;
    if (++p == q)
        return 0;
    const uint8_t features = *p++;
    for (int i = 0; i < (features & 3); i++)
        if ((p = parse_power(p, q)) == NULL)
            return 0;
    if ((features & 0x04) && ((p = parse_timing(p, q)) == NULL))
        return 0;
    if ((features & 0x08) && ((p = parse_io(p, q, ci)) == NULL))
        return 0;
    if (features & 0x10) {
        if (p == q)
            return 0;
        const uint8_t info1 = *p++;
        if (info1 & 0x10) {
            if (p + 2 > q)
                return 0;
            p += 2;
        }
    }
    switch (features & 0x60) {
        case 0x20: p += 2; break;
        case 0x40: p += 4; break;
        case 0x60: return 0;   /* the cards here have no memory spaces */
        default: break;
    }
    if (p > q)
        return 0;
    if (features & 0x80) {
        if (p == q)
            return 0;
        while (*p & 0x80)
            if (++p == q)
                return 0;
    }
    if (ci->has_config && (index == ci->last_idx))
        ci->cft_has_last = 1;
    return 1;
}

/* get_first_tuple / get_next_tuple over attribute memory, each tuple
   through its parser. */
static void
walk(const uint8_t *cis, int len, cis_info_t *ci)
{
    int i = 0;

    memset(ci, 0, sizeof(*ci));
    ci->funcid = -1;
    ci->ok     = 1;
    while (i < len) {
        const uint8_t code = cis[i];
        if (code == CISTPL_NULL) {
            i++;
            continue;
        }
        if (code == CISTPL_END) {
            ci->end = i;
            return;
        }
        if (i + 1 >= len) {
            ci->ok = 0;
            bad    = "truncated tuple header";
            return;
        }
        const int      n = cis[i + 1];
        const uint8_t *p = &cis[i + 2];
        const uint8_t *q = p + n;
        if (i + 2 + n > len) {
            ci->ok = 0;
            bad    = "tuple runs past the CIS";
            return;
        }
        if (ci->ntuples++ == 0)
            ci->first_device = (code == CISTPL_DEVICE);
        int ok = 1;
        switch (code) {
            case CISTPL_DEVICE:
                ok = (n > 0) && parse_device(p, q, ci);
                break;
            case CISTPL_NO_LINK:
                ci->no_link = 1;
                ok          = (n == 0);
                break;
            case CISTPL_VERS_1:
                ok = parse_vers1(p, q, ci) && (p[0] == 4 || p[0] == 5);
                break;
            case CISTPL_MANFID:
                ok             = (n >= 4);
                ci->has_manfid = 1;
                ci->manf       = p[0] | (p[1] << 8);
                ci->card       = p[2] | (p[3] << 8);
                break;
            case CISTPL_FUNCID:
                ok         = (n >= 2);
                ci->funcid = p[0];
                break;
            case CISTPL_CONFIG: {
                const int rasz = p[0] & 3;
                const int rmsz = (p[0] & 0x3c) >> 2;
                ok             = (n >= rasz + rmsz + 4);
                ci->has_config = 1;
                ci->last_idx   = p[1];
                ci->config_base = 0;
                for (int k = 0; k <= rasz; k++)
                    ci->config_base |= (uint32_t) p[2 + k] << (8 * k);
                ci->rmask = p[3 + rasz];
                break;
            }
            case CISTPL_CFTABLE_ENTRY:
                ok = parse_cftable(p, q, ci);
                break;
            default:
                break;
        }
        if (!ok) {
            ci->ok = 0;
            bad    = "a tuple did not parse";
            return;
        }
        i += 2 + n;
    }
    ci->ok = 0;
    bad    = "no CISTPL_END";
}

/* The rules for any card. */
static void
check_card(const char *name, const uint8_t *cis, int len, cis_info_t *ci)
{
    bad = "";
    walk(cis, len, ci);
    CHECK(ci->ok, "%s: the CIS parses (%s)", name, bad);
    CHECK(ci->first_device, "%s: the CIS starts with CISTPL_DEVICE", name);
    CHECK(ci->vers_ns >= 2, "%s: VERS_1 has a manufacturer and a product (%d strings)", name, ci->vers_ns);
    CHECK(ci->no_link, "%s: NO_LINK, so nobody looks for a CIS in common memory", name);
    if (ci->has_config) {
        CHECK(ci->cft_has_last, "%s: a CFTABLE entry for the last index, %d", name, ci->last_idx);
        CHECK(ci->config_base >= (uint32_t) (2 * len), "%s: configuration registers at %X clear of the CIS (ends %X)", name,
              ci->config_base, 2 * len);
        CHECK(!(ci->config_base & 1), "%s: configuration registers at an even address", name);
        CHECK(ci->rmask & 1, "%s: a Configuration Option Register", name);
    }
}

/* The ID Windows 9x gave three versions of the TE100's CIS (read from a
   Windows 98 SE registry), against pccard_cis_win9x_crc(). */
static void
test_win9x_crc(void)
{
    static const char *base = "01 03 00 00 FF 15 29 04 01 46 61 73 74 20 45 74 68 65 72 6E 65 74 00 31 36 2D 62 69 74 20 50 43 20 43 61 72 64 00 00 41 58 38 38 31 39 30 00 FF 20 04 49 01 AB C1 21 02 06 00 22 08 04 06 00 e0 98 bc c4 ed ";
    static const struct {
        const char *config;
        uint16_t    id;
    } seen[3] = {
        { "1A 05 01 01 C0 03 03", 0xcbf4 },
        { "1A 09 01 01 C0 03 03 C1 02 06 8D", 0x90e9 },
        { "1A 09 01 01 C0 03 03 C1 02 47 A6", 0xc8c0 },
    };

    for (int t = 0; t < 3; t++) {
        char    hex[512];
        uint8_t cis[256];
        int     n = 0;
        snprintf(hex, sizeof(hex), "%s%s 1B 0D C1 01 19 01 55 E5 60 00 03 1F 30 B8 DE FF", base, seen[t].config);
        for (char *s = hex; *s;) {
            unsigned v;
            int      used;
            if (sscanf(s, "%x%n", &v, &used) != 1)
                break;
            cis[n++] = (uint8_t) v;
            s += used;
        }
        const uint16_t crc = pccard_cis_win9x_crc(cis, n);
        CHECK(crc == seen[t].id, "Windows 98 named CIS %d ...-%04X; computed %04X", t, seen[t].id, crc);
    }
}

static void
test_te100(void)
{
    static const uint8_t mac[6] = { 0x00, 0xe0, 0x98, 0x12, 0x34, 0x56 };
    uint8_t              cis[256];
    cis_info_t           ci;
    char                 id[128];
    const int            n = pccard_cis_te100pc16(cis, mac);

    check_card("TE100-PC16", cis, n, &ci);
    CHECK(ci.funcid == CISTPL_FUNCID_NETWORK, "TE100-PC16: a network adapter");
    CHECK(ci.has_manfid && ci.manf == 0x0149 && ci.card == 0xc1ab, "TE100-PC16: MANFID 0149:C1AB (axnet_cs)");
    CHECK(ci.config_base == 0x3c0, "TE100-PC16: COR at 3C0h, where axnet_cs wants it");
    CHECK(ci.io_lines == 5 && ci.io_len == 32, "TE100-PC16: 32 ports on 5 lines (%d on %d)", ci.io_len, ci.io_lines);
    pccard_cis_win9x_id(cis, n, id, sizeof(id));
    CHECK(!strcmp(id, "PCMCIA\\Fast_Ethernet-16-bit_PC_Card-8B43"), "TE100-PC16: Windows 9x ID %s (its INF's)", id);
}

static void
test_3c589d(void)
{
    uint8_t    cis[256];
    cis_info_t ci;
    char       id[128];
    const int  n = pccard_cis_3c589d(cis);

    check_card("3C589D", cis, n, &ci);
    CHECK(ci.funcid == CISTPL_FUNCID_NETWORK, "3C589D: a network adapter");
    CHECK(ci.has_manfid && ci.manf == 0x0101 && ci.card == 0x0589, "3C589D: MANFID 0101:0589, 3Com's 3C589 (not the 3C562)");
    CHECK(ci.io_lines == 16 && ci.io_len == 16, "3C589D: 16 ports decoding 16 lines (%d on %d)", ci.io_len, ci.io_lines);
    pccard_cis_win9x_id(cis, n, id, sizeof(id));
    CHECK(!strcmp(id, "PCMCIA\\3Com_Corporation-3C589D-9CA6"), "3C589D: Windows 9x ID %s (NET3C589.INF's)", id);
}

/* The APA-1460: the Technical Reference's ranges, and the ID Windows 98's
   SCSI.INF lists -- with the comma of "Adaptec, Inc." made an underscore. */
static void
test_apa1460(void)
{
    uint8_t    cis[256];
    cis_info_t ci;
    char       id[128];
    const int  n = pccard_cis_apa1460(cis);

    check_card("APA-1460", cis, n, &ci);
    CHECK(!strcmp(ci.vers[0], "Adaptec, Inc.") && !strcmp(ci.vers[1], "APA-1460 SCSI Host Adapter"),
          "APA-1460: VERS_1 as pcmcia-cs's config binds aha152x_cs (%s, %s)", ci.vers[0], ci.vers[1]);
    CHECK(ci.config_base == 0x2000 && ci.rmask == 0x01, "APA-1460: COR alone, at 2000h (%X)", ci.config_base);
    CHECK(ci.io_lines == 10 && ci.io_len == 32, "APA-1460: 32 ports on 10 lines (%d on %d)", ci.io_len, ci.io_lines);
    pccard_cis_win9x_id(cis, n, id, sizeof(id));
    CHECK(!strcmp(id, "PCMCIA\\Adaptec__Inc.-APA-1460_SCSI_Host_Adapter-BE89"), "APA-1460: Windows 9x ID %s (SCSI.INF's)", id);
}

/* The Hayes Accura 56K PC Card: a serial function, the COR and CCSR, its
   default range COM1's on ten lines, the last index an "anywhere" one on
   three, and the ID Windows 98's MDMHAY2.INF lists. */
static void
test_accura56k(void)
{
    uint8_t    cis[256];
    cis_info_t ci;
    char       id[128];
    uint32_t   cfg = 0;
    const int  n   = pccard_cis_accura56k(cis, &cfg);

    check_card("Accura 56K", cis, n, &ci);
    CHECK(ci.funcid == CISTPL_FUNCID_SERIAL, "Accura 56K: a serial port (serial_cs binds it)");
    CHECK(ci.config_base == 0x200 && cfg == 0x200 && ci.rmask == 0x03, "Accura 56K: COR and CCSR at 200h (%X, %X)",
          ci.config_base, cfg);
    CHECK(ci.last_idx == 5 && ci.cft_has_last, "Accura 56K: five configurations (last %d)", ci.last_idx);
    CHECK(ci.io_lines == 3 && ci.io_len == 8, "Accura 56K: the last, 8 ports anywhere on 3 lines (%d on %d)", ci.io_len,
          ci.io_lines);
    int cft = 0;
    while ((cft < n) && (cis[cft] != CISTPL_CFTABLE_ENTRY))
        cft += 2 + cis[cft + 1];
    CHECK((cft < n) && (cis[cft + 2] == (0x80 | 0x41)) && (cis[cft + 9] == 0xf8) && (cis[cft + 10] == 0x03),
          "Accura 56K: the first, the default, index 1 at COM1's 3F8h");
    pccard_cis_win9x_id(cis, n, id, sizeof(id));
    CHECK(!strcmp(id, "PCMCIA\\Hayes-Accura_56K_PC_CARD-EBBB"), "Accura 56K: Windows 9x ID %s (MDMHAY2.INF's)", id);
}

int
main(void)
{
    test_win9x_crc();
    test_te100();
    test_3c589d();
    test_apa1460();
    test_accura56k();
    printf("%d checks, %d failures\n", checks, failures);
    return failures != 0;
}
