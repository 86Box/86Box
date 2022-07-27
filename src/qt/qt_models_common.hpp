#pragma once

class QString;
class QAbstractItemModel;
namespace Models
{
    int AddEntry(QAbstractItemModel* model, const QString& displayRole, int userRole);
};
