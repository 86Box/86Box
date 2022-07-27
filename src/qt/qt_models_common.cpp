/*
 * 86Box	A hypervisor and IBM PC system emulator that specializes in
 *		running old operating systems and software designed for IBM
 *		PC systems and compatibles from 1981 through fairly recent
 *		system designs based on the PCI bus.
 *
 *		This file is part of the 86Box distribution.
 *
 *		Common storage devices module.
 *
 *
 *
 * Authors:	Joakim L. Gilje <jgilje@jgilje.net>
 *
 *		Copyright 2021 Joakim L. Gilje
 */
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
