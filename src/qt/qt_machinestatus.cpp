/*
 * 86Box	A hypervisor and IBM PC system emulator that specializes in
 *		running old operating systems and software designed for IBM
 *		PC systems and compatibles from 1981 through fairly recent
 *		system designs based on the PCI bus.
 *
 *		This file is part of the 86Box distribution.
 *
 *		Joystick configuration UI module.
 *
 *
 *
 * Authors:	Joakim L. Gilje <jgilje@jgilje.net>
 *          Cacodemon345
 *
 *		Copyright 2021 Joakim L. Gilje
 *      Copyright 2021-2022 Cacodemon345
 */
#include "qt_machinestatus.hpp"

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
#include <QStatusBar>
#include <QMenu>
#include <QScreen>

#include "qt_mediamenu.hpp"
#include "qt_mainwindow.hpp"
#include "qt_soundgain.hpp"
#include "qt_progsettings.hpp"

#include <array>

extern MainWindow* main_window;

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
        void load(QString basePath);
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
        std::unique_ptr<QLabel> label;
        QTimer timer;
        PixmapSetActive* pixmaps = nullptr;
        bool active = false;

        void setActive(bool b) {
            active = b;
            if (! label) {
                return;
            }
            label->setPixmap(active ? pixmaps->active : pixmaps->normal);
            timer.start(75);
        }
    };
    struct StateEmpty {
        std::unique_ptr<QLabel> label;
        PixmapSetEmpty* pixmaps = nullptr;
        bool empty = false;

        void setEmpty(bool e) {
            empty = e;
            if (! label) {
                return;
            }
            label->setPixmap(empty ? pixmaps->empty : pixmaps->normal);
        }
    };
    struct StateEmptyActive {
        std::unique_ptr<QLabel> label;
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
            if (! label) {
                return;
            }
            if (empty) {
                label->setPixmap(active ? pixmaps->empty_active : pixmaps->empty);
            } else {
                label->setPixmap(active ? pixmaps->active : pixmaps->normal);
            }
        }
    };

    static QSize pixmap_size(16, 16);
    static const QString pixmap_empty = QStringLiteral("_empty");
    static const QString pixmap_active = QStringLiteral("_active");
    static const QString pixmap_empty_active = QStringLiteral("_empty_active");
    void PixmapSetEmpty::load(const QString &basePath) {
        normal = ProgSettings::loadIcon(basePath.arg(QStringLiteral(""))).pixmap(pixmap_size);
        empty = ProgSettings::loadIcon(basePath.arg(pixmap_empty)).pixmap(pixmap_size);
    }

    void PixmapSetActive::load(const QString &basePath) {
        normal = ProgSettings::loadIcon(basePath.arg(QStringLiteral(""))).pixmap(pixmap_size);
        active = ProgSettings::loadIcon(basePath.arg(pixmap_active)).pixmap(pixmap_size);
    }

    void PixmapSetEmptyActive::load(QString basePath) {
        normal = ProgSettings::loadIcon(basePath.arg(QStringLiteral(""))).pixmap(pixmap_size);
        active = ProgSettings::loadIcon(basePath.arg(pixmap_active)).pixmap(pixmap_size);
        empty = ProgSettings::loadIcon(basePath.arg(pixmap_empty)).pixmap(pixmap_size);
        empty_active = ProgSettings::loadIcon(basePath.arg(pixmap_empty_active)).pixmap(pixmap_size);
    }
}

struct MachineStatus::States {
    Pixmaps pixmaps;

