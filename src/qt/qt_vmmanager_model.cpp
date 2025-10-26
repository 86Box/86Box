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

VMManagerModel::VMManagerModel() {
    auto machines_vec = VMManagerSystem::scanForConfigs();
    for ( const auto& each_config : machines_vec) {
        machines.append(each_config);
        connect(each_config, &VMManagerSystem::itemDataChanged, this, &VMManagerModel::modelDataChanged);
        connect(each_config, &VMManagerSystem::globalConfigurationChanged, this, &VMManagerModel::globalConfigurationChanged);
    }
}

VMManagerModel::~VMManagerModel() {
    for ( auto machine : machines) {
        delete machine;
    }
}

int
VMManagerModel::rowCount(const QModelIndex &parent) const {
    return machines.size();
}

QVariant
VMManagerModel::data(const QModelIndex &index, int role) const {
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
VMManagerModel::headerData(int section, Qt::Orientation orientation, int role) const {

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
VMManagerModel::reload(QWidget* parent)
{
    // Scan for configs
    auto machines_vec = VMManagerSystem::scanForConfigs(parent);
    for (const auto &scanned_config : machines_vec) {
        int found = 0;
        for (const auto &existing_config : machines) {
            if (*scanned_config == *existing_config) {
                found = 1;
            }
        }
        if (!found) {
            addConfigToModel(scanned_config);
        }
    }
    // TODO: Remove missing configs
}

void
VMManagerModel::refreshConfigs() {
    for ( const auto& each_config : machines)
        each_config->reloadConfig();
}

QModelIndex
VMManagerModel::getIndexForConfigFile(const QFileInfo& config_file)
{
    int object_index = 0;
    for (const auto& config_object: machines) {
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
    emit dataChanged(this->index(0), this->index(machines.size()-1));
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
    for (const auto& system: machines) {
        stats[system->getProcessStatus()] += 1;
    }
    return stats;
}

void
VMManagerModel::sendGlobalConfigurationChanged()
{
    for (auto& system: machines) {
        if (system->getProcessStatus() != VMManagerSystem::ProcessStatus::Stopped) {
            system->sendGlobalConfigurationChanged();
        }
    }
}

int
VMManagerModel::getActiveMachineCount()
{
    int running = 0;
    for (const auto& system: machines) {
        if (system->getProcessStatus() != VMManagerSystem::ProcessStatus::Stopped)
            running++;
    }
    return running;
}
