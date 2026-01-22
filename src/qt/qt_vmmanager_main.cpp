/*
 * 86Box    A hypervisor and IBM PC system emulator that specializes in
 *          running old operating systems and software designed for IBM
 *          PC systems and compatibles from 1981 through fairly recent
 *          system designs based on the PCI bus.
 *
 *          This file is part of the 86Box distribution.
 *
 *          86Box VM manager main module
 *
 * Authors: cold-brewed
 *
 *          Copyright 2024 cold-brewed
 */
#include <QDirIterator>
#include <QLabel>
#include <QAbstractListModel>
#include <QCompleter>
#include <QDebug>
#include <QDesktopServices>
#include <QMenu>
#include <QMessageBox>
#include <QStringListModel>
#include <QTimer>
#include <QProgressDialog>
#include <QShortcut>

#include <thread>
#include <atomic>

#include "qt_vmmanager_main.hpp"
#include "qt_vmmanager_mainwindow.hpp"
#include "ui_qt_vmmanager_main.h"
#include "qt_vmmanager_model.hpp"
#include "qt_vmmanager_addmachine.hpp"

extern VMManagerMainWindow *vmm_main_window;

// https://stackoverflow.com/a/36460740
bool
copyPath(QString sourceDir, QString destinationDir, bool overWriteDirectory)
{
    QDir originDirectory(sourceDir);

    if (!originDirectory.exists())
        return false;

    QDir destinationDirectory(destinationDir);

    if (destinationDirectory.exists() && !overWriteDirectory)
        return false;
    else if (destinationDirectory.exists() && overWriteDirectory)
        destinationDirectory.removeRecursively();

    originDirectory.mkpath(destinationDir);

    foreach (QString directoryName, originDirectory.entryList(QDir::Dirs | QDir::NoDotAndDotDot)) {
        QString destinationPath = destinationDir + "/" + directoryName;
        originDirectory.mkpath(destinationPath);
        copyPath(sourceDir + "/" + directoryName, destinationPath, overWriteDirectory);
    }

    foreach (QString fileName, originDirectory.entryList(QDir::Files)) {
        QFile::copy(sourceDir + "/" + fileName, destinationDir + "/" + fileName);
    }

    /*! Possible race-condition mitigation? */
    QDir finalDestination(destinationDir);
    finalDestination.refresh();

    if (finalDestination.exists())
        return true;

    return false;
}

