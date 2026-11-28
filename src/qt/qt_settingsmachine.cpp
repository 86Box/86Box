/*
 * 86Box    A hypervisor and IBM PC system emulator that specializes in
 *          running old operating systems and software designed for IBM
 *          PC systems and compatibles from 1981 through fairly recent
 *          system designs based on the PCI bus.
 *
 *          This file is part of the 86Box distribution.
 *
 *          Machine selection and configuration UI module.
 *
 * Authors: Joakim L. Gilje <jgilje@jgilje.net>
 *
 *          Copyright 2021 Joakim L. Gilje
 */
#include "qt_settings_completer.hpp"

#include "qt_settingsmachine.hpp"
#include "ui_qt_settingsmachine.h"

#include <QDebug>
#include <QDialog>
#include <QFrame>
#include <QVBoxLayout>
#include <QDialogButtonBox>
#include <QStandardItem>
#include <QStandardItemModel>
#include <QCompleter>
#include <QSignalBlocker>
#include <QTimer>
#include <QDate>

#include <algorithm>

extern "C" {
#include "cpu_table.h"

#include <86box/86box.h>
#include <86box/config.h>
#include <86box/device.h>
#include <86box/machine.h>
#include <86box/nvr.h>
#include <86box/weitek.h>
}

#include "qt_deviceconfig.hpp"
#include "qt_models_common.hpp"

#include "qt_defs.hpp"

