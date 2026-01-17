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

#include <QCloseEvent>
#include <QDesktopServices>

extern "C" {
extern void config_load_global();
extern void config_save_global();
}

VMManagerMainWindow          *vmm_main_window = nullptr;
extern WindowsDarkModeFilter *vmm_dark_mode_filter;

VMManagerMainWindow::
    VMManagerMainWindow(QWidget *parent)
    : ui(new Ui::VMManagerMainWindow)
    , vmm(new VMManagerMain(this))
    , statusLeft(new QLabel)
    , statusRight(new QLabel)
{
    ui->setupUi(this);

    vmm_main_window = this;

    runIcon = QIcon(":/menuicons/qt/icons/run.ico");
    pauseIcon = QIcon(":/menuicons/qt/icons/pause.ico");

    // Connect signals from the VMManagerMain widget
    connect(vmm, &VMManagerMain::selectionOrStateChanged, this, &VMManagerMainWindow::vmmStateChanged);

    setWindowTitle(tr("%1 VM Manager").arg(EMU_NAME));
    setCentralWidget(vmm);

    // Set up the buttons
    connect(ui->actionNew_Machine, &QAction::triggered, vmm, &VMManagerMain::newMachineWizard);
    connect(ui->actionStartPause, &QAction::triggered, vmm, &VMManagerMain::startButtonPressed);
    connect(ui->actionSettings, &QAction::triggered, vmm, &VMManagerMain::settingsButtonPressed);
    connect(ui->actionHard_Reset, &QAction::triggered, vmm, &VMManagerMain::restartButtonPressed);
    connect(ui->actionForce_Shutdown, &QAction::triggered, vmm, &VMManagerMain::shutdownForceButtonPressed);
    connect(ui->actionCtrl_Alt_Del, &QAction::triggered, vmm, &VMManagerMain::cadButtonPressed);

// Set up menu actions
// (Disable this if the EMU_BUILD_NUM == 0)
#if EMU_BUILD_NUM == 0
    ui->actionCheck_for_updates->setVisible(false);
#else
    connect(ui->actionCheck_for_updates, &QAction::triggered, this, &VMManagerMainWindow::checkForUpdatesTriggered);
#endif

    // Set up the toolbar
    ui->actionStartPause->setEnabled(false);
    ui->actionStartPause->setIcon(runIcon);
    ui->actionStartPause->setText(tr("Start"));
    ui->actionStartPause->setToolTip(tr("Start"));
    ui->actionHard_Reset->setEnabled(false);
    ui->actionForce_Shutdown->setEnabled(false);
    ui->actionCtrl_Alt_Del->setEnabled(false);
    ui->actionSettings->setEnabled(false);

    // Preferences
    connect(ui->actionPreferences, &QAction::triggered, this, &VMManagerMainWindow::preferencesTriggered);

#ifdef Q_OS_WINDOWS
    ui->toolBar->setBackgroundRole(QPalette::Light);
#endif

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
    connect(this, &VMManagerMainWindow::preferencesUpdated, []() { vmm_dark_mode_filter->reselectDarkMode(); });
#endif

    {
        auto config = new VMManagerConfig(VMManagerConfig::ConfigType::General);
        ui->actionHide_tool_bar->setChecked(!!config->getStringValue("hide_tool_bar").toInt());
        if (ui->actionHide_tool_bar->isChecked())
            ui->toolBar->setVisible(false);
        if (!!config->getStringValue("window_remember").toInt()) {
            QString coords = config->getStringValue("window_coordinates");
            if (!coords.isEmpty()) {
                QStringList list = coords.split(',');
                for (auto &cur : list) {
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
                for (auto &cur : list) {
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

VMManagerMainWindow::~VMManagerMainWindow()
    = default;

void
VMManagerMainWindow::vmmStateChanged(const VMManagerSystem *sysconfig) const
{
    if (sysconfig == nullptr) {
        // This doubles both as a safety check and a way to disable
        // all machine-related buttons when no machines are present
        ui->actionStartPause->setEnabled(false);
        ui->actionSettings->setEnabled(false);
        ui->actionHard_Reset->setEnabled(false);
        ui->actionForce_Shutdown->setEnabled(false);
        ui->actionCtrl_Alt_Del->setEnabled(false);
        return;
    }
    const bool running = sysconfig->process->state() == QProcess::ProcessState::Running;

    if (running) {
        if (sysconfig->getProcessStatus() == VMManagerSystem::ProcessStatus::Running) {
            ui->actionStartPause->setIcon(pauseIcon);
            ui->actionStartPause->setText(tr("&Pause"));
            ui->actionStartPause->setToolTip(tr("Pause"));
            ui->actionStartPause->setIconText(tr("Pause"));
        } else {
            ui->actionStartPause->setIcon(runIcon);
            ui->actionStartPause->setText(tr("&Continue"));
            ui->actionStartPause->setToolTip(tr("Continue"));
            ui->actionStartPause->setIconText(tr("Continue"));
        }
        disconnect(ui->actionStartPause, &QAction::triggered, vmm, &VMManagerMain::startButtonPressed);
        disconnect(ui->actionStartPause, &QAction::triggered, vmm, &VMManagerMain::pauseButtonPressed);
        connect(ui->actionStartPause, &QAction::triggered, vmm, &VMManagerMain::pauseButtonPressed);
    } else {
        ui->actionStartPause->setIcon(runIcon);
        ui->actionStartPause->setText(tr("&Start"));
        ui->actionStartPause->setToolTip(tr("Start"));
        ui->actionStartPause->setIconText(tr("Start"));
        disconnect(ui->actionStartPause, &QAction::triggered, vmm, &VMManagerMain::pauseButtonPressed);
        disconnect(ui->actionStartPause, &QAction::triggered, vmm, &VMManagerMain::startButtonPressed);
        connect(ui->actionStartPause, &QAction::triggered, vmm, &VMManagerMain::startButtonPressed);
    }

    ui->actionStartPause->setEnabled(!sysconfig->window_obscured);
    ui->actionSettings->setEnabled(!sysconfig->window_obscured);
    ui->actionHard_Reset->setEnabled(sysconfig->window_obscured ? false : running);
    ui->actionForce_Shutdown->setEnabled(sysconfig->window_obscured ? false : running);
    ui->actionCtrl_Alt_Del->setEnabled(sysconfig->window_obscured ? false : running);
}
void
VMManagerMainWindow::preferencesTriggered()
{
    bool machinesRunning = (vmm->getActiveMachineCount() > 0);
    auto old_vmm_path = QString(vmm_path_cfg);
    const auto prefs = new VMManagerPreferences(this, machinesRunning);
    if (prefs->exec() == QDialog::Accepted) {
        emit preferencesUpdated();
        updateLanguage();

        auto new_vmm_path = QString(vmm_path_cfg);
        if (!machinesRunning && (new_vmm_path != old_vmm_path)) {
            qDebug() << "Machine path changed: old path " << old_vmm_path << ", new path " << new_vmm_path;
            strncpy(vmm_path, vmm_path_cfg, sizeof(vmm_path));
            vmm->reload();
        }
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
    const auto config           = new VMManagerConfig(VMManagerConfig::ConfigType::General);
    config->setStringValue("last_selection", currentSelection);
    config->setStringValue("hide_tool_bar", (ui->toolBar->isVisible() ? "0" : "1"));
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
        QMessageBox warningbox(QMessageBox::Icon::Warning, tr("%1 VM Manager").arg(EMU_NAME), tr("%n machine(s) are currently active. Are you sure you want to exit the VM manager anyway?", "", running), QMessageBox::Yes | QMessageBox::No, this);
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

void
VMManagerMainWindow::on_actionHide_tool_bar_triggered()
{
    const auto config = new VMManagerConfig(VMManagerConfig::ConfigType::General);
    int isHidden = config->getStringValue("hide_tool_bar").toInt();
    ui->toolBar->setVisible(!!isHidden);
    config->setStringValue("hide_tool_bar", (isHidden ? "0" : "1"));
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
