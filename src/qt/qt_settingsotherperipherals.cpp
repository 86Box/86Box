/*
 * 86Box    A hypervisor and IBM PC system emulator that specializes in
 *          running old operating systems and software designed for IBM
 *          PC systems and compatibles from 1981 through fairly recent
 *          system designs based on the PCI bus.
 *
 *          This file is part of the 86Box distribution.
 *
 *          Other peripherals configuration UI module.
 *
 * Authors: Joakim L. Gilje <jgilje@jgilje.net>
 *          Jasmine Iwanek <jriwanek@gmail.com>
 *
 *          Copyright 2021 Joakim L. Gilje
 *          Copyright 2025 Jasmine Iwanek
 */
#include <cstdint>
#include <cstdio>
#include <vector>
#ifdef USE_VFIO
#    include <algorithm>
#    include <cstring>

#    include <QDir>
#    include <QFile>
#    include <QHash>
#    include <QProcess>
#    include <QStandardItemModel>
#    include <QStringBuilder>
#endif

extern "C" {
#include <86box/86box.h>
#include <86box/device.h>
#include <86box/machine.h>
#include <86box/mcamem.h>
#include <86box/isamem.h>
#include <86box/isarom.h>
#include <86box/isartc.h>
#include <86box/unittester.h>
#include <86box/softpower.h>
#include <86box/novell_cardkey.h>
#ifdef USE_VFIO
#    include <86box/vfio.h>
#endif
}

#include "qt_settings_completer.hpp"

#include "qt_settingsotherperipherals.hpp"
#include "ui_qt_settingsotherperipherals.h"

#include "qt_deviceconfig.hpp"
#include "qt_models_common.hpp"

#include "qt_defs.hpp"
#ifdef USE_VFIO
#    include "qt_util.hpp"
#endif

static bool
hasIsaOrSidecarBus(int machineId)
{
    return machine_has_bus(machineId, MACHINE_BUS_ISA | MACHINE_BUS_SIDECAR) > 0;
}

#ifdef USE_VFIO
enum {
    ColumnCheck = 0,
    ColumnGroup,
    ColumnLocation,
    ColumnID,
    ColumnName
};

/* Kept on the checkbox column item. */
enum {
    GroupRole = Qt::UserRole, /* IOMMU group, -1 for a device that was not found */
    ROMEnabledRole,
    ROMFileRole
};

/* The IOMMU group decides what can be passed through: the host has to give
   up every device in a group to pass any of them through. Once a device in a
   group is checked, the others in it show partially checked (claimed, but
   not passed through), and cycle between that and checked until no device
   in the group is checked any more. */
class VFIODeviceModel : public QStandardItemModel {
public:
    using QStandardItemModel::QStandardItemModel;

    bool setData(const QModelIndex &index, const QVariant &value, int role = Qt::EditRole) override;
};

bool
VFIODeviceModel::setData(const QModelIndex &index, const QVariant &value, int role)
{
    if ((role != Qt::CheckStateRole) || (index.column() != ColumnCheck))
        return QStandardItemModel::setData(index, value, role);

    /* A view asks for checked from partially checked, and unchecked from checked. */
    auto      state = static_cast<Qt::CheckState>(value.toInt());
    const int group = index.data(GroupRole).toInt();

    /* A device that was not found is in no group. */
    if (group < 0)
        return QStandardItemModel::setData(index, static_cast<int>((state == Qt::Unchecked) ? Qt::Unchecked : Qt::Checked), role);

    QList<QModelIndex> siblings;
    bool               otherChecked = false;
    for (int row = 0; row < rowCount(); row++) {
        const QModelIndex sibling = this->index(row, ColumnCheck);
        if ((row == index.row()) || (sibling.data(GroupRole).toInt() != group))
            continue;
        siblings.append(sibling);
        if (sibling.data(Qt::CheckStateRole).toInt() == Qt::Checked)
            otherChecked = true;
    }

    if (otherChecked) {
        if (state == Qt::Unchecked)
            state = Qt::PartiallyChecked;
    } else if (state == Qt::PartiallyChecked) {
        state = Qt::Unchecked;
    }

    if (!QStandardItemModel::setData(index, static_cast<int>(state), role))
        return false;

    /* Claim the group on the first device checked, release it on the last unchecked. */
    for (const auto &sibling : siblings) {
        const int siblingState = sibling.data(Qt::CheckStateRole).toInt();
        if ((state == Qt::Checked) && (siblingState == Qt::Unchecked))
            QStandardItemModel::setData(sibling, static_cast<int>(Qt::PartiallyChecked), role);
        else if ((state == Qt::Unchecked) && (siblingState == Qt::PartiallyChecked))
            QStandardItemModel::setData(sibling, static_cast<int>(Qt::Unchecked), role);
    }

    return true;
}

