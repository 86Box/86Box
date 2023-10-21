/********************************************************************************
** Form generated from reading UI file 'qt_harddiskdialog.ui'
**
** Created by: Qt User Interface Compiler version 5.15.10
**
** WARNING! All changes made in this file will be lost when recompiling UI file!
********************************************************************************/

#ifndef UI_QT_HARDDISKDIALOG_H
#define UI_QT_HARDDISKDIALOG_H

#include <QtCore/QVariant>
#include <QtWidgets/QApplication>
#include <QtWidgets/QComboBox>
#include <QtWidgets/QDialog>
#include <QtWidgets/QDialogButtonBox>
#include <QtWidgets/QGridLayout>
#include <QtWidgets/QLabel>
#include <QtWidgets/QLineEdit>
#include <QtWidgets/QProgressBar>
#include <QtWidgets/QSpacerItem>
#include "qt_filefield.hpp"

QT_BEGIN_NAMESPACE

class Ui_HarddiskDialog
{
public:
    QGridLayout *gridLayout;
    FileField *fileField;
    QLabel *label_7;
    QLabel *label_9;
    QComboBox *comboBoxSpeed;
    QLabel *label_5;
    QLabel *label_6;
    QLineEdit *lineEditCylinders;
    QLineEdit *lineEditSize;
    QLabel *label_3;
    QComboBox *comboBoxChannel;
    QDialogButtonBox *buttonBox;
    QSpacerItem *verticalSpacer;
    QLabel *label_2;
    QLineEdit *lineEditSectors;
    QLabel *labelFormat;
    QLabel *label_4;
    QComboBox *comboBoxType;
    QLabel *label_8;
    QComboBox *comboBoxFormat;
    QComboBox *comboBoxBlockSize;
    QLineEdit *lineEditHeads;
    QComboBox *comboBoxBus;
    QLabel *labelBlockSize;
    QLabel *label;
    QProgressBar *progressBar;

