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
#include "../cpu/cpu.h"

#include <86box/86box.h>
#include <86box/config.h>
#include <86box/device.h>
#include <86box/machine.h>
#include <86box/nvr.h>
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
    ui->softFloatWarningIcon->setPixmap(warning_icon.pixmap(warning_icon.actualSize(QSize(16, 16))));
    ui->softFloatWarningIcon->setVisible(false);
    ui->softFloatWarningText->setVisible(false);
    ui->debugRegs486WarningIcon->setPixmap(warning_icon.pixmap(warning_icon.actualSize(QSize(16, 16))));
    ui->debugRegs486WarningIcon->setVisible(false);
    ui->debugRegs486WarningText->setVisible(false);

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

    auto *pitModeModel = ui->comboBoxPitMode->model();
    pitModeModel->insertRows(0, 3);
    idx = pitModeModel->index(0, 0);
    pitModeModel->setData(idx, tr("Auto"), Qt::DisplayRole);
    pitModeModel->setData(idx, -1, Qt::UserRole);
    idx = pitModeModel->index(1, 0);
    pitModeModel->setData(idx, tr("Slow"), Qt::DisplayRole);
    pitModeModel->setData(idx, 0, Qt::UserRole);
    idx = pitModeModel->index(2, 0);
    pitModeModel->setData(idx, tr("Fast"), Qt::DisplayRole);
    pitModeModel->setData(idx, 1, Qt::UserRole);

    ui->comboBoxPitMode->setCurrentIndex(-1);
    ui->comboBoxPitMode->setCurrentIndex(pit_mode + 1);

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

    ui->comboBoxMachineType->setCurrentIndex(-1);
    ui->comboBoxMachineType->setCurrentIndex(selectedMachineType);

    ui->radioButtonLargerFrames->setChecked(force_10ms);
    ui->radioButtonSmallerFrames->setChecked(!force_10ms);

    ui->checkBoxOverrideInterpreter->setChecked(cpu_override_interpreter ? true : false);
    ui->checkBoxDebugRegs486->setChecked(cpu_debug_regs_486 ? true : false);

#ifndef USE_DYNAREC
    ui->checkBoxDynamicRecompiler->setEnabled(false);
    ui->checkBoxDynamicRecompiler->setVisible(false);
    ui->checkBoxBlockLinking->setVisible(false);
#else
    ui->checkBoxBlockLinking->setVisible(codegen_has_block_link);
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
#ifdef USE_DYNAREC
    has_changed |= (cpu_use_dynarec            != (ui->checkBoxDynamicRecompiler->isChecked() ? 1 : 0));
    has_changed |= (cpu_dyn_accurate_fpu_env   != (ui->checkBoxAccurateFPUEnvironment->isChecked() ? 1 : 0));
    has_changed |= (cpu_dyn_block_link         != (ui->checkBoxBlockLinking->isChecked() ? 1 : 0));
#endif
    has_changed |= (fpu_softfloat              != (ui->checkBoxFPUSoftfloat->isChecked() ? 1 : 0));
    has_changed |= (force_10ms                 != (ui->radioButtonLargerFrames->isChecked() ? 1 : 0));

    int64_t temp_mem_size;
    if (machine_get_ram_granularity(machine) < 1024)
        temp_mem_size = ui->spinBoxRAM->value();
    else
        temp_mem_size = ui->spinBoxRAM->value() * 1024;

    const int gran = machine_get_ram_granularity(machine);
    const int min  = machine_get_min_ram(machine);
    if (temp_mem_size > min)
        temp_mem_size = min + ((temp_mem_size - min) / gran) * gran;
    if (temp_mem_size < machine_get_min_ram(machine))
        temp_mem_size = machine_get_min_ram(machine);
    else if (temp_mem_size > machine_get_max_ram(machine))
        temp_mem_size = machine_get_max_ram(machine);
    has_changed |= (mem_size                   != static_cast<uint32_t>(temp_mem_size));

    if (ui->comboBoxWaitStates->isEnabled())
        temp = ui->comboBoxWaitStates->currentData().toInt();
    else
        temp = 0;
    has_changed |= (cpu_waitstates             != temp);

    has_changed |= (pit_mode                   !=  ui->comboBoxPitMode->currentData().toInt());

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
#ifdef USE_DYNAREC
    cpu_use_dynarec          = ui->checkBoxDynamicRecompiler->isChecked() ? 1 : 0;
    cpu_dyn_accurate_fpu_env = ui->checkBoxAccurateFPUEnvironment->isChecked() ? 1 : 0;
    cpu_dyn_block_link       = ui->checkBoxBlockLinking->isChecked() ? 1 : 0;