VMManagerMain::VMManagerMain(QWidget *parent)
    : QWidget(parent)
    , ui(new Ui::VMManagerMain)
    , selected_sysconfig(new VMManagerSystem)
{
    ui->setupUi(this);

    // Set up the main listView
    ui->listView->setItemDelegate(new VMManagerListViewDelegate);
    vm_model    = new VMManagerModel;
    proxy_model = new StringListProxyModel(this);
    proxy_model->setSourceModel(vm_model);
    ui->listView->setModel(proxy_model);
    proxy_model->setSortCaseSensitivity(Qt::CaseInsensitive);
    ui->listView->model()->sort(0, Qt::AscendingOrder);

    // Connect the model signal
    connect(vm_model, &VMManagerModel::systemDataChanged, this, &VMManagerMain::modelDataChange);

    // Set up the context menu for the list view
    ui->listView->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(ui->listView, &QListView::customContextMenuRequested, [this, parent](const QPoint &pos) {
        const auto indexAt = ui->listView->indexAt(pos);
        if (indexAt.isValid()) {
            QMenu contextMenu("", ui->listView);

            QAction startAction(tr("&Start"));
            contextMenu.addAction(&startAction);
            connect(&startAction, &QAction::triggered, [this] {
                selected_sysconfig->startButtonPressed();
            });
            startAction.setEnabled(selected_sysconfig->process->state() == QProcess::NotRunning);
            startAction.setVisible(selected_sysconfig->process->state() == QProcess::NotRunning);

            QAction pauseAction(tr("&Pause"));
            contextMenu.addAction(&pauseAction);
            connect(&pauseAction, &QAction::triggered, [this] {
                selected_sysconfig->pauseButtonPressed();
            });
            pauseAction.setEnabled(selected_sysconfig->process->state() == QProcess::Running);
            pauseAction.setVisible(selected_sysconfig->process->state() == QProcess::Running);
            if (selected_sysconfig->getProcessStatus() != VMManagerSystem::ProcessStatus::Running)
                pauseAction.setText(tr("Re&sume"));

            QAction resetAction(tr("&Hard reset"));
            contextMenu.addAction(&resetAction);
            connect(&resetAction, &QAction::triggered, [this] {
                selected_sysconfig->restartButtonPressed();
            });
            resetAction.setEnabled(selected_sysconfig->process->state() == QProcess::Running);

            QAction forceShutdownAction(tr("&Force shutdown"));
            contextMenu.addAction(&forceShutdownAction);
            connect(&forceShutdownAction, &QAction::triggered, [this] {
                selected_sysconfig->shutdownForceButtonPressed();
            });
            forceShutdownAction.setEnabled(selected_sysconfig->process->state() == QProcess::Running);

            QAction cadAction(tr("&Ctrl+Alt+Del"));
            contextMenu.addAction(&cadAction);
            connect(&cadAction, &QAction::triggered, [this] {
                selected_sysconfig->cadButtonPressed();
            });
            cadAction.setEnabled(selected_sysconfig->process->state() == QProcess::Running);

            contextMenu.addSeparator();

            QAction settingsAction(tr("&Settings…"));
            contextMenu.addAction(&settingsAction);
            connect(&settingsAction, &QAction::triggered, [this] {
                selected_sysconfig->launchSettings();
            });

            QAction nameChangeAction(tr("Change &display name…"));
            contextMenu.addAction(&nameChangeAction);
            // Use a lambda to call a function so indexAt can be passed
            connect(&nameChangeAction, &QAction::triggered, ui->listView, [this, indexAt] {
                updateDisplayName(indexAt);
            });
            nameChangeAction.setEnabled(!selected_sysconfig->window_obscured);

            QAction setSystemIcon(tr("Set &icon…"));
            contextMenu.addAction(&setSystemIcon);
            connect(&setSystemIcon, &QAction::triggered, [this] {
                IconSelectionDialog dialog(":/systemicons/");
                if (dialog.exec() == QDialog::Accepted) {
                    const QString iconName = dialog.getSelectedIconName();
                    // A Blank iconName will cause setIcon to reset to the default
                    selected_sysconfig->setIcon(iconName);
                }
            });
            setSystemIcon.setEnabled(!selected_sysconfig->window_obscured);

            contextMenu.addSeparator();

            QAction cloneMachine(tr("C&lone…"));
            contextMenu.addAction(&cloneMachine);
            connect(&cloneMachine, &QAction::triggered, [this] {
                QDialog dialog = QDialog(this);
                auto    layout = new QVBoxLayout(&dialog);
                layout->setSizeConstraint(QLayout::SetFixedSize);
                layout->addWidget(new QLabel(tr("Virtual machine \"%1\" (%2) will be cloned into:").arg(selected_sysconfig->displayName, selected_sysconfig->config_dir)));
                QLineEdit *edit = new QLineEdit(&dialog);
                layout->addWidget(edit);
                QLabel *errLabel = new QLabel(&dialog);
                layout->addWidget(errLabel);
                errLabel->setVisible(false);
                QDialogButtonBox *buttonBox = new QDialogButtonBox(&dialog);
                buttonBox->setStandardButtons(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
                buttonBox->button(QDialogButtonBox::Ok)->setDisabled(true);
                connect(buttonBox, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
                connect(buttonBox, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
                layout->addWidget(buttonBox);
                connect(edit, &QLineEdit::textChanged, this, [errLabel, buttonBox](const QString &text) {
                    bool isSpaceOnly = true;
#ifdef Q_OS_WINDOWS
                    const char illegalChars[] = "<>:\"|?*\\/";
#else
                    const char illegalChars[] = "\\/";
#endif
                    for (const auto &curChar : text) {
                        for (size_t i = 0; i < sizeof(illegalChars) - 1; i++) {
                            if (illegalChars[i] == curChar) {
                                goto illegal_chars;
                            }
                            if (!curChar.isSpace()) {
                                isSpaceOnly = false;
                            }
                        }
                    }
                    errLabel->setVisible(false);
                    buttonBox->button(QDialogButtonBox::Ok)->setDisabled(isSpaceOnly || text.isEmpty());
                    if (QDir((QString(vmm_path) + "/") + text).exists() && buttonBox->button(QDialogButtonBox::Ok)->isEnabled()) {
                        goto dir_already_exists;
                    }
                    return;
dir_already_exists:
                    errLabel->setText(tr("Directory %1 already exists").arg(QDir((QString(vmm_path) + "/") + text).canonicalPath()));
                    errLabel->setVisible(true);
                    buttonBox->button(QDialogButtonBox::Ok)->setDisabled(true);
                    return;
illegal_chars:
                    QString illegalCharsDisplay;
                    for (size_t i = 0; i < sizeof(illegalChars) - 1; i++) {
                        illegalCharsDisplay.push_back(illegalChars[i]);
                        illegalCharsDisplay.push_back(' ');
                    }
                    illegalCharsDisplay.chop(1);
                    errLabel->setText(tr("You cannot use the following characters in the name: %1").arg(illegalCharsDisplay));
                    errLabel->setVisible(true);
                    buttonBox->button(QDialogButtonBox::Ok)->setDisabled(true);
                    return;
                });

                if (dialog.exec() > 0) {
                    std::atomic_bool finished { false };
                    std::atomic_bool errCode;
                    auto             vmDir = QDir(vmm_path).canonicalPath();
                    vmDir.append("/");
                    vmDir.append(edit->text());
                    vmDir.append("/");

                    if (!QDir(vmDir).mkpath(".")) {
                        QMessageBox::critical(this, tr("Clone"), tr("Failed to create directory for cloned VM"), QMessageBox::Ok);
                        return;
                    }

                    QProgressDialog *progDialog = new QProgressDialog(this);
                    progDialog->setMaximum(0);
                    progDialog->setMinimum(0);
                    progDialog->setWindowFlags(progDialog->windowFlags() & ~Qt::WindowCloseButtonHint);
                    progDialog->setMinimumSize(progDialog->sizeHint());
                    progDialog->setMaximumSize(progDialog->sizeHint());
                    progDialog->setMinimumDuration(0);
                    progDialog->setCancelButton(nullptr);
                    progDialog->setAutoClose(false);
                    progDialog->setAutoReset(false);
                    progDialog->setAttribute(Qt::WA_DeleteOnClose, true);
                    progDialog->setValue(0);
                    progDialog->setWindowTitle(tr("Clone"));
                    progDialog->show();
                    QString srcPath = selected_sysconfig->config_dir;
                    QString dstPath = vmDir;

                    std::thread copyThread([&finished, srcPath, dstPath, &errCode] {
                        errCode  = copyPath(srcPath, dstPath, true);
                        finished = true;
                    });
                    while (!finished) {
                        QApplication::processEvents();
                    }
                    copyThread.join();
                    progDialog->close();
                    if (!errCode) {
                        QDir(dstPath).removeRecursively();
                        QMessageBox::critical(this, tr("Clone"), tr("Failed to clone VM."), QMessageBox::Ok);
                        return;
                    }

                    QFileInfo configFileInfo(vmDir + CONFIG_FILE);
                    if (configFileInfo.exists()) {
                        const auto current_index = ui->listView->currentIndex();
                        vm_model->reload(this);
                        const auto created_object = vm_model->getIndexForConfigFile(configFileInfo);
                        if (created_object.row() < 0) {
                            // For some reason the index of the new object couldn't be determined. Fall back to the old index.
                            ui->listView->setCurrentIndex(current_index);
                            return;
                        }
                        auto added_system = vm_model->getConfigObjectForIndex(created_object);
                        added_system->setDisplayName(edit->text());
                        // Get the index of the newly-created system and select it
                        const QModelIndex mapped_index = proxy_model->mapFromSource(created_object);
                        ui->listView->setCurrentIndex(mapped_index);
                        modelDataChange();
                    } else {
                        QDir(dstPath).removeRecursively();
                        QMessageBox::critical(this, tr("Clone"), tr("Failed to clone VM."), QMessageBox::Ok);
                        return;
                    }
                }
            });

            QAction killIcon(tr("&Kill"));
            contextMenu.addAction(&killIcon);
            connect(&killIcon, &QAction::triggered, [this, parent] {
                QMessageBox msgbox(QMessageBox::Warning, tr("Warning"), tr("Killing a virtual machine can cause data loss. Only do this if the 86Box process gets stuck.\n\nDo you really wish to kill the virtual machine \"%1\"?").arg(selected_sysconfig->displayName), QMessageBox::StandardButton::Yes | QMessageBox::StandardButton::No, parent);
                msgbox.exec();
                if (msgbox.result() == QMessageBox::Yes) {
                    disconnect(selected_sysconfig->process, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished), nullptr, nullptr);
                    selected_sysconfig->process->kill();
                }
            });
            killIcon.setEnabled(selected_sysconfig->process->state() == QProcess::Running);

            QAction clrNvram(tr("&Wipe NVRAM"));
            contextMenu.addAction(&clrNvram);
            connect(&clrNvram, &QAction::triggered, [this, parent] {
                QMessageBox msgbox(QMessageBox::Warning, tr("Warning"), tr("This will delete all NVRAM (and related) files of the virtual machine located in the \"nvr\" subdirectory. You'll have to reconfigure the BIOS (and possibly other devices inside the VM) settings again if applicable.\n\nAre you sure you want to wipe all NVRAM contents of the virtual machine \"%1\"?").arg(selected_sysconfig->displayName), QMessageBox::StandardButton::Yes | QMessageBox::StandardButton::No, parent);
                msgbox.exec();
                if (msgbox.result() == QMessageBox::Yes) {
                    if (QDir(selected_sysconfig->config_dir + "/nvr/").removeRecursively())
                        QMessageBox::information(this, tr("Success"), tr("Successfully wiped the NVRAM contents of the virtual machine \"%1\"").arg(selected_sysconfig->displayName));
                    else {
                        QMessageBox::critical(this, tr("Error"), tr("An error occurred trying to wipe the NVRAM contents of the virtual machine \"%1\"").arg(selected_sysconfig->displayName));
                    }
                }
            });
            clrNvram.setEnabled(selected_sysconfig->process->state() == QProcess::NotRunning);

            QAction deleteAction(tr("&Delete"));
            contextMenu.addAction(&deleteAction);
            connect(&deleteAction, &QAction::triggered, [this] {
                deleteSystem(selected_sysconfig);
            });
            deleteAction.setEnabled(selected_sysconfig->process->state() == QProcess::NotRunning);

            contextMenu.addSeparator();

            QAction openSystemFolderAction(tr("&Open folder…"));
            contextMenu.addAction(&openSystemFolderAction);
            connect(&openSystemFolderAction, &QAction::triggered, [indexAt] {
                if (const auto configDir = indexAt.data(VMManagerModel::Roles::ConfigDir).toString(); !configDir.isEmpty()) {
                    QDir dir(configDir);
                    if (!dir.exists())
                        dir.mkpath(".");

                    QDesktopServices::openUrl(QUrl(QString("file:///") + dir.canonicalPath()));
                }
            });

            QAction openPrinterFolderAction(tr("Open p&rinter tray…"));
            contextMenu.addAction(&openPrinterFolderAction);
            connect(&openPrinterFolderAction, &QAction::triggered, [indexAt] {
                if (const auto printerDir = indexAt.data(VMManagerModel::Roles::ConfigDir).toString() + QString("/printer/"); !printerDir.isEmpty()) {
                    QDir dir(printerDir);
                    if (!dir.exists())
                        dir.mkpath(".");

                    QDesktopServices::openUrl(QUrl(QString("file:///") + dir.canonicalPath()));
                }
            });

            QAction openScreenshotsFolderAction(tr("Open screenshots &folder…"));
            contextMenu.addAction(&openScreenshotsFolderAction);
            connect(&openScreenshotsFolderAction, &QAction::triggered, [indexAt] {
                if (const auto screenshotsDir = indexAt.data(VMManagerModel::Roles::ConfigDir).toString() + QString("/screenshots/"); !screenshotsDir.isEmpty()) {
                    QDir dir(screenshotsDir);
                    if (!dir.exists())
                        dir.mkpath(".");

                    QDesktopServices::openUrl(QUrl(QString("file:///") + dir.canonicalPath()));
                }
            });

            QAction showRawConfigFile(tr("Show &config file"));
            contextMenu.addAction(&showRawConfigFile);
            connect(&showRawConfigFile, &QAction::triggered, [this, indexAt] {
                if (const auto configFile = indexAt.data(VMManagerModel::Roles::ConfigFile).toString(); !configFile.isEmpty()) {
                    showTextFileContents(indexAt.data(Qt::DisplayRole).toString(), configFile);
                }
            });

            contextMenu.exec(ui->listView->viewport()->mapToGlobal(pos));
        } else {
            QMenu contextMenu("", ui->listView);

            QAction newMachineAction(tr("&New machine…"));
            contextMenu.addAction(&newMachineAction);
            connect(&newMachineAction, &QAction::triggered, this, &VMManagerMain::newMachineWizard);

            contextMenu.exec(ui->listView->viewport()->mapToGlobal(pos));
        }
    });

    connect(vm_model, &VMManagerModel::globalConfigurationChanged, this, []() {
        vmm_main_window->updateSettings();
    });

    // Initial default details view
    vm_details = new VMManagerDetails(ui->detailsArea);
    ui->detailsArea->layout()->addWidget(vm_details);
    const QItemSelectionModel *selection_model = ui->listView->selectionModel();

    connect(selection_model, &QItemSelectionModel::currentChanged, this, &VMManagerMain::currentSelectionChanged);
    // If there are items in the model, make sure to select the first item by default.
    // When settings are loaded, the last selected item will be selected (if available)
    if (proxy_model->rowCount(QModelIndex()) > 0) {
        const QModelIndex first_index = proxy_model->index(0, 0);
        ui->listView->setCurrentIndex(first_index);
    }

    // Connect double-click to start VM
    connect(ui->listView, &QListView::doubleClicked, this, &VMManagerMain::startButtonPressed);

    // Connect Enter key to start VM
    auto enterShortcut = new QShortcut(QKeySequence(Qt::Key_Return), ui->listView);
    connect(enterShortcut, &QShortcut::activated, this, &VMManagerMain::startButtonPressed);

    // Load and apply settings
    loadSettings();
    ui->splitter->setSizes({ ui->detailsArea->width(), (ui->listView->minimumWidth() * 2) });

    // Set up search bar
    connect(ui->searchBar, &QLineEdit::textChanged, this, &VMManagerMain::searchSystems);
    // Create the completer
    auto *completer = new QCompleter(this);
    completer->setCaseSensitivity(Qt::CaseInsensitive);
    completer->setFilterMode(Qt::MatchContains);
    // Get the completer list
    const auto allStrings = getSearchCompletionList();
    // Set up the completer
    auto *completerModel = new QStringListModel(allStrings, completer);
    completer->setModel(completerModel);
    ui->searchBar->setCompleter(completer);

    QTimer::singleShot(0, this, [this] {
        // Set initial status bar after the event loop starts
        emit updateStatusRight(machineCountString());
        // Tell the mainwindow to enable the toolbar buttons if needed
        emit selectionOrStateChanged((this->proxy_model->rowCount(QModelIndex()) > 0) ? selected_sysconfig : nullptr);
    });

#if EMU_BUILD_NUM != 0
    // Start update check after a slight delay
    QTimer::singleShot(1000, this, [this] {
        if (updateCheck)
            backgroundUpdateCheckStart();
    });
#endif
}

VMManagerMain::~VMManagerMain()
{
    delete ui;
    delete vm_model;
}

void
VMManagerMain::reload()
{
    // Disconnect and save the old selection mdoel to be deleted later
    QItemSelectionModel *old_selection_model = ui->listView->selectionModel();
    disconnect(old_selection_model, &QItemSelectionModel::currentChanged, this, &VMManagerMain::currentSelectionChanged);
    // Disconnect and delete the model and proxy model 
    disconnect(vm_model, &VMManagerModel::systemDataChanged, this, &VMManagerMain::modelDataChange);
    disconnect(vm_model, &VMManagerModel::globalConfigurationChanged, this, nullptr);
    delete proxy_model;
    delete vm_model;

    // Reset the details view and toolbar to initial state
    selected_sysconfig = new VMManagerSystem();
    vm_details->reset();
    emit selectionOrStateChanged(nullptr);

    // Create the new model and proxy model
    vm_model = new VMManagerModel;
    proxy_model = new StringListProxyModel(this);
    proxy_model->setSourceModel(vm_model);
    ui->listView->setModel(proxy_model);
    // Delete the old selection model
    delete old_selection_model;

    // Set up the new models
    proxy_model->setSortCaseSensitivity(Qt::CaseInsensitive);
    ui->listView->model()->sort(0, Qt::AscendingOrder);
    connect(vm_model, &VMManagerModel::systemDataChanged, this, &VMManagerMain::modelDataChange);
    connect(vm_model, &VMManagerModel::globalConfigurationChanged, this, []() {
        vmm_main_window->updateSettings();
    });
    const QItemSelectionModel *selection_model = ui->listView->selectionModel();
    connect(selection_model, &QItemSelectionModel::currentChanged, this, &VMManagerMain::currentSelectionChanged);

    // Update the search completer
    auto *completerModel = new QStringListModel(getSearchCompletionList(), ui->searchBar->completer());
    ui->searchBar->completer()->setModel(completerModel);

    // If machines are found, set the selection to the first one
    if (proxy_model->rowCount(QModelIndex()) > 0) {
        const QModelIndex first_index = proxy_model->index(0, 0);
        ui->listView->setCurrentIndex(first_index);
        emit selectionOrStateChanged(selected_sysconfig);
    }

    // Notify the status bar
    emit updateStatusRight(machineCountString());
}

void
VMManagerMain::updateGlobalSettings()
{
    vmm_main_window->updateSettings();
}

void
VMManagerMain::currentSelectionChanged(const QModelIndex &current,
                                       const QModelIndex &previous)
{
    if (!current.isValid())
        return;

    disconnect(selected_sysconfig->process, &QProcess::stateChanged, this, &VMManagerMain::vmStateChange);
    disconnect(selected_sysconfig, &VMManagerSystem::windowStatusChanged, this, &VMManagerMain::vmStateChange);
    disconnect(selected_sysconfig, &VMManagerSystem::clientProcessStatusChanged, this, &VMManagerMain::vmStateChange);

    const auto mapped_index = proxy_model->mapToSource(current);
    selected_sysconfig      = vm_model->getConfigObjectForIndex(mapped_index);
    vm_details->updateData(selected_sysconfig);

    // Emit that the selection changed, include with the process state
    emit selectionOrStateChanged(selected_sysconfig);

    connect(selected_sysconfig->process, &QProcess::stateChanged, this, &VMManagerMain::vmStateChange);
    connect(selected_sysconfig, &VMManagerSystem::windowStatusChanged, this, &VMManagerMain::vmStateChange);
    connect(selected_sysconfig, &VMManagerSystem::clientProcessStatusChanged, this, &VMManagerMain::vmStateChange);

}

void
VMManagerMain::settingsButtonPressed()
{
    if (!currentSelectionIsValid())
        return;

    selected_sysconfig->launchSettings();
}

void
VMManagerMain::startButtonPressed() const
{
    if (!currentSelectionIsValid())
        return;

    selected_sysconfig->startButtonPressed();
}

void
VMManagerMain::restartButtonPressed() const
{
    if (!currentSelectionIsValid())
        return;

    selected_sysconfig->restartButtonPressed();
}

void
VMManagerMain::pauseButtonPressed() const
{
    if (!currentSelectionIsValid())
        return;

    selected_sysconfig->pauseButtonPressed();
}

void
VMManagerMain::shutdownRequestButtonPressed() const
{
    if (!currentSelectionIsValid())
        return;

    selected_sysconfig->shutdownRequestButtonPressed();
}

void
VMManagerMain::shutdownForceButtonPressed() const
{
    if (!currentSelectionIsValid())
        return;

    selected_sysconfig->shutdownForceButtonPressed();
}

void
VMManagerMain::cadButtonPressed() const
{
    if (!currentSelectionIsValid())
        return;

    selected_sysconfig->cadButtonPressed();
}

void
VMManagerMain::updateDisplayName(const QModelIndex &index)
{
    QDialog dialog;
    dialog.setMinimumWidth(400);
    dialog.setWindowTitle(tr("Set display name"));
    const auto layout = new QVBoxLayout(&dialog);
    const auto label  = new QLabel(tr("Enter the new display name (blank to reset)"));
    label->setAlignment(Qt::AlignHCenter);
    label->setContentsMargins(QMargins(0, 0, 0, 5));
    layout->addWidget(label);
    const auto lineEdit = new QLineEdit(index.data().toString(), &dialog);
    layout->addWidget(lineEdit);
    lineEdit->selectAll();

    const auto buttonBox = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
    layout->addWidget(buttonBox);

    connect(buttonBox, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    connect(buttonBox, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);

    if (const bool accepted = dialog.exec() == QDialog::Accepted; accepted) {
        const auto mapped_index = proxy_model->mapToSource(index);
        vm_model->updateDisplayName(mapped_index, lineEdit->text());
        selected_sysconfig = vm_model->getConfigObjectForIndex(mapped_index);
        vm_details->updateData(selected_sysconfig);
        ui->listView->scrollTo(ui->listView->currentIndex(), QAbstractItemView::PositionAtCenter);
    }
}

void
VMManagerMain::loadSettings()
{
    const auto config        = new VMManagerConfig(VMManagerConfig::ConfigType::General);
    const auto lastSelection = config->getStringValue("last_selection");
#if EMU_BUILD_NUM != 0
    updateCheck = config->getStringValue("update_check").toInt();
#endif
    regexSearch = config->getStringValue("regex_search").toInt();

    const auto matches = ui->listView->model()->match(vm_model->index(0, 0), VMManagerModel::Roles::ConfigName, QVariant::fromValue(lastSelection));
    if (!matches.empty()) {
        ui->listView->setCurrentIndex(matches.first());
        ui->listView->scrollTo(ui->listView->currentIndex(), QAbstractItemView::PositionAtCenter);
    }
}
bool
VMManagerMain::currentSelectionIsValid() const
{
    return ui->listView->currentIndex().isValid() && selected_sysconfig->isValid();
}

// Used from MainWindow during app exit to obtain and persist the current selection
QString
VMManagerMain::getCurrentSelection() const
{
    return ui->listView->currentIndex().data(VMManagerModel::Roles::ConfigName).toString();
}

void
VMManagerMain::searchSystems(const QString &text) const
{
    // Escape the search text string unless regular expression searching is enabled.
    // When escaped, the search string functions as a plain text match.
    const auto               searchText = regexSearch ? text : QRegularExpression::escape(text);
    const QRegularExpression regex(searchText, QRegularExpression::CaseInsensitiveOption);
    if (!regex.isValid()) {
        qDebug() << "Skipping, invalid regex";
        return;
    }
    proxy_model->setFilterRegularExpression(regex);
    // Searching (filtering) can cause the list view to change. If there is still a valid selection,
    // make sure to scroll to it
    if (ui->listView->currentIndex().isValid()) {
        ui->listView->scrollTo(ui->listView->currentIndex(), QAbstractItemView::PositionAtCenter);
    }
}

void
VMManagerMain::newMachineWizard()
{
    const auto wizard = new VMManagerAddMachine(this);
    if (wizard->exec() == QDialog::Accepted) {
        const auto newName = wizard->field("systemName").toString();
#ifdef CUSTOM_SYSTEM_LOCATION
        const auto systemDir = wizard->field("systemLocation").toString();
#else
        const auto systemDir = QDir(vmm_path).path();
#endif
        const auto existingConfiguration = wizard->field("existingConfiguration").toString();
        const auto displayName           = wizard->field("displayName").toString();
        addNewSystem(newName, systemDir, displayName, existingConfiguration);
    }
}

void
VMManagerMain::addNewSystem(const QString &name, const QString &dir, const QString &displayName, const QString &configFile)
{
    const auto newSystemDirectory = QDir(QDir::cleanPath(dir + "/" + name));

    // qt replaces `/` with native separators
    const auto newSystemConfigFile = QFileInfo(newSystemDirectory.path() + "/" + CONFIG_FILE);
    if (newSystemConfigFile.exists() || newSystemDirectory.exists()) {
        QMessageBox::critical(this, tr("Directory in use"), tr("The selected directory is already in use. Please select a different directory."));
        return;
    }
    // Create the directory
    const QDir qmkdir;
    if (const bool mkdirResult = qmkdir.mkdir(newSystemDirectory.path()); !mkdirResult) {
        QMessageBox::critical(this, tr("Create directory failed"), tr("Unable to create the directory for the new system"));
        return;
    }
    // If specified, write the contents of the configuration file before starting
    if (!configFile.isEmpty()) {
        const auto configPath = newSystemConfigFile.absoluteFilePath();
        const auto file       = new QFile(configPath);
        if (!file->open(QIODevice::WriteOnly)) {
            qWarning() << "Unable to open file " << configPath;
            QMessageBox::critical(this, tr("Configuration write failed"), tr("Unable to open the configuration file at %1 for writing").arg(configPath));
            return;
        }
        file->write(configFile.toUtf8());
        file->flush();
        file->close();
    }

    const auto new_system = new VMManagerSystem(newSystemConfigFile.absoluteFilePath());
    new_system->launchSettings();
    // Handle this in a closure so we can capture the temporary new_system object
    disconnect(new_system->process, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished), nullptr, nullptr);
    connect(new_system->process, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished),
            [=](const int exitCode, const QProcess::ExitStatus exitStatus) {
                bool fail = false;
                if (exitCode != 0 || exitStatus != QProcess::NormalExit) {
                    qInfo().nospace().noquote() << "Abnormal program termination while creating new system: exit code " << exitCode << ", exit status " << exitStatus;
                    qInfo() << "Not adding system due to errors";
                    QString errMsg = tr("The virtual machine \"%1\"'s process has unexpectedly terminated with exit code %2.").arg((!displayName.isEmpty() ? displayName : name), QString::number(exitCode));
                    QMessageBox::critical(this, tr("Error adding system"),
                                          QString("%1\n\n%2").arg(errMsg, tr("The system will not be added.")));
                    fail = true;
                }
                // Create a new QFileInfo because the info from the old one may be cached
                if (const auto fi = QFileInfo(new_system->config_file.absoluteFilePath()); !fi.exists()) {
                    // No config file which means the cancel button was pressed in the settings dialog
                    // Attempt to clean up the directory that was created
                    const QDir qrmdir;
                    if (const bool result = qrmdir.rmdir(newSystemDirectory.path()); !result) {
                        qWarning() << "Error cleaning up the old directory for canceled operation. Continuing anyway.";
                    }
                    fail = true;
                }
                if (fail) {
                    delete new_system;
                    return;
                }
                const auto current_index = ui->listView->currentIndex();
                vm_model->reload(this);
                const auto created_object = vm_model->getIndexForConfigFile(new_system->config_file);
                if (created_object.row() < 0) {
                    // For some reason the index of the new object couldn't be determined. Fall back to the old index.
                    ui->listView->setCurrentIndex(current_index);
                    delete new_system;
                    return;
                }
                auto added_system = vm_model->getConfigObjectForIndex(created_object);
                added_system->setDisplayName(displayName);
                // Get the index of the newly-created system and select it
                const QModelIndex mapped_index = proxy_model->mapFromSource(created_object);
                ui->listView->setCurrentIndex(mapped_index);
                delete new_system;
                modelDataChange();
            });
}

void
VMManagerMain::deleteSystem(VMManagerSystem *sysconfig)
{
    QMessageBox msgbox(QMessageBox::Icon::Warning, tr("Warning"), tr("Do you really want to delete the virtual machine \"%1\" and all its files? This action cannot be undone!").arg(sysconfig->displayName), QMessageBox::StandardButton::Yes | QMessageBox::StandardButton::No, qobject_cast<QWidget *>(this->parent()));
    msgbox.exec();
    if (msgbox.result() == QMessageBox::Yes) {
        auto qrmdir = new QDir(sysconfig->config_dir);
        if (const bool rmdirResult = qrmdir->removeRecursively(); !rmdirResult) {
            QMessageBox::critical(this, tr("Remove directory failed"), tr("Some files in the machine's directory were unable to be deleted. Please delete them manually."));
            return;
        }
        auto config = new VMManagerConfig(VMManagerConfig::ConfigType::General);
        config->remove(sysconfig->uuid);
        vm_model->removeConfigFromModel(sysconfig);
        delete sysconfig;

        if (vm_model->rowCount(QModelIndex()) <= 0) {
            /* no machines left - get rid of the last machine's leftovers */
            selected_sysconfig = new VMManagerSystem();
            vm_details->reset();
            /* tell the mainwindow to disable the toolbar buttons */
            emit selectionOrStateChanged(nullptr);
        }
    }
}

QStringList
VMManagerMain::getSearchCompletionList() const
{
    QSet<QString> uniqueStrings;
    for (int row = 0; row < vm_model->rowCount(QModelIndex()); ++row) {
        QModelIndex index    = vm_model->index(row, 0);
        auto        fullList = vm_model->data(index, VMManagerModel::Roles::SearchList).toStringList();
        QSet        uniqueSet(fullList.begin(), fullList.end());
        uniqueStrings.unite(uniqueSet);
    }
    // Convert the set back to a QStringList
    QStringList allStrings = uniqueStrings.values();
    return allStrings;
}

QString
VMManagerMain::machineCountString(QString states) const
{
    const auto count = vm_model->rowCount(QModelIndex());
    if (!states.isEmpty())
        states.append(", ");
    states.append(tr("%1 total").arg(count));

    return tr("VMs: %1").arg(states);
}

QList<int>
VMManagerMain::getPaneSizes() const
{
    return ui->splitter->sizes();
}

void
VMManagerMain::setPaneSizes(const QList<int> &sizes)
{
    ui->splitter->setSizes(sizes);
}

void
VMManagerMain::modelDataChange()
{
    // Model data has changed. This includes process status.
    // Update the counts / totals accordingly
    auto        modelStats = vm_model->getProcessStats();
    QStringList stats;
    for (auto it = modelStats.constBegin(); it != modelStats.constEnd(); ++it) {
        const auto &key  = it.key();
        QString     text = "";
        switch (key) {
            case VMManagerSystem::ProcessStatus::Running:
                text = tr("%n running", "", modelStats[key]);
                break;
            case VMManagerSystem::ProcessStatus::Paused:
                text = tr("%n paused", "", modelStats[key]);
                break;
            case VMManagerSystem::ProcessStatus::PausedWaiting:
            case VMManagerSystem::ProcessStatus::RunningWaiting:
                text = tr("%n waiting", "", modelStats[key]);
                break;
            default:
                break;
        }
        if (!text.isEmpty())
            stats.append(text);
    }
    auto states = stats.join(", ");
    emit updateStatusRight(machineCountString(states));
}

void
VMManagerMain::vmStateChange()
{
    if (!currentSelectionIsValid())
        return;

    emit selectionOrStateChanged(selected_sysconfig);
}

void
VMManagerMain::onPreferencesUpdated()
{
    // Only reload values that we care about
    const auto config         = new VMManagerConfig(VMManagerConfig::ConfigType::General);
    const auto oldRegexSearch = regexSearch;
    regexSearch               = config->getStringValue("regex_search").toInt();
    if (oldRegexSearch != regexSearch)
        ui->searchBar->clear();

    if (vm_model)
        vm_model->sendGlobalConfigurationChanged();
}

void
VMManagerMain::onLanguageUpdated()
{
    vm_model->refreshConfigs();
    modelDataChange();
    ui->searchBar->setPlaceholderText(tr("Search"));
    /* Hack to work around details widgets not being re-translatable
       without going through layers of abstraction */
    ui->detailsArea->layout()->removeWidget(vm_details);
    delete vm_details;
    vm_details = new VMManagerDetails();
    ui->detailsArea->layout()->addWidget(vm_details);
    if (vm_model->rowCount(QModelIndex()) > 0)
        vm_details->updateData(selected_sysconfig);
}

#ifdef Q_OS_WINDOWS
void
VMManagerMain::onDarkModeUpdated()
{
    vm_details->updateStyle();
}
#endif

int
VMManagerMain::getActiveMachineCount()
{
    return vm_model->getActiveMachineCount();
}

#if EMU_BUILD_NUM != 0
void
VMManagerMain::backgroundUpdateCheckStart() const
{
    auto updateChannel = UpdateCheck::UpdateChannel::CI;
#    ifdef RELEASE_BUILD
    updateChannel = UpdateCheck::UpdateChannel::Stable;
#    endif
    const auto updateCheck = new UpdateCheck(updateChannel);
    connect(updateCheck, &UpdateCheck::updateCheckComplete, this, &VMManagerMain::backgroundUpdateCheckComplete);
    connect(updateCheck, &UpdateCheck::updateCheckError, this, &VMManagerMain::backgroundUpdateCheckError);
    updateCheck->checkForUpdates();
}

void
VMManagerMain::backgroundUpdateCheckComplete(const UpdateCheck::UpdateResult &result)
{
    qDebug() << "Check complete: update available?" << result.updateAvailable;
    if (result.updateAvailable) {
        auto       type          = result.channel == UpdateCheck::UpdateChannel::CI ? tr("build") : tr("version");
        const auto updateMessage = tr("An update to 86Box is available: %1 %2").arg(type, result.latestVersion);
        emit       updateStatusLeft(updateMessage);
    }
}

void
VMManagerMain::backgroundUpdateCheckError(const QString &errorMsg)
{
    qDebug() << "Update check failed with the following error:" << errorMsg;
    emit updateStatusLeft(tr("An error has occurred while checking for updates: %1").arg(errorMsg));
}
#endif

void
VMManagerMain::showTextFileContents(const QString &title, const QString &path)
{
    // Make sure we can open the file
    const auto fi = QFileInfo(path);
    if (!fi.exists()) {
        qWarning("Requested file does not exist: %s", path.toUtf8().constData());
        return;
    }
    // Read the file
    QFile displayFile(path);
    if (!displayFile.open(QIODevice::ReadOnly | QIODevice::Text)) {
        qWarning("Couldn't open the file: error %d", displayFile.error());
        return;
    }
    const QString configFileContents = displayFile.readAll();
    displayFile.close();

    const auto textDisplayDialog = new QDialog(this);
    textDisplayDialog->setMinimumSize(QSize(540, 360));
    textDisplayDialog->setWindowTitle(QString("%1 - %2").arg(title, fi.fileName()));

    const auto textEdit      = new QPlainTextEdit();
    const auto monospaceFont = new QFont();
#ifdef Q_OS_WINDOWS
    monospaceFont->setFamily("Consolas");
#elif defined(Q_OS_MACOS)
    monospaceFont->setFamily("Menlo");
#else
    monospaceFont->setFamily("Monospace");
#endif
    monospaceFont->setStyleHint(QFont::Monospace);
    monospaceFont->setFixedPitch(true);
    textEdit->setFont(*monospaceFont);
    textEdit->setReadOnly(true);
    textEdit->setPlainText(configFileContents);
    const auto buttonBox = new QDialogButtonBox(QDialogButtonBox::Ok);
    connect(buttonBox, &QDialogButtonBox::accepted, textDisplayDialog, &QDialog::accept);
    const auto layout = new QVBoxLayout();
    textDisplayDialog->setLayout(layout);
    textDisplayDialog->layout()->addWidget(textEdit);
    textDisplayDialog->layout()->addWidget(buttonBox);
    textDisplayDialog->exec();
}