    void setupUi(QDialog *HarddiskDialog)
    {
        if (HarddiskDialog->objectName().isEmpty())
            HarddiskDialog->setObjectName(QString::fromUtf8("HarddiskDialog"));
        HarddiskDialog->resize(421, 269);
        QSizePolicy sizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
        sizePolicy.setHorizontalStretch(0);
        sizePolicy.setVerticalStretch(0);
        sizePolicy.setHeightForWidth(HarddiskDialog->sizePolicy().hasHeightForWidth());
        HarddiskDialog->setSizePolicy(sizePolicy);
        HarddiskDialog->setMinimumSize(QSize(421, 269));
        HarddiskDialog->setMaximumSize(QSize(421, 269));
        gridLayout = new QGridLayout(HarddiskDialog);
        gridLayout->setObjectName(QString::fromUtf8("gridLayout"));
        fileField = new FileField(HarddiskDialog);
        fileField->setObjectName(QString::fromUtf8("fileField"));

        gridLayout->addWidget(fileField, 0, 1, 1, 5);

        label_7 = new QLabel(HarddiskDialog);
        label_7->setObjectName(QString::fromUtf8("label_7"));

        gridLayout->addWidget(label_7, 5, 4, 1, 1);

        label_9 = new QLabel(HarddiskDialog);
        label_9->setObjectName(QString::fromUtf8("label_9"));

        gridLayout->addWidget(label_9, 6, 4, 1, 1);

        comboBoxSpeed = new QComboBox(HarddiskDialog);
        comboBoxSpeed->setObjectName(QString::fromUtf8("comboBoxSpeed"));
        comboBoxSpeed->setMaxVisibleItems(30);

        gridLayout->addWidget(comboBoxSpeed, 6, 5, 1, 1);

        label_5 = new QLabel(HarddiskDialog);
        label_5->setObjectName(QString::fromUtf8("label_5"));

        gridLayout->addWidget(label_5, 3, 4, 1, 1);

        label_6 = new QLabel(HarddiskDialog);
        label_6->setObjectName(QString::fromUtf8("label_6"));

        gridLayout->addWidget(label_6, 4, 2, 1, 1);

        lineEditCylinders = new QLineEdit(HarddiskDialog);
        lineEditCylinders->setObjectName(QString::fromUtf8("lineEditCylinders"));
        sizePolicy.setHeightForWidth(lineEditCylinders->sizePolicy().hasHeightForWidth());
        lineEditCylinders->setSizePolicy(sizePolicy);
        lineEditCylinders->setMaximumSize(QSize(64, 16777215));

        gridLayout->addWidget(lineEditCylinders, 3, 1, 1, 1);

        lineEditSize = new QLineEdit(HarddiskDialog);
        lineEditSize->setObjectName(QString::fromUtf8("lineEditSize"));
        sizePolicy.setHeightForWidth(lineEditSize->sizePolicy().hasHeightForWidth());
        lineEditSize->setSizePolicy(sizePolicy);
        lineEditSize->setMaximumSize(QSize(64, 16777215));

        gridLayout->addWidget(lineEditSize, 4, 1, 1, 1);

        label_3 = new QLabel(HarddiskDialog);
        label_3->setObjectName(QString::fromUtf8("label_3"));

        gridLayout->addWidget(label_3, 4, 0, 1, 1);

        comboBoxChannel = new QComboBox(HarddiskDialog);
        comboBoxChannel->setObjectName(QString::fromUtf8("comboBoxChannel"));
        comboBoxChannel->setMaxVisibleItems(30);

        gridLayout->addWidget(comboBoxChannel, 5, 5, 1, 1);

        buttonBox = new QDialogButtonBox(HarddiskDialog);
        buttonBox->setObjectName(QString::fromUtf8("buttonBox"));
        buttonBox->setOrientation(Qt::Horizontal);
        buttonBox->setStandardButtons(QDialogButtonBox::Cancel|QDialogButtonBox::Ok);

        gridLayout->addWidget(buttonBox, 11, 0, 1, 6);

        verticalSpacer = new QSpacerItem(20, 20, QSizePolicy::Minimum, QSizePolicy::Expanding);

        gridLayout->addItem(verticalSpacer, 9, 0, 1, 6);

        label_2 = new QLabel(HarddiskDialog);
        label_2->setObjectName(QString::fromUtf8("label_2"));

        gridLayout->addWidget(label_2, 3, 0, 1, 1);

        lineEditSectors = new QLineEdit(HarddiskDialog);
        lineEditSectors->setObjectName(QString::fromUtf8("lineEditSectors"));
        sizePolicy.setHeightForWidth(lineEditSectors->sizePolicy().hasHeightForWidth());
        lineEditSectors->setSizePolicy(sizePolicy);
        lineEditSectors->setMaximumSize(QSize(64, 16777215));
        lineEditSectors->setMaxLength(32767);

        gridLayout->addWidget(lineEditSectors, 3, 5, 1, 1);

        labelFormat = new QLabel(HarddiskDialog);
        labelFormat->setObjectName(QString::fromUtf8("labelFormat"));

        gridLayout->addWidget(labelFormat, 6, 0, 1, 1);

        label_4 = new QLabel(HarddiskDialog);
        label_4->setObjectName(QString::fromUtf8("label_4"));

        gridLayout->addWidget(label_4, 3, 2, 1, 1);

        comboBoxType = new QComboBox(HarddiskDialog);
        comboBoxType->setObjectName(QString::fromUtf8("comboBoxType"));
        comboBoxType->setMaxVisibleItems(30);

        gridLayout->addWidget(comboBoxType, 4, 3, 1, 3);

        label_8 = new QLabel(HarddiskDialog);
        label_8->setObjectName(QString::fromUtf8("label_8"));

        gridLayout->addWidget(label_8, 5, 0, 1, 1);

        comboBoxFormat = new QComboBox(HarddiskDialog);
        comboBoxFormat->setObjectName(QString::fromUtf8("comboBoxFormat"));
        comboBoxFormat->setMaxVisibleItems(30);

        gridLayout->addWidget(comboBoxFormat, 6, 1, 1, 3);

        comboBoxBlockSize = new QComboBox(HarddiskDialog);
        comboBoxBlockSize->setObjectName(QString::fromUtf8("comboBoxBlockSize"));
        comboBoxBlockSize->setMaxVisibleItems(30);

        gridLayout->addWidget(comboBoxBlockSize, 7, 1, 1, 3);

        lineEditHeads = new QLineEdit(HarddiskDialog);
        lineEditHeads->setObjectName(QString::fromUtf8("lineEditHeads"));
        sizePolicy.setHeightForWidth(lineEditHeads->sizePolicy().hasHeightForWidth());
        lineEditHeads->setSizePolicy(sizePolicy);
        lineEditHeads->setMaximumSize(QSize(64, 16777215));
        lineEditHeads->setMaxLength(32767);

        gridLayout->addWidget(lineEditHeads, 3, 3, 1, 1);

        comboBoxBus = new QComboBox(HarddiskDialog);
        comboBoxBus->setObjectName(QString::fromUtf8("comboBoxBus"));
        comboBoxBus->setMaxVisibleItems(30);

        gridLayout->addWidget(comboBoxBus, 5, 1, 1, 3);

        labelBlockSize = new QLabel(HarddiskDialog);
        labelBlockSize->setObjectName(QString::fromUtf8("labelBlockSize"));

        gridLayout->addWidget(labelBlockSize, 7, 0, 1, 1);

        label = new QLabel(HarddiskDialog);
        label->setObjectName(QString::fromUtf8("label"));

        gridLayout->addWidget(label, 0, 0, 1, 1);

        progressBar = new QProgressBar(HarddiskDialog);
        progressBar->setObjectName(QString::fromUtf8("progressBar"));
        progressBar->setVisible(false);
        progressBar->setValue(0);
        progressBar->setTextVisible(true);

        gridLayout->addWidget(progressBar, 8, 0, 1, 6);

        QWidget::setTabOrder(lineEditCylinders, lineEditHeads);
        QWidget::setTabOrder(lineEditHeads, lineEditSectors);
        QWidget::setTabOrder(lineEditSectors, lineEditSize);
        QWidget::setTabOrder(lineEditSize, comboBoxType);
        QWidget::setTabOrder(comboBoxType, comboBoxBus);
        QWidget::setTabOrder(comboBoxBus, comboBoxChannel);
        QWidget::setTabOrder(comboBoxChannel, comboBoxFormat);
        QWidget::setTabOrder(comboBoxFormat, comboBoxBlockSize);

        retranslateUi(HarddiskDialog);
        QObject::connect(buttonBox, SIGNAL(accepted()), HarddiskDialog, SLOT(accept()));
        QObject::connect(buttonBox, SIGNAL(rejected()), HarddiskDialog, SLOT(reject()));

        QMetaObject::connectSlotsByName(HarddiskDialog);
    } // setupUi

