/*
* 86Box	A hypervisor and IBM PC system emulator that specializes in
*		running old operating systems and software designed for IBM
*		PC systems and compatibles from 1981 through fairly recent
*		system designs based on the PCI bus.
*
*		This file is part of the 86Box distribution.
*
*		86Box VM manager main module
*
*
*
* Authors:	cold-brewed
*
*		Copyright 2024 cold-brewed
*/

#include <QAbstractListModel>
#include <QCompleter>
#include <QDebug>
#include <QDesktopServices>
#include <QMenu>
#include <QMessageBox>
#include <QStringListModel>
#include <QTimer>

#include "qt_vmmanager_main.hpp"
#include "ui_qt_vmmanager_main.h"
#include "qt_vmmanager_model.hpp"
#include "qt_vmmanager_addmachine.hpp"

VMManagerMain::VMManagerMain(QWidget *parent) :
    QWidget(parent), ui(new Ui::VMManagerMain), selected_sysconfig(new VMManagerSystem) {
    ui->setupUi(this);
    this->setWindowTitle("86Box VM Manager");

    // Set up the main listView
    ui->listView->setItemDelegate(new VMManagerListViewDelegate);
    vm_model = new VMManagerModel;
    proxy_model = new StringListProxyModel(this);
    proxy_model->setSourceModel(vm_model);
    ui->listView->setModel(proxy_model);
    proxy_model->setSortCaseSensitivity(Qt::CaseInsensitive);
    ui->listView->model()->sort(0, Qt::AscendingOrder);

    // Connect the model signal
    connect(vm_model, &VMManagerModel::systemDataChanged, this, &VMManagerMain::modelDataChange);

    // Set up the context menu for the list view
    ui->listView->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(ui->listView, &QListView::customContextMenuRequested, [this](const QPoint &pos) {
        const auto indexAt = ui->listView->indexAt(pos);
        if (indexAt.isValid()) {
            QMenu contextMenu(tr("Context Menu"), ui->listView);

            QAction nameChangeAction(tr("Change display name"));
            contextMenu.addAction(&nameChangeAction);
            // Use a lambda to call a function so indexAt can be passed
            connect(&nameChangeAction, &QAction::triggered, ui->listView, [this, indexAt] {
                   updateDisplayName(indexAt);
            });

            QAction openSystemFolderAction(tr("Open folder"));
            contextMenu.addAction(&openSystemFolderAction);
            connect(&openSystemFolderAction, &QAction::triggered, [this, indexAt] {
                if (const auto configDir = indexAt.data(VMManagerModel::Roles::ConfigDir).toString(); !configDir.isEmpty()) {
                    QDir dir(configDir);
                    if (!dir.exists())
                        dir.mkpath(".");
                    
                    QDesktopServices::openUrl(QUrl(QString("file:///") + dir.canonicalPath()));
                }
            });

            QAction convertToP3(tr("Convert system to PIII"));
            contextMenu.addAction(&convertToP3);
            convertToP3.setEnabled(false);

            QAction setSystemIcon(tr("Set icon"));
            contextMenu.addAction(&setSystemIcon);
            connect(&setSystemIcon, &QAction::triggered, [this, indexAt] {
                IconSelectionDialog dialog(":/systemicons/");
                if(dialog.exec() == QDialog::Accepted) {
                    const QString iconName = dialog.getSelectedIconName();
                    // A Blank iconName will cause setIcon to reset to the default
                    selected_sysconfig->setIcon(iconName);
                }
            });

            contextMenu.addSeparator();

            QAction showRawConfigFile(tr("Show config file"));
            contextMenu.addAction(&showRawConfigFile);
            connect(&showRawConfigFile, &QAction::triggered, [this, indexAt] {
                if (const auto configFile = indexAt.data(VMManagerModel::Roles::ConfigFile).toString(); !configFile.isEmpty()) {
                    showTextFileContents(indexAt.data(Qt::DisplayRole).toString(), configFile);
                }
            });

            contextMenu.exec(ui->listView->viewport()->mapToGlobal(pos));
        }
    });

    // Initial default details view
    vm_details = new VMManagerDetails();
    ui->detailsArea->layout()->addWidget(vm_details);
    const QItemSelectionModel *selection_model = ui->listView->selectionModel();

    connect(selection_model, &QItemSelectionModel::currentChanged, this, &VMManagerMain::currentSelectionChanged);
    // If there are items in the model, make sure to select the first item by default.
    // When settings are loaded, the last selected item will be selected (if available)
    if (proxy_model->rowCount(QModelIndex()) > 0) {
        const QModelIndex first_index = proxy_model->index(0, 0);
        ui->listView->setCurrentIndex(first_index);
    }

    // Load and apply settings
    loadSettings();

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

    // Set initial status bar after the event loop starts
    QTimer::singleShot(0, this, [this] {
        emit updateStatusRight(totalCountString());
    });

    // Start update check after a slight delay
    QTimer::singleShot(1000, this, [this] {
            if(updateCheck) {
                backgroundUpdateCheckStart();
            }
    });
}

