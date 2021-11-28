#include "qt_machinestatus.hpp"
#include "ui_qt_machinestatus.h"

extern "C" {
#define EMU_CPU_H // superhack - don't want timer.h to include cpu.h here, and some combo is preventing a compile
extern uint64_t		tsc;

#include <86box/hdd.h>
#include <86box/timer.h>
#include <86box/86box.h>
#include <86box/device.h>
#include <86box/cartridge.h>
#include <86box/cassette.h>
#include <86box/cdrom.h>
#include <86box/fdd.h>
#include <86box/hdc.h>
#include <86box/scsi.h>
#include <86box/scsi_device.h>
#include <86box/zip.h>
#include <86box/mo.h>
#include <86box/plat.h>
#include <86box/machine.h>
#include <86box/network.h>
#include <86box/ui.h>
};

#include <QIcon>
#include <QPicture>
#include <QLabel>
#include <QTimer>

namespace {
    struct PixmapSetActive {
        QPixmap normal;
        QPixmap active;
        void load(const QString& basePath);
    };
    struct PixmapSetEmpty {
        QPixmap normal;
        QPixmap empty;
        void load(const QString& basePath);
    };
    struct PixmapSetEmptyActive {
        QPixmap normal;
        QPixmap active;
        QPixmap empty;
        QPixmap empty_active;
        void load(const QString& basePath);
    };
    struct Pixmaps {
        PixmapSetEmpty cartridge;
        PixmapSetEmptyActive cassette;
        PixmapSetEmptyActive floppy_disabled;
        PixmapSetEmptyActive floppy_525;
        PixmapSetEmptyActive floppy_35;
        PixmapSetEmptyActive cdrom;
        PixmapSetEmptyActive zip;
        PixmapSetEmptyActive mo;
        PixmapSetActive hd;
        PixmapSetActive net;
        QPixmap sound;
    };

    struct StateActive {
        QLabel label;
        QTimer timer;
        PixmapSetActive* pixmaps = nullptr;
        bool active = false;

        void setActive(bool b) {
            active = b;
            label.setPixmap(active ? pixmaps->active : pixmaps->normal);
            timer.start(75);
        }
    };
    struct StateEmpty {
        QLabel label;
        PixmapSetEmpty* pixmaps = nullptr;
        bool empty = false;

        void setEmpty(bool e) {
            empty = e;
            label.setPixmap(empty ? pixmaps->empty : pixmaps->normal);
        }
    };
    struct StateEmptyActive {
        QLabel label;
        QTimer timer;
        PixmapSetEmptyActive* pixmaps = nullptr;
        bool empty = false;
        bool active = false;

        void setActive(bool b) {
            active = b;
            refresh();
            timer.start(75);
        }
        void setEmpty(bool b) {
            empty = b;
            refresh();
        }
        void refresh() {
            if (empty) {
                label.setPixmap(active ? pixmaps->empty_active : pixmaps->empty);
            } else {
                label.setPixmap(active ? pixmaps->active : pixmaps->normal);
            }
        }
    };

    static const QSize pixmap_size(32, 32);
    static const QString pixmap_empty = QStringLiteral("_empty");
    static const QString pixmap_active = QStringLiteral("_active");
    static const QString pixmap_empty_active = QStringLiteral("_empty_active");
    void PixmapSetEmpty::load(const QString &basePath) {
        normal = QIcon(basePath.arg(QStringLiteral(""))).pixmap(pixmap_size);
        empty = QIcon(basePath.arg(pixmap_empty)).pixmap(pixmap_size);
    }

    void PixmapSetActive::load(const QString &basePath) {
        normal = QIcon(basePath.arg(QStringLiteral(""))).pixmap(pixmap_size);
        active = QIcon(basePath.arg(pixmap_active)).pixmap(pixmap_size);
    }

    void PixmapSetEmptyActive::load(const QString &basePath) {
        normal = QIcon(basePath.arg(QStringLiteral(""))).pixmap(pixmap_size);
        active = QIcon(basePath.arg(pixmap_active)).pixmap(pixmap_size);
        empty = QIcon(basePath.arg(pixmap_empty)).pixmap(pixmap_size);
        empty_active = QIcon(basePath.arg(pixmap_empty_active)).pixmap(pixmap_size);
    }
}

