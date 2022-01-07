/********************************************************************************
** Form generated from reading UI file 'qt_harddiskdialog.ui'
**
** Created by: Qt User Interface Compiler version 6.2.2
**
** WARNING! All changes made in this file will be lost when recompiling UI file!
********************************************************************************/

#ifndef UI_QT_HARDDISKDIALOG_H
#define UI_QT_HARDDISKDIALOG_H

#include <QtCore/QVariant>
#include <QtWidgets/QAbstractButton>
#include <QtWidgets/QApplication>
#include <QtWidgets/QComboBox>
#include <QtWidgets/QDialog>
#include <QtWidgets/QDialogButtonBox>
#include <QtWidgets/QGridLayout>
#include <QtWidgets/QHBoxLayout>
#include <QtWidgets/QLabel>
#include <QtWidgets/QLineEdit>
#include <QtWidgets/QSpacerItem>
#include <QtWidgets/QVBoxLayout>
#include "qt_filefield.hpp"

QT_BEGIN_NAMESPACE

class Ui_HarddiskDialog
{
public:
    QVBoxLayout *verticalLayout;
    QVBoxLayout *verticalLayout_2;
    QLabel *label;
    FileField *fileField;
    QGridLayout *gridLayout;
    QLabel *label_2;
    QLabel *label_5;
    QLineEdit *lineEditCylinders;
    QLineEdit *lineEditSize;
    QLabel *label_3;
    QLabel *label_4;
    QLineEdit *lineEditHeads;
    QLabel *label_6;
    QLineEdit *lineEditSectors;
    QComboBox *comboBoxType;
    QHBoxLayout *horizontalLayout;
    QLabel *label_8;
    QComboBox *comboBoxBus;
    QLabel *label_7;
    QComboBox *comboBoxChannel;
    QHBoxLayout *horizontalLayoutFormat;
    QLabel *labelFormat;
    QComboBox *comboBoxFormat;
    QHBoxLayout *horizontalLayout_2;
    QLabel *labelBlockSize;
    QComboBox *comboBoxBlockSize;
    QSpacerItem *verticalSpacer;
    QDialogButtonBox *buttonBox;

