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
        if (event->type() == QEvent::FocusOut)
            comboBoxMain->lineEdit()->setText(machine_getname(comboBoxMain->currentData().toInt()));
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
        for (int i = 0; i < comboBoxSort->model()->rowCount(); i++) {
            if ((sort_id == -1) || (comboBoxSort->model()->data(comboBoxSort->model()->index(i, 0), Qt::UserRole).toInt() == sort_id)) {
                comboBoxSort->setCurrentIndex(i);

                for (int j = 0; j < comboBoxMain->model()->rowCount(); j++) {
                    if (comboBoxMain->model()->data(comboBoxMain->model()->index(j, 0), Qt::DisplayRole).toString() == name) {
                        comboBoxMain->setCurrentIndex(j);
                        break;
                    }
                }
                break;
            }
        }
    });
}

void
SettingsCompleter::addMachine(int i, int j)
{
    QStandardItem *item = new QStandardItem(machines[j].name);
    item->setData(machine_types[machine_get_type(j)].id);
    item->setData(machines[j].name, Qt::UserRole + 2);
    model->appendRow(item);

    int k = 0;
    while (machines[j].aliases[k][0] != 0x00) {
        QString stored_alias = QString("%1 (%2)").arg(machines[j].name).arg(machines[j].aliases[k]);
        QStandardItem *item = new QStandardItem(stored_alias);
        item->setData(machine_types[machine_get_type(j)].id);
        item->setData(machines[j].name, Qt::UserRole + 2);
        model->appendRow(item);
        k++;
    };
}