SettingsMachine::SettingsMachine(QWidget *parent)
    : QWidget(parent)
    , ui(new Ui::SettingsMachine)
{
    ui->setupUi(this);

    machine_cfg_changed             = 0;

    sc                              = new SettingsCompleter(ui->comboBoxMachine, ui->comboBoxMachineType);

    int         selectedMachineType = 0;
    auto       *machineTypesModel   = ui->comboBoxMachineType->model();
    int         i                   = -1;
    int         j                   = 0;
    int         cur_j               = 0;
    const void *miname;
    QSignalBlocker machineTypeSignals(ui->comboBoxMachineType);
    Models::Batch machineTypeRows(machineTypesModel);
    do {
        miname = machine_get_internal_name_ex(j);

        if ((miname == nullptr) || (machine_get_type(j) != i)) {
            if ((i != -1) && (cur_j != 0)) {
                int row = machineTypeRows.add(tr(machine_types[i].name), machine_types[i].id);
                if (machine_types[i].id == machine_get_type(machine))
                    selectedMachineType = row;
            }

            i     = machine_get_type(j);
            cur_j = 0;
        }

        if (machine_available(j)) {
            sc->addMachine(i, j);

            cur_j++;
        }

        j++;
    } while (miname != nullptr);
    machineTypeRows.commit();

    auto warning_icon = QIcon(":/misc/qt/icons/warning.ico");
    ui->accuracyWarningIcon->setPixmap(warning_icon.pixmap(warning_icon.actualSize(QSize(16, 16))));

    auto *waitStatesModel = ui->comboBoxWaitStates->model();
    waitStatesModel->insertRows(0, 9);
    auto idx = waitStatesModel->index(0, 0);
    waitStatesModel->setData(idx, tr("Default"), Qt::DisplayRole);
    waitStatesModel->setData(idx, 0, Qt::UserRole);
    for (int i = 0; i < 8; ++i) {
        idx = waitStatesModel->index(i + 1, 0);
        waitStatesModel->setData(idx, tr("%n wait state(s)", "", i), Qt::DisplayRole);
        waitStatesModel->setData(idx, i + 1, Qt::UserRole);
    }

    if (pit_mode == 0)
        ui->radioButtonPitModeAccurate->setChecked(true);
    else if (pit_mode == 1)
        ui->radioButtonPitModeFast->setChecked(true);
    else
        ui->radioButtonPitModeAuto->setChecked(true);

    ui->groupBoxTimeSync->setChecked(time_sync & TIME_SYNC_ENABLED);
    if (time_sync & TIME_SYNC_UTC)
        ui->radioButtonUTC->setChecked(true);
    else
        ui->radioButtonLocalTime->setChecked(true);

    /* Clamp so the resulting year stays in 1980..2099. Config may hold
       a wider range; the spinner shows the clamped value, but the
       original is preserved on save unless the user changes the control. */
    original_years_offset = time_offset_years;
    original_fixed_year   = time_fixed_year;
    years_offset_manipulated = false;
    fixed_year_manipulated   = false;

    const int host_year  = QDate::currentDate().year();
    ui->spinBoxYearsOffset->setMinimum(1980 - host_year);
    ui->spinBoxYearsOffset->setMaximum(2099 - host_year);
    ui->spinBoxFixedYear->setMinimum(1980);
    ui->spinBoxFixedYear->setMaximum(2099);

    /* Pre-fill both spinners so the inactive mode already matches the
       guest year. Switching modes then needs no manual entry. */
    int offset_value = time_offset_years;
    int fixed_value  = (time_fixed_year != 0) ? time_fixed_year
                                              : qBound(1980, host_year, 2099);
    if (time_fixed_year != 0 && time_offset_years == 0) {
        offset_value = time_fixed_year - host_year;
        original_years_offset = offset_value;
    } else if (time_offset_years != 0 && time_fixed_year == 0) {
        fixed_value = host_year + time_offset_years;
        original_fixed_year = fixed_value;
    }
    {
        const QSignalBlocker b1(ui->spinBoxYearsOffset);
        const QSignalBlocker b2(ui->spinBoxFixedYear);
        ui->spinBoxYearsOffset->setValue(offset_value);
        ui->spinBoxFixedYear->setValue(fixed_value);
    }

    if (time_keep_date)
        ui->radioButtonKeepDate->setChecked(true);
    else if (time_fixed_year != 0)
        ui->radioButtonFixedYear->setChecked(true);
    else if (time_offset_years != 0)
        ui->radioButtonOffsetYears->setChecked(true);
    else
        ui->radioButtonHostDate->setChecked(true);

    updateTimeControlsEnabled();
    updateGuestYearLabel();

    preferredWaitStates = cpu_waitstates;
    preferSoftFloat = fpu_softfloat != 0;
    connect(ui->comboBoxWaitStates, QOverload<int>::of(&QComboBox::activated), this,
            [this](int index) { preferredWaitStates = index; });
    connect(ui->checkBoxFPUSoftfloat, &QCheckBox::clicked, this,
            [this](bool checked) { preferSoftFloat = checked; });
#ifdef USE_DYNAREC
    preferDynarec = cpu_use_dynarec != 0;
    connect(ui->checkBoxDynamicRecompiler, &QCheckBox::clicked, this,
            [this](bool checked) { preferDynarec = checked; });
#endif
    ui->checkBoxAccurateFPUEnvironment->setChecked(cpu_dyn_accurate_fpu_env);

    ui->comboBoxMachineType->setCurrentIndex(-1);
    machineTypeSignals.unblock();
    ui->comboBoxMachineType->setCurrentIndex(selectedMachineType);

    ui->checkBoxOverrideInterpreter->setChecked(cpu_override_interpreter ? true : false);
    ui->checkBoxDebugRegs486->setChecked(cpu_debug_regs_486 ? true : false);
    ui->checkBoxWeitek->setChecked(weitek_3167 != 0);
    ui->checkBoxWeitek4167->setChecked(weitek_4167 != 0);

#ifndef USE_DYNAREC
    ui->checkBoxDynamicRecompiler->setEnabled(false);
    ui->checkBoxDynamicRecompiler->setVisible(false);
    ui->checkBoxAccurateFPUEnvironment->setEnabled(false);
    ui->checkBoxAccurateFPUEnvironment->setVisible(false);
#endif
}

