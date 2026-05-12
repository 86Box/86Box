/*
 * 86Box    A hypervisor and IBM PC system emulator that specializes in
 *          running old operating systems and software designed for IBM
 *          PC systems and compatibles from 1981 through fairly recent
 *          system designs based on the PCI bus.
 *
 *          This file is part of the 86Box distribution.
 *
 *          Directory field widget.
 *
 * Authors: Joakim L. Gilje <jgilje@jgilje.net>
 *          Cacodemon345
 *          win2kgamer
 *
 *          Copyright 2021 Joakim L. Gilje
 *          Copyright 2022 Cacodemon345
 *          Copyright 2026 win2kgamer
 */
#include "qt_dirfield.hpp"
#include "ui_qt_dirfield.h"

#include <QFileDialog>

DirField::DirField(QWidget *parent)
    : QWidget(parent)
    , ui(new Ui::DirField)
{
    ui->setupUi(this);

    connect(ui->label, &QLineEdit::editingFinished, this, [this]() {
        dirName_ = ui->label->text();
        emit dirSelected(ui->label->text(), true);
    });

    connect(ui->label, &QLineEdit::textChanged, this, [this]() {
        dirName_ = ui->label->text();
        emit dirTextEntered(ui->label->text(), true);
    });

    this->setFixedWidth(this->sizeHint().width() + ui->pushButton->sizeHint().width());
}

DirField::~DirField()
{
    delete ui;
}

void
DirField::setDirName(const QString &dirName)
{
    dirName_ = dirName;
    ui->label->setText(dirName);
}

void
DirField::on_pushButton_clicked()
{
    QString dirName;
    dirName = QFileDialog::getExistingDirectory(this, QString(), QString(), QFileDialog::ShowDirsOnly);

    if (!dirName.isNull()) {
        dirName_ = dirName;
        ui->label->setText(dirName);
        emit dirSelected(dirName);
    }
}