#endif
    fpu_softfloat            = ui->checkBoxFPUSoftfloat->isChecked() ? 1 : 0;
    force_10ms               = ui->radioButtonLargerFrames->isChecked() ? 1 : 0;

    int64_t temp_mem_size;
    if (machine_get_ram_granularity(machine) < 1024)
        temp_mem_size = ui->spinBoxRAM->value();
    else
        temp_mem_size = ui->spinBoxRAM->value() * 1024;

    const int gran = machine_get_ram_granularity(machine);
    const int min  = machine_get_min_ram(machine);
    if (temp_mem_size > min)
        temp_mem_size = min + ((temp_mem_size - min) / gran) * gran;
    if (temp_mem_size < machine_get_min_ram(machine))
        temp_mem_size = machine_get_min_ram(machine);
    else if (temp_mem_size > machine_get_max_ram(machine))
        temp_mem_size = machine_get_max_ram(machine);
    mem_size = static_cast<uint32_t>(temp_mem_size);

    if (ui->comboBoxWaitStates->isEnabled())
        cpu_waitstates = ui->comboBoxWaitStates->currentData().toInt();
    else
        cpu_waitstates = 0;

    pit_mode = ui->comboBoxPitMode->currentData().toInt();

    timeSyncSettings(time_sync, time_offset_years, time_fixed_year, time_keep_date);
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

        auto *modelCpu   = ui->comboBoxCPU->model();
        int   removeRows = modelCpu->rowCount();

        int i                    = 0;
        int eligibleRows         = 0;
        int selectedCpuFamilyRow = 0;
        Models::Batch cpuRows(modelCpu);
        while (cpu_families[i].package != 0) {
            if (cpu_family_is_eligible(&cpu_families[i], machineId)) {
                cpuRows.add(QString("%1 %2").arg(cpu_families[i].manufacturer, cpu_families[i].name), i);
                if (&cpu_families[i] == cpu_f)
                    selectedCpuFamilyRow = eligibleRows;
                ++eligibleRows;
            }
            ++i;
        }
        cpuRows.commit();
        modelCpu->removeRows(0, removeRows);
        ui->comboBoxCPU->setEnabled(eligibleRows > 1);
        ui->comboBoxCPU->setCurrentIndex(-1);
        ui->comboBoxCPU->setCurrentIndex(selectedCpuFamilyRow);

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
        ui->spinBoxRAM->setValue(mem_size / divisor);
        ui->spinBoxRAM->setEnabled(machine_get_min_ram(machineId) != machine_get_max_ram(machineId));

        emit currentMachineChanged(machineId);
    }
}

void
SettingsMachine::on_comboBoxCPU_currentIndexChanged(int index)
{
    if (index >= 0) {
        int         machineId   = ui->comboBoxMachine->currentData().toInt();
        int         cpuFamilyId = ui->comboBoxCPU->currentData().toInt();
        const auto *cpuFamily   = &cpu_families[cpuFamilyId];

        auto *modelSpeed = ui->comboBoxSpeed->model();
        int   removeRows = modelSpeed->rowCount();

        // win_settings_machine_recalc_cpu_m
        int i                = 0;
        int eligibleRows     = 0;
        int selectedSpeedRow = 0;
        Models::Batch speedRows(modelSpeed);
        while (cpuFamily->cpus[i].cpu_type != 0) {
            if (cpu_is_eligible(cpuFamily, i, machineId)) {
                speedRows.add(QString("%1").arg(cpuFamily->cpus[i].name), i);
                if (cpu == i)
                    selectedSpeedRow = eligibleRows;
                ++eligibleRows;
            }
            ++i;
        }
        speedRows.commit();
        modelSpeed->removeRows(0, removeRows);
        ui->comboBoxSpeed->setEnabled(eligibleRows > 1);
        ui->comboBoxSpeed->setCurrentIndex(-1);
        ui->comboBoxSpeed->setCurrentIndex(selectedSpeedRow);
    }
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
            ui->comboBoxWaitStates->setCurrentIndex(cpu_waitstates);
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
            ui->checkBoxBlockLinking->setEnabled(false);
        } else if ((flags & CPU_REQUIRES_DYNAREC) && !cpu_override) {
            ui->checkBoxDynamicRecompiler->setChecked(true);
            ui->checkBoxDynamicRecompiler->setEnabled(false);
            ui->checkBoxAccurateFPUEnvironment->setEnabled(true);
            ui->checkBoxBlockLinking->setEnabled(true);
        } else {
            ui->checkBoxDynamicRecompiler->setChecked(cpu_use_dynarec);
            ui->checkBoxDynamicRecompiler->setEnabled(true);
            ui->checkBoxAccurateFPUEnvironment->setEnabled(cpu_use_dynarec);
            ui->checkBoxBlockLinking->setEnabled(cpu_use_dynarec);
        }
