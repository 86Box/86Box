#include "qt_models_common.hpp"

#include <QAbstractItemModel>

int Models::AddEntry(QAbstractItemModel *model, const QString& displayRole, int userRole)
{
    int row = model->rowCount();
    model->insertRow(row);
    auto idx = model->index(row, 0);

    model->setData(idx, displayRole, Qt::DisplayRole);
    model->setData(idx, userRole, Qt::UserRole);

    return row;
}