SettingsMachine::~SettingsMachine()
{
    delete sc;

    delete ui;
}

int
SettingsMachine::changed()
{
    int has_changed = 0;
    int temp;

    has_changed |= (machine                    != ui->comboBoxMachine->currentData().toInt());
    has_changed |= machine_cfg_changed;
    has_changed |= (cpu_f                      != const_cast<cpu_family_t *>(&cpu_families[ui->comboBoxCPU->currentData().toInt()]));
    has_changed |= (cpu                        != ui->comboBoxSpeed->currentData().toInt());
    has_changed |= (fpu_type                   != ui->comboBoxFPU->currentData().toInt());
    has_changed |= (cpu_override_interpreter   != (ui->checkBoxOverrideInterpreter->isChecked() ? 1 : 0));
    has_changed |= (cpu_debug_regs_486         != (ui->checkBoxDebugRegs486->isChecked() ? 1 : 0));
    has_changed |= (weitek_3167                != ui->checkBoxWeitek->isChecked());
    has_changed |= (weitek_4167                != ui->checkBoxWeitek4167->isChecked());
#ifdef USE_DYNAREC
    has_changed |= (cpu_use_dynarec            != (ui->checkBoxDynamicRecompiler->isChecked() ? 1 : 0));
    has_changed |= (cpu_dyn_accurate_fpu_env   != (ui->checkBoxAccurateFPUEnvironment->isChecked() ? 1 : 0));
#endif
    has_changed |= (fpu_softfloat              != (ui->checkBoxFPUSoftfloat->isChecked() ? 1 : 0));

    has_changed |= (mem_size                   != selectedRamKiB());

    if (ui->comboBoxWaitStates->isEnabled())
        temp = ui->comboBoxWaitStates->currentData().toInt();
    else
        temp = 0;
    has_changed |= (cpu_waitstates             != temp);

    has_changed |= (pit_mode                   != pitMode());

    int sync;
    int offset_years;
    int fixed_year;
    int keep_date;
    timeSyncSettings(sync, offset_years, fixed_year, keep_date);
    has_changed |= (time_sync         != sync);
    has_changed |= (time_offset_years != offset_years);
    has_changed |= (time_fixed_year   != fixed_year);
    has_changed |= (time_keep_date    != keep_date);

    return has_changed ? (SETTINGS_CHANGED | SETTINGS_REQUIRE_HARD_RESET) : 0;
}

void
SettingsMachine::restore()
{
}

int
SettingsMachine::currentMachineId() const
{
    return ui->comboBoxMachine->currentData().toInt();
}

uint32_t
SettingsMachine::selectedRamKiB() const
{
    const int machineId = currentMachineId();
    const int granularity = machine_get_ram_granularity(machineId);
    const int minimum = machine_get_min_ram(machineId);
    const int maximum = machine_get_max_ram(machineId);
    const int ram = qBound(minimum, ui->spinBoxRAM->value() * ramUnitKiB, maximum);
    if (granularity == 0)
        return ram;
    return minimum + ((ram - minimum) / granularity) * granularity;
}

void
SettingsMachine::save(int soft)
{
    if (soft)
        return;

    machine                  = ui->comboBoxMachine->currentData().toInt();
    cpu_f                    = const_cast<cpu_family_t *>(&cpu_families[ui->comboBoxCPU->currentData().toInt()]);
    cpu                      = ui->comboBoxSpeed->currentData().toInt();
    fpu_type                 = ui->comboBoxFPU->currentData().toInt();
    cpu_override_interpreter = ui->checkBoxOverrideInterpreter->isChecked() ? 1 : 0;
    cpu_debug_regs_486       = ui->checkBoxDebugRegs486->isChecked() ? 1 : 0;
    weitek_3167              = ui->checkBoxWeitek->isChecked();
    weitek_4167              = ui->checkBoxWeitek4167->isChecked();
#ifdef USE_DYNAREC
    cpu_use_dynarec          = ui->checkBoxDynamicRecompiler->isChecked() ? 1 : 0;
    cpu_dyn_accurate_fpu_env = ui->checkBoxAccurateFPUEnvironment->isChecked() ? 1 : 0;
#endif
    fpu_softfloat            = ui->checkBoxFPUSoftfloat->isChecked() ? 1 : 0;

    mem_size = selectedRamKiB();

    if (ui->comboBoxWaitStates->isEnabled())
        cpu_waitstates = ui->comboBoxWaitStates->currentData().toInt();
    else
        cpu_waitstates = 0;

    pit_mode = pitMode();

    timeSyncSettings(time_sync, time_offset_years, time_fixed_year, time_keep_date);
}

