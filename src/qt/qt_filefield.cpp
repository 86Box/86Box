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