static const QString iommuGroupsPath = QStringLiteral("/sys/kernel/iommu_groups");

/* A configured location as dddd:bb:dd.f, or as written if it does not parse. */
static QString
normalizedLocation(const char *location)
{
    char buf[16];
    if (vfio_parse_location(location, buf, sizeof(buf)))
        return QString(buf);
    return QString(location);
}

/* A vendor or device ID from sysfs ("0x8086") as 4 hex digits. */
static QString
readSysfsId(const QString &path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
        return QStringLiteral("????");

    QString id = QString::fromLatin1(file.readAll()).trimmed();
    if (id.startsWith(QStringLiteral("0x")))
        id = id.mid(2);
    return id.isEmpty() ? QStringLiteral("????") : id.rightJustified(4, '0');
}

/* Device names as lspci gives them, by dddd:bb:dd.f location. */
static QHash<QString, QString>
lspciNames()
{
    QHash<QString, QString> names;
    QProcess                lspci;

    lspci.start(QStringLiteral("lspci"), { QStringLiteral("-D") });
    if (!lspci.waitForFinished(5000)) {
        lspci.kill();
        lspci.waitForFinished();
        return names;
    }

    const auto lines = QString::fromLocal8Bit(lspci.readAllStandardOutput()).split('\n');
    for (const auto &line : lines) {
        const int space = line.indexOf(' ');
        if (space > 0)
            names.insert(line.left(space), line.mid(space + 1).trimmed());
    }

    return names;
}

static QModelIndex
appendDevice(QStandardItemModel *model, int group, const QString &location, const QString &id, const QString &name)
{
    auto *check = new QStandardItem;
    check->setCheckable(true);
    check->setEditable(false);
    check->setData(group, GroupRole);
    check->setData(false, ROMEnabledRole);
    check->setData(QString(), ROMFileRole);

    QList<QStandardItem *> row = { check,
                                   new QStandardItem((group >= 0) ? QString::number(group) : QStringLiteral("??")),
                                   new QStandardItem(location),
                                   new QStandardItem(QStringLiteral("[%1]").arg(id)),
                                   new QStandardItem(name) };
    for (auto *item : row)
        item->setEditable(false);
    model->appendRow(row);

    return check->index();
}
#endif

SettingsOtherPeripherals::SettingsOtherPeripherals(QWidget *parent)
    : QWidget(parent)
    , ui(new Ui::SettingsOtherPeripherals)
{
    ui->setupUi(this);

    /* Memory expansion cards: one shared set of four slots, fed from the
       ISA board list (isamem) on ISA machines and from the MCA board list
       (mcamem) on MCA machines. */
    for (uint8_t i = 0; i < ISAMEM_MAX; ++i) {
        scMemExpCard[i] = new SettingsCompleter(findChild<QComboBox *>(QString("comboBoxMemExpCard%1").arg(i + 1)), nullptr);
        memexp_cfg_changed[i] = 0;
    }

    for (uint8_t i = 0; i < ISAROM_MAX; ++i) {
        scIsaRomCard[i] = new SettingsCompleter(findChild<QComboBox *>(QString("comboBoxIsaRomCard%1").arg(i + 1)), nullptr);
        isarom_cfg_changed[i] = 0;
    }

    scRTC           = new SettingsCompleter(ui->comboBoxRTC, nullptr);
    isartc_cfg_changed         = 0;

    unittester_cfg_changed     = 0;
    softpower_cfg_changed      = 0;
    softpower_card_enabled     = softpower_enabled > 0;
    novell_keycard_cfg_changed = 0;

#ifdef USE_VFIO
    vfioModel = new VFIODeviceModel(this);
    vfioModel->setHorizontalHeaderLabels({ QString(), tr("Group"), tr("Location"), tr("ID"), tr("Name") });
    ui->treeViewVFIO->setModel(vfioModel);

    /* FileField fixes its own width; let this one stretch across the panel. */
    ui->fileFieldVFIOROM->setMinimumWidth(0);
    ui->fileFieldVFIOROM->setMaximumWidth(QWIDGETSIZE_MAX);
    ui->fileFieldVFIOROM->setFilter(tr("ROM images") % util::DlgFilter({ "bin", "rom", "vbi" }) % tr("All files") % util::DlgFilter({ "*" }, true));

    vfioEnumerate();
    vfioRestore();

    for (int i = 0; i < vfioModel->columnCount(); i++)
        ui->treeViewVFIO->resizeColumnToContents(i);

    connect(ui->treeViewVFIO->selectionModel(), &QItemSelectionModel::currentRowChanged, this,
            [this](const QModelIndex &, const QModelIndex &) { vfioUpdateControls(); });
    connect(vfioModel, &QAbstractItemModel::dataChanged, this,
            [this](const QModelIndex &, const QModelIndex &, const QVector<int> &roles) {
                if (roles.isEmpty() || roles.contains(Qt::CheckStateRole))
                    vfioUpdateControls();
            });

    /* The custom ROM applies to the selected device. */
    connect(ui->checkBoxVFIOCustomROM, &QCheckBox::toggled, this, [this](bool checked) {
        if (vfioUpdating)
            return;
        const QModelIndex index = ui->treeViewVFIO->currentIndex().siblingAtColumn(ColumnCheck);
        if (index.isValid())
            vfioModel->setData(index, checked, ROMEnabledRole);
        ui->fileFieldVFIOROM->setEnabled(checked && ui->checkBoxVFIOCustomROM->isEnabled());
    });
    auto romFileChanged = [this](const QString &fileName) {
        if (vfioUpdating)
            return;
        const QModelIndex index = ui->treeViewVFIO->currentIndex().siblingAtColumn(ColumnCheck);
        if (index.isValid())
            vfioModel->setData(index, fileName, ROMFileRole);
    };
    connect(ui->fileFieldVFIOROM, &FileField::fileSelected, this, romFileChanged);
    connect(ui->fileFieldVFIOROM, &FileField::fileTextEntered, this, romFileChanged);

    if (vfioModel->rowCount() > 0)
        ui->treeViewVFIO->setCurrentIndex(vfioModel->index(0, 0));
    vfioUpdateControls();
#else
    /* VFIO is only available on Linux, with the development branch. */
    ui->tabWidgetSound->removeTab(ui->tabWidgetSound->indexOf(ui->tabVFIO));
    delete ui->tabVFIO;
#endif

    onCurrentMachineChanged(machine);
}