/* The PIT mode the page selects: -1 auto, 0 accurate, 1 fast. */
int
SettingsMachine::pitMode() const
{
    if (ui->radioButtonPitModeAccurate->isChecked())
        return 0;
    if (ui->radioButtonPitModeFast->isChecked())
        return 1;
    return -1;
}

/* The time sync settings the page currently describes. At most one date mode
   applies; the others are cleared. */
void
SettingsMachine::timeSyncSettings(int &sync, int &offset_years, int &fixed_year, int &keep_date) const
{
    sync = TIME_SYNC_DISABLED;
    if (ui->groupBoxTimeSync->isChecked())
        sync = TIME_SYNC_ENABLED | (ui->radioButtonUTC->isChecked() ? TIME_SYNC_UTC : 0);

    offset_years = 0;
    fixed_year   = 0;
    keep_date    = ui->radioButtonKeepDate->isChecked() ? 1 : 0;
    if (ui->radioButtonFixedYear->isChecked())
        fixed_year = fixed_year_manipulated
                         ? ui->spinBoxFixedYear->value()
                         : (original_fixed_year != 0 ? original_fixed_year
                                                    : ui->spinBoxFixedYear->value());
    else if (ui->radioButtonOffsetYears->isChecked())
        offset_years = years_offset_manipulated
                           ? ui->spinBoxYearsOffset->value()
                           : original_years_offset;
}

void
SettingsMachine::on_comboBoxMachineType_currentIndexChanged(int index)
{
    if (index >= 0) {
        auto *model      = ui->comboBoxMachine->model();
        int   removeRows = model->rowCount();

        int selectedMachineRow = 0;
        {
            /* Swapping the list over moves the selection through machines
               nobody chose; only the one it lands on is announced, so the
               other pages are not rebuilt for the others, nor drop devices
               those machines cannot take. */
            const QSignalBlocker blocker(ui->comboBoxMachine);

            Models::Batch rows(model);
            for (int i = 0; i < machine_count(); ++i) {
                if ((machine_get_type(i) == ui->comboBoxMachineType->currentData().toInt()) && machine_available(i)) {
                    int row = rows.add(machines[i].name, i);
                    if (i == machine)
                        selectedMachineRow = row - removeRows;
                }
            }
            rows.commit();
            model->removeRows(0, removeRows);

            ui->comboBoxMachine->setCurrentIndex(-1);
        }
        ui->comboBoxMachine->setCurrentIndex(selectedMachineRow);
    }
}

