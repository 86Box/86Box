/*
 * 86Box       CompactFlash PC Card: CIS, ATA I/O and raw image persistence.
 *
 *             Released under the GNU General Public License version 2 or
 *             later. See COPYING for more information.
 */
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>
#include <86box/86box.h>
#include <86box/device.h>
#include <86box/hdc_ide.h>
#include <86box/hdd.h>
#include <86box/pcmcia.h>
#include <86box/plat.h>
#include <86box/plat_unused.h>

extern const device_t pccard_cf_device;
extern void ide_test_run_timer(void);

static const pccard_t *card;
static int irq, wp, failures;
static char image[L_tmpnam];

#define CHECK(c) do { if (!(c)) { printf("FAIL at line %d: %s\n", __LINE__, #c); failures++; } } while (0)

int device_get_instance(void) { return 1; }
void pclog(UNUSED(const char *fmt), ...) { }
FILE *plat_fopen64(const char *path, const char *mode) { return fopen(path, mode); }
void pcmcia_insert(UNUSED(int socket), const pccard_t *inserted) { card = inserted; }
void pcmcia_card_irq(UNUSED(int socket), int level) { irq = level; }

const char *
pcmcia_cf_target_image(UNUSED(int socket), int *write_protect)
{
    *write_protect = wp;
    return image;
}

#define BASE 0x320
#define OUT(reg, value) card->io_write(BASE + (reg), (value), card->priv)
#define IN(reg) card->io_read(BASE + (reg), card->priv)
#define OUTW(value) card->io_writew(BASE, (value), card->priv)
#define INW() card->io_readw(BASE, card->priv)
#define COR(value) card->attr_write(0x200, (value), card->priv)

static void
set_lba(uint8_t sector, uint8_t count)
{
    OUT(6, 0xe0);
    OUT(2, count);
    OUT(3, sector);
    OUT(4, 0);
    OUT(5, 0);
}

static void *
open_fixed_disk(void)
{
    hdd[0].bus_type = HDD_BUS_IDE;
    hdd[0].ide_channel = 0;
    hdd[0].tracks = 1;
    hdd[0].hpc = 1;
    hdd[0].spt = 16;
    hdd[0].max_multiple_block = 8;
    return ide_xtide_init();
}

static void
test_fixed_disk(void *board)
{
    ide_writeb(7, 0xec, board);
    CHECK(ide_readb(7, board) == 0x58);
    CHECK(ide_readw(0, board) == 0x0040); /* fixed ATA IDENTIFY signature */
    for (int i = 1; i < 256; i++) {
        const uint16_t word = ide_readw(0, board);
        if (i == 49)
            CHECK(!(word & 0x0200)); /* small CHS geometry */
    }

    ide_writeb(2, 1, board);
    ide_writeb(7, 0x30, board);
    CHECK(ide_readb(7, board) & 8);
    for (int i = 0; i < 256; i++)
        ide_writew(0, 0x4321, board);
    CHECK(ide_read_alt_status(0, board) & 0x80);
    ide_test_run_timer();
    CHECK(ide_readb(7, board) == 0x50);

    ide_writeb(2, 1, board);
    ide_writeb(7, 0x20, board);
    CHECK(ide_read_alt_status(0, board) & 0x80);
    ide_test_run_timer();
    CHECK(ide_readb(7, board) & 8);
    CHECK(ide_readb(0, board) == 0x21); /* fixed-drive byte I/O consumes a word */
    for (int i = 1; i < 256; i++)
        CHECK(ide_readw(0, board) == 0x4321);
    CHECK(ide_readb(7, board) == 0x50);
}

