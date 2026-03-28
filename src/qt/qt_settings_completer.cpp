/*
 * 86Box    A hypervisor and IBM PC system emulator that specializes in
 *          running old operating systems and software designed for IBM
 *          PC systems and compatibles from 1981 through fairly recent
 *          system designs based on the PCI bus.
 *
 *          This file is part of the 86Box distribution.
 *
 *          Media history management module
 *
 * Authors: cold-brewed
 *
 *          Copyright 2022 The 86Box development team
 */
#include <QApplication>
#include <QFileInfo>
#include <QMetaEnum>
#include <QStringBuilder>
#include <QComboBox>
#include <QCompleter>
#include <QLineEdit>
#include <QStandardItemModel>
#include <utility>
#include "qt_settings_completer.hpp"
#ifdef Q_OS_WINDOWS
#    include <windows.h>
#endif

extern "C" {
#include "../cpu/cpu.h"

#include <86box/86box.h>
#include <86box/config.h>
#include <86box/device.h>
#include <86box/machine.h>
#include <86box/nvr.h>
}

bool
SettingsCompleter::eventFilter(QObject *watched, QEvent *event)
{
    if (watched == comboBoxMain) {
        if (event->type() == QEvent::FocusOut) {
            int     i    = comboBoxMain->currentIndex();
            QString name = comboBoxMain->model()->data(comboBoxMain->model()->index(i, 0), Qt::DisplayRole).toString();
            comboBoxMain->lineEdit()->setText(name);
        }
    }

    return false;
}

SettingsCompleter::SettingsCompleter(QComboBox *cb, QComboBox *cbSort)
{
    comboBoxMain = cb;
    comboBoxSort = cbSort;

    comboBoxMain->setEditable(true);
    completer = new QCompleter(comboBoxMain->lineEdit());
    model     = new QStandardItemModel(completer);
    completer->setModel(model);
    comboBoxMain->lineEdit()->setCompleter(completer);
    completer->setCompletionMode(QCompleter::PopupCompletion);
    completer->setFilterMode(Qt::MatchContains);
    completer->setCompletionRole(Qt::DisplayRole);
    completer->setCaseSensitivity(Qt::CaseInsensitive);
    completer->setMaxVisibleItems(30);
    comboBoxMain->lineEdit()->setClearButtonEnabled(true);
    comboBoxMain->setFocusPolicy(Qt::StrongFocus);
    comboBoxMain->installEventFilter(this);

    connect(completer, QOverload<const QModelIndex &>::of(&QCompleter::activated), this, [this](const QModelIndex &idx) {
        int  sort_id = idx.model()->data(idx, Qt::UserRole + 1).toInt();
        auto name    = idx.model()->data(idx, Qt::UserRole + 2).toString();
        if ((sort_id != -1) && (comboBoxSort != nullptr))  for (int i = 0; i < comboBoxSort->model()->rowCount(); i++) {
            if (comboBoxSort->model()->data(comboBoxSort->model()->index(i, 0), Qt::UserRole).toInt() == sort_id) {
                comboBoxSort->setCurrentIndex(i);

                for (int j = 0; j < comboBoxMain->model()->rowCount(); j++) {
                    if (comboBoxMain->model()->data(comboBoxMain->model()->index(j, 0), Qt::DisplayRole).toString() == name) {
                        comboBoxMain->setCurrentIndex(j);
                        break;
                    }
                }
                break;
            }
        } else  for (int i = 0; i < comboBoxMain->model()->rowCount(); i++) {
            if (comboBoxMain->model()->data(comboBoxMain->model()->index(i, 0), Qt::DisplayRole).toString() == name) {
                comboBoxMain->setCurrentIndex(i);
                break;
            }
        }
    });

    rows = 0;
}

void
SettingsCompleter::addRow(QString name, QString alias, int special, int id)
{
    QString stored_alias;
    if (name == alias)
        stored_alias = QString(name);
    else if (special)
        stored_alias = alias;
    else
        stored_alias = QString("%1 (%2)").arg(name).arg(alias);

    QStandardItem *item = new QStandardItem(stored_alias);
    item->setData(id);
    item->setData(name, Qt::UserRole + 2);
    model->appendRow(item);

    rows++;
}

void
SettingsCompleter::addMachine(int i, int j)
{
    addRow(machines[j].name, machines[j].name, 0, machine_types[machine_get_type(j)].id);

    int k = 0;
    while (machines[j].aliases[k][0] != 0x00) {
        addRow(machines[j].name, machines[j].aliases[k], 0, machine_types[machine_get_type(j)].id);
        k++;
    };
}

void
SettingsCompleter::addDevice(const void *device, QString name)
{
    addRow(name, name, 0, -1);

    const device_t *dev = (const device_t *) device;
    if (dev != nullptr) {
        const char *alias = device_get_alias(dev);
        if ((alias != nullptr) && (strlen(alias) > 0))
            addRow(name, alias, dev->flags & DEVICE_BIOS_ALIAS, -1);

        const device_config_t *config = dev->config;
        while (config && (config->type != CONFIG_END)) {
            if (config->type == CONFIG_BIOS) {
                const device_config_bios_t *bios = (const device_config_bios_t *) config->bios;

                /* Go through the ROMs in the device configuration. */
                while ((bios != nullptr) &&
                       (bios->name != nullptr) &&
                       (bios->internal_name != nullptr) &&
                       (bios->files_no != 0)) {
                    addRow(name, bios->name, dev->flags & DEVICE_BIOS_ALIAS, -1);
                    bios++;
                }
            }
            config++;
        }
    }
}

void
SettingsCompleter::removeRows()
{
    if (rows > 0) {
        auto removeRows = model->rowCount();

        model->removeRows(0, removeRows);

        rows = 0;
    }
}