void
SettingsMachine::on_comboBoxMachine_currentIndexChanged(int index)
{
    // win_settings_machine_recalc_machine
    if (index >= 0) {
        int         machineId = ui->comboBoxMachine->currentData().toInt();
        const auto *device    = machine_get_device(machineId);
        ui->pushButtonConfigure->setEnabled((device != nullptr) && (device->config != nullptr));

        const QVariant currentFamily = ui->comboBoxCPU->currentData();
        const auto *preferredFamily = currentFamily.isValid() ? &cpu_families[currentFamily.toInt()] : cpu_f;
        const QVariant currentSpeed = ui->comboBoxSpeed->currentData();
        const int preferredSpeed = currentSpeed.isValid() ? currentSpeed.toInt() : cpu;

        auto *modelCpu   = ui->comboBoxCPU->model();
        int   removeRows = modelCpu->rowCount();
        QSignalBlocker cpuSignals(ui->comboBoxCPU);

        int i                    = 0;
        int eligibleRows         = 0;
        int selectedCpuFamilyRow = 0;
        Models::Batch cpuRows(modelCpu);
        while (cpu_families[i].package != 0) {
            if (cpu_family_is_eligible(&cpu_families[i], machineId)) {
                cpuRows.add(QString("%1 %2").arg(cpu_families[i].manufacturer, cpu_families[i].name), i);
                if (&cpu_families[i] == preferredFamily)
                    selectedCpuFamilyRow = eligibleRows;
                ++eligibleRows;
            }
            ++i;
        }
        cpuRows.commit();
        modelCpu->removeRows(0, removeRows);
        ui->comboBoxCPU->setEnabled(eligibleRows > 1);
        ui->comboBoxCPU->setCurrentIndex(selectedCpuFamilyRow);
        cpuSignals.unblock();
        const auto *selectedFamily = &cpu_families[ui->comboBoxCPU->currentData().toInt()];
        updateCpuSpeeds(selectedFamily == preferredFamily ? preferredSpeed : 0);

        const int pendingRam = ramUnitKiB ? ui->spinBoxRAM->value() * ramUnitKiB : mem_size;
        int divisor;
        if (machine_get_ram_granularity(machineId) < 1024) {
            divisor = 1;
            ui->spinBoxRAM->setSuffix(QCoreApplication::translate("", "KB").prepend(' '));
        } else {
            divisor = 1024;
            ui->spinBoxRAM->setSuffix(QCoreApplication::translate("", "MB").prepend(' '));
        }
        ui->spinBoxRAM->setMinimum(machine_get_min_ram(machineId) / divisor);
        ui->spinBoxRAM->setMaximum(machine_get_max_ram(machineId) / divisor);
        ui->spinBoxRAM->setSingleStep(machine_get_ram_granularity(machineId) / divisor);
        ui->spinBoxRAM->setValue(pendingRam / divisor);
        ramUnitKiB = divisor;
        ui->spinBoxRAM->setEnabled(machine_get_min_ram(machineId) != machine_get_max_ram(machineId));

        emit currentMachineChanged(machineId);
    }
}

void
SettingsMachine::on_comboBoxCPU_currentIndexChanged(int index)
{
    if (index >= 0) {
        const auto *family = &cpu_families[ui->comboBoxCPU->currentData().toInt()];
        updateCpuSpeeds(family == cpu_f ? cpu : 0);
    }
}

void
SettingsMachine::updateCpuSpeeds(int preferredSpeed)
{
    const int machineId = currentMachineId();
    const auto *cpuFamily = &cpu_families[ui->comboBoxCPU->currentData().toInt()];
    auto *model = ui->comboBoxSpeed->model();
    const int removeRows = model->rowCount();
    QSignalBlocker speedSignals(ui->comboBoxSpeed);

    int eligibleRows = 0;
    int selectedRow = 0;
    Models::Batch rows(model);
    for (int i = 0; cpuFamily->cpus[i].cpu_type != 0; ++i) {
        if (cpu_is_eligible(cpuFamily, i, machineId)) {
            rows.add(QString::fromUtf8(cpuFamily->cpus[i].name), i);
            if (i == preferredSpeed)
                selectedRow = eligibleRows;
            ++eligibleRows;
        }
    }
    rows.commit();
    model->removeRows(0, removeRows);
    ui->comboBoxSpeed->setEnabled(eligibleRows > 1);
    ui->comboBoxSpeed->setCurrentIndex(selectedRow);
    speedSignals.unblock();
    on_comboBoxSpeed_currentIndexChanged(ui->comboBoxSpeed->currentIndex());
}