int
main(void)
{
    uint8_t cis[256];
    char    id[64];

    if (!tmpnam(image))
        return 1;
    FILE *fp = fopen(image, "wb");
    if (!fp)
        return 1;
    for (int i = 0; i < 4 * 512; i++)
        fputc(i / 512 + 1, fp);
    fclose(fp);

    void *fixed = open_fixed_disk();
    void *dev = pccard_cf_device.init(&pccard_cf_device);
    if (!dev || !card) {
        ide_xtide_close();
        remove(image);
        return 1;
    }
    for (unsigned i = 0; i < sizeof(cis); i++)
        cis[i] = card->attr_read(i * 2, card->priv);
    pccard_cis_win9x_id(cis, sizeof(cis), id, sizeof(id));
    CHECK(!strcmp(id, "PCMCIA\\SunDisk-SDP-6DC7"));

    card->common_write(7, 0xec, card->priv);
    CHECK(card->common_readw(0, card->priv) == 0x848a);
    card->reset(card->priv);
    CHECK(IN(7) == 0xff);

    COR(0x41);
    CHECK(IN(7) == 0x50);
    test_fixed_disk(fixed);
    OUT(7, 0xec);
    CHECK(irq && (IN(14) & 8));
    CHECK(irq && (IN(7) & 8));
    CHECK(!irq && (INW() == 0x848a));
    for (int i = 1; i < 256; i++) {
        uint16_t word = INW();
        if (i == 60)
            CHECK(word == 4);
    }
    CHECK(IN(7) == 0x50);

    set_lba(1, 2);
    OUT(7, 0x20);
    for (int sector = 2; sector <= 3; sector++) {
        CHECK(irq && (IN(7) & 8));
        for (int i = 0; i < 256; i++)
            CHECK(INW() == sector * 0x0101);
    }
    CHECK(!irq && (IN(7) == 0x50) && !IN(2));

    set_lba(0, 1);
    OUT(7, 0x20);
    CHECK(IN(7) & 8);
    CHECK(IN(0) == 1);
    for (int i = 0; i < 255; i++)
        CHECK(INW() == 0x0101);
    CHECK(IN(0) == 1 && IN(7) == 0x50);

    OUT(2, 2);
    OUT(7, 0xc6);
    CHECK(IN(7) == 0x50);
    set_lba(0, 2);
    OUT(7, 0xc4);
    CHECK(irq && (IN(7) & 8));
    for (int i = 0; i < 512; i++)
        CHECK(INW() == (i < 256 ? 0x0101 : 0x0202));
    CHECK(!irq && IN(7) == 0x50);

    OUT(7, 0xa1); /* IDENTIFY PACKET DEVICE */
    CHECK((IN(7) & 1) && IN(1) == 4);
    set_lba(0, 1);
    OUT(7, 0xc8); /* DMA is unavailable on the private channel. */
    CHECK((IN(7) & 1) && IN(1) == 4);

    OUT(14, 4);
    CHECK(IN(14) & 0x80);
    OUT(14, 0);
    CHECK(IN(7) == 0x50 && IN(1) == 1);

    OUT(6, 0xb0);
    CHECK(IN(7) == 0xff);
    OUT(6, 0xa0);
    CHECK(IN(7) == 0x50);

    set_lba(3, 2);
    OUT(7, 0x20);
    CHECK((IN(7) & 1) && (IN(1) == 0x10));

    set_lba(2, 1);
    OUT(7, 0x30);
    CHECK(!irq && (IN(7) & 8));
    for (int i = 0; i < 256; i++)
        OUTW(0xa55a);
    CHECK(irq && (IN(7) == 0x50));
    pccard_cf_device.close(dev);
    CHECK(!card);

    wp = 1;
    dev = pccard_cf_device.init(&pccard_cf_device);
    if (!dev || !card) {
        ide_xtide_close();
        remove(image);
        return 1;
    }
    COR(0x41);
    OUT(14, 2);
    set_lba(2, 1);
    OUT(7, 0x20);
    CHECK(!irq && (IN(14) & 8));
    OUT(14, 0);
    CHECK(irq && (IN(7) & 8));
    for (int i = 0; i < 256; i++)
        CHECK(INW() == 0xa55a);

    set_lba(2, 1);
    OUT(7, 0x30);
    CHECK((IN(7) & 0x09) == 0x01 && IN(1) == 4);

    COR(0x42);
    CHECK(IN(7) == 0xff);
    card->io_write(0x1f7, 0xec, card->priv);
    CHECK(card->io_readw(0x1f0, card->priv) == 0x848a);
    COR(0x43);
    card->io_write(0x177, 0xec, card->priv);
    CHECK(card->io_readw(0x170, card->priv) == 0x848a);

    pccard_cf_device.close(dev);
    CHECK(!card);
    test_fixed_disk(fixed);
    ide_xtide_close();
    remove(image);
    printf("CompactFlash test: %d failures\n", failures);
    return failures ? 1 : 0;
}
