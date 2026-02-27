/*
 * 86Box    A hypervisor and IBM PC system emulator that specializes in
 *          running old operating systems and software designed for IBM
 *          PC systems and compatibles from 1981 through fairly recent
 *          system designs based on the PCI bus.
 *
 *          This file is part of the 86Box distribution.
 *
 *          Joystick configuration UI module.
 *
 * Authors: Joakim L. Gilje <jgilje@jgilje.net>
 *          Cacodemon345
 *
 *          Copyright 2021 Joakim L. Gilje
 *          Copyright 2021-2022 Cacodemon345
 */
#include "qt_machinestatus.hpp"

extern "C" {
#include <86box/86box.h>
#include <86box/hdd.h>
#include <86box/timer.h>
#include <86box/device.h>
#include <86box/cartridge.h>
#include <86box/cassette.h>
#include <86box/cdrom.h>
#include <86box/cdrom_interface.h>
#include <86box/fdd.h>
#include <86box/hdc.h>
#include <86box/scsi.h>
#include <86box/scsi_device.h>
#include <86box/rdisk.h>
#include <86box/mo.h>
#include <86box/plat.h>
#include <86box/machine.h>
#include <86box/thread.h>
#include <86box/network.h>
#include <86box/ui.h>
#include <86box/machine_status.h>
#include <86box/config.h>

extern volatile int fdcinited;
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
#include "qt_iconindicators.hpp"

#include <array>

extern MainWindow *main_window;

static bool sbar_initialized = false;

namespace {
struct PixmapSetActive {
    QPixmap normal;
    QPixmap active;
    QPixmap write_active;
    QPixmap read_write_active;
    void    load(const QIcon &icon);
};
struct PixmapSetDisabled {
    QPixmap normal;
    QPixmap disabled;
    void    load(const QIcon &icon);
};
struct PixmapSetEmpty {
    QPixmap normal;
    QPixmap empty;
    void    load(const QIcon &icon);
};
struct PixmapSetEmptyActive {
    QPixmap normal;
    QPixmap active;
    QPixmap record;
    QPixmap play;
    QPixmap pause;
    QPixmap play_active;
    QPixmap pause_active;
    QPixmap empty;
    QPixmap empty_active;
    QPixmap write_active;
    QPixmap record_write_active;
    QPixmap read_write_active;
    QPixmap empty_write_active;
    QPixmap empty_read_write_active;
    QPixmap wp;
    QPixmap wp_active;
    void    load(const QIcon &icon);
};
struct Pixmaps {
    PixmapSetEmpty       cartridge;
    PixmapSetEmptyActive cassette;
    PixmapSetEmptyActive floppy_disabled;
    PixmapSetEmptyActive floppy_525;
    PixmapSetEmptyActive floppy_35;
    PixmapSetEmptyActive cdrom;
    PixmapSetEmptyActive rdisk_disabled;
    PixmapSetEmptyActive rdisk;
    PixmapSetEmptyActive zip;
    PixmapSetEmptyActive mo;
    PixmapSetActive      hd;
    PixmapSetEmptyActive net;
    PixmapSetDisabled    sound;
};

struct StateActive {
    std::unique_ptr<QLabel> label;
    PixmapSetActive        *pixmaps      = nullptr;
    bool                    active       = false;
    bool                    write_active = false;

    void setActive(bool b)
    {
        if (!label || b == active)
            return;
        active = b;

        refresh();
    }

    void setWriteActive(bool b)
    {
        if (!label || b == write_active)
            return;

        write_active = b;
        refresh();
    }

    void refresh()
    {
        if (!label)
            return;
        if (active && write_active)
            label->setPixmap(pixmaps->read_write_active);
        else
            label->setPixmap(write_active ? pixmaps->write_active : (active ? pixmaps->active : pixmaps->normal));
    }
};
struct StateEmpty {
    std::unique_ptr<QLabel> label;
    PixmapSetEmpty         *pixmaps = nullptr;
    bool                    empty   = false;

    void setEmpty(bool e)
    {
        if (!label || e == empty)
            return;
        empty = e;

        refresh();
    }

    void refresh()
    {
        if (!label)
            return;
        label->setPixmap(empty ? pixmaps->empty : pixmaps->normal);
    }
};
struct StateEmptyActive {
    std::unique_ptr<QLabel> label;
    PixmapSetEmptyActive   *pixmaps      = nullptr;
    bool                    empty        = false;
    bool                    active       = false;
    bool                    write_active = false;
    bool                    wp           = false;
    bool                    play         = false;
    bool                    pause        = false;
    bool                    record       = false;

    void setRecord(bool b)
    {
        if (!label || b == record)
            return;

        record = b;
        refresh();
    }

    void setPlay(bool b)
    {
        if (!label || b == play)
            return;

        play = b;
        refresh();
    }

    void setPause(bool b)
    {
        if (!label || b == pause)
            return;

        pause = b;
        refresh();
    }

    void setActive(bool b)
    {
        if (!label || b == active)
            return;

        active = b;
        refresh();
    }
    void setWriteActive(bool b)
    {
        if (!label || b == write_active)
            return;

        write_active = b;
        refresh();
    }
    void setEmpty(bool b)
    {
        if (!label || b == empty)
            return;

        empty = b;
        refresh();
    }
    void setWriteProtected(bool b)
    {
        if (!label || b == wp)
            return;

        wp = b;
        refresh();
    }
    void refresh()
    {
        if (!label)
            return;
        if (empty) {
            if (active && write_active)
                label->setPixmap(pixmaps->empty_read_write_active);
            else
                label->setPixmap(write_active ? pixmaps->empty_write_active : (active ? pixmaps->empty_active : pixmaps->empty));
        } else {
            if (wp && !(play || pause))
                label->setPixmap(active ? pixmaps->wp_active : pixmaps->wp);
            else if (active && write_active && !wp)
                label->setPixmap(pixmaps->read_write_active);
            else if (record && !active && !wp)
                label->setPixmap(write_active ? pixmaps->record_write_active : pixmaps->record);
            else if ((play || pause) && !write_active)
                label->setPixmap(play ? (active ? pixmaps->play_active : pixmaps->play) : (active ? pixmaps->pause_active : pixmaps->pause));
            else
                label->setPixmap(write_active ? pixmaps->write_active : (active ? pixmaps->active : pixmaps->normal));
        }
    }
};

static QSize pixmap_size(16, 16);

void
PixmapSetEmpty::load(const QIcon &icon)
{
    normal = getIconWithIndicator(icon, pixmap_size, QIcon::Normal, None);
    empty  = getIconWithIndicator(icon, pixmap_size, QIcon::Disabled, None);
}

void
PixmapSetActive::load(const QIcon &icon)
{
    normal = getIconWithIndicator(icon, pixmap_size, QIcon::Normal, None);
    active = getIconWithIndicator(icon, pixmap_size, QIcon::Normal, Active);

    write_active      = getIconWithIndicator(icon, pixmap_size, QIcon::Normal, WriteActive);
    read_write_active = getIconWithIndicator(icon, pixmap_size, QIcon::Normal, ReadWriteActive);
}

void
PixmapSetDisabled::load(const QIcon &icon)
{
    normal   = getIconWithIndicator(icon, pixmap_size, QIcon::Normal, None);
    disabled = getIconWithIndicator(icon, pixmap_size, QIcon::Disabled, Disabled);
}

void
PixmapSetEmptyActive::load(const QIcon &icon)
{
    normal                  = getIconWithIndicator(icon, pixmap_size, QIcon::Normal, None);
    play                    = getIconWithIndicator(icon, pixmap_size, QIcon::Normal, Play);
    pause                   = getIconWithIndicator(icon, pixmap_size, QIcon::Normal, Pause);
    record                  = getIconWithIndicator(icon, pixmap_size, QIcon::Normal, Record);
    play_active             = getIconWithIndicator(icon, pixmap_size, QIcon::Normal, PlayActive);
    pause_active            = getIconWithIndicator(icon, pixmap_size, QIcon::Normal, PauseActive);
    wp                      = getIconWithIndicator(icon, pixmap_size, QIcon::Normal, WriteProtected);
    wp_active               = getIconWithIndicator(icon, pixmap_size, QIcon::Normal, WriteProtectedActive);
    active                  = getIconWithIndicator(icon, pixmap_size, QIcon::Normal, Active);
    write_active            = getIconWithIndicator(icon, pixmap_size, QIcon::Normal, WriteActive);
    record_write_active     = getIconWithIndicator(icon, pixmap_size, QIcon::Normal, RecordWriteActive);
    read_write_active       = getIconWithIndicator(icon, pixmap_size, QIcon::Normal, ReadWriteActive);
    empty                   = getIconWithIndicator(icon, pixmap_size, QIcon::Disabled, None);
    empty_active            = getIconWithIndicator(icon, pixmap_size, QIcon::Disabled, Active);
    empty_write_active      = getIconWithIndicator(icon, pixmap_size, QIcon::Disabled, WriteActive);
    empty_read_write_active = getIconWithIndicator(icon, pixmap_size, QIcon::Disabled, ReadWriteActive);
}
}

struct MachineStatus::States {
    Pixmaps pixmaps;

    States(QObject *parent)
    {
        pixmaps.cartridge.load(QIcon(":/settings/qt/icons/cartridge.ico"));
        pixmaps.cassette.load(QIcon(":/settings/qt/icons/cassette.ico"));
        pixmaps.floppy_disabled.normal                  = QIcon(":/settings/qt/icons/floppy_disabled.ico").pixmap(pixmap_size);
        pixmaps.floppy_disabled.active                  = pixmaps.floppy_disabled.normal;
        pixmaps.floppy_disabled.read_write_active       = pixmaps.floppy_disabled.normal;
        pixmaps.floppy_disabled.empty                   = pixmaps.floppy_disabled.normal;
        pixmaps.floppy_disabled.empty_active            = pixmaps.floppy_disabled.normal;
        pixmaps.floppy_disabled.empty_write_active      = pixmaps.floppy_disabled.normal;
        pixmaps.floppy_disabled.empty_read_write_active = pixmaps.floppy_disabled.normal;
        pixmaps.floppy_525.load(QIcon(":/settings/qt/icons/floppy_525.ico"));
        pixmaps.floppy_35.load(QIcon(":/settings/qt/icons/floppy_35.ico"));
        pixmaps.cdrom.load(QIcon(":/settings/qt/icons/cdrom.ico"));
        pixmaps.rdisk_disabled.normal                  = QIcon(":/settings/qt/icons/rdisk_disabled.ico").pixmap(pixmap_size);
        pixmaps.rdisk_disabled.active                  = pixmaps.rdisk_disabled.normal;
        pixmaps.rdisk_disabled.read_write_active       = pixmaps.rdisk_disabled.normal;
        pixmaps.rdisk_disabled.empty                   = pixmaps.rdisk_disabled.normal;
        pixmaps.rdisk_disabled.empty_active            = pixmaps.rdisk_disabled.normal;
        pixmaps.rdisk_disabled.empty_write_active      = pixmaps.rdisk_disabled.normal;
        pixmaps.rdisk_disabled.empty_read_write_active = pixmaps.rdisk_disabled.normal;
        pixmaps.rdisk.load(QIcon(":/settings/qt/icons/rdisk.ico"));
        pixmaps.zip.load(QIcon(":/settings/qt/icons/zip.ico"));
        pixmaps.mo.load(QIcon(":/settings/qt/icons/mo.ico"));
        pixmaps.hd.load(QIcon(":/settings/qt/icons/hard_disk.ico"));
        pixmaps.net.load(QIcon(":/settings/qt/icons/network.ico"));
        pixmaps.sound.load(QIcon(":/settings/qt/icons/sound.ico"));

        cartridge[0].pixmaps = &pixmaps.cartridge;
        cartridge[1].pixmaps = &pixmaps.cartridge;
        cassette.pixmaps     = &pixmaps.cassette;
        for (auto &f : fdd) {
            f.pixmaps = &pixmaps.floppy_disabled;
        }
        for (auto &c : cdrom) {
            c.pixmaps = &pixmaps.cdrom;
        }
        for (auto &z : rdisk) {
            z.pixmaps = &pixmaps.rdisk;
        }
        for (auto &m : mo) {
            m.pixmaps = &pixmaps.mo;
        }
        for (auto &h : hdds) {
            h.pixmaps = &pixmaps.hd;
        }
        for (auto &n : net) {
            n.pixmaps = &pixmaps.net;
        }
    }

    std::array<StateEmpty, 2>                  cartridge;
    StateEmptyActive                           cassette;
    std::array<StateEmptyActive, FDD_NUM>      fdd;
    std::array<StateEmptyActive, CDROM_NUM>    cdrom;
    std::array<StateEmptyActive, RDISK_NUM>    rdisk;
    std::array<StateEmptyActive, MO_NUM>       mo;
    std::array<StateActive, HDD_BUS_USB>       hdds;
    std::array<StateEmptyActive, NET_CARD_MAX> net;
    std::unique_ptr<ClickableLabel>            sound;
    std::unique_ptr<QLabel>                    text;
};

MachineStatus::MachineStatus(QObject *parent)
    : QObject(parent)
    , refreshTimer(new QTimer(this))
{
    d         = std::make_unique<MachineStatus::States>(this);
    soundMenu = nullptr;
    connect(refreshTimer, &QTimer::timeout, this, &MachineStatus::refreshIcons);
    refreshTimer->start(75);
}

MachineStatus::~MachineStatus() = default;

void
MachineStatus::setSoundMenu(QMenu *menu)
{
    soundMenu = menu;
}

bool
MachineStatus::hasCassette()
{
    return cassette_enable > 0 ? true : false;
}

bool
MachineStatus::hasIDE()
{
    return (machine_has_flags(machine, MACHINE_IDE_QUAD) > 0) || other_ide_present;
}

bool
MachineStatus::hasSCSI()
{
    return (machine_has_flags(machine, MACHINE_SCSI) > 0) || other_scsi_present;
}

void
MachineStatus::iterateFDD(const std::function<void(int)> &cb)
{
    if (!fdcinited)
        return;

    for (int i = 0; i < FDD_NUM; ++i) {
        if (fdd_get_type(i) != 0) {
            cb(i);
        }
    }
}

void
MachineStatus::iterateCDROM(const std::function<void(int)> &cb)
{
    auto hdc_name = QString(hdc_get_internal_name(hdc_current[0]));
    for (size_t i = 0; i < CDROM_NUM; i++) {
        /* Could be Internal or External IDE.. */
        if ((cdrom[i].bus_type == CDROM_BUS_ATAPI) && !hasIDE() &&
            (hdc_name.left(3) != QStringLiteral("ide")) &&
            (hdc_name.left(5) != QStringLiteral("xtide")) &&
            (hdc_name.left(5) != QStringLiteral("mcide")))
            continue;
        if ((cdrom[i].bus_type == CDROM_BUS_SCSI) && !hasSCSI() &&
            (scsi_card_current[0] == 0) && (scsi_card_current[1] == 0) &&
            (scsi_card_current[2] == 0) && (scsi_card_current[3] == 0))
            continue;
        if ((cdrom[i].bus_type == CDROM_BUS_MITSUMI || cdrom[i].bus_type == CDROM_BUS_MKE) && (cdrom_interface_current == 0))
            continue;
        if (cdrom[i].bus_type != 0) {
            cb(i);
        }
    }
}

void
MachineStatus::iterateRDisk(const std::function<void(int)> &cb)
{
    auto hdc_name = QString(hdc_get_internal_name(hdc_current[0]));
    for (size_t i = 0; i < RDISK_NUM; i++) {
        /* Could be Internal or External IDE.. */
        if ((rdisk_drives[i].bus_type == RDISK_BUS_ATAPI) && !hasIDE() &&
            (hdc_name.left(3) != QStringLiteral("ide")) &&
            (hdc_name.left(5) != QStringLiteral("xtide")) &&
            (hdc_name.left(5) != QStringLiteral("mcide")))
            continue;
        if ((rdisk_drives[i].bus_type == RDISK_BUS_SCSI) && !hasSCSI() &&
            (scsi_card_current[0] == 0) && (scsi_card_current[1] == 0) &&
            (scsi_card_current[2] == 0) && (scsi_card_current[3] == 0))
            continue;
        if (rdisk_drives[i].bus_type != 0) {
            cb(i);
        }
    }
}

void
MachineStatus::iterateMO(const std::function<void(int)> &cb)
{
    auto hdc_name = QString(hdc_get_internal_name(hdc_current[0]));
    for (size_t i = 0; i < MO_NUM; i++) {
        /* Could be Internal or External IDE.. */
        if ((mo_drives[i].bus_type == MO_BUS_ATAPI) && !hasIDE() &&
            (hdc_name.left(3) != QStringLiteral("ide")) &&
            (hdc_name.left(5) != QStringLiteral("xtide")) &&
            (hdc_name.left(5) != QStringLiteral("mcide")))
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

void
MachineStatus::iterateNIC(const std::function<void(int)> &cb)
{
    for (int i = 0; i < NET_CARD_MAX; i++) {
        if (network_dev_available(i)) {
            cb(i);
        }
    }
}

static int
hdd_count(const int bus_type)
{
    int c = 0;

    for (uint8_t i = 0; i < HDD_NUM; i++) {
        if (hdd[i].bus_type == bus_type) {
            c++;
        }
    }

    return c;
}

void
MachineStatus::refreshEmptyIcons()
{
    /* Check if icons are initialized. */
    if (!sbar_initialized)
        return;

    for (size_t i = 0; i < FDD_NUM; ++i) {
        d->fdd[i].setEmpty(machine_status.fdd[i].empty);
        d->fdd[i].setWriteProtected(machine_status.fdd[i].write_prot);
    }
    for (size_t i = 0; i < CDROM_NUM; ++i)
        d->cdrom[i].setEmpty(machine_status.cdrom[i].empty);
    for (size_t i = 0; i < RDISK_NUM; i++) {
        d->rdisk[i].setEmpty(machine_status.rdisk[i].empty);
        d->rdisk[i].setWriteProtected(machine_status.rdisk[i].write_prot);
    }
    for (size_t i = 0; i < MO_NUM; i++) {
        d->mo[i].setEmpty(machine_status.mo[i].empty);
        d->mo[i].setWriteProtected(machine_status.mo[i].write_prot);
    }

    d->cassette.setEmpty(machine_status.cassette.empty);
    d->cassette.setWriteProtected(machine_status.cassette.write_prot);

    for (size_t i = 0; i < NET_CARD_MAX; i++)
        d->net[i].setEmpty(machine_status.net[i].empty);

    for (int i = 0; i < 2; ++i)
        d->cartridge[i].setEmpty(machine_status.cartridge[i].empty);
}

void
MachineStatus::refreshIcons()
{
    /* Always show record/play statuses of cassette even if icon updates are disabled, since it's important to indicate play/record modes. */
    if (cassette_enable && cassette) {
        d->cassette.setRecord(!!cassette->save);
        d->cassette.setPlay(!cassette->save);
    }

    /* Check if icons should show activity. */
    if (!update_icons)
        return;

    if (cassette_enable) {
        d->cassette.setWriteActive(machine_status.cassette.write_active);
        d->cassette.setActive(machine_status.cassette.active);
    }

    for (size_t i = 0; i < FDD_NUM; ++i) {
        d->fdd[i].setActive(machine_status.fdd[i].active);
        d->fdd[i].setWriteActive(machine_status.fdd[i].write_active);
    }
    for (size_t i = 0; i < CDROM_NUM; ++i) {
        d->cdrom[i].setActive(machine_status.cdrom[i].active);
        d->cdrom[i].setWriteActive(machine_status.cdrom[i].write_active);
        d->cdrom[i].setPlay(cdrom_is_playing(i));
        d->cdrom[i].setPause(cdrom_is_paused(i));
        if (machine_status.cdrom[i].active) {
            ui_sb_update_icon(SB_CDROM | i, 0);
        }
        if (machine_status.cdrom[i].write_active) {
            ui_sb_update_icon_write(SB_CDROM | i, 0);
        }
    }
    for (size_t i = 0; i < RDISK_NUM; i++) {
        d->rdisk[i].setActive(machine_status.rdisk[i].active);
        d->rdisk[i].setWriteActive(machine_status.rdisk[i].write_active);
        if (machine_status.rdisk[i].active)
            ui_sb_update_icon(SB_RDISK | i, 0);
        if (machine_status.rdisk[i].write_active)
            ui_sb_update_icon_write(SB_RDISK | i, 0);
    }
    for (size_t i = 0; i < MO_NUM; i++) {
        d->mo[i].setActive(machine_status.mo[i].active);
        d->mo[i].setWriteActive(machine_status.mo[i].write_active);
        if (machine_status.mo[i].active)
            ui_sb_update_icon(SB_MO | i, 0);
        if (machine_status.mo[i].write_active)
            ui_sb_update_icon_write(SB_MO | i, 0);
    }

    for (size_t i = 0; i < HDD_BUS_USB; i++) {
        d->hdds[i].setActive(machine_status.hdd[i].active);
        d->hdds[i].setWriteActive(machine_status.hdd[i].write_active);
        if (machine_status.hdd[i].active)
            ui_sb_update_icon(SB_HDD | i, 0);
        if (machine_status.hdd[i].write_active)
            ui_sb_update_icon_write(SB_HDD | i, 0);
    }

    for (size_t i = 0; i < NET_CARD_MAX; i++) {
        d->net[i].setActive(machine_status.net[i].active);
        d->net[i].setWriteActive(machine_status.net[i].write_active);
    }
}

void
MachineStatus::clearActivity()
{
    for (auto &fdd : d->fdd) {
        fdd.setActive(false);
        fdd.setWriteActive(false);
    }
    for (auto &cdrom : d->cdrom) {
        cdrom.setActive(false);
        cdrom.setWriteActive(false);
    }
    for (auto &rdisk : d->rdisk) {
        rdisk.setActive(false);
        rdisk.setWriteActive(false);
    }
    for (auto &mo : d->mo) {
        mo.setActive(false);
        mo.setWriteActive(false);
    }
    for (auto &hdd : d->hdds) {
        hdd.setActive(false);
        hdd.setWriteActive(false);
    }
    for (auto &net : d->net) {
        net.setActive(false);
        net.setWriteActive(false);
    }
}

void
MachineStatus::refresh(QStatusBar *sbar)
{
    bool has_mfm  = machine_has_flags(machine, MACHINE_MFM) > 0;
    bool has_xta  = machine_has_flags(machine, MACHINE_XTA) > 0;
    bool has_esdi = machine_has_flags(machine, MACHINE_ESDI) > 0;

    int c_mfm   = hdd_count(HDD_BUS_MFM);
    int c_esdi  = hdd_count(HDD_BUS_ESDI);
    int c_xta   = hdd_count(HDD_BUS_XTA);
    int c_ide   = hdd_count(HDD_BUS_IDE);
    int c_atapi = hdd_count(HDD_BUS_ATAPI);
    int c_scsi  = hdd_count(HDD_BUS_SCSI);

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
    for (size_t i = 0; i < RDISK_NUM; i++) {
        sbar->removeWidget(d->rdisk[i].label.get());
    }
    for (size_t i = 0; i < MO_NUM; i++) {
        sbar->removeWidget(d->mo[i].label.get());
    }
    for (size_t i = 0; i < HDD_BUS_USB; i++) {
        sbar->removeWidget(d->hdds[i].label.get());
    }
    for (size_t i = 0; i < NET_CARD_MAX; i++) {
        sbar->removeWidget(d->net[i].label.get());
    }
    sbar->removeWidget(d->sound.get());

    if (cassette_enable) {
        d->cassette.label = std::make_unique<ClickableLabel>();
        d->cassette.setEmpty(QString(cassette_fname).isEmpty());
        if (QString(cassette_fname).isEmpty())
            d->cassette.setWriteProtected(false);
        else if (QString(cassette_fname).left(5) == "wp://")
            d->cassette.setWriteProtected(true);
        else
            d->cassette.setWriteProtected(cassette_ui_writeprot);
        d->cassette.refresh();
        connect((ClickableLabel *) d->cassette.label.get(), &ClickableLabel::clicked, [](QPoint pos) {
            MediaMenu::ptr->cassetteMenu->popup(pos - QPoint(0, MediaMenu::ptr->cassetteMenu->sizeHint().height()));
        });
        connect((ClickableLabel *) d->cassette.label.get(), &ClickableLabel::dropped, [](QString str) {
            MediaMenu::ptr->cassetteMount(str, false);
        });
        d->cassette.label->setToolTip(MediaMenu::ptr->cassetteMenu->toolTip());
        d->cassette.label->setAcceptDrops(true);
        sbar->addWidget(d->cassette.label.get());
    }

    if (machine_has_cartridge(machine)) {
        for (int i = 0; i < 2; ++i) {
            d->cartridge[i].label = std::make_unique<ClickableLabel>();
            d->cartridge[i].setEmpty(QString(cart_fns[i]).isEmpty());
            d->cartridge[i].refresh();
            connect((ClickableLabel *) d->cartridge[i].label.get(), &ClickableLabel::clicked, [i](QPoint pos) {
                MediaMenu::ptr->cartridgeMenus[i]->popup(pos - QPoint(0, MediaMenu::ptr->cartridgeMenus[i]->sizeHint().height()));
            });
            connect((ClickableLabel *) d->cartridge[i].label.get(), &ClickableLabel::dropped, [i](QString str) {
                MediaMenu::ptr->cartridgeMount(i, str);
            });
            d->cartridge[i].label->setToolTip(MediaMenu::ptr->cartridgeMenus[i]->toolTip());
            d->cartridge[i].label->setAcceptDrops(true);
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
        if (QString(floppyfns[i]).isEmpty())
            d->fdd[i].setWriteProtected(false);
        else if (QString(floppyfns[i]).left(5) == "wp://")
            d->fdd[i].setWriteProtected(true);
        else
            d->fdd[i].setWriteProtected(ui_writeprot[i]);
        d->fdd[i].setActive(false);
        d->fdd[i].setWriteActive(false);
        d->fdd[i].refresh();
        connect((ClickableLabel *) d->fdd[i].label.get(), &ClickableLabel::clicked, [i](QPoint pos) {
            MediaMenu::ptr->floppyMenus[i]->popup(pos - QPoint(0, MediaMenu::ptr->floppyMenus[i]->sizeHint().height()));
        });
        connect((ClickableLabel *) d->fdd[i].label.get(), &ClickableLabel::dropped, [i](QString str) {
            MediaMenu::ptr->floppyMount(i, str, false);
        });
        d->fdd[i].label->setToolTip(MediaMenu::ptr->floppyMenus[i]->toolTip());
        d->fdd[i].label->setAcceptDrops(true);
        sbar->addWidget(d->fdd[i].label.get());
    });

    iterateCDROM([this, sbar](int i) {
        d->cdrom[i].label = std::make_unique<ClickableLabel>();
        d->cdrom[i].setEmpty(QString(cdrom[i].image_path).isEmpty());
        d->cdrom[i].setActive(false);
        d->cdrom[i].setWriteActive(false);
        d->cdrom[i].refresh();
        connect((ClickableLabel *) d->cdrom[i].label.get(), &ClickableLabel::clicked, [i](QPoint pos) {
            MediaMenu::ptr->cdromMenus[i]->popup(pos - QPoint(0, MediaMenu::ptr->cdromMenus[i]->sizeHint().height()));
        });
        connect((ClickableLabel *) d->cdrom[i].label.get(), &ClickableLabel::dropped, [i](QString str) {
            MediaMenu::ptr->cdromMount(i, str);
        });
        d->cdrom[i].label->setToolTip(MediaMenu::ptr->cdromMenus[i]->toolTip());
        d->cdrom[i].label->setAcceptDrops(true);
        sbar->addWidget(d->cdrom[i].label.get());
    });

    iterateRDisk([this, sbar](int i) {
        int t = rdisk_drives[i].type;
        if (rdisk_drives[i].bus_type == RDISK_BUS_DISABLED) {
            d->rdisk[i].pixmaps = &d->pixmaps.rdisk_disabled;
        } else if ((t == RDISK_TYPE_ZIP_100) || (t == RDISK_TYPE_ZIP_250)) {
            d->rdisk[i].pixmaps = &d->pixmaps.zip;
        } else {
            d->rdisk[i].pixmaps = &d->pixmaps.rdisk;
        }
        d->rdisk[i].label = std::make_unique<ClickableLabel>();
        d->rdisk[i].setEmpty(QString(rdisk_drives[i].image_path).isEmpty());
        if (QString(rdisk_drives[i].image_path).isEmpty())
            d->rdisk[i].setWriteProtected(false);
        else if (QString(rdisk_drives[i].image_path).left(5) == "wp://")
            d->rdisk[i].setWriteProtected(true);
        else
            d->rdisk[i].setWriteProtected(rdisk_drives[i].read_only);
        d->rdisk[i].setActive(false);
        d->rdisk[i].setWriteActive(false);
        d->rdisk[i].refresh();
        connect((ClickableLabel *) d->rdisk[i].label.get(), &ClickableLabel::clicked, [i](QPoint pos) {
            MediaMenu::ptr->rdiskMenus[i]->popup(pos - QPoint(0, MediaMenu::ptr->rdiskMenus[i]->sizeHint().height()));
        });
        connect((ClickableLabel *) d->rdisk[i].label.get(), &ClickableLabel::dropped, [i](QString str) {
            MediaMenu::ptr->rdiskMount(i, str, false);
        });
        d->rdisk[i].label->setToolTip(MediaMenu::ptr->rdiskMenus[i]->toolTip());
        d->rdisk[i].label->setAcceptDrops(true);
        sbar->addWidget(d->rdisk[i].label.get());
    });

    iterateMO([this, sbar](int i) {
        d->mo[i].label = std::make_unique<ClickableLabel>();
        d->mo[i].setEmpty(QString(mo_drives[i].image_path).isEmpty());
        if (QString(rdisk_drives[i].image_path).isEmpty())
            d->mo[i].setWriteProtected(false);
        else if (QString(rdisk_drives[i].image_path).left(5) == "wp://")
            d->mo[i].setWriteProtected(true);
        else
            d->mo[i].setWriteProtected(rdisk_drives[i].read_only);
        d->mo[i].setActive(false);
        d->mo[i].setWriteActive(false);
        d->mo[i].refresh();
        connect((ClickableLabel *) d->mo[i].label.get(), &ClickableLabel::clicked, [i](QPoint pos) {
            MediaMenu::ptr->moMenus[i]->popup(pos - QPoint(0, MediaMenu::ptr->moMenus[i]->sizeHint().height()));
        });
        connect((ClickableLabel *) d->mo[i].label.get(), &ClickableLabel::dropped, [i](QString str) {
            MediaMenu::ptr->moMount(i, str, false);
        });
        d->mo[i].label->setToolTip(MediaMenu::ptr->moMenus[i]->toolTip());
        d->mo[i].label->setAcceptDrops(true);
        sbar->addWidget(d->mo[i].label.get());
    });

    iterateNIC([this, sbar](int i) {
        d->net[i].label = std::make_unique<ClickableLabel>();
        d->net[i].setEmpty(!network_is_connected(i));
        d->net[i].setActive(false);
        d->net[i].setWriteActive(false);
        d->net[i].refresh();
        d->net[i].label->setToolTip(MediaMenu::ptr->netMenus[i]->toolTip());
        connect((ClickableLabel *) d->net[i].label.get(), &ClickableLabel::clicked, [i](QPoint pos) {
            MediaMenu::ptr->netMenus[i]->popup(pos - QPoint(0, MediaMenu::ptr->netMenus[i]->sizeHint().height()));
        });
        sbar->addWidget(d->net[i].label.get());
    });

    auto hdc_name = QString(hdc_get_internal_name(hdc_current[0]));
    if ((has_mfm || (hdc_name.left(5) == QStringLiteral("st506"))) && (c_mfm > 0)) {
        d->hdds[HDD_BUS_MFM].label = std::make_unique<QLabel>();
        d->hdds[HDD_BUS_MFM].setActive(false);
        d->hdds[HDD_BUS_MFM].setWriteActive(false);
        d->hdds[HDD_BUS_MFM].refresh();
        d->hdds[HDD_BUS_MFM].label->setToolTip(tr("Hard disk (%1)").arg("MFM/RLL"));
        auto tooltip = d->hdds[HDD_BUS_MFM].label->toolTip();
        tooltip.append("\n");
        for (int i = 0; i < HDD_NUM; i++) {
            if (hdd[i].bus_type == HDD_BUS_MFM && hdd[i].fn[0] != 0) {
                tooltip.append(QString("\n%5:%6: %1 (C:H:S = %2:%3:%4, %7 %8)").arg(QString::fromUtf8(hdd[i].fn), QString::number(hdd[i].tracks), QString::number(hdd[i].hpc), QString::number(hdd[i].spt), QString::number(hdd[i].channel >> 1), QString::number(hdd[i].channel & 1), QString::number((((qulonglong) hdd[i].hpc * (qulonglong) hdd[i].spt * (qulonglong) hdd[i].tracks) * 512ull) / 1048576ull), tr("MB")));
            }
        }
        d->hdds[HDD_BUS_MFM].label->setToolTip(tooltip);
        sbar->addWidget(d->hdds[HDD_BUS_MFM].label.get());
    }
    if ((has_esdi || (hdc_name.left(4) == QStringLiteral("esdi"))) && (c_esdi > 0)) {
        d->hdds[HDD_BUS_ESDI].label = std::make_unique<QLabel>();
        d->hdds[HDD_BUS_ESDI].setActive(false);
        d->hdds[HDD_BUS_ESDI].setWriteActive(false);
        d->hdds[HDD_BUS_ESDI].refresh();
        d->hdds[HDD_BUS_ESDI].label->setToolTip(tr("Hard disk (%1)").arg("ESDI"));
        auto tooltip = d->hdds[HDD_BUS_ESDI].label->toolTip();
        tooltip.append("\n");
        for (int i = 0; i < HDD_NUM; i++) {
            if (hdd[i].bus_type == HDD_BUS_ESDI && hdd[i].fn[0] != 0) {
                tooltip.append(QString("\n%5:%6: %1 (C:H:S = %2:%3:%4, %7 %8)").arg(QString::fromUtf8(hdd[i].fn), QString::number(hdd[i].tracks), QString::number(hdd[i].hpc), QString::number(hdd[i].spt), QString::number(hdd[i].channel >> 1), QString::number(hdd[i].channel & 1), QString::number((((qulonglong) hdd[i].hpc * (qulonglong) hdd[i].spt * (qulonglong) hdd[i].tracks) * 512ull) / 1048576ull), tr("MB")));
            }
        }
        d->hdds[HDD_BUS_ESDI].label->setToolTip(tooltip);
        sbar->addWidget(d->hdds[HDD_BUS_ESDI].label.get());
    }
    if ((has_xta || (hdc_name.left(3) == QStringLiteral("xta"))) && (c_xta > 0)) {
        d->hdds[HDD_BUS_XTA].label = std::make_unique<QLabel>();
        d->hdds[HDD_BUS_XTA].setActive(false);
        d->hdds[HDD_BUS_XTA].setWriteActive(false);
        d->hdds[HDD_BUS_XTA].refresh();
        d->hdds[HDD_BUS_XTA].label->setToolTip(tr("Hard disk (%1)").arg("XTA"));
        auto tooltip = d->hdds[HDD_BUS_XTA].label->toolTip();
        tooltip.append("\n");
        for (int i = 0; i < HDD_NUM; i++) {
            if (hdd[i].bus_type == HDD_BUS_XTA && hdd[i].fn[0] != 0) {
                tooltip.append(QString("\n%5:%6: %1 (C:H:S = %2:%3:%4, %7 %8)").arg(QString::fromUtf8(hdd[i].fn), QString::number(hdd[i].tracks), QString::number(hdd[i].hpc), QString::number(hdd[i].spt), QString::number(hdd[i].channel >> 1), QString::number(hdd[i].channel & 1), QString::number((((qulonglong) hdd[i].hpc * (qulonglong) hdd[i].spt * (qulonglong) hdd[i].tracks) * 512ull) / 1048576ull), tr("MB")));
            }
        }
        d->hdds[HDD_BUS_XTA].label->setToolTip(tooltip);
        sbar->addWidget(d->hdds[HDD_BUS_XTA].label.get());
    }
    if (hasIDE() || (hdc_name.left(5) == QStringLiteral("xtide")) ||
        (hdc_name.left(5) == QStringLiteral("mcide")) ||
        (hdc_name.left(3) == QStringLiteral("ide"))) {
        if (c_ide > 0) {
            d->hdds[HDD_BUS_IDE].label = std::make_unique<QLabel>();
            d->hdds[HDD_BUS_IDE].setActive(false);
            d->hdds[HDD_BUS_IDE].setWriteActive(false);
            d->hdds[HDD_BUS_IDE].refresh();
            d->hdds[HDD_BUS_IDE].label->setToolTip(tr("Hard disk (%1)").arg("IDE"));
            auto tooltip = d->hdds[HDD_BUS_IDE].label->toolTip();
            tooltip.append("\n");
            for (int i = 0; i < HDD_NUM; i++) {
                if (hdd[i].bus_type == HDD_BUS_IDE && hdd[i].fn[0] != 0) {
                    tooltip.append(QString("\n%5:%6: %1 (C:H:S = %2:%3:%4, %7 %8)").arg(QString::fromUtf8(hdd[i].fn), QString::number(hdd[i].tracks), QString::number(hdd[i].hpc), QString::number(hdd[i].spt), QString::number(hdd[i].channel >> 1), QString::number(hdd[i].channel & 1), QString::number((((qulonglong) hdd[i].hpc * (qulonglong) hdd[i].spt * (qulonglong) hdd[i].tracks) * 512ull) / 1048576ull), tr("MB")));
                }
            }
            d->hdds[HDD_BUS_IDE].label->setToolTip(tooltip);
            sbar->addWidget(d->hdds[HDD_BUS_IDE].label.get());
        }
        if (c_atapi > 0) {
            d->hdds[HDD_BUS_ATAPI].label = std::make_unique<QLabel>();
            d->hdds[HDD_BUS_ATAPI].setActive(false);
            d->hdds[HDD_BUS_ATAPI].setWriteActive(false);
            d->hdds[HDD_BUS_ATAPI].refresh();
            d->hdds[HDD_BUS_ATAPI].label->setToolTip(tr("Hard disk (%1)").arg("ATAPI"));
            auto tooltip = d->hdds[HDD_BUS_ATAPI].label->toolTip();
            tooltip.append("\n");
            for (int i = 0; i < HDD_NUM; i++) {
                if (hdd[i].bus_type == HDD_BUS_ATAPI && hdd[i].fn[0] != 0) {
                    tooltip.append(QString("\n%5:%6: %1 (C:H:S = %2:%3:%4, %7 %8)").arg(QString::fromUtf8(hdd[i].fn), QString::number(hdd[i].tracks), QString::number(hdd[i].hpc), QString::number(hdd[i].spt), QString::number(hdd[i].channel >> 1), QString::number(hdd[i].channel & 1), QString::number((((qulonglong) hdd[i].hpc * (qulonglong) hdd[i].spt * (qulonglong) hdd[i].tracks) * 512ull) / 1048576ull), tr("MB")));
                }
            }
            d->hdds[HDD_BUS_ATAPI].label->setToolTip(tooltip);
            sbar->addWidget(d->hdds[HDD_BUS_ATAPI].label.get());
        }
    }
    if ((hasSCSI() ||
        (scsi_card_current[0] != 0) || (scsi_card_current[1] != 0) ||
        (scsi_card_current[2] != 0) || (scsi_card_current[3] != 0)) &&
        (c_scsi > 0)) {
        d->hdds[HDD_BUS_SCSI].label = std::make_unique<QLabel>();
        d->hdds[HDD_BUS_SCSI].setActive(false);
        d->hdds[HDD_BUS_SCSI].setWriteActive(false);
        d->hdds[HDD_BUS_SCSI].refresh();
        d->hdds[HDD_BUS_SCSI].label->setToolTip(tr("Hard disk (%1)").arg("SCSI"));
        auto tooltip = d->hdds[HDD_BUS_SCSI].label->toolTip();
        tooltip.append("\n");
        for (int i = 0; i < HDD_NUM; i++) {
            if (hdd[i].bus_type == HDD_BUS_SCSI && hdd[i].fn[0] != 0) {
                tooltip.append(QString("\n%5:%6: %1 (C:H:S = %2:%3:%4, %7 %8)").arg(QString::fromUtf8(hdd[i].fn), QString::number(hdd[i].tracks), QString::number(hdd[i].hpc), QString::number(hdd[i].spt), QString::number(hdd[i].channel >> 4), QString::asprintf("%02d", hdd[i].channel & 15), QString::number((((qulonglong) hdd[i].hpc * (qulonglong) hdd[i].spt * (qulonglong) hdd[i].tracks) * 512ull) / 1048576ull), tr("MB")));
            }
        }
        d->hdds[HDD_BUS_SCSI].label->setToolTip(tooltip);
        sbar->addWidget(d->hdds[HDD_BUS_SCSI].label.get());
    }

    d->sound = std::make_unique<ClickableLabel>();
    d->sound->setPixmap(sound_muted ? d->pixmaps.sound.disabled : d->pixmaps.sound.normal);

    connect(d->sound.get(), &ClickableLabel::clicked, this, [this](QPoint pos) {
        this->soundMenu->popup(pos - QPoint(0, this->soundMenu->sizeHint().height()));
    });

    d->sound->setToolTip(tr("Sound"));
    sbar->addWidget(d->sound.get());
    d->text = std::make_unique<QLabel>();
    sbar->addWidget(d->text.get());

    sbar_initialized = true;

    refreshEmptyIcons();
}

void
MachineStatus::updateSoundIcon()
{
    if (d->sound)
        d->sound->setPixmap(sound_muted ? d->pixmaps.sound.disabled : d->pixmaps.sound.normal);
}

void
MachineStatus::message(const QString &msg)
{
    d->text->setText(msg);
}

QString
MachineStatus::getMessage()
{
    return d->text->text();
}

void
MachineStatus::updateTip(int tag)
{
    int category = tag & 0xfffffff0;
    int item     = tag & 0xf;
    if (!MediaMenu::ptr)
        return;
    switch (category) {
        case SB_CASSETTE:
            if (d->cassette.label && MediaMenu::ptr->cassetteMenu)
                d->cassette.label->setToolTip(MediaMenu::ptr->cassetteMenu->toolTip());
            break;
        case SB_CARTRIDGE:
            if (d->cartridge[item].label && MediaMenu::ptr->cartridgeMenus[item])
                d->cartridge[item].label->setToolTip(MediaMenu::ptr->cartridgeMenus[item]->toolTip());
            break;
        case SB_FLOPPY:
            if (d->fdd[item].label && MediaMenu::ptr->floppyMenus[item])
                d->fdd[item].label->setToolTip(MediaMenu::ptr->floppyMenus[item]->toolTip());
            break;
        case SB_CDROM:
            if (d->cdrom[item].label && MediaMenu::ptr->cdromMenus[item])
                d->cdrom[item].label->setToolTip(MediaMenu::ptr->cdromMenus[item]->toolTip());
            break;
        case SB_RDISK:
            if (d->rdisk[item].label && MediaMenu::ptr->rdiskMenus[item])
                d->rdisk[item].label->setToolTip(MediaMenu::ptr->rdiskMenus[item]->toolTip());
            break;
        case SB_MO:
            if (d->mo[item].label && MediaMenu::ptr->moMenus[item])
                d->mo[item].label->setToolTip(MediaMenu::ptr->moMenus[item]->toolTip());
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

    refreshEmptyIcons();
}
