/*
 * 86Box    A hypervisor and IBM PC system emulator that specializes in
 *          running old operating systems and software designed for IBM
 *          PC systems and compatibles from 1981 through fairly recent
 *          system designs based on the PCI bus.
 *
 *          This file is part of the 86Box distribution.
 *
 *          Advanced machine search dialog.
 *
 *          Copyright 2026 86Box contributors.
 */
#include "qt_machinesearch.hpp"

#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QGridLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QProgressDialog>
#include <QPushButton>
#include <QSignalBlocker>
#include <QSpinBox>
#include <QTreeWidget>
#include <QVBoxLayout>

#include <algorithm>

extern "C" {
#include <86box/86box.h>
#include <86box/device.h>
#include <86box/machine.h>
}

#include "qt_deviceconfig.hpp"

static QString
chipsetName(uintptr_t id)
{
    for (int i = 0; machine_chipsets[i].name != nullptr; i++) {
        if ((uintptr_t) machine_chipsets[i].id == id)
            return QString(machine_chipsets[i].name);
    }

    return QString("#%1").arg(id);
}

/* Some machines pick their on-board device at init time, so the table
   only has the flag for it. */
static QString
onboardName(int m, const device_t *dev, uintptr_t flag)
{
    if (dev != nullptr)
        return DeviceConfig::DeviceName(dev, dev->internal_name, 0);
    else if (machine_has_flags_64(m, flag))
        return MachineSearchDialog::tr("Built-in");

    return QString();
}