VMManagerMain::~VMManagerMain() {
    delete ui;
    delete vm_model;
}

void
VMManagerMain::currentSelectionChanged(const QModelIndex &current,
                             const QModelIndex &previous)
{
    if(!current.isValid()) {
        return;
    }

    const auto mapped_index = proxy_model->mapToSource(current);
    selected_sysconfig = vm_model->getConfigObjectForIndex(mapped_index);
    vm_details->updateData(selected_sysconfig);

    // Emit that the selection changed, include with the process state
    emit selectionChanged(current, selected_sysconfig->process->state());

}

void
VMManagerMain::settingsButtonPressed() {
    if(!currentSelectionIsValid()) {
        return;
    }
    selected_sysconfig->launchSettings();
    // If the process is already running, the system will be instructed to open its settings window.
    // Otherwise the process will be launched and will need to be tracked here.
    if (!selected_sysconfig->isProcessRunning()) {
        connect(selected_sysconfig->process, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished),
            [=](const int exitCode, const QProcess::ExitStatus exitStatus){
                if (exitCode != 0 || exitStatus != QProcess::NormalExit) {
                    qInfo().nospace().noquote() << "Abnormal program termination while launching settings: exit code " <<  exitCode << ", exit status " << exitStatus;
                    return;
                }
                selected_sysconfig->reloadConfig();
                vm_details->updateData(selected_sysconfig);
            });
    }
}

void
VMManagerMain::startButtonPressed() const
{
    if(!currentSelectionIsValid()) {
        return;
    }
    selected_sysconfig->startButtonPressed();
}

void
VMManagerMain::restartButtonPressed() const
{
    if(!currentSelectionIsValid()) {
        return;
    }
    selected_sysconfig->restartButtonPressed();

}

void
VMManagerMain::pauseButtonPressed() const
{
    if(!currentSelectionIsValid()) {
        return;
    }
    selected_sysconfig->pauseButtonPressed();
}

void
VMManagerMain::shutdownRequestButtonPressed() const
{
    if (!currentSelectionIsValid()) {
        return;
    }
    selected_sysconfig->shutdownRequestButtonPressed();
}

void
VMManagerMain::shutdownForceButtonPressed() const
{
    if (!currentSelectionIsValid()) {
        return;
    }
    selected_sysconfig->shutdownForceButtonPressed();
}

// This function doesn't appear to be needed any longer
void
VMManagerMain::refresh()
{
    bool running = selected_sysconfig->process->state() == QProcess::ProcessState::Running;
    const auto current_index = ui->listView->currentIndex();
    emit selectionChanged(current_index, selected_sysconfig->process->state());

    // if(!selected_sysconfig->config_file.path().isEmpty()) {
    if(!selected_sysconfig->isValid()) {
        // what was happening here?
    }
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
    updateCheck = config->getStringValue("update_check").toInt();
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
    const auto searchText = regexSearch ? text : QRegularExpression::escape(text);
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
        const auto newName        = wizard->field("systemName").toString();
        const auto systemDir      = wizard->field("systemLocation").toString();
        const auto existingConfiguration = wizard->field("existingConfiguration").toString();
        addNewSystem(newName, systemDir, existingConfiguration);
    }
}

