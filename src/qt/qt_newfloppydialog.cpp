/*
 * 86Box	A hypervisor and IBM PC system emulator that specializes in
 *		running old operating systems and software designed for IBM
 *		PC systems and compatibles from 1981 through fairly recent
 *		system designs based on the PCI bus.
 *
 *		This file is part of the 86Box distribution.
 *
 *		Common storage devices module.
 *
 *
 *
 * Authors:	Joakim L. Gilje <jgilje@jgilje.net>
 *          Cacodemon345
 *          Teemu Korhonen
 *
 *		Copyright 2021 Joakim L. Gilje
 *      Copyright 2022 Cacodemon345
 *      Copyright 2022 Teemu Korhonen
 */
#include "qt_newfloppydialog.hpp"
#include "ui_qt_newfloppydialog.h"

#include "qt_models_common.hpp"
#include "qt_util.hpp"

extern "C" {
#include <86box/plat.h>
#include <86box/random.h>
#include <86box/scsi_device.h>
#include <86box/zip.h>
#include <86box/mo.h>
}

#include <cstdio>
#include <cstdlib>

#include <QFile>
#include <QFileInfo>
#include <QMessageBox>
#include <QProgressDialog>
#include <thread>
#include <QStringBuilder>

struct disk_size_t {
    int hole;
    int sides;
    int data_rate;
    int encoding;
    int rpm;
    int tracks;
    int sectors;	/* For IMG and Japanese FDI only. */
    int sector_len;	/* For IMG and Japanese FDI only. */
    int media_desc;
    int spc;
    int num_fats;
    int spfat;
    int root_dir_entries;
};

static const disk_size_t disk_sizes[14] = {	{	0,  1, 2, 1, 0,  40,  8, 2, 0xfe, 2, 2,  1,  64 },		/* 160k */
                                           {	0,  1, 2, 1, 0,  40,  9, 2, 0xfc, 2, 2,  1,  64 },		/* 180k */
                                           {	0,  2, 2, 1, 0,  40,  8, 2, 0xff, 2, 2,  1, 112 },		/* 320k */
                                           {	0,  2, 2, 1, 0,  40,  9, 2, 0xfd, 2, 2,  2, 112 },		/* 360k */
                                           {	0,  2, 2, 1, 0,  80,  8, 2, 0xfb, 2, 2,  2, 112 },		/* 640k */
                                           {	0,  2, 2, 1, 0,  80,  9, 2, 0xf9, 2, 2,  3, 112 },		/* 720k */
                                           {	1,  2, 0, 1, 1,  80, 15, 2, 0xf9, 1, 2,  7, 224 },		/* 1.2M */
                                           {	1,  2, 0, 1, 1,  77,  8, 3, 0xfe, 1, 2,  2, 192 },		/* 1.25M */
                                           {	1,  2, 0, 1, 0,  80, 18, 2, 0xf0, 1, 2,  9, 224 },		/* 1.44M */
                                           {	1,  2, 0, 1, 0,  80, 21, 2, 0xf0, 2, 2,  5,  16 },		/* DMF cluster 1024 */
                                           {	1,  2, 0, 1, 0,  80, 21, 2, 0xf0, 4, 2,  3,  16 },		/* DMF cluster 2048 */
                                           {	2,  2, 3, 1, 0,  80, 36, 2, 0xf0, 2, 2,  9, 240 },		/* 2.88M */
                                           {	0, 64, 0, 0, 0,  96, 32, 2,    0, 0, 0,  0,   0 },		/* ZIP 100 */
                                           {	0, 64, 0, 0, 0, 239, 32, 2,    0, 0, 0,  0,   0 }	};	/* ZIP 250 */

static const QStringList rpmModes = {
    "Perfect RPM",
    "1% below perfect RPM",
    "1.5% below perfect RPM",
    "2% below perfect RPM",
};

static const QStringList floppyTypes = {
    "160 kB",
    "180 kB",
    "320 kB",
    "360 kB",
    "640 kB",
    "720 kB",
    "1.2 MB",
    "1.25 MB",
    "1.44 MB",
    "DMF (cluster 1024)",
    "DMF (cluster 2048)",
    "2.88 MB",
};

