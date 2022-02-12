/*
 * 86Box	A hypervisor and IBM PC system emulator that specializes in
 *		running old operating systems and software designed for IBM
 *		PC systems and compatibles from 1981 through fairly recent
 *		system designs based on the PCI bus.
 *
 *		This file is part of the 86Box distribution.
 *
 *		File field widget.
 *
 *
 *
 * Authors:	Joakim L. Gilje <jgilje@jgilje.net>
 *          Cacodemon345
 *
 *		Copyright 2021 Joakim L. Gilje
 *      Copyright 2022 Cacodemon345
 */
#include "qt_filefield.hpp"
#include "ui_qt_filefield.h"

#include <QFileDialog>

FileField::FileField(QWidget *parent) :
    QWidget(parent),
    ui(new Ui::FileField)
{
    ui->setupUi(this);

    connect(ui->label, &QLineEdit::editingFinished, this, [this] () {
        fileName_ = ui->label->text();
        emit fileSelected(ui->label->text());
    });
    this->setFixedWidth(this->sizeHint().width() + ui->pushButton->sizeHint().width());
}

FileField::~FileField()
{
    delete ui;
}

void FileField::setFileName(const QString &fileName) {
    fileName_ = fileName;
    ui->label->setText(fileName);
}

void FileField::on_pushButton_clicked() {
    QString fileName;
    if (createFile_) {
        fileName = QFileDialog::getSaveFileName(this, QString(), QString(), filter_, &selectedFilter_);
    } else {
        fileName = QFileDialog::getOpenFileName(this, QString(), QString(), filter_, &selectedFilter_);
    }

    if (!fileName.isNull()) {
        fileName_ = fileName;
        ui->label->setText(fileName);
        emit fileSelected(fileName);
    }
}