SettingsOtherPeripherals::~SettingsOtherPeripherals()
{
    for (uint8_t i = 0; i < ISAMEM_MAX; ++i)
        delete scMemExpCard[i];

    for (uint8_t i = 0; i < ISAROM_MAX; ++i)
        delete scIsaRomCard[i];

    delete scRTC;

    delete ui;
}

void
SettingsOtherPeripherals::onCurrentMachineChanged(int machineId)
{
    this->machineId = machineId;

    bool machineHasIsa          = (machine_has_bus(machineId, MACHINE_BUS_ISA) > 0);
    bool machineHasIsaOrSidecar = hasIsaOrSidecarBus(machineId);
    const bool nativeSoftPower = machines[machineId].init == machine_ibm5140_init;

    ui->pushButtonConfigureRTC->setEnabled(machineHasIsaOrSidecar);
    ui->comboBoxRTC->setEnabled(machineHasIsaOrSidecar);
    ui->checkBoxISABugger->setEnabled(machineHasIsa);
    ui->pushButtonConfigureUT->setEnabled(unittester_enabled > 0);
    ui->checkBoxKeyCard->setEnabled(machineHasIsa);
    ui->pushButtonConfigureKeyCard->setEnabled(novell_keycard_enabled > 0);
    ui->checkBoxSoftPower->setEnabled(machineHasIsa && !nativeSoftPower);
    ui->pushButtonConfigureSoftPower->setEnabled(machineHasIsa && !nativeSoftPower && softpower_card_enabled);

    ui->checkBoxISABugger->setChecked((machineHasIsa && (bugger_enabled > 0)) ? true : false);
    ui->checkBoxPOSTCard->setChecked(postcard_enabled > 0 ? true : false);
    ui->checkBoxUnitTester->setChecked(unittester_enabled > 0 ? true : false);
    ui->checkBoxKeyCard->setChecked((machineHasIsa && (novell_keycard_enabled > 0)) ? true : false);
    /* Native power hardware must not overwrite the optional card preference. */
    ui->checkBoxSoftPower->setChecked(nativeSoftPower || (machineHasIsa && softpower_card_enabled));

    scRTC->removeRows();
    ui->comboBoxRTC->clear();

    for (uint8_t i = 0; i < ISAMEM_MAX; ++i) {
        scMemExpCard[i]->removeRows();
        if (auto *cb = findChild<QComboBox *>(QString("comboBoxMemExpCard%1").arg(i + 1)))
            cb->clear();
    }

    for (uint8_t i = 0; i < ISAROM_MAX; ++i) {
        scIsaRomCard[i]->removeRows();
        if (auto *cb = findChild<QComboBox *>(QString("comboBoxIsaRomCard%1").arg(i + 1)))
            cb->clear();
    }

    int selectedRow = 0;

    // ISA RTC Cards
    auto *model = ui->comboBoxRTC->model();
    Models::Batch rtcRows(model);
    for (const auto &rtc : Models::Devices(isartc_get_device, isartc_get_internal_name, nullptr, 0)) {
        if (!device_is_valid(rtc.dev, machineId))
            continue;

        int row = rtcRows.add(rtc.name, rtc.id);
        scRTC->addDevice(rtc.dev, rtc.name);
        if (rtc.id == isartc_type)
            selectedRow = row;
    }
    rtcRows.commit();
    ui->comboBoxRTC->setCurrentIndex(selectedRow);
    ui->pushButtonConfigureRTC->setEnabled((isartc_type != 0) && isartc_has_config(isartc_type) && machineHasIsaOrSidecar);

    // Memory Expansion Cards (shared UI: ISA or MCA boards depending on
    // the machine bus).  The isamem/mcamem databases and their config
    // globals remain separate; only the four dropdown slots are shared.
    const bool mca_bus = (machine_has_bus(machineId, MACHINE_BUS_MCA) > 0);
    const bool isa_bus = !mca_bus && hasIsaOrSidecarBus(machineId);

    QComboBox          *mem_cbox[ISAMEM_MAX]         = { 0 };
    QAbstractItemModel *mem_models[ISAMEM_MAX]       = { 0 };
    int                 mem_removeRows_[ISAMEM_MAX]  = { 0 };
    int                 mem_selectedRows[ISAMEM_MAX] = { 0 };

    for (uint8_t i = 0; i < ISAMEM_MAX; ++i) {
        mem_cbox[i]        = findChild<QComboBox *>(QString("comboBoxMemExpCard%1").arg(i + 1));
        mem_models[i]      = mem_cbox[i]->model();
        mem_removeRows_[i] = mem_models[i]->rowCount();
    }

    static const QVector<Models::Device> none;
    const auto &mem_cards = mca_bus ? Models::Devices(mcamem_get_device, mcamem_get_internal_name,
                                                      [](int c) -> int { return device_available(mcamem_get_device(c)); }, 0)
                          : isa_bus ? Models::Devices(isamem_get_device, isamem_get_internal_name,
                                                      [](int c) -> int { return device_available(isamem_get_device(c)); }, 0)
                                    : none;

    std::vector<Models::Batch> mem_rows(mem_models, mem_models + ISAMEM_MAX);
    for (const auto &card : mem_cards) {
        if (card.available && device_is_valid(card.dev, machineId)) {
            for (uint8_t i = 0; i < ISAMEM_MAX; ++i) {
                int cur = mca_bus ? mcamem_type[i] : isamem_type[i];
                int row = mem_rows[i].add(card.name, card.id);
                scMemExpCard[i]->addDevice(card.dev, card.name);

                if (card.id == cur)
                    mem_selectedRows[i] = row - mem_removeRows_[i];
            }
        }
    }

    for (uint8_t i = 0; i < ISAMEM_MAX; ++i) {
        mem_rows[i].commit();
        const device_t *seldev = mca_bus ? mcamem_get_device(mcamem_type[i]) : isamem_get_device(isamem_type[i]);
        bool            hascfg = mca_bus ? (mcamem_has_config(mcamem_type[i]) != 0) : (isamem_has_config(isamem_type[i]) != 0);

        mem_models[i]->removeRows(0, mem_removeRows_[i]);
        mem_cbox[i]->setEnabled(mem_models[i]->rowCount() > 1);
        mem_cbox[i]->setCurrentIndex(-1);
        mem_cbox[i]->setCurrentIndex(mem_selectedRows[i]);
        findChild<QPushButton *>(QString("pushButtonConfigureMemExpCard%1").arg(i + 1))->setEnabled(device_is_valid(seldev, machineId) && hascfg);
    }

    // ISA ROM Expansion Cards
    QComboBox          *isarom_cbox[ISAROM_MAX]         = { 0 };
    QAbstractItemModel *isarom_models[ISAROM_MAX]       = { 0 };
    int                 isarom_removeRows_[ISAROM_MAX]  = { 0 };
    int                 isarom_selectedRows[ISAROM_MAX] = { 0 };

    for (uint8_t i = 0; i < ISAROM_MAX; ++i) {
        isarom_cbox[i]        = findChild<QComboBox *>(QString("comboBoxIsaRomCard%1").arg(i + 1));
        isarom_models[i]      = isarom_cbox[i]->model();
        isarom_removeRows_[i] = isarom_models[i]->rowCount();
    }

    std::vector<Models::Batch> isarom_rows(isarom_models, isarom_models + ISAROM_MAX);
    for (const auto &card : Models::Devices(isarom_get_device, isarom_get_internal_name, nullptr, 0)) {
        if (device_is_valid(card.dev, machineId)) {
            for (uint8_t i = 0; i < ISAROM_MAX; ++i) {
                int row = isarom_rows[i].add(card.name, card.id);
                scIsaRomCard[i]->addDevice(card.dev, card.name);

                if (card.id == isarom_type[i])
                    isarom_selectedRows[i] = row - isarom_removeRows_[i];
            }
        }
    }

    for (uint8_t i = 0; i < ISAROM_MAX; ++i) {
        isarom_rows[i].commit();
        isarom_models[i]->removeRows(0, isarom_removeRows_[i]);
        isarom_cbox[i]->setEnabled(isarom_models[i]->rowCount() > 1);
        isarom_cbox[i]->setCurrentIndex(-1);
        isarom_cbox[i]->setCurrentIndex(isarom_selectedRows[i]);
        findChild<QPushButton *>(QString("pushButtonConfigureIsaRomCard%1").arg(i + 1))->setEnabled((isarom_type[i] != 0) && isarom_has_config(isarom_type[i]) && machineHasIsaOrSidecar);
    }
}