    States(QObject* parent) {
        pixmaps.cartridge.load("/cartridge%1.ico");
        pixmaps.cassette.load("/cassette%1.ico");
        pixmaps.floppy_disabled.normal = ProgSettings::loadIcon(QStringLiteral("/floppy_disabled.ico")).pixmap(pixmap_size);
        pixmaps.floppy_disabled.active = pixmaps.floppy_disabled.normal;
        pixmaps.floppy_disabled.empty = pixmaps.floppy_disabled.normal;
        pixmaps.floppy_disabled.empty_active = pixmaps.floppy_disabled.normal;
        pixmaps.floppy_525.load("/floppy_525%1.ico");
        pixmaps.floppy_35.load("/floppy_35%1.ico");
        pixmaps.cdrom.load("/cdrom%1.ico");
        pixmaps.zip.load("/zip%1.ico");
        pixmaps.mo.load("/mo%1.ico");
        pixmaps.hd.load("/hard_disk%1.ico");
        pixmaps.net.load("/network%1.ico");
        pixmaps.sound = ProgSettings::loadIcon("/sound.ico").pixmap(pixmap_size);

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
    }

    std::array<StateEmpty, 2> cartridge;
    StateEmptyActive cassette;
    std::array<StateEmptyActive, FDD_NUM> fdd;
    std::array<StateEmptyActive, CDROM_NUM> cdrom;
    std::array<StateEmptyActive, ZIP_NUM> zip;
    std::array<StateEmptyActive, MO_NUM> mo;
    std::array<StateActive, HDD_BUS_USB> hdds;
    StateActive net;
    std::unique_ptr<ClickableLabel> sound;
    std::unique_ptr<QLabel> text;
};

MachineStatus::MachineStatus(QObject *parent) :
    QObject(parent)
{
    d = std::make_unique<MachineStatus::States>(this);
}

MachineStatus::~MachineStatus() = default;

bool MachineStatus::hasCassette() {
    return cassette_enable > 0 ? true : false;
}

bool MachineStatus::hasIDE() {
    return machine_has_flags(machine, MACHINE_IDE_QUAD) > 0;
}

bool MachineStatus::hasSCSI() {
    return machine_has_flags(machine, MACHINE_SCSI_DUAL) > 0;
}

void MachineStatus::iterateFDD(const std::function<void (int)> &cb) {
    for (int i = 0; i < FDD_NUM; ++i) {
        if (fdd_get_type(i) != 0) {
            cb(i);
        }
    }
}

void MachineStatus::iterateCDROM(const std::function<void (int)> &cb) {
    auto hdc_name = QString(hdc_get_internal_name(hdc_current));
    for (size_t i = 0; i < CDROM_NUM; i++) {
        /* Could be Internal or External IDE.. */
        if ((cdrom[i].bus_type == CDROM_BUS_ATAPI) &&
            !hasIDE() && hdc_name.left(3) != QStringLiteral("ide") &&
            hdc_name.left(5) != QStringLiteral("xtide"))
            continue;
        if ((cdrom[i].bus_type == CDROM_BUS_SCSI) && !hasSCSI() &&
            (scsi_card_current[0] == 0) && (scsi_card_current[1] == 0) &&
            (scsi_card_current[2] == 0) && (scsi_card_current[3] == 0))
            continue;
        if (cdrom[i].bus_type != 0) {
            cb(i);
        }
    }
}

void MachineStatus::iterateZIP(const std::function<void (int)> &cb) {
    auto hdc_name = QString(hdc_get_internal_name(hdc_current));
    for (size_t i = 0; i < ZIP_NUM; i++) {
        /* Could be Internal or External IDE.. */
        if ((zip_drives[i].bus_type == ZIP_BUS_ATAPI) &&
            !hasIDE() && hdc_name.left(3) != QStringLiteral("ide") &&
            hdc_name.left(5) != QStringLiteral("xtide"))
            continue;
        if ((zip_drives[i].bus_type == ZIP_BUS_SCSI) && !hasSCSI() &&
            (scsi_card_current[0] == 0) && (scsi_card_current[1] == 0) &&
            (scsi_card_current[2] == 0) && (scsi_card_current[3] == 0))
            continue;
        if (zip_drives[i].bus_type != 0) {
            cb(i);
        }
    }
}