#endif

        ui->checkBoxAccurateFPUEnvironment->setChecked(cpu_dyn_accurate_fpu_env);
        ui->checkBoxBlockLinking->setChecked(cpu_dyn_block_link);

        if ((cpuType < CPU_286) || (cpuType == CPU_IBM486SLC) || (cpuType == CPU_IBM486BL) ||
            !strcmp(cpuFamily->manufacturer, "Cyrix") || !strcmp(cpuFamily->manufacturer, "ST") ||
            (cpuType > CPU_486DLC))
            ui->checkBoxOverrideInterpreter->setEnabled(false);
        else
            ui->checkBoxOverrideInterpreter->setEnabled(true);

        // win_settings_machine_recalc_fpu
        auto *modelFpu   = ui->comboBoxFPU->model();
        int   removeRows = modelFpu->rowCount();

        int i              = 0;
        int selectedFpuRow = 0;
        Models::Batch fpuRows(modelFpu);
        for (const char *fpuName         = fpu_get_name_from_index(cpuFamily, cpuId, i);
             fpuName != nullptr; fpuName = fpu_get_name_from_index(cpuFamily, cpuId, ++i)) {
            auto fpuType = fpu_get_type_from_index(cpuFamily, cpuId, i);
            fpuRows.add(tr(fpuName), fpuType);
            if (fpu_type == fpuType)
                selectedFpuRow = i;
        }

        fpuRows.commit();
        modelFpu->removeRows(0, removeRows);
        ui->comboBoxFPU->setEnabled(modelFpu->rowCount() > 1);
        ui->comboBoxFPU->setCurrentIndex(-1);
        ui->comboBoxFPU->setCurrentIndex(selectedFpuRow);
    }
}

void
SettingsMachine::on_comboBoxFPU_currentIndexChanged(int index)
{
    if (index >= 0) {
        int         cpuFamilyId = ui->comboBoxCPU->currentData().toInt();
        const auto *cpuFamily   = &cpu_families[cpuFamilyId];
        int         cpuId       = ui->comboBoxSpeed->currentData().toInt();
        int         machineId   = ui->comboBoxMachine->currentData().toInt();

        if (fpu_get_type_from_index(cpuFamily, cpuId, index) == FPU_NONE) {
            ui->checkBoxFPUSoftfloat->setChecked(false);
            ui->checkBoxFPUSoftfloat->setEnabled(false);
        } else {
            ui->checkBoxFPUSoftfloat->setChecked(machine_has_flags(machineId, MACHINE_SOFTFLOAT_ONLY) ? true : fpu_softfloat);
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
SettingsMachine::on_checkBoxFPUSoftfloat_stateChanged(int state)
{
    if (state == Qt::Checked) {
        ui->softFloatWarningIcon->setVisible(true);
        ui->softFloatWarningText->setVisible(true);
    } else {
        ui->softFloatWarningIcon->setVisible(false);
        ui->softFloatWarningText->setVisible(false);
    }
}

void
SettingsMachine::on_checkBoxDebugRegs486_stateChanged(int state)
{
    ui->debugRegs486WarningIcon->setVisible(state == Qt::Checked);
    ui->debugRegs486WarningText->setVisible(state == Qt::Checked);
}

void
SettingsMachine::on_checkBoxDynamicRecompiler_stateChanged(int state)
{
#ifdef USE_DYNAREC
    ui->checkBoxAccurateFPUEnvironment->setEnabled(state);
    ui->checkBoxBlockLinking->setEnabled(state);
#endif
}

void
SettingsMachine::on_radioButtonSmallerFrames_clicked()
{
    ui->radioButtonLargerFrames->setChecked(false);
}

void
SettingsMachine::on_radioButtonLargerFrames_clicked()
{
    ui->radioButtonSmallerFrames->setChecked(false);
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