int
SettingsOtherPeripherals::changed()
{
    int has_changed = 0;

    has_changed |= (isartc_type            != ui->comboBoxRTC->currentData().toInt());
    has_changed |= isartc_cfg_changed;
    has_changed |= (bugger_enabled         != (ui->checkBoxISABugger->isChecked() ? 1 : 0));
    has_changed |= (postcard_enabled       != (ui->checkBoxPOSTCard->isChecked() ? 1 : 0));
    has_changed |= (unittester_enabled     != (ui->checkBoxUnitTester->isChecked() ? 1 : 0));
    has_changed |= unittester_cfg_changed;
    has_changed |= (softpower_enabled      != (softpower_card_enabled ? 1 : 0));
    has_changed |= softpower_cfg_changed;
    has_changed |= (novell_keycard_enabled != (ui->checkBoxKeyCard->isChecked() ? 1 : 0));
    has_changed |= novell_keycard_cfg_changed;

    /* Memory expansion boards (shared slots; active family by machine). */
    {
        const bool mca_bus = (machine_has_bus(machineId, MACHINE_BUS_MCA) > 0);

        if (mca_bus || hasIsaOrSidecarBus(machineId)) {
            for (int i = 0; i < ISAMEM_MAX; i++) {
                auto *cbox    = findChild<QComboBox *>(QString("comboBoxMemExpCard%1").arg(i + 1));
                int   cur     = mca_bus ? mcamem_type[i] : isamem_type[i];
                has_changed  |= (cur != cbox->currentData().toInt());
                has_changed  |= memexp_cfg_changed[i];
            }
        }
    }

    /* ISA ROM boards. */
    for (int i = 0; i < ISAROM_MAX; i++) {
        auto *cbox     = findChild<QComboBox *>(QString("comboBoxIsaRomCard%1").arg(i + 1));
        has_changed |= (isarom_type[i]         != cbox->currentData().toInt());
        has_changed |= isarom_cfg_changed[i];
    }

#ifdef USE_VFIO
    has_changed |= vfioChanged();
#endif

    return has_changed ? (SETTINGS_CHANGED | SETTINGS_REQUIRE_HARD_RESET) : 0;
}

