/*
 * 86Box    A hypervisor and IBM PC system emulator that specializes in
 *          running old operating systems and software designed for IBM
 *          PC systems and compatibles from 1981 through fairly recent
 *          system designs based on the PCI bus.
 *
 *          This file is part of the 86Box distribution.
 *
 *          86Box VM manager model module
 *
 * Authors: cold-brewed
 *
 *          Copyright 2024 cold-brewed
 */
#include <QDebug>
#include "qt_vmmanager_model.hpp"
#include <86box/86box.h>
#include <QDir>
#include <QFileInfo>
#include <QDirIterator>
#include <QApplication>
#include <QThread>
#include <QTimer>
#include <QSet>
#include <QTimer>
#include <QSet>

VMManagerModel::VMManagerModel()
{
    // Start empty; cache load and discovery happen after the window is shown
}

VMManagerModel::~VMManagerModel()
{
    for (auto machine : machines) {
        delete machine;
    }
}

int
VMManagerModel::rowCount(const QModelIndex &parent) const
{
    return machines.size();
}

QVariant
VMManagerModel::data(const QModelIndex &index, int role) const
{
    if (!index.isValid())
        return {};

    if (index.row() >= machines.size())
        return {};

    switch (role) {
        case Qt::DisplayRole:
            return machines.at(index.row())->displayName;
        case ConfigName:
            return machines.at(index.row())->config_name;
        case ConfigDir:
            return machines.at(index.row())->config_dir;
        case ConfigFile:
            return machines.at(index.row())->config_file.canonicalFilePath();
        case UUID:
            return machines.at(index.row())->uuid;
        case Notes:
            return machines.at(index.row())->notes;
        case SearchList:
            return machines.at(index.row())->searchTerms;
        case LastUsed:
            return machines.at(index.row())->timestamp();
        case Icon:
            return machines.at(index.row())->icon;
        case Qt::ToolTipRole:
            return machines.at(index.row())->shortened_dir;
        case Qt::UserRole:
            return machines.at(index.row())->getAll("General");
        case ProcessStatusString:
            return machines.at(index.row())->getProcessStatusString();
        case ProcessStatus:
            return QVariant::fromValue(machines.at(index.row())->getProcessStatus());
        default:
            return {};
    }
}

QVariant
VMManagerModel::headerData(int section, Qt::Orientation orientation, int role) const
{

    if (role != Qt::DisplayRole)
        return {};

    if (orientation == Qt::Horizontal)
        return QStringLiteral("Column %1").arg(section);
    else
        return QStringLiteral("Row %1").arg(section);
}

VMManagerSystem *
VMManagerModel::getConfigObjectForIndex(const QModelIndex &index) const
{
    return machines.at(index.row());
}
void
VMManagerModel::reload(QWidget *parent)
{
    // Prefer the non-blocking background discovery path
    Q_UNUSED(parent);
    startBackgroundDiscovery();
}

void
VMManagerModel::refreshConfigs()
{
    for (const auto &each_config : machines)
        each_config->reloadConfig();
}

QModelIndex
VMManagerModel::getIndexForConfigFile(const QFileInfo &config_file)
{
    int object_index = 0;
    for (const auto &config_object : machines) {
        if (config_object->config_file == config_file) {
            return this->index(object_index);
        }
        object_index++;
    }
    return {};
}

void
VMManagerModel::addConfigToModel(VMManagerSystem *system_config)
{
    beginInsertRows(QModelIndex(), this->rowCount(QModelIndex()), this->rowCount(QModelIndex()));
    machines.append(system_config);
    connect(system_config, &VMManagerSystem::itemDataChanged, this, &VMManagerModel::modelDataChanged);
    connect(system_config, &VMManagerSystem::globalConfigurationChanged, this, &VMManagerModel::globalConfigurationChanged);
    endInsertRows();
    emit systemDataChanged();
}

void
VMManagerModel::removeConfigFromModel(VMManagerSystem *system_config)
{
    const QModelIndex index = getIndexForConfigFile(system_config->config_file);
    disconnect(system_config, &VMManagerSystem::itemDataChanged, this, &VMManagerModel::modelDataChanged);
    beginRemoveRows(QModelIndex(), index.row(), index.row());
    machines.remove(index.row());
    endRemoveRows();
    emit systemDataChanged();
}

