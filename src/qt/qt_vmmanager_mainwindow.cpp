/*
 * 86Box    A hypervisor and IBM PC system emulator that specializes in
 *          running old operating systems and software designed for IBM
 *          PC systems and compatibles from 1981 through fairly recent
 *          system designs based on the PCI bus.
 *
 *          This file is part of the 86Box distribution.
 *
 *          86Box VM manager main window
 *
 * Authors: cold-brewed
 *
 *          Copyright 2024 cold-brewed
 */
#include "qt_vmmanager_mainwindow.hpp"
#include "qt_vmmanager_main.hpp"
#include "qt_vmmanager_preferences.hpp"
#include "qt_vmmanager_windarkmodefilter.hpp"
#include "ui_qt_vmmanager_mainwindow.h"
#if EMU_BUILD_NUM != 0
#    include "qt_updatecheckdialog.hpp"
#endif
#include "qt_about.hpp"
#include "qt_progsettings.hpp"
#include "qt_util.hpp"

#include <QLineEdit>
#include <QStringListModel>
#include <QCompleter>
#include <QCloseEvent>
#include <QDesktopServices>

extern "C"
{
extern void config_load_global();
extern void config_save_global();
}

VMManagerMainWindow* vmm_main_window = nullptr;
extern WindowsDarkModeFilter* vmm_dark_mode_filter;

VMManagerMainWindow::
VMManagerMainWindow(QWidget *parent)
    : ui(new Ui::VMManagerMainWindow)
    , vmm(new VMManagerMain(this))
    , statusLeft(new QLabel)
    , statusRight(new QLabel)
{
    ui->setupUi(this);

    vmm_main_window = this;

    // Connect signals from the VMManagerMain widget
    connect(vmm, &VMManagerMain::selectionChanged, this, &VMManagerMainWindow::vmmSelectionChanged);

    setWindowTitle(tr("%1 VM Manager").arg(EMU_NAME));
    setCentralWidget(vmm);

    // Set up the buttons
    connect(ui->actionNew_Machine, &QAction::triggered, vmm, &VMManagerMain::newMachineWizard);
    connect(ui->actionStartPause, &QAction::triggered, vmm, &VMManagerMain::startButtonPressed);
    connect(ui->actionSettings, &QAction::triggered, vmm, &VMManagerMain::settingsButtonPressed);
    connect(ui->actionHard_Reset, &QAction::triggered, vmm, &VMManagerMain::restartButtonPressed);
    connect(ui->actionForce_Shutdown, &QAction::triggered, vmm, &VMManagerMain::shutdownForceButtonPressed);

    // Set up menu actions
    // (Disable this if the EMU_BUILD_NUM == 0)
    #if EMU_BUILD_NUM == 0
        ui->actionCheck_for_updates->setVisible(false);
    #else
        connect(ui->actionCheck_for_updates, &QAction::triggered, this, &VMManagerMainWindow::checkForUpdatesTriggered);
    #endif
    
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
    searchBar->setPlaceholderText(tr("Search"));
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
#ifdef Q_OS_WINDOWS
    ui->toolBar->setBackgroundRole(QPalette::Light);
#endif
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
    connect(this, &VMManagerMainWindow::languageUpdated, vmm, &VMManagerMain::onLanguageUpdated);
#ifdef Q_OS_WINDOWS
    connect(this, &VMManagerMainWindow::darkModeUpdated, vmm, &VMManagerMain::onDarkModeUpdated);
    connect(this, &VMManagerMainWindow::preferencesUpdated, [] () { vmm_dark_mode_filter->reselectDarkMode(); });
#endif

    {
        auto config = new VMManagerConfig(VMManagerConfig::ConfigType::General);
        if (!!config->getStringValue("window_remember").toInt()) {
            QString coords = config->getStringValue("window_coordinates");
            if (!coords.isEmpty()) {
                QStringList list = coords.split(',');
                for (auto& cur : list) {
                    cur = cur.trimmed();
                }
                QRect geom;
                geom.setX(list[0].toInt());
                geom.setY(list[1].toInt());
                geom.setWidth(list[2].toInt());
                geom.setHeight(list[3].toInt());

                setGeometry(geom);
            }

            if (!!config->getStringValue("window_maximized").toInt()) {
                setWindowState(windowState() | Qt::WindowMaximized);
            }

            QString splitter = config->getStringValue("window_splitter");
            if (!splitter.isEmpty()) {
                QStringList list = splitter.split(',');
                for (auto& cur : list) {
                    cur = cur.trimmed();
                }
                QList<int> paneSizes;
                paneSizes.append(list[0].toInt());
                paneSizes.append(list[1].toInt());

                vmm->setPaneSizes(paneSizes);
            }
        } else {
            config->setStringValue("window_coordinates", "");
            config->setStringValue("window_maximized", "");
            config->setStringValue("window_splitter", "");
        }
        delete config;
    }
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
        disconnect(ui->actionStartPause, &QAction::triggered, vmm, &VMManagerMain::startButtonPressed);
        connect(ui->actionStartPause, &QAction::triggered, vmm, &VMManagerMain::pauseButtonPressed);
        ui->actionHard_Reset->setEnabled(true);
        ui->actionForce_Shutdown->setEnabled(true);
        ui->actionCtrl_Alt_Del->setEnabled(true);
    } else {
        ui->actionStartPause->setEnabled(true);
        ui->actionStartPause->setIcon(QIcon(":/menuicons/qt/icons/run.ico"));
        ui->actionStartPause->setText(tr("Start"));
        ui->actionStartPause->setToolTip(tr("Start"));
        disconnect(ui->actionStartPause, &QAction::triggered, vmm, &VMManagerMain::pauseButtonPressed);
        connect(ui->actionStartPause, &QAction::triggered, vmm, &VMManagerMain::startButtonPressed);
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
        updateLanguage();
    }
}