void
SettingsOtherPeripherals::restore()
{
}

void
SettingsOtherPeripherals::save(int soft)
{
    if (soft)
        return;

    /* Other peripherals category */
    isartc_type            = ui->comboBoxRTC->currentData().toInt();
    bugger_enabled         = ui->checkBoxISABugger->isChecked() ? 1 : 0;
    postcard_enabled       = ui->checkBoxPOSTCard->isChecked() ? 1 : 0;
    unittester_enabled     = ui->checkBoxUnitTester->isChecked() ? 1 : 0;
    softpower_enabled      = softpower_card_enabled ? 1 : 0;
    novell_keycard_enabled = ui->checkBoxKeyCard->isChecked() ? 1 : 0;

    /* Memory expansion boards (shared slots; write the active family and
       clear the inactive one, matching the single-UI layout). */
    {
        const bool mca_bus = (machine_has_bus(machineId, MACHINE_BUS_MCA) > 0);
        const bool isa_bus = !mca_bus && hasIsaOrSidecarBus(machineId);

        for (int i = 0; i < ISAMEM_MAX; i++) {
            int val = (mca_bus || isa_bus) ? findChild<QComboBox *>(QString("comboBoxMemExpCard%1").arg(i + 1))->currentData().toInt() : 0;

            if (mca_bus) {
                mcamem_type[i] = val;
                isamem_type[i] = 0;
            } else {
                isamem_type[i] = val;
                mcamem_type[i] = 0;
            }
        }
    }

    /* ISA ROM boards. */
    for (int i = 0; i < ISAROM_MAX; i++) {
        auto *cbox     = findChild<QComboBox *>(QString("comboBoxIsaRomCard%1").arg(i + 1));
        isarom_type[i] = cbox->currentData().toInt();
    }

#ifdef USE_VFIO
    vfioSave();
#endif
}

