/*
 * 86Box    A hypervisor and IBM PC system emulator that specializes in
 *          running old operating systems and software designed for IBM
 *          PC systems and compatibles from 1981 through fairly recent
 *          system designs based on the PCI bus.
 *
 *          This file is part of the 86Box distribution.
 *
 *          Mouse/Joystick configuration UI module.
 *
 * Authors: Joakim L. Gilje <jgilje@jgilje.net>
 *
 *          Copyright 2021 Joakim L. Gilje
 */
#include "qt_preferences.hpp"
#include "qt_preferenceskeybindings.hpp"
#include "ui_qt_preferenceskeybindings.h"
#include "qt_mainwindow.hpp"

#include <QDialog>
#include <QTranslator>
#include <QDebug>
#include <QKeySequence>
#include <QMessageBox>
#include <string>

extern "C" {
#include <86box/86box.h>
#include <86box/device.h>
#include <86box/machine.h>
#include <86box/keyboard.h>
#include <86box/mouse.h>
#include <86box/gameport.h>
#include <86box/ui.h>
}

#include "qt_models_common.hpp"
#include "qt_deviceconfig.hpp"
#include "qt_joystickconfiguration.hpp"
#include "qt_keybind.hpp"

extern MainWindow *main_window;

accelKey org_acc_keys_t[NUM_ACCELS];

// Temporary working copy of key list
accelKey acc_keys_t[NUM_ACCELS];

PreferencesKeyBindings::PreferencesKeyBindings(QWidget *parent)
    : QWidget(parent)
    , ui(new Ui::PreferencesKeyBindings)
{
    ui->setupUi(this);

    QStringList horizontalHeader;
    QStringList verticalHeader;

    horizontalHeader.append(tr("Action"));
    horizontalHeader.append(tr("Keybind"));

    QTableWidget *keyTable = ui->tableKeys;
    keyTable->setRowCount(NUM_ACCELS);
    for (int i = 0; i < NUM_ACCELS; i++)
        keyTable->setRowHeight(i, 25);
    keyTable->setColumnCount(3);
    keyTable->setColumnHidden(2, true);
    keyTable->setColumnWidth(0, 200);
    keyTable->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
    QStringList headers;
    // headers << "Action" << "Bound key";
    keyTable->setHorizontalHeaderLabels(horizontalHeader);
    keyTable->verticalHeader()->setVisible(false);
    keyTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
    keyTable->setSelectionBehavior(QAbstractItemView::SelectRows);
    keyTable->setSelectionMode(QAbstractItemView::SingleSelection);
    keyTable->setShowGrid(true);

    // Make a working copy of acc_keys so we can check for dupes later without getting
    // confused
    for (int x = 0; x < NUM_ACCELS; x++) {
        strcpy(acc_keys_t[x].name, acc_keys[x].name);
        strcpy(acc_keys_t[x].desc, acc_keys[x].desc);
        strcpy(acc_keys_t[x].seq, acc_keys[x].seq);
    }

    refreshInputList();
}

PreferencesKeyBindings::~PreferencesKeyBindings()
{
    delete ui;
}

void
PreferencesKeyBindings::save()
{
    // Copy accelerators from working set to global set
    for (int x = 0; x < NUM_ACCELS; x++) {
        strcpy(acc_keys[x].name, acc_keys_t[x].name);
        strcpy(acc_keys[x].desc, acc_keys_t[x].desc);
        strcpy(acc_keys[x].seq, acc_keys_t[x].seq);
    }
    Preferences::reloadStrings();
}

void
PreferencesKeyBindings::refreshInputList()
{
    for (int x = 0; x < NUM_ACCELS; x++) {
        ui->tableKeys->setItem(x, 0, new QTableWidgetItem(tr(acc_keys_t[x].desc)));
        ui->tableKeys->setItem(x, 1, new QTableWidgetItem(QKeySequence(acc_keys_t[x].seq, QKeySequence::PortableText).toString(QKeySequence::NativeText)));
        ui->tableKeys->setItem(x, 2, new QTableWidgetItem(acc_keys_t[x].name));
    }
}

void
PreferencesKeyBindings::on_tableKeys_currentCellChanged(int currentRow, int currentColumn, int previousRow, int previousColumn)
{
    // Enable/disable bind/clear buttons if user clicked valid row
    QTableWidgetItem *cell = ui->tableKeys->item(currentRow, 1);
    if (!cell) {
        ui->pushButtonBind->setEnabled(false);
        ui->pushButtonClearBind->setEnabled(false);
    } else {
        ui->pushButtonBind->setEnabled(true);
        ui->pushButtonClearBind->setEnabled(true);
    }
}

void
PreferencesKeyBindings::on_tableKeys_cellDoubleClicked(int row, int col)
{
    // Edit bind
    QTableWidgetItem *cell = ui->tableKeys->item(row, 1);
    if (!cell)
        return;

    QKeySequence keyseq = KeyBinder::BindKey(this, cell->text());
    if (keyseq != false) {
        // If no change was made, don't change anything.
        if (keyseq.toString(QKeySequence::NativeText) == cell->text())
            return;

        // Otherwise, check for conflicts.
        // Check against the *working* copy - NOT the one in use by the app,
        // so we don't test against shortcuts the user already changed.
        for (int x = 0; x < NUM_ACCELS; x++) {
            if (QString::fromStdString(acc_keys_t[x].seq) == keyseq.toString(QKeySequence::PortableText)) {
                // That key is already in use
                QMessageBox::warning(this, tr("Bind conflict"), tr("This key combo is already in use."), QMessageBox::StandardButton::Ok);
                return;
            }
        }
        // If we made it here, there were no conflicts.
        // Go ahead and apply the bind.

        // Find the correct accelerator key entry
        int accKeyID = FindAccelerator(ui->tableKeys->item(row, 2)->text().toUtf8().constData());
        if (accKeyID < 0)
            return; // this should never happen

        // Make the change
        cell->setText(keyseq.toString(QKeySequence::NativeText));
        strcpy(acc_keys_t[accKeyID].seq, keyseq.toString(QKeySequence::PortableText).toUtf8().constData());

        refreshInputList();
    }
}

void
PreferencesKeyBindings::on_pushButtonBind_clicked()
{
    // Edit bind
    QTableWidgetItem *cell = ui->tableKeys->currentItem();
    if (!cell)
        return;

    on_tableKeys_cellDoubleClicked(cell->row(), cell->column());
}

void
PreferencesKeyBindings::on_pushButtonClearBind_clicked()
{
    // Wipe bind
    QTableWidgetItem *cell = ui->tableKeys->item(ui->tableKeys->currentRow(), 1);
    if (!cell)
        return;

    cell->setText("");
    // Find the correct accelerator key entry
    int accKeyID = FindAccelerator(ui->tableKeys->item(cell->row(), 2)->text().toUtf8().constData());
    if (accKeyID < 0)
        return; // this should never happen

    // Make the change
    cell->setText("");
    strcpy(acc_keys_t[accKeyID].seq, "");
}
