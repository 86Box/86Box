#pragma once

#include <QList>
#include <QPair>
#include <QString>
#include <QVariant>
#include <QVector>

class QAbstractItemModel;
struct _device_;

namespace Models {
int AddEntry(QAbstractItemModel *model, const QString &displayRole, int userRole);
int AddEntry(QAbstractItemModel *model, const QString &displayRole, const QString &userRole);

/* Rows collected and then inserted into the model all at once, so a view
   on it (a combo box sizing itself to its contents, a completer) handles
   one insertion rather than one per row. add() returns the row the entry
   will have, as AddEntry() does. */
class Batch {
public:
    explicit Batch(QAbstractItemModel *model);
    Batch(const Batch &)            = delete;
    Batch &operator=(const Batch &) = delete;
    ~Batch();

    int  add(const QString &displayRole, int userRole);
    int  add(const QString &displayRole, const QString &userRole);
    int  count() const { return rows.size(); }
    void commit();

private:
    QAbstractItemModel             *model;
    int                             base;
    QList<QPair<QString, QVariant>> rows;
};

/* One entry of a device table: its index, its name as the settings show
   it, the device, and whether its ROMs are present. None of it depends on
   the machine, so a table is walked once per opening of the settings and
   the pages filter the entries by machine themselves. */
struct Device {
    int              id;
    QString          name;
    const _device_  *dev;
    bool             available;
};

using DeviceFn    = const _device_ *(*) (int);
using NameFn      = const char *(*) (int);
using AvailableFn = int (*)(int);

/* The table read through device(), up to its first entry without a name;
   available may be nullptr for a table whose entries are all offered. */
const QVector<Device> &Devices(DeviceFn device, NameFn internalName, AvailableFn available, int bus);
void                   ClearDevices();
};