void
SettingsOtherPeripherals::on_comboBoxRTC_currentIndexChanged(int index)
{
    if (index < 0)
        return;

    ui->pushButtonConfigureRTC->setEnabled((index != 0) && isartc_has_config(index) && hasIsaOrSidecarBus(machineId));
}

void
SettingsOtherPeripherals::on_pushButtonConfigureRTC_clicked()
{
    isartc_cfg_changed |= DeviceConfig::ConfigureDevice(isartc_get_device(ui->comboBoxRTC->currentData().toInt()));
}

/* Shared memory-expansion slot helpers: resolve the board device and its
   config capability from the machine's active bus family (MCA or ISA). */
static const device_t *
memexp_slot_device(int idx, bool mca_bus)
{
    return mca_bus ? mcamem_get_device(idx) : isamem_get_device(idx);
}

static int
memexp_slot_has_config(int idx, bool mca_bus)
{
    return mca_bus ? mcamem_has_config(idx) : isamem_has_config(idx);
}

void
SettingsOtherPeripherals::on_comboBoxMemExpCard1_currentIndexChanged(int index)
{
    if (index < 0)
        return;

    const bool mca_bus = (machine_has_bus(machineId, MACHINE_BUS_MCA) > 0);
    ui->pushButtonConfigureMemExpCard1->setEnabled(device_is_valid(memexp_slot_device(index, mca_bus), machineId) && memexp_slot_has_config(index, mca_bus));
}

void
SettingsOtherPeripherals::on_pushButtonConfigureMemExpCard1_clicked()
{
    const bool mca_bus = (machine_has_bus(machineId, MACHINE_BUS_MCA) > 0);
    memexp_cfg_changed[0] |= DeviceConfig::ConfigureDevice(memexp_slot_device(ui->comboBoxMemExpCard1->currentData().toInt(), mca_bus), 1);
}

void
SettingsOtherPeripherals::on_comboBoxMemExpCard2_currentIndexChanged(int index)
{
    if (index < 0)
        return;

    const bool mca_bus = (machine_has_bus(machineId, MACHINE_BUS_MCA) > 0);
    ui->pushButtonConfigureMemExpCard2->setEnabled(device_is_valid(memexp_slot_device(index, mca_bus), machineId) && memexp_slot_has_config(index, mca_bus));
}

void
SettingsOtherPeripherals::on_pushButtonConfigureMemExpCard2_clicked()
{
    const bool mca_bus = (machine_has_bus(machineId, MACHINE_BUS_MCA) > 0);
    memexp_cfg_changed[1] |= DeviceConfig::ConfigureDevice(memexp_slot_device(ui->comboBoxMemExpCard2->currentData().toInt(), mca_bus), 2);
}

void
SettingsOtherPeripherals::on_comboBoxMemExpCard3_currentIndexChanged(int index)
{
    if (index < 0)
        return;

    const bool mca_bus = (machine_has_bus(machineId, MACHINE_BUS_MCA) > 0);
    ui->pushButtonConfigureMemExpCard3->setEnabled(device_is_valid(memexp_slot_device(index, mca_bus), machineId) && memexp_slot_has_config(index, mca_bus));
}

void
SettingsOtherPeripherals::on_pushButtonConfigureMemExpCard3_clicked()
{
    const bool mca_bus = (machine_has_bus(machineId, MACHINE_BUS_MCA) > 0);
    memexp_cfg_changed[2] |= DeviceConfig::ConfigureDevice(memexp_slot_device(ui->comboBoxMemExpCard3->currentData().toInt(), mca_bus), 3);
}

void
SettingsOtherPeripherals::on_comboBoxMemExpCard4_currentIndexChanged(int index)
{
    if (index < 0)
        return;

    const bool mca_bus = (machine_has_bus(machineId, MACHINE_BUS_MCA) > 0);
    ui->pushButtonConfigureMemExpCard4->setEnabled(device_is_valid(memexp_slot_device(index, mca_bus), machineId) && memexp_slot_has_config(index, mca_bus));
}

void
SettingsOtherPeripherals::on_pushButtonConfigureMemExpCard4_clicked()
{
    const bool mca_bus = (machine_has_bus(machineId, MACHINE_BUS_MCA) > 0);
    memexp_cfg_changed[3] |= DeviceConfig::ConfigureDevice(memexp_slot_device(ui->comboBoxMemExpCard4->currentData().toInt(), mca_bus), 4);
}

void
SettingsOtherPeripherals::on_comboBoxIsaRomCard1_currentIndexChanged(int index)
{
    if (index < 0)
        return;

    ui->pushButtonConfigureIsaRomCard1->setEnabled((index != 0) && isarom_has_config(index) && hasIsaOrSidecarBus(machineId));
}