struct MachineStatus::States {
    Pixmaps pixmaps;

    States(QObject* parent) {
        pixmaps.cartridge.load(":/settings/win/icons/cartridge%1.ico");
        pixmaps.cassette.load(":/settings/win/icons/cassette%1.ico");
        pixmaps.floppy_disabled.normal = QIcon(QStringLiteral(":/settings/win/icons/floppy_disabled.ico")).pixmap(pixmap_size);
        pixmaps.floppy_disabled.active = pixmaps.floppy_disabled.normal;
        pixmaps.floppy_disabled.empty = pixmaps.floppy_disabled.normal;
        pixmaps.floppy_disabled.empty_active = pixmaps.floppy_disabled.normal;
        pixmaps.floppy_525.load(":/settings/win/icons/floppy_525%1.ico");
        pixmaps.floppy_35.load(":/settings/win/icons/floppy_35%1.ico");
        pixmaps.cdrom.load(":/settings/win/icons/cdrom%1.ico");
        pixmaps.zip.load(":/settings/win/icons/zip%1.ico");
        pixmaps.mo.load(":/settings/win/icons/mo%1.ico");
        pixmaps.hd.load(":/settings/win/icons/hard_disk%1.ico");
        pixmaps.net.load(":/settings/win/icons/network%1.ico");
        pixmaps.sound = QIcon(":/settings/win/icons/sound.ico").pixmap(pixmap_size);

        cartridge[0].pixmaps = &pixmaps.cartridge;
        cartridge[1].pixmaps = &pixmaps.cartridge;
        cassette.pixmaps = &pixmaps.cassette;
        QObject::connect(&cassette.timer, &QTimer::timeout, parent, [&]{ cassette.setActive(false); });
        for (auto& f : fdd) {
            f.pixmaps = &pixmaps.floppy_disabled;
            QObject::connect(&f.timer, &QTimer::timeout, parent, [&]{ f.setActive(false); });
        }
        for (auto& c : cdrom) {
            c.pixmaps = &pixmaps.cdrom;
            QObject::connect(&c.timer, &QTimer::timeout, parent, [&]{ c.setActive(false); });
        }
        for (auto& z : zip) {
            z.pixmaps = &pixmaps.zip;
            QObject::connect(&z.timer, &QTimer::timeout, parent, [&]{ z.setActive(false); });
        }
        for (auto& m : mo) {
            m.pixmaps = &pixmaps.mo;
            QObject::connect(&m.timer, &QTimer::timeout, parent, [&]{ m.setActive(false); });
        }
        for (auto& h : hdds) {
            h.pixmaps = &pixmaps.hd;
            QObject::connect(&h.timer, &QTimer::timeout, parent, [&]{ h.setActive(false); });
        }
        net.pixmaps = &pixmaps.net;
        sound.setPixmap(pixmaps.sound);
    }

    std::array<StateEmpty, 2> cartridge;
    StateEmptyActive cassette;
    std::array<StateEmptyActive, FDD_NUM> fdd;
    std::array<StateEmptyActive, CDROM_NUM> cdrom;
    std::array<StateEmptyActive, ZIP_NUM> zip;
    std::array<StateEmptyActive, MO_NUM> mo;
    std::array<StateActive, HDD_BUS_USB> hdds;
    StateActive net;
    QLabel sound;
};

MachineStatus::MachineStatus(QWidget *parent) :
    QWidget(parent),
    ui(new Ui::MachineStatus)
{
    ui->setupUi(this);
    d = std::make_unique<MachineStatus::States>(this);
}

MachineStatus::~MachineStatus()
{
    delete ui;
}

static int hdd_count(int bus) {
    int c = 0;
    int i;

    for (i = 0; i < HDD_NUM; i++) {
        if (hdd[i].bus == bus) {
            c++;
        }
    }

    return(c);
}