void
SettingsMachine::on_comboBoxSpeed_currentIndexChanged(int index)
{
    if (index >= 0) {
        // win_settings_machine_recalc_cpu
        int         cpuFamilyId = ui->comboBoxCPU->currentData().toInt();
        const auto *cpuFamily   = &cpu_families[cpuFamilyId];
        int         cpuId       = ui->comboBoxSpeed->currentData().toInt();
        uint        cpuType     = cpuFamily->cpus[cpuId].cpu_type;
        int         machineId   = ui->comboBoxMachine->currentData().toInt();
        /* The Tandy VIS has no wait states. */
        if ((cpuType >= CPU_286) && (cpuType < CPU_386DX) && machines[machineId].init != machine_at_tandy_vis_init) {
            ui->comboBoxWaitStates->setEnabled(true);
            ui->comboBoxWaitStates->setCurrentIndex(preferredWaitStates);
        } else {
            ui->comboBoxWaitStates->setCurrentIndex(0);
            ui->comboBoxWaitStates->setEnabled(false);
        }

#ifdef USE_DYNAREC
        uint8_t flags = cpuFamily->cpus[cpuId].cpu_flags;
        if (!(flags & CPU_SUPPORTS_DYNAREC)) {
            ui->checkBoxDynamicRecompiler->setChecked(false);
            ui->checkBoxDynamicRecompiler->setEnabled(false);
            ui->checkBoxAccurateFPUEnvironment->setEnabled(false);
        } else if ((flags & CPU_REQUIRES_DYNAREC) && !cpu_override) {
            ui->checkBoxDynamicRecompiler->setChecked(true);
            ui->checkBoxDynamicRecompiler->setEnabled(false);
            ui->checkBoxAccurateFPUEnvironment->setEnabled(true);
        } else {
            ui->checkBoxDynamicRecompiler->setChecked(preferDynarec);
            ui->checkBoxDynamicRecompiler->setEnabled(true);
            ui->checkBoxAccurateFPUEnvironment->setEnabled(cpu_use_dynarec);
        }
#endif

        ui->checkBoxAccurateFPUEnvironment->setChecked(cpu_dyn_accurate_fpu_env);

        if ((cpuType < CPU_286) || (cpuType == CPU_IBM486SLC) || (cpuType == CPU_IBM486BL) ||
            !strcmp(cpuFamily->manufacturer, "Cyrix") || !strcmp(cpuFamily->manufacturer, "ST") ||
            (cpuType > CPU_486DLC))
            ui->checkBoxOverrideInterpreter->setEnabled(false);
        else
            ui->checkBoxOverrideInterpreter->setEnabled(true);

        // win_settings_machine_recalc_fpu
        // A board without an FPU socket only offers the CPU's own FPU.
        bool  noFpuSocket = machine_has_flags(machineId, MACHINE_NO_FPU_SOCKET);
        ui->checkBoxWeitek->setEnabled(!noFpuSocket && weitek_cpu_family_ok(cpuFamily));
        ui->checkBoxWeitek4167->setEnabled(weitek_4167_supported(cpuFamily, machineId));
        auto *modelFpu    = ui->comboBoxFPU->model();
        int   removeRows  = modelFpu->rowCount();
        const QVariant currentFpu = ui->comboBoxFPU->currentData();
        const int preferredFpu = currentFpu.isValid() ? currentFpu.toInt() : fpu_type;
        QSignalBlocker fpuSignals(ui->comboBoxFPU);

        int i              = 0;
        int row            = 0;
        int selectedFpuRow = 0;
        Models::Batch fpuRows(modelFpu);
        for (const char *fpuName         = fpu_get_name_from_index(cpuFamily, cpuId, i);
             fpuName != nullptr; fpuName = fpu_get_name_from_index(cpuFamily, cpuId, ++i)) {
            auto fpuType = fpu_get_type_from_index(cpuFamily, cpuId, i);
            if (noFpuSocket && (fpuType != FPU_NONE) && (fpuType != FPU_INTERNAL))
                continue;
            fpuRows.add(tr(fpuName), fpuType);
            if (preferredFpu == fpuType)
                selectedFpuRow = row;
            row++;
        }

        fpuRows.commit();
        modelFpu->removeRows(0, removeRows);
        ui->comboBoxFPU->setEnabled(modelFpu->rowCount() > 1);
        ui->comboBoxFPU->setCurrentIndex(selectedFpuRow);
        fpuSignals.unblock();
        on_comboBoxFPU_currentIndexChanged(ui->comboBoxFPU->currentIndex());
    }
}