static const QStringList zipTypes = {
    "ZIP 100",
    "ZIP 250",
};

static const QStringList moTypes = {
    "3.5\" 128 MB (ISO 10090)",
    "3.5\" 230 MB (ISO 13963)",
    "3.5\" 540 MB (ISO 15498)",
    "3.5\" 640 MB (ISO 15498)",
    "3.5\" 1.3 GB (GigaMO)",
    "3.5\" 2.3 GB (GigaMO 2)",
    "5.25\" 600 MB",
    "5.25\" 650 MB",
    "5.25\" 1 GB",
    "5.25\" 1.3 GB",
};

NewFloppyDialog::NewFloppyDialog(MediaType type, QWidget *parent) :
    QDialog(parent),
    ui(new Ui::NewFloppyDialog),
    mediaType_(type)
{
    ui->setupUi(this);
    ui->fileField->setCreateFile(true);

    auto* model = ui->comboBoxSize->model();
    switch (type) {
    case MediaType::Floppy:
        for (int i = 0; i < floppyTypes.size(); ++i) {
            Models::AddEntry(model, tr(floppyTypes[i].toUtf8().data()), i);
        }
        ui->fileField->setFilter(
            tr("All images") %
            util::DlgFilter({ "86f","dsk","flp","im?","*fd?" }) %
            tr("Basic sector images") %
            util::DlgFilter({ "dsk","flp","im?","img","*fd?" }) %
            tr("Surface images") %
            util::DlgFilter({ "86f" }, true));

        break;
    case MediaType::Zip:
        for (int i = 0; i < zipTypes.size(); ++i) {
            Models::AddEntry(model, tr(zipTypes[i].toUtf8().data()), i);
        }
        ui->fileField->setFilter(tr("ZIP images") % util::DlgFilter({ "im?","zdi" }, true));
        break;
    case MediaType::Mo:
        for (int i = 0; i < moTypes.size(); ++i) {
            Models::AddEntry(model, tr(moTypes[i].toUtf8().data()), i);
        }
        ui->fileField->setFilter(tr("MO images") % util::DlgFilter({ "im?","mdi" }) % tr("All files") % util::DlgFilter({ "*" }, true));
        break;
    }

    model = ui->comboBoxRpm->model();
    for (int i = 0; i < rpmModes.size(); ++i) {
        Models::AddEntry(model, tr(rpmModes[i].toUtf8().data()), i);
    }

    connect(ui->fileField, &FileField::fileSelected, this, [this](const QString& filename) {
        bool hide = true;
        if (mediaType_ == MediaType::Floppy) {
            if (QFileInfo(filename).suffix().toLower() == QStringLiteral("86f")) {
                hide = false;
            }
        }

        ui->labelRpm->setHidden(hide);
        ui->comboBoxRpm->setHidden(hide);
    });
    connect(ui->buttonBox, &QDialogButtonBox::accepted, this, &NewFloppyDialog::onCreate);

    ui->labelRpm->setHidden(true);
    ui->comboBoxRpm->setHidden(true);
}

NewFloppyDialog::~NewFloppyDialog() {
    delete ui;
}

QString NewFloppyDialog::fileName() const{
    return ui->fileField->fileName();
}