MachineSearchDialog::MachineSearchDialog(QWidget *parent)
    : QDialog(parent)
{
    setWindowTitle(tr("Advanced Search"));

    auto *filters = new QVBoxLayout;

    auto *form   = new QFormLayout;
    lineEditName = new QLineEdit;
    lineEditName->setClearButtonEnabled(true);
    lineEditName->setPlaceholderText(tr("Name or alias"));
    form->addRow(tr("Name:"), lineEditName);
    comboBoxType = new QComboBox;
    form->addRow(tr("Machine type:"), comboBoxType);
    comboBoxChipset = new QComboBox;
    form->addRow(tr("Chipset:"), comboBoxChipset);
    spinBoxRAM = new QSpinBox;
    spinBoxRAM->setRange(0, 65536);
    spinBoxRAM->setSuffix(tr(" MB"));
    spinBoxRAM->setSpecialValueText(tr("Any"));
    form->addRow(tr("Supports at least:"), spinBoxRAM);
    filters->addLayout(form);

    auto *legend = new QLabel(tr("Checked: must have. Filled: must not have. Empty: either."));
    legend->setWordWrap(true);
    filters->addWidget(legend);

    auto *busBox = new QGroupBox(tr("Bus"));
    busGrid      = new QGridLayout(busBox);
    addFlagFilter(busGrid, tr("ISA (8-bit)"), MACHINE_BUS_ISA, true);
    addFlagFilter(busGrid, tr("ISA (16-bit)"), MACHINE_BUS_ISA16, true);
    addFlagFilter(busGrid, tr("MCA"), MACHINE_BUS_MCA, true);
    addFlagFilter(busGrid, tr("EISA"), MACHINE_BUS_EISA, true);
    addFlagFilter(busGrid, tr("VLB"), MACHINE_BUS_VLB, true);
    addFlagFilter(busGrid, tr("PCI"), MACHINE_BUS_PCI, true);
    addFlagFilter(busGrid, tr("AGP"), MACHINE_BUS_AGP, true);
    addFlagFilter(busGrid, tr("AC'97 (AMR/CNR)"), MACHINE_BUS_AC97, true);
    addFlagFilter(busGrid, tr("PCMCIA"), MACHINE_BUS_PCMCIA, true);
    addFlagFilter(busGrid, tr("CardBus"), MACHINE_BUS_CARDBUS, true);
    addFlagFilter(busGrid, tr("USB"), MACHINE_BUS_USB, true);
    addFlagFilter(busGrid, tr("PS/2 ports"), MACHINE_BUS_PS2_PORTS, true);
    addFlagFilter(busGrid, tr("C-BUS"), MACHINE_BUS_CBUS, true);
    addFlagFilter(busGrid, tr("Cassette"), MACHINE_BUS_CASSETTE, true);
    addFlagFilter(busGrid, tr("PCjr sidecar"), MACHINE_BUS_SIDECAR, true);
    addFlagFilter(busGrid, tr("Mylex AT/32"), MACHINE_BUS_AT32, true);
    addFlagFilter(busGrid, tr("OPTi local bus"), MACHINE_BUS_OLB, true);
    addFlagFilter(busGrid, tr("HP HIL"), MACHINE_BUS_HIL, true);
    filters->addWidget(busBox);

    auto *intBox  = new QGroupBox(tr("On-board devices"));
    auto *intGrid = new QGridLayout(intBox);
    addFlagFilter(intGrid, tr("Video"), MACHINE_VIDEO, false);
    addFlagFilter(intGrid, tr("Sound"), MACHINE_SOUND, false);
    addFlagFilter(intGrid, tr("Network"), MACHINE_NIC_PRI | MACHINE_NIC_SEC, false);
    addFlagFilter(intGrid, tr("IDE"), MACHINE_IDE_QUAD, false);
    addFlagFilter(intGrid, tr("SCSI"), MACHINE_SCSI, false);
    addFlagFilter(intGrid, tr("MFM/RLL"), MACHINE_MFM, false);
    addFlagFilter(intGrid, tr("ESDI"), MACHINE_ESDI, false);
    addFlagFilter(intGrid, tr("XTA"), MACHINE_XTA, false);
    addFlagFilter(intGrid, tr("USB"), MACHINE_USB, false);
    filters->addWidget(intBox);

    auto *pmBox  = new QGroupBox(tr("Power management and APIC"));
    auto *pmGrid = new QGridLayout(pmBox);
    addFlagFilter(pmGrid, tr("APM"), MACHINE_APM, false);
    addFlagFilter(pmGrid, tr("ACPI"), MACHINE_ACPI, false);
    addFlagFilter(pmGrid, tr("APIC"), MACHINE_APIC, false);
    filters->addWidget(pmBox);

    auto *devBox  = new QGroupBox(tr("Internal devices"));
    auto *devForm = new QFormLayout(devBox);
    comboBoxVideo = new QComboBox;
    devForm->addRow(tr("Video:"), comboBoxVideo);
    comboBoxSound = new QComboBox;
    devForm->addRow(tr("Sound:"), comboBoxSound);
    comboBoxNet = new QComboBox;
    devForm->addRow(tr("Network:"), comboBoxNet);
    comboBoxSCSI = new QComboBox;
    devForm->addRow(tr("SCSI:"), comboBoxSCSI);
    filters->addWidget(devBox);
    filters->addStretch(1);

    auto *results = new QVBoxLayout;
    treeResults   = new QTreeWidget;
    treeResults->setHeaderLabels({ tr("Machine"), tr("Machine type"), tr("Chipset"),
                                   tr("On-board video"), tr("On-board sound"), tr("On-board network"),
                                   tr("On-board SCSI") });
    treeResults->setRootIsDecorated(false);
    treeResults->setUniformRowHeights(true);
    treeResults->header()->setSectionResizeMode(QHeaderView::Interactive);
    results->addWidget(treeResults);
    labelCount = new QLabel;
    results->addWidget(labelCount);

    auto *columns = new QHBoxLayout;
    columns->addLayout(filters);
    columns->addLayout(results, 1);

    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel | QDialogButtonBox::Reset);
    buttons->button(QDialogButtonBox::Ok)->setText(tr("Select"));
    buttons->button(QDialogButtonBox::Ok)->setEnabled(false);
    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    connect(buttons->button(QDialogButtonBox::Reset), &QPushButton::clicked, this, &MachineSearchDialog::reset);

    auto *layout = new QVBoxLayout(this);
    layout->addLayout(columns);
    layout->addWidget(buttons);

    connect(treeResults, &QTreeWidget::itemSelectionChanged, this, [this, buttons]() {
        buttons->button(QDialogButtonBox::Ok)->setEnabled(selectedMachine() != -1);
    });
    connect(treeResults, &QTreeWidget::itemDoubleClicked, this, &QDialog::accept);

    connect(lineEditName, &QLineEdit::textChanged, this, &MachineSearchDialog::updateResults);
    connect(spinBoxRAM, QOverload<int>::of(&QSpinBox::valueChanged), this, &MachineSearchDialog::updateResults);
    for (auto *cb : { comboBoxType, comboBoxChipset, comboBoxVideo, comboBoxSound, comboBoxNet, comboBoxSCSI })
        connect(cb, QOverload<int>::of(&QComboBox::currentIndexChanged), this, &MachineSearchDialog::updateResults);
    for (const auto &f : flagFilters)
        connect(f.checkBox, &QCheckBox::stateChanged, this, &MachineSearchDialog::updateResults);

    resize(1280, 640);
}

