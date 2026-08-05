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
#include <QStandardItemModel>
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

// Temporary working copy of key list
accelKey acc_keys_t[NUM_ACCELS];

PreferencesKeyBindings::PreferencesKeyBindings(QWidget *parent)
    : QWidget(parent)
    , ui(new Ui::PreferencesKeyBindings)
{
    ui->setupUi(this);

    QAbstractItemModel *model = new QStandardItemModel(0, 2, this);

    model->setHeaderData(0, Qt::Horizontal, tr("Action"));
    model->setHeaderData(1, Qt::Horizontal, tr("Keybind"));
    ui->treeViewKeys->setModel(model);

    model->insertRows(0, NUM_ACCELS);
    ui->treeViewKeys->header()->setSectionResizeMode(0, QHeaderView::Stretch);

    connect(ui->treeViewKeys->selectionModel(), &QItemSelectionModel::currentRowChanged,
            this, &PreferencesKeyBindings::onKeyBindingsRowChanged);
    ui->treeViewKeys->setCurrentIndex(model->index(0, 0));

    // Make a working copy of acc_keys so we can check for dupes later without getting
    // confused
    for (int x = 0; x < NUM_ACCELS; x++)
        strcpy(acc_keys_t[x].seq, acc_keys[x].seq);

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
    for (int x = 0; x < NUM_ACCELS; x++)
        strcpy(acc_keys[x].seq, acc_keys_t[x].seq);
    Preferences::reloadStrings();
}

void
PreferencesKeyBindings::refreshInputList()
{
    auto *model = ui->treeViewKeys->model();

    for (int x = 0; x < NUM_ACCELS; x++) {
        QModelIndex idx = model->index(x, 0);
        model->setData(idx, tr(acc_keys[x].desc));
        model->setData(idx, QString(acc_keys[x].name), Qt::UserRole);
        model->setData(idx.siblingAtColumn(1), QKeySequence(acc_keys_t[x].seq, QKeySequence::PortableText).toString(QKeySequence::NativeText));
    }
    ui->treeViewKeys->resizeColumnToContents(0);
}

void
PreferencesKeyBindings::onKeyBindingsRowChanged(const QModelIndex &current)
{
    if (!current.isValid()) {
        ui->pushButtonBind->setEnabled(false);
        ui->pushButtonClearBind->setEnabled(false);
    } else {
        ui->pushButtonBind->setEnabled(true);
        ui->pushButtonClearBind->setEnabled(true);
    }
}

void
PreferencesKeyBindings::on_treeViewKeys_doubleClicked(const QModelIndex &idx)
{
    // Edit bind
    if (!idx.isValid())
        return;

    auto        *model      = ui->treeViewKeys->model();
    QString      keyseqText = model->data(idx.siblingAtColumn(1)).toString();
    QKeySequence keyseq     = KeyBinder::BindKey(this, keyseqText);
    if (keyseq != false) {
        // If no change was made, don't change anything.
        if (keyseq.toString(QKeySequence::NativeText) == keyseqText)
            return;

        // Otherwise, check for conflicts.
        // Check against the *working* copy - NOT the one in use by the app,
        // so we don't test against shortcuts the user already changed.
        for (int x = 0; x < NUM_ACCELS; x++) {
            if (QString(acc_keys_t[x].seq) == keyseq.toString(QKeySequence::PortableText)) {
                // That key is already in use
                QMessageBox::warning(this, tr("Bind conflict"), tr("This key combo is already in use."), QMessageBox::StandardButton::Ok);
                return;
            }
        }

        // If we made it here, there were no conflicts.
        // Go ahead and apply the bind.

        // Find the correct accelerator key entry
        int accKeyID = FindAccelerator(model->data(idx.siblingAtColumn(0), Qt::UserRole).toString().toUtf8().constData());
        if (accKeyID < 0)
            return; // this should never happen

        // Make the change
        model->setData(idx.siblingAtColumn(1), keyseq.toString(QKeySequence::NativeText));
        strncpy(acc_keys_t[accKeyID].seq, keyseq.toString(QKeySequence::PortableText).toUtf8().constData(), sizeof(acc_keys_t[accKeyID].seq) - 1);

        refreshInputList();
    }
}

void
PreferencesKeyBindings::on_pushButtonBind_clicked()
{
    // Edit bind
    auto idx = ui->treeViewKeys->selectionModel()->currentIndex();
    if (!idx.isValid())
        return;

    on_treeViewKeys_doubleClicked(idx);
}

void
PreferencesKeyBindings::on_pushButtonClearBind_clicked()
{
    // Wipe bind
    auto idx = ui->treeViewKeys->selectionModel()->currentIndex();
    if (!idx.isValid())
        return;

    auto *model = ui->treeViewKeys->model();
    model->setData(idx.siblingAtColumn(1), "");

    // Find the correct accelerator key entry
    int accKeyID = FindAccelerator(model->data(idx.siblingAtColumn(0), Qt::UserRole).toString().toUtf8().constData());
    if (accKeyID < 0)
        return; // this should never happen

    // Make the change
    strcpy(acc_keys_t[accKeyID].seq, "");
}