void
VMManagerModel::modelDataChanged()
{
    // Inform the model
    emit dataChanged(this->index(0), this->index(machines.size() - 1));
    // Inform any interested observers
    emit systemDataChanged();
}

void
VMManagerModel::updateDisplayName(const QModelIndex &index, const QString &newDisplayName)
{
    machines.at(index.row())->setDisplayName(newDisplayName);
    modelDataChanged();
}
QMap<VMManagerSystem::ProcessStatus, int>
VMManagerModel::getProcessStats()
{
    QMap<VMManagerSystem::ProcessStatus, int> stats;
    for (const auto &system : machines) {
        stats[system->getProcessStatus()] += 1;
    }
    return stats;
}

void
VMManagerModel::sendGlobalConfigurationChanged()
{
    for (auto &system : machines) {
        if (system->getProcessStatus() != VMManagerSystem::ProcessStatus::Stopped) {
            system->sendGlobalConfigurationChanged();
        }
    }
}

int
VMManagerModel::getActiveMachineCount()
{
    int running = 0;
    for (const auto &system : machines) {
        if (system->getProcessStatus() != VMManagerSystem::ProcessStatus::Stopped)
            running++;
    }
    return running;
}


void
VMManagerModel::loadFromCache(const QString &selectName)
{
    const auto paths = VMManagerSystem::cachedConfigPaths();
    for (const auto &path : paths) {
        auto *sys = new VMManagerSystem(path);
        if (sys->isValid()) {
            addConfigToModel(sys);
            // If this is the previously selected VM, select it immediately
            if (!selectName.isEmpty() && sys->config_name == selectName) {
                const QModelIndex idx = index(machines.size() - 1);
                emit selectionRestored(idx);
            }
            // Let the view update so the list fills in as each VM is loaded
            QApplication::processEvents(QEventLoop::ExcludeUserInputEvents);
        } else {
            delete sys;
        }
    }
    qDebug() << "Populated" << machines.size() << "VMs from cache";
}

void
VMManagerModel::startBackgroundDiscovery()
{
    // Run only the filesystem scan on a background thread.
    // Creating VMManagerSystem (QWidget) objects must stay on the main thread.
    auto *thread = QThread::create([this]() {
        // Silent scan that only collects paths (no VMManagerSystem construction)
        QStringList foundPaths;
        const QString search_directory = QString::fromUtf8(vmm_path);
        if (QDir(search_directory).exists()) {
            const QStringList filters = { QString(CONFIG_FILE) };
            QDirIterator it(search_directory, filters, QDir::Files, QDirIterator::Subdirectories);
            while (it.hasNext()) {
                const QString filename = it.next();
                // Skip the root config in the search directory itself
                QString root_cfg;
                if (search_directory.endsWith('/') || search_directory.endsWith('\\'))
                    root_cfg = search_directory + QString(CONFIG_FILE);
                else
                    root_cfg = search_directory + "/" + QString(CONFIG_FILE);
                if (filename.compare(root_cfg, Qt::CaseInsensitive) != 0)
                    foundPaths.append(filename);
            }
        }

        // Post the paths back to the main thread for object creation
        QMetaObject::invokeMethod(this, [this, foundPaths]() {
            QSet<QString> existingPaths;
            for (const auto *sys : machines)
                existingPaths.insert(sys->config_file.canonicalFilePath());

            for (const auto &path : foundPaths) {
                const QFileInfo fi(path);
                const QString canonical = fi.canonicalFilePath();
                if (canonical.isEmpty() || existingPaths.contains(canonical))
                    continue;
                auto *sys = new VMManagerSystem(path);
                if (sys->isValid()) {
                    addConfigToModel(sys);
                    existingPaths.insert(canonical);
                    QApplication::processEvents(QEventLoop::ExcludeUserInputEvents);
                } else {
                    delete sys;
                }
            }

            // Remove cached entries whose config files no longer exist
            QVector<VMManagerSystem *> toRemove;
            for (auto *sys : machines) {
                if (!sys->config_file.exists())
                    toRemove.append(sys);
            }
            for (auto *sys : toRemove)
                removeConfigFromModel(sys);

            qDebug() << "Background discovery complete. Total VMs:" << machines.size();
        }, Qt::QueuedConnection);
    });
    thread->start();
    connect(thread, &QThread::finished, thread, &QObject::deleteLater);
}
