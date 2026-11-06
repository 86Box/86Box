/*
 * 86Box    A hypervisor and IBM PC system emulator that specializes in
 *          running old operating systems and software designed for IBM
 *          PC systems and compatibles from 1981 through fairly recent
 *          system designs based on the PCI bus.
 *
 *          This file is part of the 86Box distribution.
 *
 *          Common storage devices module.
 *
 * Authors: Joakim L. Gilje <jgilje@jgilje.net>
 *
 *          Copyright 2021 Joakim L. Gilje
 */
#include "qt_models_common.hpp"
#include "qt_deviceconfig.hpp"

#include <QAbstractItemModel>
#include <QHash>
#include <QStandardItem>
#include <QStandardItemModel>

int
Models::AddEntry(QAbstractItemModel *model, const QString &displayRole, int userRole)
{
    int row = model->rowCount();
    model->insertRow(row);
    auto idx = model->index(row, 0);

    model->setData(idx, displayRole, Qt::DisplayRole);
    model->setData(idx, userRole, Qt::UserRole);

    return row;
}

int
Models::AddEntry(QAbstractItemModel *model, const QString &displayRole, const QString &userRole)
{
    int row = model->rowCount();
    model->insertRow(row);
    auto idx = model->index(row, 0);

    model->setData(idx, displayRole, Qt::DisplayRole);
    model->setData(idx, userRole, Qt::UserRole);

    return row;
}

Models::Batch::Batch(QAbstractItemModel *model)
    : model(model)
    , base(model->rowCount())
{
}

Models::Batch::~Batch()
{
    commit();
}

int
Models::Batch::add(const QString &displayRole, int userRole)
{
    rows.append({ displayRole, userRole });

    return base + rows.size() - 1;
}

int
Models::Batch::add(const QString &displayRole, const QString &userRole)
{
    rows.append({ displayRole, userRole });

    return base + rows.size() - 1;
}

void
Models::Batch::commit()
{
    if (rows.isEmpty())
        return;

    if (auto *standard = qobject_cast<QStandardItemModel *>(model)) {
        QList<QStandardItem *> items;

        items.reserve(rows.size());
        for (const auto &row : rows) {
            auto *item = new QStandardItem(row.first);
            item->setData(row.second, Qt::UserRole);
            items.append(item);
        }
        standard->invisibleRootItem()->insertRows(standard->rowCount(), items);
    } else {
        int first = model->rowCount();

        model->insertRows(first, rows.size());
        for (int i = 0; i < rows.size(); ++i) {
            auto idx = model->index(first + i, 0);

            model->setData(idx, rows[i].first, Qt::DisplayRole);
            model->setData(idx, rows[i].second, Qt::UserRole);
        }
    }

    base += rows.size();
    rows.clear();
}

static QHash<QPair<quintptr, int>, QVector<Models::Device>> device_lists;

const QVector<Models::Device> &
Models::Devices(DeviceFn device, NameFn internalName, AvailableFn available, int bus)
{
    const QPair<quintptr, int> key(reinterpret_cast<quintptr>(device), bus);
    auto                       found = device_lists.constFind(key);

    if (found != device_lists.constEnd())
        return found.value();

    QVector<Device> list;
    for (int c = 0;; ++c) {
        const _device_ *dev  = device(c);
        const QString   name = DeviceConfig::DeviceName(dev, internalName(c), bus);

        if (name.isEmpty())
            break;

        list.append({ c, name, dev, (available == nullptr) || (available(c) != 0) });
    }

    return device_lists.insert(key, list).value();
}

void
Models::ClearDevices()
{
    device_lists.clear();
}