void MachineStatus::refresh() {
    bool has_cart = machines[machine].flags & MACHINE_CARTRIDGE;
    bool has_mfm = machines[machine].flags & MACHINE_MFM;
    bool has_xta = machines[machine].flags & MACHINE_XTA;
    bool has_esdi = machines[machine].flags & MACHINE_ESDI;
    bool has_ide = machines[machine].flags & MACHINE_IDE_QUAD;
    bool has_scsi = machines[machine].flags & MACHINE_SCSI_DUAL;

    int c_mfm = hdd_count(HDD_BUS_MFM);
    int c_esdi = hdd_count(HDD_BUS_ESDI);
    int c_xta = hdd_count(HDD_BUS_XTA);
    int c_ide = hdd_count(HDD_BUS_IDE);
    int c_scsi = hdd_count(HDD_BUS_SCSI);
    int do_net = (network_type == NET_TYPE_NONE) || (network_card == 0);

    while (ui->statusIcons->count() > 0) {
        auto item = ui->statusIcons->itemAt(0);
        ui->statusIcons->removeItem(item);
        delete item;
    }

    if (cassette_enable) {
        d->cassette.setEmpty(QString(cassette_fname).isEmpty());
        ui->statusIcons->addWidget(&d->cassette.label);
    }

    if (has_cart) {
        for (int i = 0; i < 2; ++i) {
            d->cartridge[i].setEmpty(QString(cart_fns[i]).isEmpty());
            ui->statusIcons->addWidget(&d->cartridge[i].label);
        }
    }

    for (size_t i = 0; i < FDD_NUM; ++i) {
        if (fdd_get_type(i) != 0) {
            int t = fdd_get_type(i);
            if (t == 0) {
                d->fdd[i].pixmaps = &d->pixmaps.floppy_disabled;
            } else if (t >= 1 && t <= 6) {
                d->fdd[i].pixmaps = &d->pixmaps.floppy_525;
            } else {
                d->fdd[i].pixmaps = &d->pixmaps.floppy_35;
            }
            d->fdd[i].setEmpty(QString(floppyfns[i]).isEmpty());
            d->fdd[i].setActive(false);
            ui->statusIcons->addWidget(&d->fdd[i].label);
        }
    }

    auto hdc_name = QString(hdc_get_internal_name(hdc_current));
    for (size_t i = 0; i < CDROM_NUM; i++) {
        /* Could be Internal or External IDE.. */
        if ((cdrom[i].bus_type == CDROM_BUS_ATAPI) &&
            !has_ide && hdc_name != QStringLiteral("ide"))
            continue;
        if ((cdrom[i].bus_type == CDROM_BUS_SCSI) && !has_scsi &&
            (scsi_card_current[0] == 0) && (scsi_card_current[1] == 0) &&
            (scsi_card_current[2] == 0) && (scsi_card_current[3] == 0))
            continue;
        if (cdrom[i].bus_type != 0) {
            d->cdrom[i].setEmpty(cdrom[i].host_drive != 200 || QString(cdrom[i].image_path).isEmpty());
            d->cdrom[i].setActive(false);
            ui->statusIcons->addWidget(&d->cdrom[i].label);
        }
    }
    for (size_t i = 0; i < ZIP_NUM; i++) {
        /* Could be Internal or External IDE.. */
        if ((zip_drives[i].bus_type == ZIP_BUS_ATAPI) &&
            !has_ide && hdc_name != QStringLiteral("ide"))
            continue;
        if ((zip_drives[i].bus_type == ZIP_BUS_SCSI) && !has_scsi &&
            (scsi_card_current[0] == 0) && (scsi_card_current[1] == 0) &&
            (scsi_card_current[2] == 0) && (scsi_card_current[3] == 0))
            continue;
        if (zip_drives[i].bus_type != 0) {
            d->zip[i].setEmpty(QString(zip_drives[i].image_path).isEmpty());
            d->zip[i].setActive(false);
            ui->statusIcons->addWidget(&d->zip[i].label);
        }
    }
    for (size_t i = 0; i < MO_NUM; i++) {
        /* Could be Internal or External IDE.. */
        if ((mo_drives[i].bus_type == MO_BUS_ATAPI) &&
            !has_ide && hdc_name != QStringLiteral("ide"))
            continue;
        if ((mo_drives[i].bus_type == MO_BUS_SCSI) && !has_scsi &&
            (scsi_card_current[0] == 0) && (scsi_card_current[1] == 0) &&
            (scsi_card_current[2] == 0) && (scsi_card_current[3] == 0))
            continue;
        if (mo_drives[i].bus_type != 0) {
            d->mo[i].setEmpty(QString(mo_drives[i].image_path).isEmpty());
            d->mo[i].setActive(false);
            ui->statusIcons->addWidget(&d->mo[i].label);
        }
    }

    if ((has_mfm || hdc_name == QStringLiteral("st506")) && c_mfm > 0) {
        d->hdds[HDD_BUS_MFM].setActive(false);
        ui->statusIcons->addWidget(&d->hdds[HDD_BUS_MFM].label);
    }
    if ((has_esdi || hdc_name == QStringLiteral("esdi")) && c_esdi > 0) {
        d->hdds[HDD_BUS_ESDI].setActive(false);
        ui->statusIcons->addWidget(&d->hdds[HDD_BUS_ESDI].label);
    }
    if ((has_xta || hdc_name == QStringLiteral("xta")) && c_xta > 0) {
        d->hdds[HDD_BUS_XTA].setActive(false);
        ui->statusIcons->addWidget(&d->hdds[HDD_BUS_XTA].label);
    }
    if ((has_ide || hdc_name == QStringLiteral("xtide") || hdc_name == QStringLiteral("ide")) && c_ide > 0) {
        d->hdds[HDD_BUS_IDE].setActive(false);
        ui->statusIcons->addWidget(&d->hdds[HDD_BUS_IDE].label);
    }
    if ((has_scsi || (scsi_card_current[0] != 0) || (scsi_card_current[1] != 0) ||
         (scsi_card_current[2] != 0) || (scsi_card_current[3] != 0)) && c_scsi > 0) {
        d->hdds[HDD_BUS_SCSI].setActive(false);
        ui->statusIcons->addWidget(&d->hdds[HDD_BUS_SCSI].label);
    }

    if (do_net) {
        d->net.setActive(false);
        ui->statusIcons->addWidget(&d->net.label);
    }
    ui->statusIcons->addWidget(&d->sound);
    ui->statusIcons->addItem(new QSpacerItem(20, 40, QSizePolicy::Expanding, QSizePolicy::Minimum));
}