void NewFloppyDialog::onCreate() {
    auto filename = ui->fileField->fileName();
    QFileInfo fi(filename);
    FileType fileType;

    QProgressDialog progress("Creating floppy image", QString(), 0, 100, this);
    connect(this, &NewFloppyDialog::fileProgress, &progress, &QProgressDialog::setValue);
    switch (mediaType_) {
    case MediaType::Floppy:
        if (fi.suffix().toLower() == QStringLiteral("86f")) {
            if (create86f(filename, disk_sizes[ui->comboBoxSize->currentIndex()], ui->comboBoxRpm->currentIndex())) {
                return;
            }
        } else {
            fileType = fi.suffix().toLower() == QStringLiteral("zdi") ? FileType::Fdi : FileType::Img;
            if (createSectorImage(filename, disk_sizes[ui->comboBoxSize->currentIndex()], fileType)) {
                return;
            }
        }
        break;
    case MediaType::Zip:
    {
        fileType = fi.suffix().toLower() == QStringLiteral("zdi") ? FileType::Zdi: FileType::Img;

        std::atomic_bool res;
        std::thread t([this, &res, filename, fileType, &progress] {
            res = createZipSectorImage(filename, disk_sizes[ui->comboBoxSize->currentIndex() + 12], fileType, progress);
        });
        progress.exec();
        t.join();

        if (res) {
            return;
        }
    }
        break;
    case MediaType::Mo:
    {
        fileType = fi.suffix().toLower() == QStringLiteral("mdi") ? FileType::Mdi: FileType::Img;

        std::atomic_bool res;
        std::thread t([this, &res, filename, fileType, &progress] {
            res = createMoSectorImage(filename, ui->comboBoxSize->currentIndex(), fileType, progress);
        });
        progress.exec();
        t.join();

        if (res) {
            return;
        }
    }
        break;
    }

    QMessageBox::critical(this, tr("Unable to write file"), tr("Make sure the file is being saved to a writable directory"));
    reject();
}

bool NewFloppyDialog::create86f(const QString& filename, const disk_size_t& disk_size, uint8_t rpm_mode)
{
    FILE *f;

    uint32_t magic = 0x46423638;
    uint16_t version = 0x020C;
    uint16_t dflags = 0;
    uint16_t tflags = 0;
    uint32_t index_hole_pos = 0;
    uint32_t tarray[512];
    uint32_t array_size;
    uint32_t track_base, track_size;
    int i;
    uint32_t shift = 0;

    dflags = 0;					/* Has surface data? - Assume no for now. */
    dflags |= (disk_size.hole << 1);		/* Hole */
    dflags |= ((disk_size.sides - 1) << 3);	/* Sides. */
    dflags |= (0 << 4);				/* Write protect? - Assume no for now. */
    dflags |= (rpm_mode << 5);			/* RPM mode. */
    dflags |= (0 << 7);				/* Has extra bit cells? - Assume no for now. */

    tflags = disk_size.data_rate;		/* Data rate. */
    tflags |= (disk_size.encoding << 3);	/* Encoding. */
    tflags |= (disk_size.rpm << 5);		/* RPM. */

    switch (disk_size.hole) {
	case 0:
	case 1:
	default:
		switch(rpm_mode) {
			case 1:
				array_size = 25250;
				break;
			case 2:
				array_size = 25374;
				break;
			case 3:
				array_size = 25750;
				break;
			default:
				array_size = 25000;
				break;
		}
		break;
	case 2:
		switch(rpm_mode) {
			case 1:
				array_size = 50500;
				break;
			case 2:
				array_size = 50750;
				break;
			case 3:
				array_size = 51000;
				break;
			default:
				array_size = 50000;
				break;
		}
		break;
    }

    auto empty = (unsigned char *) malloc(array_size);

    memset(tarray, 0, 2048);
    memset(empty, 0, array_size);

    f = plat_fopen(filename.toUtf8().data(), "wb");
    if (!f)
	return false;

    fwrite(&magic, 4, 1, f);
    fwrite(&version, 2, 1, f);
    fwrite(&dflags, 2, 1, f);

    track_size = array_size + 6;

    track_base = 8 + ((disk_size.sides == 2) ? 2048 : 1024);

    if (disk_size.tracks <= 43)
	shift = 1;

    for (i = 0; i < (disk_size.tracks * disk_size.sides) << shift; i++)
	tarray[i] = track_base + (i * track_size);

    fwrite(tarray, 1, (disk_size.sides == 2) ? 2048 : 1024, f);

    for (i = 0; i < (disk_size.tracks * disk_size.sides) << shift; i++) {
	fwrite(&tflags, 2, 1, f);
	fwrite(&index_hole_pos, 4, 1, f);
	fwrite(empty, 1, array_size, f);
    }

    free(empty);

    fclose(f);

    return true;
}

/* Ignore false positive warning caused by a bug on gcc */
#if __GNUC__ >= 11
#pragma GCC diagnostic ignored "-Wstringop-overflow"
#endif

