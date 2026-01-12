/*
 * 86Box    A hypervisor and IBM PC system emulator that specializes in
 *          running old operating systems and software designed for IBM
 *          PC systems and compatibles from 1981 through fairly recent
 *          system designs based on the PCI bus.
 *
 *          This file is part of the 86Box distribution.
 *
 *          Header for 86Box VM manager model module
 *
 * Authors: cold-brewed
 *
 *          Copyright 2024 cold-brewed
 */
#ifndef QT_VMMANAGER_MODEL_H
#define QT_VMMANAGER_MODEL_H

#include "qt_vmmanager_system.hpp"

#include <QSortFilterProxyModel>

class VMManagerModel final : public QAbstractListModel {

    Q_OBJECT

public:
    //    VMManagerModel(const QStringList &strings, QObject *parent = nullptr)
    //            : QAbstractListModel(parent), machines(strings) {}
    VMManagerModel();
    ~VMManagerModel() override;
    enum Roles {
        ProcessStatusString = Qt::UserRole + 1,
        ProcessStatus,
        DisplayName,
        ConfigName,
        ConfigDir,
        ConfigFile,
        LastUsed,
        UUID,
        Notes,
        SearchList,
        Icon
    };

    [[nodiscard]] int      rowCount(const QModelIndex &parent) const override;
    [[nodiscard]] QVariant data(const QModelIndex &index, int role) const override;
    [[nodiscard]] QVariant headerData(int section, Qt::Orientation orientation,
                                      int role) const override;
    void                   addConfigToModel(VMManagerSystem *system_config);
    void                   removeConfigFromModel(VMManagerSystem *system_config);

    [[nodiscard]] VMManagerSystem            *getConfigObjectForIndex(const QModelIndex &index) const;
    QModelIndex                               getIndexForConfigFile(const QFileInfo &config_file);
    void                                      reload(QWidget *parent = nullptr);
    void                                      updateDisplayName(const QModelIndex &index, const QString &newDisplayName);
    QMap<VMManagerSystem::ProcessStatus, int> getProcessStats();
    int                                       getActiveMachineCount();
    void                                      refreshConfigs();
    void                                      sendGlobalConfigurationChanged();

signals:
    void systemDataChanged();
    void globalConfigurationChanged();

private:
    QVector<VMManagerSystem *> machines;
    void                       modelDataChanged();
};

// Note: Custom QSortFilterProxyModel is included here instead of its own file as
// its only use is in this model

class StringListProxyModel final : public QSortFilterProxyModel {
public:
    explicit StringListProxyModel(QObject *parent = nullptr)
        : QSortFilterProxyModel(parent)
    {
    }

protected:
    [[nodiscard]] bool filterAcceptsRow(const int sourceRow, const QModelIndex &sourceParent) const override
    {
        const QModelIndex index = sourceModel()->index(sourceRow, filterKeyColumn(), sourceParent);

        QStringList stringList = sourceModel()->data(index, VMManagerModel::Roles::SearchList).toStringList();

        const QRegularExpression regex = filterRegularExpression();

        const auto result = std::any_of(stringList.begin(), stringList.end(), [&regex](const QString &string) {
            return regex.match(string).hasMatch();
        });
        return result;
    }
};

#endif // QT_VMMANAGER_MODEL_H
