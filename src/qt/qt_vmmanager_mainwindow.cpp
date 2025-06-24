/*
* 86Box	A hypervisor and IBM PC system emulator that specializes in
*		running old operating systems and software designed for IBM
*		PC systems and compatibles from 1981 through fairly recent
*		system designs based on the PCI bus.
*
*		This file is part of the 86Box distribution.
*
*		86Box VM manager main window
*
*
*
* Authors:	cold-brewed
*
*		Copyright 2024 cold-brewed
*/

#include "qt_vmmanager_mainwindow.hpp"
#include "qt_vmmanager_main.hpp"
#include "qt_vmmanager_preferences.hpp"
#include "ui_qt_vmmanager_mainwindow.h"
#include "qt_updatecheckdialog.hpp"

#include <QLineEdit>
#include <QStringListModel>
#include <QCompleter>

VMManagerMainWindow::
VMManagerMainWindow(QWidget *parent)
    : ui(new Ui::VMManagerMainWindow)
    , vmm(new VMManagerMain(this))
    , statusLeft(new QLabel)
    , statusRight(new QLabel)
{
    ui->setupUi(this);

    // Connect signals from the VMManagerMain widget
    connect(vmm, &VMManagerMain::selectionChanged, this, &VMManagerMainWindow::vmmSelectionChanged);

    setWindowTitle(tr("86Box VM Manager"));
    setCentralWidget(vmm);

    // Set up the buttons
    connect(ui->actionStartPause, &QAction::triggered, vmm, &VMManagerMain::startButtonPressed);
    connect(ui->actionSettings, &QAction::triggered, vmm, &VMManagerMain::settingsButtonPressed);
    connect(ui->actionHard_Reset, &QAction::triggered, vmm, &VMManagerMain::restartButtonPressed);
    connect(ui->actionForce_Shutdown, &QAction::triggered, vmm, &VMManagerMain::shutdownForceButtonPressed);
    connect(ui->actionNew_Machine, &QAction::triggered, vmm, &VMManagerMain::newMachineWizard);

    // Set up menu actions
    connect(ui->actionCheck_for_updates, &QAction::triggered, this, &VMManagerMainWindow::checkForUpdatesTriggered);

    // TODO: Remove all of this (all the way to END REMOVE) once certain the search will no longer be in the toolbar.
    // BEGIN REMOVE
    // Everything is still setup here for it but it is all hidden. None of it will be
    // needed if the search stays in VMManagerMain
    ui->actionStartPause->setEnabled(true);
    ui->actionStartPause->setIcon(QIcon(":/menuicons/qt/icons/run.ico"));
    ui->actionStartPause->setText(tr("Start"));
    ui->actionStartPause->setToolTip(tr("Start"));
    ui->actionHard_Reset->setEnabled(false);
    ui->actionForce_Shutdown->setEnabled(false);
    ui->actionCtrl_Alt_Del->setEnabled(false);

    const auto searchBar = new QLineEdit();
    searchBar->setMinimumWidth(150);
    searchBar->setPlaceholderText(" " + tr("Search"));
    searchBar->setSizePolicy(QSizePolicy::Maximum, QSizePolicy::Preferred);
    searchBar->setClearButtonEnabled(true);
    // Spacer to make the search go all the way to the right
    const auto spacer = new QWidget();
    spacer->setSizePolicy(QSizePolicy::Expanding,QSizePolicy::Preferred);
    ui->toolBar->addWidget(spacer);
    ui->toolBar->addWidget(searchBar);
    // Connect signal for search
    connect(searchBar, &QLineEdit::textChanged, vmm, &VMManagerMain::searchSystems);
    // Preferences
    connect(ui->actionPreferences, &QAction::triggered, this, &VMManagerMainWindow::preferencesTriggered);

    // Create a completer for the search bar
    auto *completer = new QCompleter(this);
    completer->setCaseSensitivity(Qt::CaseInsensitive);
    completer->setFilterMode(Qt::MatchContains);
    // Get the completer list
    const auto allStrings = vmm->getSearchCompletionList();
    // Set up the completer
    auto *completerModel = new QStringListModel(allStrings, completer);
    completer->setModel(completerModel);
    searchBar->setCompleter(completer);
    ui->toolBar->setVisible(false);
    // END REMOVE

    // Status bar widgets
    statusLeft->setAlignment(Qt::AlignLeft);
    statusRight->setAlignment(Qt::AlignRight);
    ui->statusbar->addPermanentWidget(statusLeft, 1);
    ui->statusbar->addPermanentWidget(statusRight, 1);
    connect(vmm, &VMManagerMain::updateStatusLeft, this, &VMManagerMainWindow::setStatusLeft);
    connect(vmm, &VMManagerMain::updateStatusRight, this, &VMManagerMainWindow::setStatusRight);

    // Inform the main view when preferences are updated
    connect(this, &VMManagerMainWindow::preferencesUpdated, vmm, &VMManagerMain::onPreferencesUpdated);

}

VMManagerMainWindow::~
VMManagerMainWindow()
    = default;

void
VMManagerMainWindow::vmmSelectionChanged(const QModelIndex &currentSelection, const QProcess::ProcessState processState) const
{
    if (processState == QProcess::Running) {
        ui->actionStartPause->setEnabled(true);
        ui->actionStartPause->setIcon(QIcon(":/menuicons/qt/icons/pause.ico"));
        ui->actionStartPause->setText(tr("Pause"));
        ui->actionStartPause->setToolTip(tr("Pause"));
        ui->actionHard_Reset->setEnabled(true);
        ui->actionForce_Shutdown->setEnabled(true);
        ui->actionCtrl_Alt_Del->setEnabled(true);
    } else {
        ui->actionStartPause->setEnabled(true);
        ui->actionStartPause->setIcon(QIcon(":/menuicons/qt/icons/run.ico"));
        ui->actionStartPause->setText(tr("Start"));
        ui->actionStartPause->setToolTip(tr("Start"));
        ui->actionHard_Reset->setEnabled(false);
        ui->actionForce_Shutdown->setEnabled(false);
        ui->actionCtrl_Alt_Del->setEnabled(false);
    }
}
void
VMManagerMainWindow::preferencesTriggered()
{
    const auto prefs = new VMManagerPreferences();
    if (prefs->exec() == QDialog::Accepted) {
        emit preferencesUpdated();
    }
}

void
VMManagerMainWindow::saveSettings() const
{
    const auto currentSelection = vmm->getCurrentSelection();
    const auto config = new VMManagerConfig(VMManagerConfig::ConfigType::General);
    config->setStringValue("last_selection", currentSelection);
    // Sometimes required to ensure the settings save before the app exits
    config->sync();
}

void
VMManagerMainWindow::closeEvent(QCloseEvent *event)
{
    saveSettings();
    QMainWindow::closeEvent(event);
}

void
VMManagerMainWindow::setStatusLeft(const QString &text) const
{
    statusLeft->setText(text);
}

void
VMManagerMainWindow::setStatusRight(const QString &text) const
{
    statusRight->setText(text);
}

void
VMManagerMainWindow::checkForUpdatesTriggered()
{
    auto updateChannel = UpdateCheck::UpdateChannel::CI;
#ifdef RELEASE_BUILD
    updateChannel = UpdateCheck::UpdateChannel::Stable;
#endif
    const auto updateCheck = new UpdateCheckDialog(updateChannel);
    updateCheck->exec();
}