bool NewFloppyDialog::createSectorImage(const QString &filename, const disk_size_t& disk_size, FileType type)
{
    uint32_t total_size = 0;
    uint32_t total_sectors = 0;
    uint32_t sector_bytes = 0;
    uint32_t root_dir_bytes = 0;
    uint32_t fat_size = 0;
    uint32_t fat1_offs = 0;
    uint32_t fat2_offs = 0;
    uint32_t zero_bytes = 0;
    uint16_t base = 0x1000;

    QFile file(filename);
    if (! file.open(QIODevice::WriteOnly)) {
        return false;
    }
    QDataStream stream(&file);
    stream.setByteOrder(QDataStream::LittleEndian);

    sector_bytes = (128 << disk_size.sector_len);
    total_sectors = disk_size.sides * disk_size.tracks * disk_size.sectors;
    if (total_sectors > ZIP_SECTORS)
        total_sectors = ZIP_250_SECTORS;
    total_size = total_sectors * sector_bytes;
    root_dir_bytes = (disk_size.root_dir_entries << 5);
    fat_size = (disk_size.spfat * sector_bytes);
    fat1_offs = sector_bytes;
    fat2_offs = fat1_offs + fat_size;
    zero_bytes = fat2_offs + fat_size + root_dir_bytes;

    if (type == FileType::Fdi) {
        QByteArray bytes(base, 0);
        auto empty = bytes.data();
        *(uint32_t *) &(empty[0x08]) = (uint32_t) base;
        *(uint32_t *) &(empty[0x0C]) = total_size;
        *(uint16_t *) &(empty[0x10]) = (uint16_t) sector_bytes;
        *(uint8_t *)  &(empty[0x14]) = (uint8_t)  disk_size.sectors;
        *(uint8_t *)  &(empty[0x18]) = (uint8_t)  disk_size.sides;
        *(uint8_t *)  &(empty[0x1C]) = (uint8_t)  disk_size.tracks;
        stream.writeRawData(empty, base);
    }

    QByteArray bytes(total_size, 0);
    auto empty = bytes.data();

    memset(empty + zero_bytes, 0xF6, total_size - zero_bytes);

    empty[0x00] = 0xEB;			/* Jump to make MS-DOS happy. */
    empty[0x01] = 0x58;
    empty[0x02] = 0x90;

    empty[0x03] = 0x38;			/* '86BOX5.0' OEM ID. */
    empty[0x04] = 0x36;
    empty[0x05] = 0x42;
    empty[0x06] = 0x4F;
    empty[0x07] = 0x58;
    empty[0x08] = 0x35;
    empty[0x09] = 0x2E;
    empty[0x0A] = 0x30;

    *(uint16_t *) &(empty[0x0B]) = (uint16_t) sector_bytes;
    *(uint8_t  *) &(empty[0x0D]) = (uint8_t)  disk_size.spc;
    *(uint16_t *) &(empty[0x0E]) = (uint16_t) 1;
    *(uint8_t  *) &(empty[0x10]) = (uint8_t)  disk_size.num_fats;
    *(uint16_t *) &(empty[0x11]) = (uint16_t) disk_size.root_dir_entries;
    *(uint16_t *) &(empty[0x13]) = (uint16_t) total_sectors;
    *(uint8_t *)  &(empty[0x15]) = (uint8_t)  disk_size.media_desc;
    *(uint16_t *) &(empty[0x16]) = (uint16_t) disk_size.spfat;
    *(uint8_t *)  &(empty[0x18]) = (uint8_t)  disk_size.sectors;
    *(uint8_t *)  &(empty[0x1A]) = (uint8_t)  disk_size.sides;

    empty[0x26] = 0x29;			/* ')' followed by randomly-generated volume serial number. */
    empty[0x27] = random_generate();
    empty[0x28] = random_generate();
    empty[0x29] = random_generate();
    empty[0x2A] = random_generate();

    memset(&(empty[0x2B]), 0x20, 11);

    empty[0x36] = 'F';
    empty[0x37] = 'A';
    empty[0x38] = 'T';
    empty[0x39] = '1';
    empty[0x3A] = '2';
    memset(&(empty[0x3B]), 0x20, 0x0003);

    empty[0x1FE] = 0x55;
    empty[0x1FF] = 0xAA;

    empty[fat1_offs + 0x00] = empty[fat2_offs + 0x00] = empty[0x15];
    empty[fat1_offs + 0x01] = empty[fat2_offs + 0x01] = 0xFF;
    empty[fat1_offs + 0x02] = empty[fat2_offs + 0x02] = 0xFF;

    stream.writeRawData(empty, total_size);
    return true;
}