void
MachineSearchDialog::hideUnusedBuses()
{
    /* Packed again in order, so the hidden ones leave no holes. */
    int n = 0;
    for (const auto &f : flagFilters) {
        if (!f.bus)
            continue;

        bool used = false;
        for (int m : available) {
            if (machine_has_bus(m, f.flags)) {
                used = true;
                break;
            }
        }

        busGrid->removeWidget(f.checkBox);
        f.checkBox->setVisible(used);
        if (used) {
            busGrid->addWidget(f.checkBox, n / 3, n % 3);
            n++;
        }
    }
}

void
MachineSearchDialog::addFlagFilter(QGridLayout *grid, const QString &text, uintptr_t flags, bool bus)
{
    auto *checkBox = new QCheckBox(text);
    checkBox->setTristate(true);

    const int n = grid->count();
    grid->addWidget(checkBox, n / 3, n % 3);

    flagFilters.append({ checkBox, flags, bus });
}

void
MachineSearchDialog::fillDeviceCombo(QComboBox *comboBox, const QVector<const void *> &devices)
{
    QVector<QPair<QString, const void *>> rows;
    for (const void *dev : devices) {
        const auto *d = static_cast<const device_t *>(dev);
        rows.append({ DeviceConfig::DeviceName(d, d->internal_name, 0), dev });
    }
    std::sort(rows.begin(), rows.end(), [](const auto &a, const auto &b) {
        return QString::localeAwareCompare(a.first, b.first) < 0;
    });

    comboBox->addItem(tr("Any"), QVariant());
    for (const auto &row : rows)
        comboBox->addItem(row.first, QVariant::fromValue<quintptr>((quintptr) row.second));
    comboBox->setEnabled(!rows.isEmpty());
}

bool
MachineSearchDialog::load(QProgressDialog *progress)
{
    const int count = machine_count();
    progress->setMaximum(count);

    QVector<uintptr_t>    chipsets;
    QVector<int>          types;
    QVector<const void *> video;
    QVector<const void *> sound;
    QVector<const void *> net;
    QVector<const void *> scsi;

    for (int m = 0; m < count; m++) {
        progress->setValue(m);
        if (progress->wasCanceled())
            return false;

        if (!machine_available(m))
            continue;

        available.append(m);

        if (!chipsets.contains(machines[m].chipset))
            chipsets.append(machines[m].chipset);
        if (!types.contains(machine_get_type(m)))
            types.append(machine_get_type(m));

        if (machines[m].vid_device && !video.contains(machines[m].vid_device))
            video.append(machines[m].vid_device);
        if (machines[m].snd_device && !sound.contains(machines[m].snd_device))
            sound.append(machines[m].snd_device);
        if (machines[m].net_device && !net.contains(machines[m].net_device))
            net.append(machines[m].net_device);
        if (machines[m].scsi_device && !scsi.contains(machines[m].scsi_device))
            scsi.append(machines[m].scsi_device);
    }
    progress->setValue(count);

    const QSignalBlocker b1(comboBoxType);
    const QSignalBlocker b2(comboBoxChipset);
    const QSignalBlocker b3(comboBoxVideo);
    const QSignalBlocker b4(comboBoxSound);
    const QSignalBlocker b5(comboBoxNet);
    const QSignalBlocker b6(comboBoxSCSI);

    /* Machine types in table order, as the machine page lists them. */
    std::sort(types.begin(), types.end());
    comboBoxType->addItem(tr("Any"), -1);
    for (int type : types)
        comboBoxType->addItem(tr(machine_types[type].name), type);

    QVector<QPair<QString, uintptr_t>> chipsetRows;
    for (uintptr_t id : chipsets)
        chipsetRows.append({ chipsetName(id), id });
    std::sort(chipsetRows.begin(), chipsetRows.end(), [](const auto &a, const auto &b) {
        return QString::localeAwareCompare(a.first, b.first) < 0;
    });
    comboBoxChipset->addItem(tr("Any"), QVariant());
    for (const auto &row : chipsetRows)
        comboBoxChipset->addItem(row.first, QVariant::fromValue<quintptr>(row.second));

    fillDeviceCombo(comboBoxVideo, video);
    fillDeviceCombo(comboBoxSound, sound);
    fillDeviceCombo(comboBoxNet, net);
    fillDeviceCombo(comboBoxSCSI, scsi);

    hideUnusedBuses();
    updateResults();

    /* Sized once, to the full list, so filtering does not make them jump. */
    for (int c = 0; c < treeResults->columnCount(); c++)
        treeResults->resizeColumnToContents(c);

    return true;
}