void MachineStatus::iterateMO(const std::function<void (int)> &cb) {
    auto hdc_name = QString(hdc_get_internal_name(hdc_current));
    for (size_t i = 0; i < MO_NUM; i++) {
        /* Could be Internal or External IDE.. */
        if ((mo_drives[i].bus_type == MO_BUS_ATAPI) &&
            !hasIDE() && hdc_name.left(3) != QStringLiteral("ide") &&
            hdc_name.left(5) != QStringLiteral("xtide"))
            continue;
        if ((mo_drives[i].bus_type == MO_BUS_SCSI) && !hasSCSI() &&
            (scsi_card_current[0] == 0) && (scsi_card_current[1] == 0) &&
            (scsi_card_current[2] == 0) && (scsi_card_current[3] == 0))
            continue;
        if (mo_drives[i].bus_type != 0) {
            cb(i);
        }
    }
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

void MachineStatus::refresh(QStatusBar* sbar) {
    bool has_mfm = machine_has_flags(machine, MACHINE_MFM) > 0;
    bool has_xta = machine_has_flags(machine, MACHINE_XTA) > 0;
    bool has_esdi = machine_has_flags(machine, MACHINE_ESDI) > 0;

    int c_mfm = hdd_count(HDD_BUS_MFM);
    int c_esdi = hdd_count(HDD_BUS_ESDI);
    int c_xta = hdd_count(HDD_BUS_XTA);
    int c_ide = hdd_count(HDD_BUS_IDE);
    int c_scsi = hdd_count(HDD_BUS_SCSI);
    int do_net = network_available();

    sbar->removeWidget(d->cassette.label.get());
    for (int i = 0; i < 2; ++i) {
        sbar->removeWidget(d->cartridge[i].label.get());
    }
    for (size_t i = 0; i < FDD_NUM; ++i) {
        sbar->removeWidget(d->fdd[i].label.get());
    }
    for (size_t i = 0; i < CDROM_NUM; i++) {
        sbar->removeWidget(d->cdrom[i].label.get());
    }
    for (size_t i = 0; i < ZIP_NUM; i++) {
        sbar->removeWidget(d->zip[i].label.get());
    }
    for (size_t i = 0; i < MO_NUM; i++) {
        sbar->removeWidget(d->mo[i].label.get());
    }
    for (size_t i = 0; i < HDD_BUS_USB; i++) {
        sbar->removeWidget(d->hdds[i].label.get());
    }
    sbar->removeWidget(d->net.label.get());
    sbar->removeWidget(d->sound.get());

    if (cassette_enable) {
        d->cassette.label = std::make_unique<ClickableLabel>();
        d->cassette.setEmpty(QString(cassette_fname).isEmpty());
        connect((ClickableLabel*)d->cassette.label.get(), &ClickableLabel::clicked, [](QPoint pos) {
            MediaMenu::ptr->cassetteMenu->popup(pos - QPoint(0, MediaMenu::ptr->cassetteMenu->sizeHint().height()));
        });
        d->cassette.label->setToolTip(MediaMenu::ptr->cassetteMenu->title());
        sbar->addWidget(d->cassette.label.get());
    }

    if (machine_has_cartridge(machine)) {
        for (int i = 0; i < 2; ++i) {
            d->cartridge[i].label = std::make_unique<ClickableLabel>();
            d->cartridge[i].setEmpty(QString(cart_fns[i]).isEmpty());
            connect((ClickableLabel*)d->cartridge[i].label.get(), &ClickableLabel::clicked, [i](QPoint pos) {
                MediaMenu::ptr->cartridgeMenus[i]->popup(pos - QPoint(0, MediaMenu::ptr->cartridgeMenus[i]->sizeHint().height()));
            });
            d->cartridge[i].label->setToolTip(MediaMenu::ptr->cartridgeMenus[i]->title());
            sbar->addWidget(d->cartridge[i].label.get());
        }
    }

    iterateFDD([this, sbar](int i) {
        int t = fdd_get_type(i);
        if (t == 0) {
            d->fdd[i].pixmaps = &d->pixmaps.floppy_disabled;
        } else if (t >= 1 && t <= 6) {
            d->fdd[i].pixmaps = &d->pixmaps.floppy_525;
        } else {
            d->fdd[i].pixmaps = &d->pixmaps.floppy_35;
        }
        d->fdd[i].label = std::make_unique<ClickableLabel>();
        d->fdd[i].setEmpty(QString(floppyfns[i]).isEmpty());
        d->fdd[i].setActive(false);
        connect((ClickableLabel*)d->fdd[i].label.get(), &ClickableLabel::clicked, [i](QPoint pos) {
            MediaMenu::ptr->floppyMenus[i]->popup(pos - QPoint(0, MediaMenu::ptr->floppyMenus[i]->sizeHint().height()));
        });
        d->fdd[i].label->setToolTip(MediaMenu::ptr->floppyMenus[i]->title());
        sbar->addWidget(d->fdd[i].label.get());
    });

    iterateCDROM([this, sbar](int i) {
        d->cdrom[i].label = std::make_unique<ClickableLabel>();
        d->cdrom[i].setEmpty(cdrom[i].host_drive != 200 || QString(cdrom[i].image_path).isEmpty());
        d->cdrom[i].setActive(false);
        connect((ClickableLabel*)d->cdrom[i].label.get(), &ClickableLabel::clicked, [i](QPoint pos) {
            MediaMenu::ptr->cdromMenus[i]->popup(pos - QPoint(0, MediaMenu::ptr->cdromMenus[i]->sizeHint().height()));
        });
        d->cdrom[i].label->setToolTip(MediaMenu::ptr->cdromMenus[i]->title());
        sbar->addWidget(d->cdrom[i].label.get());
    });

    iterateZIP([this, sbar](int i) {
        d->zip[i].label = std::make_unique<ClickableLabel>();
        d->zip[i].setEmpty(QString(zip_drives[i].image_path).isEmpty());
        d->zip[i].setActive(false);
        connect((ClickableLabel*)d->zip[i].label.get(), &ClickableLabel::clicked, [i](QPoint pos) {
            MediaMenu::ptr->zipMenus[i]->popup(pos - QPoint(0, MediaMenu::ptr->zipMenus[i]->sizeHint().height()));
        });
        d->zip[i].label->setToolTip(MediaMenu::ptr->zipMenus[i]->title());
        sbar->addWidget(d->zip[i].label.get());
    });

    iterateMO([this, sbar](int i) {
        d->mo[i].label = std::make_unique<ClickableLabel>();
        d->mo[i].setEmpty(QString(mo_drives[i].image_path).isEmpty());
        d->mo[i].setActive(false);
        connect((ClickableLabel*)d->mo[i].label.get(), &ClickableLabel::clicked, [i](QPoint pos) {
            MediaMenu::ptr->moMenus[i]->popup(pos - QPoint(0, MediaMenu::ptr->moMenus[i]->sizeHint().height()));
        });
        d->mo[i].label->setToolTip(MediaMenu::ptr->moMenus[i]->title());
        sbar->addWidget(d->mo[i].label.get());
    });

    auto hdc_name = QString(hdc_get_internal_name(hdc_current));
    if ((has_mfm || hdc_name.left(5) == QStringLiteral("st506")) && c_mfm > 0) {
        d->hdds[HDD_BUS_MFM].label = std::make_unique<QLabel>();
        d->hdds[HDD_BUS_MFM].setActive(false);
        d->hdds[HDD_BUS_MFM].label->setToolTip(tr("Hard disk (%s)").replace("%s", "MFM/RLL"));
        sbar->addWidget(d->hdds[HDD_BUS_MFM].label.get());
    }
    if ((has_esdi || hdc_name.left(4) == QStringLiteral("esdi")) && c_esdi > 0) {
        d->hdds[HDD_BUS_ESDI].label = std::make_unique<QLabel>();
        d->hdds[HDD_BUS_ESDI].setActive(false);
        d->hdds[HDD_BUS_ESDI].label->setToolTip(tr("Hard disk (%s)").replace("%s", "ESDI"));
        sbar->addWidget(d->hdds[HDD_BUS_ESDI].label.get());
    }
    if ((has_xta || hdc_name.left(3) == QStringLiteral("xta")) && c_xta > 0) {
        d->hdds[HDD_BUS_XTA].label = std::make_unique<QLabel>();
        d->hdds[HDD_BUS_XTA].setActive(false);
        d->hdds[HDD_BUS_XTA].label->setToolTip(tr("Hard disk (%s)").replace("%s", "XTA"));
        sbar->addWidget(d->hdds[HDD_BUS_XTA].label.get());
    }
    if ((hasIDE() || hdc_name.left(5) == QStringLiteral("xtide") || hdc_name.left(3) == QStringLiteral("ide")) && c_ide > 0) {
        d->hdds[HDD_BUS_IDE].label = std::make_unique<QLabel>();
        d->hdds[HDD_BUS_IDE].setActive(false);
        d->hdds[HDD_BUS_IDE].label->setToolTip(tr("Hard disk (%s)").replace("%s", "IDE"));
        sbar->addWidget(d->hdds[HDD_BUS_IDE].label.get());
    }
    if ((hasSCSI() || (scsi_card_current[0] != 0) || (scsi_card_current[1] != 0) ||
         (scsi_card_current[2] != 0) || (scsi_card_current[3] != 0)) && c_scsi > 0) {
        d->hdds[HDD_BUS_SCSI].label = std::make_unique<QLabel>();
        d->hdds[HDD_BUS_SCSI].setActive(false);
        d->hdds[HDD_BUS_SCSI].label->setToolTip(tr("Hard disk (%s)").replace("%s", "SCSI"));
        sbar->addWidget(d->hdds[HDD_BUS_SCSI].label.get());
    }

    if (do_net) {
        d->net.label = std::make_unique<QLabel>();
        d->net.setActive(false);
        d->net.label->setToolTip(tr("Network"));
        sbar->addWidget(d->net.label.get());
    }
    d->sound = std::make_unique<ClickableLabel>();
    d->sound->setPixmap(d->pixmaps.sound);

    connect(d->sound.get(), &ClickableLabel::doubleClicked, d->sound.get(), [](QPoint pos) {
        SoundGain gain(main_window);
        gain.exec();
    });
    d->sound->setToolTip(tr("Sound"));
    sbar->addWidget(d->sound.get());
    d->text = std::make_unique<QLabel>();
    sbar->addWidget(d->text.get());
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

void MachineStatus::message(const QString &msg) {
    d->text->setText(msg);
}

QString MachineStatus::getMessage() {
    return d->text->text();
}

void MachineStatus::updateTip(int tag)
{
    int category = tag & 0xfffffff0;
    int item = tag & 0xf;
    switch (category) {
    case SB_CASSETTE:
        d->cassette.label->setToolTip(MediaMenu::ptr->cassetteMenu->title());
        break;
    case SB_CARTRIDGE:
        d->cartridge[item].label->setToolTip(MediaMenu::ptr->cartridgeMenus[item]->title());
        break;
    case SB_FLOPPY:
        d->fdd[item].label->setToolTip(MediaMenu::ptr->floppyMenus[item]->title());
        break;
    case SB_CDROM:
        d->cdrom[item].label->setToolTip(MediaMenu::ptr->cdromMenus[item]->title());
        break;
    case SB_ZIP:
        d->zip[item].label->setToolTip(MediaMenu::ptr->zipMenus[item]->title());
        break;
    case SB_MO:
        d->mo[item].label->setToolTip(MediaMenu::ptr->moMenus[item]->title());
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
