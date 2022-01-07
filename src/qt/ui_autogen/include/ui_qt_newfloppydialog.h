/********************************************************************************
** Form generated from reading UI file 'qt_newfloppydialog.ui'
**
** Created by: Qt User Interface Compiler version 6.2.2
**
** WARNING! All changes made in this file will be lost when recompiling UI file!
********************************************************************************/

#ifndef UI_QT_NEWFLOPPYDIALOG_H
#define UI_QT_NEWFLOPPYDIALOG_H

#include <QtCore/QVariant>
#include <QtWidgets/QAbstractButton>
#include <QtWidgets/QApplication>
#include <QtWidgets/QComboBox>
#include <QtWidgets/QDialog>
#include <QtWidgets/QDialogButtonBox>
#include <QtWidgets/QGridLayout>
#include <QtWidgets/QLabel>
#include <QtWidgets/QSpacerItem>
#include "qt_filefield.hpp"

QT_BEGIN_NAMESPACE

class Ui_NewFloppyDialog
{
public:
    QGridLayout *gridLayout;
    QComboBox *comboBoxSize;
    QSpacerItem *verticalSpacer;
    QDialogButtonBox *buttonBox;
    QLabel *label_2;
    QLabel *label;
    FileField *fileField;
    QLabel *labelRpm;
    QComboBox *comboBoxRpm;

    void setupUi(QDialog *NewFloppyDialog)
    {
        if (NewFloppyDialog->objectName().isEmpty())
            NewFloppyDialog->setObjectName(QString::fromUtf8("NewFloppyDialog"));
        NewFloppyDialog->resize(287, 140);
        gridLayout = new QGridLayout(NewFloppyDialog);
        gridLayout->setObjectName(QString::fromUtf8("gridLayout"));
        comboBoxSize = new QComboBox(NewFloppyDialog);
        comboBoxSize->setObjectName(QString::fromUtf8("comboBoxSize"));

        gridLayout->addWidget(comboBoxSize, 2, 1, 1, 1);

        verticalSpacer = new QSpacerItem(20, 40, QSizePolicy::Minimum, QSizePolicy::Expanding);

        gridLayout->addItem(verticalSpacer, 4, 0, 1, 1);

        buttonBox = new QDialogButtonBox(NewFloppyDialog);
        buttonBox->setObjectName(QString::fromUtf8("buttonBox"));
        buttonBox->setOrientation(Qt::Horizontal);
        buttonBox->setStandardButtons(QDialogButtonBox::Cancel|QDialogButtonBox::Ok);

        gridLayout->addWidget(buttonBox, 5, 0, 1, 2);

        label_2 = new QLabel(NewFloppyDialog);
        label_2->setObjectName(QString::fromUtf8("label_2"));

        gridLayout->addWidget(label_2, 1, 0, 1, 1);

        label = new QLabel(NewFloppyDialog);
        label->setObjectName(QString::fromUtf8("label"));

        gridLayout->addWidget(label, 2, 0, 1, 1);

        fileField = new FileField(NewFloppyDialog);
        fileField->setObjectName(QString::fromUtf8("fileField"));

        gridLayout->addWidget(fileField, 1, 1, 1, 1);

        labelRpm = new QLabel(NewFloppyDialog);
        labelRpm->setObjectName(QString::fromUtf8("labelRpm"));

        gridLayout->addWidget(labelRpm, 3, 0, 1, 1);

        comboBoxRpm = new QComboBox(NewFloppyDialog);
        comboBoxRpm->setObjectName(QString::fromUtf8("comboBoxRpm"));

        gridLayout->addWidget(comboBoxRpm, 3, 1, 1, 1);


        retranslateUi(NewFloppyDialog);
        QObject::connect(buttonBox, &QDialogButtonBox::accepted, NewFloppyDialog, qOverload<>(&QDialog::accept));
        QObject::connect(buttonBox, &QDialogButtonBox::rejected, NewFloppyDialog, qOverload<>(&QDialog::reject));

        QMetaObject::connectSlotsByName(NewFloppyDialog);
    } // setupUi

    void retranslateUi(QDialog *NewFloppyDialog)
    {
        NewFloppyDialog->setWindowTitle(QCoreApplication::translate("NewFloppyDialog", "New Image", nullptr));
        label_2->setText(QCoreApplication::translate("NewFloppyDialog", "File name:", nullptr));
        label->setText(QCoreApplication::translate("NewFloppyDialog", "Disk size:", nullptr));
        labelRpm->setText(QCoreApplication::translate("NewFloppyDialog", "RPM mode:", nullptr));
    } // retranslateUi

};

namespace Ui {
    class NewFloppyDialog: public Ui_NewFloppyDialog {};
} // namespace Ui

QT_END_NAMESPACE

#endif // UI_QT_NEWFLOPPYDIALOG_H