void
VMManagerMain::addNewSystem(const QString &name, const QString &dir, const QString &configFile)
{
    const auto newSytemDirectory = QDir(QDir::cleanPath(dir + "/" + name));

    // qt replaces `/` with native separators
    const auto newSystemConfigFile = QFileInfo(newSytemDirectory.path() + "/" + "86box.cfg");
    if (newSystemConfigFile.exists() || newSytemDirectory.exists()) {
        QMessageBox::critical(this, tr("Directory in use"), tr("The selected directory is already in use. Please select a different directory."));
        return;
    }
    // Create the directory
    const QDir qmkdir;
    if (const bool mkdirResult = qmkdir.mkdir(newSytemDirectory.path()); !mkdirResult) {
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
    connect(new_system->process, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished),
            [=](const int exitCode, const QProcess::ExitStatus exitStatus) {
                if (exitCode != 0 || exitStatus != QProcess::NormalExit) {
                    qInfo().nospace().noquote() << "Abnormal program termination while creating new system: exit code " << exitCode << ", exit status " << exitStatus;
                    qInfo() << "Not adding system due to errors";
                    QMessageBox::critical(this, tr("Error adding system"),
                                          tr("Abnormal program termination while creating new system: exit code %1, exit status %2.\n\nThe system will not be added.").arg(QString::number(exitCode), exitStatus));
                    delete new_system;
                    return;
                }
                // Create a new QFileInfo because the info from the old one may be cached
                if (const auto fi = QFileInfo(new_system->config_file.absoluteFilePath()); !fi.exists()) {
                    // No config file which means the cancel button was pressed in the settings dialog
                    // Attempt to clean up the directory that was created
                    const QDir qrmdir;
                    if (const bool result = qrmdir.rmdir(newSytemDirectory.path()); !result) {
                        qWarning() << "Error cleaning up the old directory for canceled operation. Continuing anyway.";
                    }
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
                // Get the index of the newly-created system and select it
                const QModelIndex mapped_index = proxy_model->mapFromSource(created_object);
                ui->listView->setCurrentIndex(mapped_index);
                delete new_system;
            });
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
VMManagerMain::totalCountString() const
{
    const auto count = vm_model->rowCount(QModelIndex());
    return QString("%1 %2").arg(QString::number(count), tr("total"));
}

void
VMManagerMain::modelDataChange()
{
    // Model data has changed. This includes process status.
    // Update the counts / totals accordingly
    auto        modelStats = vm_model->getProcessStats();
    QStringList stats;
    for (auto it = modelStats.constBegin(); it != modelStats.constEnd(); ++it) {
        const auto &key = it.key();
        stats.append(QString("%1 %2").arg(QString::number(modelStats[key]), key));
    }
    auto states = stats.join(", ");
    if (!modelStats.isEmpty()) {
        states.append(", ");
    }

    emit updateStatusRight(states + totalCountString());
}

void
VMManagerMain::onPreferencesUpdated()
{
    // Only reload values that we care about
    const auto config        = new VMManagerConfig(VMManagerConfig::ConfigType::General);
    const auto oldRegexSearch = regexSearch;
    regexSearch = config->getStringValue("regex_search").toInt();
    if (oldRegexSearch != regexSearch) {
        ui->searchBar->clear();
    }
}

void
VMManagerMain::backgroundUpdateCheckStart() const
{
    auto updateChannel = UpdateCheck::UpdateChannel::CI;
#ifdef RELEASE_BUILD
    updateChannel = UpdateCheck::UpdateChannel::Stable;
#endif
    const auto updateCheck = new UpdateCheck(updateChannel);
    connect(updateCheck, &UpdateCheck::updateCheckComplete, this, &VMManagerMain::backgroundUpdateCheckComplete);
    connect(updateCheck, &UpdateCheck::updateCheckError, this, &VMManagerMain::backgroundUpdateCheckError);
    updateCheck->checkForUpdates();
}

void
VMManagerMain::backgroundUpdateCheckComplete(const UpdateCheck::UpdateResult &result)
{
    qDebug() << "Check complete: update available?" << result.updateAvailable;
    auto type = result.channel == UpdateCheck::UpdateChannel::CI ? tr("Build") : tr("Version");
    const auto updateMessage = QString("%1: %2 %3").arg( tr("An update to 86Box is available"), type, result.latestVersion);
    emit updateStatusLeft(updateMessage);
}

void
VMManagerMain::backgroundUpdateCheckError(const QString &errorMsg)
{
    qDebug() << "Update check failed with the following error:" << errorMsg;
    // TODO: Update the status bar
}

void
VMManagerMain::showTextFileContents(const QString &title, const QString &path)
{
    // Make sure we can open the file
    const auto fi = QFileInfo(path);
    if(!fi.exists()) {
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
    textDisplayDialog->setFixedSize(QSize(540, 360));
    textDisplayDialog->setWindowTitle(QString("%1 - %2").arg(title, fi.fileName()));

    const auto textEdit = new QPlainTextEdit();
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