bool NewFloppyDialog::createZipSectorImage(const QString &filename, const disk_size_t& disk_size, FileType type, QProgressDialog& pbar)
{
    uint32_t total_size = 0;
    uint32_t total_sectors = 0;
    uint32_t sector_bytes = 0;
    uint32_t root_dir_bytes = 0;
    uint32_t fat_size = 0;
    uint32_t fat1_offs = 0;
    uint32_t fat2_offs = 0;
    uint32_t zero_bytes = 0;
    uint16_t base = 0x1000;
    uint32_t pbar_max = 0;

    QFile file(filename);
    if (! file.open(QIODevice::WriteOnly)) {
        return false;
    }
    QDataStream stream(&file);
    stream.setByteOrder(QDataStream::LittleEndian);

    sector_bytes = (128 << disk_size.sector_len);
    total_sectors = disk_size.sides * disk_size.tracks * disk_size.sectors;
    if (total_sectors > ZIP_SECTORS)
        total_sectors = ZIP_250_SECTORS;
    total_size = total_sectors * sector_bytes;
    root_dir_bytes = (disk_size.root_dir_entries << 5);
    fat_size = (disk_size.spfat * sector_bytes);
    fat1_offs = sector_bytes;
    fat2_offs = fat1_offs + fat_size;
    zero_bytes = fat2_offs + fat_size + root_dir_bytes;

    pbar_max = total_size;
    if (type == FileType::Zdi) {
        pbar_max += base;
    }
    pbar_max >>= 11;

    if (type == FileType::Zdi) {
        QByteArray data(base, 0);
        auto empty = data.data();

        *(uint32_t *) &(empty[0x08]) = (uint32_t) base;
        *(uint32_t *) &(empty[0x0C]) = total_size;
        *(uint16_t *) &(empty[0x10]) = (uint16_t) sector_bytes;
        *(uint8_t *)  &(empty[0x14]) = (uint8_t)  disk_size.sectors;
        *(uint8_t *)  &(empty[0x18]) = (uint8_t)  disk_size.sides;
        *(uint8_t *)  &(empty[0x1C]) = (uint8_t)  disk_size.tracks;

        stream.writeRawData(empty, base);
        pbar_max -= 2;
    }

    QByteArray bytes(total_size, 0);
    auto empty = bytes.data();

    if (total_sectors == ZIP_SECTORS) {
        /* ZIP 100 */
        /* MBR */
        *(uint64_t *) &(empty[0x0000]) = 0x2054524150492EEBLL;
        *(uint64_t *) &(empty[0x0008]) = 0x3930302065646F63LL;
        *(uint64_t *) &(empty[0x0010]) = 0x67656D6F49202D20LL;
        *(uint64_t *) &(empty[0x0018]) = 0x726F70726F432061LL;
        *(uint64_t *) &(empty[0x0020]) = 0x202D206E6F697461LL;
        *(uint64_t *) &(empty[0x0028]) = 0x30392F33322F3131LL;

        *(uint64_t *) &(empty[0x01AE]) = 0x0116010100E90644LL;
        *(uint64_t *) &(empty[0x01B6]) = 0xED08BBE5014E0135LL;
        *(uint64_t *) &(empty[0x01BE]) = 0xFFFFFE06FFFFFE80LL;
        *(uint64_t *) &(empty[0x01C6]) = 0x0002FFE000000020LL;

        *(uint16_t *) &(empty[0x01FE]) = 0xAA55;

        /* 31 sectors filled with 0x48 */
        memset(&(empty[0x0200]), 0x48, 0x3E00);

        /* Boot sector */
        *(uint64_t *) &(empty[0x4000]) = 0x584F4236389058EBLL;
        *(uint64_t *) &(empty[0x4008]) = 0x0008040200302E35LL;
        *(uint64_t *) &(empty[0x4010]) = 0x00C0F80000020002LL;
        *(uint64_t *) &(empty[0x4018]) = 0x0000002000FF003FLL;
        *(uint32_t *) &(empty[0x4020]) = 0x0002FFE0;
        *(uint16_t *) &(empty[0x4024]) = 0x0080;

        empty[0x4026] = 0x29;			/* ')' followed by randomly-generated volume serial number. */
        empty[0x4027] = random_generate();
        empty[0x4028] = random_generate();
        empty[0x4029] = random_generate();
        empty[0x402A] = random_generate();

        memset(&(empty[0x402B]), 0x00, 0x000B);
        memset(&(empty[0x4036]), 0x20, 0x0008);

        empty[0x4036] = 'F';
        empty[0x4037] = 'A';
        empty[0x4038] = 'T';
        empty[0x4039] = '1';
        empty[0x403A] = '6';
        memset(&(empty[0x403B]), 0x20, 0x0003);

        empty[0x41FE] = 0x55;
        empty[0x41FF] = 0xAA;

        empty[0x5000] = empty[0x1D000] = empty[0x4015];
        empty[0x5001] = empty[0x1D001] = 0xFF;
        empty[0x5002] = empty[0x1D002] = 0xFF;
        empty[0x5003] = empty[0x1D003] = 0xFF;

        /* Root directory = 0x35000
       Data = 0x39000 */
    } else {
        /* ZIP 250 */
        /* MBR */
        *(uint64_t *) &(empty[0x0000]) = 0x2054524150492EEBLL;
        *(uint64_t *) &(empty[0x0008]) = 0x3930302065646F63LL;
        *(uint64_t *) &(empty[0x0010]) = 0x67656D6F49202D20LL;
        *(uint64_t *) &(empty[0x0018]) = 0x726F70726F432061LL;
        *(uint64_t *) &(empty[0x0020]) = 0x202D206E6F697461LL;
        *(uint64_t *) &(empty[0x0028]) = 0x30392F33322F3131LL;

        *(uint64_t *) &(empty[0x01AE]) = 0x0116010100E900E9LL;
        *(uint64_t *) &(empty[0x01B6]) = 0x2E32A7AC014E0135LL;

        *(uint64_t *) &(empty[0x01EE]) = 0xEE203F0600010180LL;
        *(uint64_t *) &(empty[0x01F6]) = 0x000777E000000020LL;
        *(uint16_t *) &(empty[0x01FE]) = 0xAA55;

        /* 31 sectors filled with 0x48 */
        memset(&(empty[0x0200]), 0x48, 0x3E00);

        /* The second sector begins with some strange data
       in my reference image. */
        *(uint64_t *) &(empty[0x0200]) = 0x3831393230334409LL;
        *(uint64_t *) &(empty[0x0208]) = 0x6A57766964483130LL;
        *(uint64_t *) &(empty[0x0210]) = 0x3C3A34676063653FLL;
        *(uint64_t *) &(empty[0x0218]) = 0x586A56A8502C4161LL;
        *(uint64_t *) &(empty[0x0220]) = 0x6F2D702535673D6CLL;
        *(uint64_t *) &(empty[0x0228]) = 0x255421B8602D3456LL;
        *(uint64_t *) &(empty[0x0230]) = 0x577B22447B52603ELL;
        *(uint64_t *) &(empty[0x0238]) = 0x46412CC871396170LL;
        *(uint64_t *) &(empty[0x0240]) = 0x704F55237C5E2626LL;
        *(uint64_t *) &(empty[0x0248]) = 0x6C7932C87D5C3C20LL;
        *(uint64_t *) &(empty[0x0250]) = 0x2C50503E47543D6ELL;
        *(uint64_t *) &(empty[0x0258]) = 0x46394E807721536ALL;
        *(uint64_t *) &(empty[0x0260]) = 0x505823223F245325LL;
        *(uint64_t *) &(empty[0x0268]) = 0x365C79B0393B5B6ELL;

        /* Boot sector */
        *(uint64_t *) &(empty[0x4000]) = 0x584F4236389058EBLL;
        *(uint64_t *) &(empty[0x4008]) = 0x0001080200302E35LL;
        *(uint64_t *) &(empty[0x4010]) = 0x00EFF80000020002LL;
        *(uint64_t *) &(empty[0x4018]) = 0x0000002000400020LL;
        *(uint32_t *) &(empty[0x4020]) = 0x000777E0;
        *(uint16_t *) &(empty[0x4024]) = 0x0080;

        empty[0x4026] = 0x29;			/* ')' followed by randomly-generated volume serial number. */
        empty[0x4027] = random_generate();
        empty[0x4028] = random_generate();
        empty[0x4029] = random_generate();
        empty[0x402A] = random_generate();

        memset(&(empty[0x402B]), 0x00, 0x000B);
        memset(&(empty[0x4036]), 0x20, 0x0008);

        empty[0x4036] = 'F';
        empty[0x4037] = 'A';
        empty[0x4038] = 'T';
        empty[0x4039] = '1';
        empty[0x403A] = '6';
        memset(&(empty[0x403B]), 0x20, 0x0003);

        empty[0x41FE] = 0x55;
        empty[0x41FF] = 0xAA;

        empty[0x4200] = empty[0x22000] = empty[0x4015];
        empty[0x4201] = empty[0x22001] = 0xFF;
        empty[0x4202] = empty[0x22002] = 0xFF;
        empty[0x4203] = empty[0x22003] = 0xFF;

        /* Root directory = 0x3FE00
       Data = 0x38200 */
    }

    pbar.setMaximum(pbar_max);
    for (uint32_t i = 0; i < pbar_max; i++) {
        stream.writeRawData(&empty[i << 11], 2048);
        fileProgress(i);
    }
    fileProgress(pbar_max);

    return true;
}