void MachineStatus::setActivity(int tag, bool active) {
    int category = tag & 0xfffffff0;
    int item = tag & 0xf;
    switch (category) {
    case SB_CASSETTE:
        break;
    case SB_CARTRIDGE:
        break;
    case SB_FLOPPY:
        d->fdd[item].setActive(active);
        break;
    case SB_CDROM:
        d->cdrom[item].setActive(active);
        break;
    case SB_ZIP:
        d->zip[item].setActive(active);
        break;
    case SB_MO:
        d->mo[item].setActive(active);
        break;
    case SB_HDD:
        d->hdds[item].setActive(active);
        break;
    case SB_NETWORK:
        d->net.setActive(active);
        break;
    case SB_SOUND:
        break;
    case SB_TEXT:
        break;
    }
}

void MachineStatus::setEmpty(int tag, bool empty) {
    int category = tag & 0xfffffff0;
    int item = tag & 0xf;
    switch (category) {
    case SB_CASSETTE:
        d->cassette.setEmpty(empty);
        break;
    case SB_CARTRIDGE:
        d->cartridge[item].setEmpty(empty);
        break;
    case SB_FLOPPY:
        d->fdd[item].setEmpty(empty);
        break;
    case SB_CDROM:
        d->cdrom[item].setEmpty(empty);
        break;
    case SB_ZIP:
        d->zip[item].setEmpty(empty);
        break;
    case SB_MO:
        d->mo[item].setEmpty(empty);
        break;
    case SB_HDD:
        break;
    case SB_NETWORK:
        break;
    case SB_SOUND:
        break;
    case SB_TEXT:
        break;
    }
}