    void setupUi(QDialog *HarddiskDialog)
    {
        if (HarddiskDialog->objectName().isEmpty())
            HarddiskDialog->setObjectName(QString::fromUtf8("HarddiskDialog"));
        HarddiskDialog->resize(400, 300);
        verticalLayout = new QVBoxLayout(HarddiskDialog);
        verticalLayout->setObjectName(QString::fromUtf8("verticalLayout"));
        verticalLayout_2 = new QVBoxLayout();
        verticalLayout_2->setSpacing(0);
        verticalLayout_2->setObjectName(QString::fromUtf8("verticalLayout_2"));
        label = new QLabel(HarddiskDialog);
        label->setObjectName(QString::fromUtf8("label"));

        verticalLayout_2->addWidget(label);

        fileField = new FileField(HarddiskDialog);
        fileField->setObjectName(QString::fromUtf8("fileField"));

        verticalLayout_2->addWidget(fileField);


        verticalLayout->addLayout(verticalLayout_2);

        gridLayout = new QGridLayout();
        gridLayout->setObjectName(QString::fromUtf8("gridLayout"));
        label_2 = new QLabel(HarddiskDialog);
        label_2->setObjectName(QString::fromUtf8("label_2"));

        gridLayout->addWidget(label_2, 0, 0, 1, 1);

        label_5 = new QLabel(HarddiskDialog);
        label_5->setObjectName(QString::fromUtf8("label_5"));

        gridLayout->addWidget(label_5, 0, 4, 1, 1);

        lineEditCylinders = new QLineEdit(HarddiskDialog);
        lineEditCylinders->setObjectName(QString::fromUtf8("lineEditCylinders"));

        gridLayout->addWidget(lineEditCylinders, 0, 1, 1, 1);

        lineEditSize = new QLineEdit(HarddiskDialog);
        lineEditSize->setObjectName(QString::fromUtf8("lineEditSize"));

        gridLayout->addWidget(lineEditSize, 1, 1, 1, 1);

        label_3 = new QLabel(HarddiskDialog);
        label_3->setObjectName(QString::fromUtf8("label_3"));

        gridLayout->addWidget(label_3, 1, 0, 1, 1);

        label_4 = new QLabel(HarddiskDialog);
        label_4->setObjectName(QString::fromUtf8("label_4"));

        gridLayout->addWidget(label_4, 0, 2, 1, 1);

        lineEditHeads = new QLineEdit(HarddiskDialog);
        lineEditHeads->setObjectName(QString::fromUtf8("lineEditHeads"));
        lineEditHeads->setMaxLength(32767);

        gridLayout->addWidget(lineEditHeads, 0, 3, 1, 1);

        label_6 = new QLabel(HarddiskDialog);
        label_6->setObjectName(QString::fromUtf8("label_6"));

        gridLayout->addWidget(label_6, 1, 2, 1, 1);

        lineEditSectors = new QLineEdit(HarddiskDialog);
        lineEditSectors->setObjectName(QString::fromUtf8("lineEditSectors"));
        lineEditSectors->setMaxLength(32767);

        gridLayout->addWidget(lineEditSectors, 0, 5, 1, 1);

        comboBoxType = new QComboBox(HarddiskDialog);
        comboBoxType->setObjectName(QString::fromUtf8("comboBoxType"));

        gridLayout->addWidget(comboBoxType, 1, 3, 1, 3);


        verticalLayout->addLayout(gridLayout);

        horizontalLayout = new QHBoxLayout();
        horizontalLayout->setObjectName(QString::fromUtf8("horizontalLayout"));
        label_8 = new QLabel(HarddiskDialog);
        label_8->setObjectName(QString::fromUtf8("label_8"));

        horizontalLayout->addWidget(label_8);

        comboBoxBus = new QComboBox(HarddiskDialog);
        comboBoxBus->setObjectName(QString::fromUtf8("comboBoxBus"));

        horizontalLayout->addWidget(comboBoxBus);

        label_7 = new QLabel(HarddiskDialog);
        label_7->setObjectName(QString::fromUtf8("label_7"));

        horizontalLayout->addWidget(label_7);

        comboBoxChannel = new QComboBox(HarddiskDialog);
        comboBoxChannel->setObjectName(QString::fromUtf8("comboBoxChannel"));

        horizontalLayout->addWidget(comboBoxChannel);


        verticalLayout->addLayout(horizontalLayout);

        horizontalLayoutFormat = new QHBoxLayout();
        horizontalLayoutFormat->setObjectName(QString::fromUtf8("horizontalLayoutFormat"));
        labelFormat = new QLabel(HarddiskDialog);
        labelFormat->setObjectName(QString::fromUtf8("labelFormat"));

        horizontalLayoutFormat->addWidget(labelFormat);

        comboBoxFormat = new QComboBox(HarddiskDialog);
        comboBoxFormat->setObjectName(QString::fromUtf8("comboBoxFormat"));

        horizontalLayoutFormat->addWidget(comboBoxFormat);


        verticalLayout->addLayout(horizontalLayoutFormat);

        horizontalLayout_2 = new QHBoxLayout();
        horizontalLayout_2->setObjectName(QString::fromUtf8("horizontalLayout_2"));
        horizontalLayout_2->setContentsMargins(-1, 5, -1, -1);
        labelBlockSize = new QLabel(HarddiskDialog);
        labelBlockSize->setObjectName(QString::fromUtf8("labelBlockSize"));

        horizontalLayout_2->addWidget(labelBlockSize);

        comboBoxBlockSize = new QComboBox(HarddiskDialog);
        comboBoxBlockSize->setObjectName(QString::fromUtf8("comboBoxBlockSize"));

        horizontalLayout_2->addWidget(comboBoxBlockSize);


        verticalLayout->addLayout(horizontalLayout_2);

        verticalSpacer = new QSpacerItem(20, 40, QSizePolicy::Minimum, QSizePolicy::Expanding);

        verticalLayout->addItem(verticalSpacer);

        buttonBox = new QDialogButtonBox(HarddiskDialog);
        buttonBox->setObjectName(QString::fromUtf8("buttonBox"));
        buttonBox->setOrientation(Qt::Horizontal);
        buttonBox->setStandardButtons(QDialogButtonBox::Cancel|QDialogButtonBox::Ok);

        verticalLayout->addWidget(buttonBox);

        QWidget::setTabOrder(lineEditCylinders, lineEditHeads);
        QWidget::setTabOrder(lineEditHeads, lineEditSectors);
        QWidget::setTabOrder(lineEditSectors, lineEditSize);
        QWidget::setTabOrder(lineEditSize, comboBoxType);
        QWidget::setTabOrder(comboBoxType, comboBoxBus);
        QWidget::setTabOrder(comboBoxBus, comboBoxChannel);
        QWidget::setTabOrder(comboBoxChannel, comboBoxFormat);
        QWidget::setTabOrder(comboBoxFormat, comboBoxBlockSize);

        retranslateUi(HarddiskDialog);
        QObject::connect(buttonBox, &QDialogButtonBox::accepted, HarddiskDialog, qOverload<>(&QDialog::accept));
        QObject::connect(buttonBox, &QDialogButtonBox::rejected, HarddiskDialog, qOverload<>(&QDialog::reject));

        QMetaObject::connectSlotsByName(HarddiskDialog);
    } // setupUi

    void retranslateUi(QDialog *HarddiskDialog)
    {
        HarddiskDialog->setWindowTitle(QCoreApplication::translate("HarddiskDialog", "Dialog", nullptr));
        label->setText(QCoreApplication::translate("HarddiskDialog", "File name:", nullptr));
        label_2->setText(QCoreApplication::translate("HarddiskDialog", "Cylinders:", nullptr));
        label_5->setText(QCoreApplication::translate("HarddiskDialog", "Sectors:", nullptr));
        label_3->setText(QCoreApplication::translate("HarddiskDialog", "Size (MB):", nullptr));
        label_4->setText(QCoreApplication::translate("HarddiskDialog", "Heads:", nullptr));
        label_6->setText(QCoreApplication::translate("HarddiskDialog", "Type:", nullptr));
        label_8->setText(QCoreApplication::translate("HarddiskDialog", "Bus:", nullptr));
        label_7->setText(QCoreApplication::translate("HarddiskDialog", "Channel:", nullptr));
        labelFormat->setText(QCoreApplication::translate("HarddiskDialog", "Image Format:", nullptr));
        labelBlockSize->setText(QCoreApplication::translate("HarddiskDialog", "Block Size:", nullptr));
    } // retranslateUi

};

namespace Ui {
    class HarddiskDialog: public Ui_HarddiskDialog {};
} // namespace Ui

QT_END_NAMESPACE

#endif // UI_QT_HARDDISKDIALOG_H