void
SettingsOtherPeripherals::on_pushButtonConfigureIsaRomCard1_clicked()
{
    isarom_cfg_changed[0] |= DeviceConfig::ConfigureDevice(isarom_get_device(ui->comboBoxIsaRomCard1->currentData().toInt()), 1);
}

void
SettingsOtherPeripherals::on_comboBoxIsaRomCard2_currentIndexChanged(int index)
{
    if (index < 0)
        return;

    ui->pushButtonConfigureIsaRomCard2->setEnabled((index != 0) && isarom_has_config(index) && hasIsaOrSidecarBus(machineId));
}

void
SettingsOtherPeripherals::on_pushButtonConfigureIsaRomCard2_clicked()
{
    isarom_cfg_changed[1] |= DeviceConfig::ConfigureDevice(isarom_get_device(ui->comboBoxIsaRomCard2->currentData().toInt()), 2);
}

void
SettingsOtherPeripherals::on_comboBoxIsaRomCard3_currentIndexChanged(int index)
{
    if (index < 0)
        return;

    ui->pushButtonConfigureIsaRomCard3->setEnabled((index != 0) && isarom_has_config(index) && hasIsaOrSidecarBus(machineId));
}

void
SettingsOtherPeripherals::on_pushButtonConfigureIsaRomCard3_clicked()
{
    isarom_cfg_changed[2] |= DeviceConfig::ConfigureDevice(isarom_get_device(ui->comboBoxIsaRomCard3->currentData().toInt()), 3);
}

void
SettingsOtherPeripherals::on_comboBoxIsaRomCard4_currentIndexChanged(int index)
{
    if (index < 0)
        return;

    ui->pushButtonConfigureIsaRomCard4->setEnabled((index != 0) && isarom_has_config(index) && hasIsaOrSidecarBus(machineId));
}

void
SettingsOtherPeripherals::on_pushButtonConfigureIsaRomCard4_clicked()
{
    isarom_cfg_changed[3] |= DeviceConfig::ConfigureDevice(isarom_get_device(ui->comboBoxIsaRomCard4->currentData().toInt()), 4);
}

void
SettingsOtherPeripherals::on_checkBoxUnitTester_stateChanged(int arg1)
{
    ui->pushButtonConfigureUT->setEnabled(arg1 != 0);
}

void
SettingsOtherPeripherals::on_pushButtonConfigureUT_clicked()
{
    unittester_cfg_changed |= DeviceConfig::ConfigureDevice(&unittester_device);
}

void
SettingsOtherPeripherals::on_checkBoxSoftPower_stateChanged(int arg1)
{
    const bool optionalCard = (machines[machineId].init != machine_ibm5140_init) &&
                              (machine_has_bus(machineId, MACHINE_BUS_ISA) > 0);
    if (optionalCard)
        softpower_card_enabled = arg1 != 0;
    ui->pushButtonConfigureSoftPower->setEnabled(optionalCard && (arg1 != 0));
}

void
SettingsOtherPeripherals::on_pushButtonConfigureSoftPower_clicked()
{
    softpower_cfg_changed |= DeviceConfig::ConfigureDevice(&softpower_device);
}

void
SettingsOtherPeripherals::on_checkBoxKeyCard_stateChanged(int arg1)
{
    ui->pushButtonConfigureKeyCard->setEnabled(arg1 != 0);
}

void
SettingsOtherPeripherals::on_pushButtonConfigureKeyCard_clicked()
{
    novell_keycard_cfg_changed |= DeviceConfig::ConfigureDevice(&novell_keycard_device);
}

#ifdef USE_VFIO
/* List every device in an IOMMU group, by group. */
void
SettingsOtherPeripherals::vfioEnumerate()
{
    struct Device {
        int     group;
        QString location;
        QString id;
    };
    QList<Device> devices;

    const QDir groupsDir(iommuGroupsPath);
    for (const auto &groupName : groupsDir.entryList(QDir::Dirs | QDir::NoDotAndDotDot)) {
        bool      ok;
        const int group = groupName.toInt(&ok);
        if (!ok)
            continue;

        const QDir devicesDir(groupsDir.filePath(groupName % QStringLiteral("/devices")));
        for (const auto &location : devicesDir.entryList(QDir::Dirs | QDir::NoDotAndDotDot)) {
            const QString path = devicesDir.filePath(location);
            devices.append({ group, location,
                             readSysfsId(path % QStringLiteral("/vendor")) % ':' % readSysfsId(path % QStringLiteral("/device")) });
        }
    }

    std::sort(devices.begin(), devices.end(), [](const Device &a, const Device &b) {
        return (a.group != b.group) ? (a.group < b.group) : (a.location < b.location);
    });

    const auto names = devices.isEmpty() ? QHash<QString, QString>() : lspciNames();
    for (const auto &device : devices)
        appendDevice(vfioModel, device.group, device.location, device.id,
                     names.value(device.location, tr("Unknown device")));

    if (devices.isEmpty()) {
        const auto warning_icon = QIcon(":/misc/qt/icons/warning.ico");
        ui->labelVFIONoGroupsIcon->setPixmap(warning_icon.pixmap(warning_icon.actualSize(QSize(16, 16))));
    }
    ui->vfioNoGroupsWarning->setVisible(devices.isEmpty());
}