bool NewFloppyDialog::createMoSectorImage(const QString& filename, int8_t disk_size, FileType type, QProgressDialog& pbar)
{
    const mo_type_t *dp = &mo_types[disk_size];
    uint32_t total_size = 0, total_size2;
    uint32_t total_sectors = 0;
    uint32_t sector_bytes = 0;
    uint16_t base = 0x1000;
    uint32_t pbar_max = 0, blocks_num;

    QFile file(filename);
    if (! file.open(QIODevice::WriteOnly)) {
        return false;
    }
    QDataStream stream(&file);
    stream.setByteOrder(QDataStream::LittleEndian);

    sector_bytes = dp->bytes_per_sector;
    total_sectors = dp->sectors;
    total_size = total_sectors * sector_bytes;

    total_size2 = (total_size >> 20) << 20;
    total_size2 = total_size - total_size2;

    pbar_max = total_size;
    pbar_max >>= 20;
    blocks_num = pbar_max;
    if (type == FileType::Mdi)
        pbar_max++;
    if (total_size2 == 0)
        pbar_max++;

    if (type == FileType::Mdi) {
        QByteArray bytes(base, 0);
        auto empty = bytes.data();

        *(uint32_t *) &(empty[0x08]) = (uint32_t) base;
        *(uint32_t *) &(empty[0x0C]) = total_size;
        *(uint16_t *) &(empty[0x10]) = (uint16_t) sector_bytes;
        *(uint8_t *)  &(empty[0x14]) = (uint8_t)  25;
        *(uint8_t *)  &(empty[0x18]) = (uint8_t)  64;
        *(uint8_t *)  &(empty[0x1C]) = (uint8_t)  (dp->sectors / 64) / 25;

        stream.writeRawData(empty, base);
    }

    QByteArray bytes(1048576, 0);
    auto empty = bytes.data();

    pbar.setMaximum(blocks_num);
    for (uint32_t i = 0; i < blocks_num; i++) {
        stream.writeRawData(empty, bytes.size());
        fileProgress(i);
    }

    if (total_size2 > 0) {
        QByteArray extra_bytes(total_size2, 0);
        stream.writeRawData(extra_bytes.data(), total_size2);
    }
    fileProgress(blocks_num);

    return true;
}