bool
MachineSearchDialog::matches(int m) const
{
    const machine_t *mach = &machines[m];

    const QString name = lineEditName->text().trimmed();
    if (!name.isEmpty()) {
        bool found = QString(mach->name).contains(name, Qt::CaseInsensitive);
        for (int k = 0; !found && (k < 16) && (mach->aliases[k] != nullptr) && (mach->aliases[k][0] != 0x00); k++)
            found = QString(mach->aliases[k]).contains(name, Qt::CaseInsensitive);
        if (!found)
            return false;
    }

    const int type = comboBoxType->currentData().toInt();
    if ((type != -1) && (machine_get_type(m) != type))
        return false;

    const QVariant chipset = comboBoxChipset->currentData();
    if (chipset.isValid() && (mach->chipset != chipset.value<quintptr>()))
        return false;

    if ((spinBoxRAM->value() != 0) && (mach->ram.max < ((uint32_t) spinBoxRAM->value() << 10)))
        return false;

    for (const auto &f : flagFilters) {
        const Qt::CheckState state = f.checkBox->checkState();
        if (state == Qt::Unchecked)
            continue;

        const bool has = f.bus ? (machine_has_bus(m, f.flags) != 0) : (machine_has_flags_64(m, f.flags) != 0);
        if ((state == Qt::Checked) != has)
            return false;
    }

    const struct {
        QComboBox      *comboBox;
        const device_t *device;
    } devices[] = {
        { comboBoxVideo, mach->vid_device },
        { comboBoxSound, mach->snd_device },
        { comboBoxNet,   mach->net_device },
        { comboBoxSCSI,  mach->scsi_device },
    };
    for (const auto &d : devices) {
        const QVariant dev = d.comboBox->currentData();
        if (dev.isValid() && ((quintptr) d.device != dev.value<quintptr>()))
            return false;
    }

    return true;
}

void
MachineSearchDialog::updateResults()
{
    const int selected = selectedMachine();

    treeResults->clear();

    QList<QTreeWidgetItem *> items;
    QTreeWidgetItem         *selectedItem = nullptr;
    for (int m : available) {
        if (!matches(m))
            continue;

        auto *item = new QTreeWidgetItem({ machines[m].name,
                                           tr(machine_types[machine_get_type(m)].name),
                                           chipsetName(machines[m].chipset),
                                           onboardName(m, machines[m].vid_device, MACHINE_VIDEO),
                                           onboardName(m, machines[m].snd_device, MACHINE_SOUND),
                                           onboardName(m, machines[m].net_device, MACHINE_NIC_PRI | MACHINE_NIC_SEC),
                                           onboardName(m, machines[m].scsi_device, MACHINE_SCSI) });
        item->setData(0, Qt::UserRole, m);
        items.append(item);
        if (m == selected)
            selectedItem = item;
    }
    treeResults->addTopLevelItems(items);
    if (selectedItem != nullptr)
        treeResults->setCurrentItem(selectedItem);

    labelCount->setText(tr("Machines found: %1").arg(items.size()));
}

void
MachineSearchDialog::reset()
{
    /* One refresh at the end, not one per control. */
    {
        const QSignalBlocker b1(lineEditName);
        const QSignalBlocker b2(spinBoxRAM);
        lineEditName->clear();
        spinBoxRAM->setValue(0);
        for (auto *cb : { comboBoxType, comboBoxChipset, comboBoxVideo, comboBoxSound, comboBoxNet, comboBoxSCSI }) {
            const QSignalBlocker b(cb);
            cb->setCurrentIndex(0);
        }
        for (const auto &f : flagFilters) {
            const QSignalBlocker b(f.checkBox);
            f.checkBox->setCheckState(Qt::Unchecked);
        }
    }

    updateResults();
}

int
MachineSearchDialog::selectedMachine() const
{
    const auto *item = treeResults->currentItem();
    if ((item == nullptr) || !item->isSelected())
        return -1;

    return item->data(0, Qt::UserRole).toInt();
}