/* Check the configured devices, adding the ones not found at the end. */
void
SettingsOtherPeripherals::vfioRestore()
{
    for (int i = 0; (i < VFIO_DEVICE_MAX) && vfio_device_conf[i].location[0]; i++) {
        const QString location = normalizedLocation(vfio_device_conf[i].location);

        QModelIndex     index;
        QModelIndexList matches;
        if (vfioModel->rowCount() > 0)
            matches = vfioModel->match(vfioModel->index(0, ColumnLocation), Qt::DisplayRole, location, 1, Qt::MatchExactly);
        if (matches.isEmpty())
            index = appendDevice(vfioModel, -1, location, QStringLiteral("????:????"), tr("Unknown device"));
        else
            index = matches.first().siblingAtColumn(ColumnCheck);

        vfioModel->setData(index, static_cast<int>(Qt::Checked), Qt::CheckStateRole);
        vfioModel->setData(index, vfio_device_conf[i].rom_fn[0] != '\0', ROMEnabledRole);
        vfioModel->setData(index, QString(vfio_device_conf[i].rom_fn), ROMFileRole);
    }
}

SettingsOtherPeripherals::VFIODeviceList
SettingsOtherPeripherals::vfioSelectedDevices() const
{
    VFIODeviceList devices;

    for (int row = 0; row < vfioModel->rowCount(); row++) {
        const QModelIndex index = vfioModel->index(row, ColumnCheck);
        if (index.data(Qt::CheckStateRole).toInt() != Qt::Checked)
            continue;

        const QString romFile = index.data(ROMEnabledRole).toBool() ? index.data(ROMFileRole).toString() : QString();
        devices.append({ index.siblingAtColumn(ColumnLocation).data().toString(), romFile });
    }

    return devices;
}

int
SettingsOtherPeripherals::vfioChanged() const
{
    VFIODeviceList current;
    for (int i = 0; (i < VFIO_DEVICE_MAX) && vfio_device_conf[i].location[0]; i++)
        current.append({ normalizedLocation(vfio_device_conf[i].location), QString(vfio_device_conf[i].rom_fn) });

    /* The order the devices are listed in is not a change. */
    VFIODeviceList selected = vfioSelectedDevices();
    std::sort(current.begin(), current.end());
    std::sort(selected.begin(), selected.end());

    return (current != selected) ? (SETTINGS_CHANGED | SETTINGS_REQUIRE_HARD_RESET) : 0;
}

void
SettingsOtherPeripherals::vfioSave()
{
    memset(vfio_device_conf, 0, sizeof(vfio_device_conf));

    int i = 0;
    for (const auto &device : vfioSelectedDevices()) {
        if (i >= VFIO_DEVICE_MAX)
            break;
        strncpy(vfio_device_conf[i].location, device.first.toUtf8().constData(), sizeof(vfio_device_conf[i].location) - 1);
        strncpy(vfio_device_conf[i].rom_fn, device.second.toUtf8().constData(), sizeof(vfio_device_conf[i].rom_fn) - 1);
        i++;
    }
}

/* The custom ROM applies to the selected device, if it is passed through. */
void
SettingsOtherPeripherals::vfioUpdateControls()
{
    const QModelIndex index   = ui->treeViewVFIO->currentIndex().siblingAtColumn(ColumnCheck);
    const bool        enabled = index.isValid() && (index.data(Qt::CheckStateRole).toInt() == Qt::Checked);

    vfioUpdating = true;
    ui->checkBoxVFIOCustomROM->setChecked(index.isValid() && index.data(ROMEnabledRole).toBool());
    const QString romFile = index.isValid() ? index.data(ROMFileRole).toString() : QString();
    if (ui->fileFieldVFIOROM->fileName() != romFile)
        ui->fileFieldVFIOROM->setFileName(romFile);
    vfioUpdating = false;

    ui->checkBoxVFIOCustomROM->setEnabled(enabled);
    ui->fileFieldVFIOROM->setEnabled(enabled && ui->checkBoxVFIOCustomROM->isChecked());
}
#endif