void
SettingsMachine::on_comboBoxFPU_currentIndexChanged(int index)
{
    if (index >= 0) {
        int machineId = ui->comboBoxMachine->currentData().toInt();

        /* The FPU list can be filtered (a board without an FPU socket), so the
           type comes from the item data rather than from the raw CPU index. */
        if (ui->comboBoxFPU->currentData().toInt() == FPU_NONE) {
            ui->checkBoxFPUSoftfloat->setChecked(false);
            ui->checkBoxFPUSoftfloat->setEnabled(false);
        } else {
            ui->checkBoxFPUSoftfloat->setChecked(machine_has_flags(machineId, MACHINE_SOFTFLOAT_ONLY) || preferSoftFloat);
            ui->checkBoxFPUSoftfloat->setEnabled(machine_has_flags(machineId, MACHINE_SOFTFLOAT_ONLY) ? false : true);
        }
    }
}

void
SettingsMachine::on_pushButtonConfigure_clicked()
{
    // deviceconfig_inst_open
    int         machineId = ui->comboBoxMachine->currentData().toInt();
    const auto *device    = machine_get_device(machineId);
    machine_cfg_changed |= DeviceConfig::ConfigureDevice(device);
}

void
SettingsMachine::on_checkBoxDynamicRecompiler_stateChanged(int state)
{
#ifdef USE_DYNAREC
    ui->checkBoxAccurateFPUEnvironment->setEnabled(state);
#endif
}

void
SettingsMachine::updateGuestYearLabel()
{
    const int host_year = QDate::currentDate().year();
    const int guest_year = host_year + ui->spinBoxYearsOffset->value();
    //: Same-line preview after the offset spinner. %1 is the guest year the offset gives this year.
    ui->labelGuestYear->setText(tr("(currently %1)").arg(guest_year));
}

void
SettingsMachine::updateTimeControlsEnabled()
{
    /* The group box enables and disables its children as it is checked;
       this only narrows that down to the selected date mode. */
    const bool sync = ui->groupBoxTimeSync->isChecked();

    const bool offset_mode = sync && ui->radioButtonOffsetYears->isChecked();
    const bool fixed_mode  = sync && ui->radioButtonFixedYear->isChecked();
    ui->spinBoxYearsOffset->setEnabled(offset_mode);
    ui->labelGuestYear->setEnabled(offset_mode);
    ui->spinBoxFixedYear->setEnabled(fixed_mode);
}

void
SettingsMachine::on_spinBoxYearsOffset_valueChanged(int value)
{
    Q_UNUSED(value);
    years_offset_manipulated = true;
    updateGuestYearLabel();
}

void
SettingsMachine::on_spinBoxFixedYear_valueChanged(int value)
{
    Q_UNUSED(value);
    fixed_year_manipulated = true;
}

void
SettingsMachine::on_groupBoxTimeSync_toggled(bool checked)
{
    Q_UNUSED(checked);
    updateTimeControlsEnabled();
}

void
SettingsMachine::on_radioButtonOffsetYears_toggled(bool checked)
{
    Q_UNUSED(checked);
    updateTimeControlsEnabled();
}

void
SettingsMachine::on_radioButtonFixedYear_toggled(bool checked)
{
    Q_UNUSED(checked);
    updateTimeControlsEnabled();
}