void
VMManagerMainWindow::updateSettings()
{
    config_load_global();
    emit preferencesUpdated();
    updateLanguage();
}

void
VMManagerMainWindow::saveSettings() const
{
    const auto currentSelection = vmm->getCurrentSelection();
    const auto config = new VMManagerConfig(VMManagerConfig::ConfigType::General);
    config->setStringValue("last_selection", currentSelection);
    if (!!config->getStringValue("window_remember").toInt()) {
        config->setStringValue("window_coordinates", QString::asprintf("%i, %i, %i, %i", this->geometry().x(), this->geometry().y(), this->geometry().width(), this->geometry().height()));
        config->setStringValue("window_maximized", this->isMaximized() ? "1" : "");
        config->setStringValue("window_splitter", QString::asprintf("%i, %i", vmm->getPaneSizes()[0], vmm->getPaneSizes()[1]));
    } else {
        config->setStringValue("window_coordinates", "");
        config->setStringValue("window_maximized", "");
        config->setStringValue("window_splitter", "");
    }
    // Sometimes required to ensure the settings save before the app exits
    config->sync();
}

void
VMManagerMainWindow::updateLanguage()
{
    ProgSettings::loadTranslators(QCoreApplication::instance());
    ProgSettings::reloadStrings();
    ui->retranslateUi(this);
    setWindowTitle(tr("%1 VM Manager").arg(EMU_NAME));
    emit languageUpdated();
}


#ifdef Q_OS_WINDOWS
void
VMManagerMainWindow::updateDarkMode()
{
    emit darkModeUpdated();
}
#endif

void
VMManagerMainWindow::changeEvent(QEvent *event)
{
#ifdef Q_OS_WINDOWS
    if (event->type() == QEvent::LanguageChange) {
        QApplication::setFont(QFont(ProgSettings::getFontName(lang_id), 9));
    }
#endif
    QWidget::changeEvent(event);
}

void
VMManagerMainWindow::closeEvent(QCloseEvent *event)
{
    int running = vmm->getActiveMachineCount();
    if (running > 0) {
        QMessageBox warningbox(QMessageBox::Icon::Warning, tr("%1 VM Manager").arg(EMU_NAME), tr("%1 machine(s) are currently active. Are you sure you want to exit the VM manager anyway?").arg(running), QMessageBox::Yes | QMessageBox::No, this);
        warningbox.exec();
        if (warningbox.result() == QMessageBox::No) {
            event->ignore();
            return;
        }
    }
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

#if EMU_BUILD_NUM != 0
void
VMManagerMainWindow::checkForUpdatesTriggered()
{
    auto updateChannel = UpdateCheck::UpdateChannel::CI;
#    ifdef RELEASE_BUILD
    updateChannel = UpdateCheck::UpdateChannel::Stable;
#    endif
    const auto updateCheck = new UpdateCheckDialog(updateChannel, this);
    updateCheck->exec();
}
#endif

void
VMManagerMainWindow::on_actionExit_triggered()
{
    this->close();
}

void
VMManagerMainWindow::on_actionAbout_Qt_triggered()
{
    QApplication::aboutQt();
}

void
VMManagerMainWindow::on_actionAbout_86Box_triggered()
{
    const auto msgBox = new About(this);
    msgBox->exec();
}

void
VMManagerMainWindow::on_actionDocumentation_triggered()
{
    QDesktopServices::openUrl(QUrl(EMU_DOCS_URL));
}