    void retranslateUi(QDialog *HarddiskDialog)
    {
        HarddiskDialog->setWindowTitle(QCoreApplication::translate("HarddiskDialog", "Dialog", nullptr));
        label_7->setText(QCoreApplication::translate("HarddiskDialog", "Channel:", nullptr));
        label_9->setText(QCoreApplication::translate("HarddiskDialog", "Speed:", nullptr));
        label_5->setText(QCoreApplication::translate("HarddiskDialog", "Sectors:", nullptr));
        label_6->setText(QCoreApplication::translate("HarddiskDialog", "Type:", nullptr));
        label_3->setText(QCoreApplication::translate("HarddiskDialog", "Size (MB):", nullptr));
        label_2->setText(QCoreApplication::translate("HarddiskDialog", "Cylinders:", nullptr));
        labelFormat->setText(QCoreApplication::translate("HarddiskDialog", "Image Format:", nullptr));
        label_4->setText(QCoreApplication::translate("HarddiskDialog", "Heads:", nullptr));
        label_8->setText(QCoreApplication::translate("HarddiskDialog", "Bus:", nullptr));
        labelBlockSize->setText(QCoreApplication::translate("HarddiskDialog", "Block Size:", nullptr));
        label->setText(QCoreApplication::translate("HarddiskDialog", "File name:", nullptr));
    } // retranslateUi

};

namespace Ui {
    class HarddiskDialog: public Ui_HarddiskDialog {};
} // namespace Ui

QT_END_NAMESPACE

#endif // UI_QT_HARDDISKDIALOG_H
