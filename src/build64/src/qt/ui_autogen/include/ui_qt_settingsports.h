/********************************************************************************
** Form generated from reading UI file 'qt_settingsports.ui'
**
** Created by: Qt User Interface Compiler version 5.15.10
**
** WARNING! All changes made in this file will be lost when recompiling UI file!
********************************************************************************/

#ifndef UI_QT_SETTINGSPORTS_H
#define UI_QT_SETTINGSPORTS_H

#include <QtCore/QVariant>
#include <QtWidgets/QApplication>
#include <QtWidgets/QCheckBox>
#include <QtWidgets/QComboBox>
#include <QtWidgets/QFormLayout>
#include <QtWidgets/QGridLayout>
#include <QtWidgets/QLabel>
#include <QtWidgets/QPushButton>
#include <QtWidgets/QSpacerItem>
#include <QtWidgets/QWidget>

QT_BEGIN_NAMESPACE

class Ui_SettingsPorts
{
public:
    QGridLayout *gridLayout_4;
    QFormLayout *formLayout;
    QLabel *label;
    QComboBox *comboBoxLpt1;
    QLabel *label_2;
    QComboBox *comboBoxLpt2;
    QLabel *label_3;
    QComboBox *comboBoxLpt3;
    QLabel *label_4;
    QComboBox *comboBoxLpt4;
    QGridLayout *gridLayout;
    QCheckBox *checkBoxParallel2;
    QCheckBox *checkBoxParallel3;
    QCheckBox *checkBoxSerial3;
    QCheckBox *checkBoxSerial1;
    QCheckBox *checkBoxParallel4;
    QCheckBox *checkBoxSerial2;
    QCheckBox *checkBoxParallel1;
    QCheckBox *checkBoxSerial4;
    QGridLayout *gridLayout_5;
    QSpacerItem *horizontalSpacer;
    QCheckBox *checkBoxSerialPassThru3;
    QSpacerItem *verticalSpacer;
    QPushButton *pushButtonSerialPassThru1;
    QCheckBox *checkBoxSerialPassThru1;
    QCheckBox *checkBoxSerialPassThru2;
    QCheckBox *checkBoxSerialPassThru4;
    QPushButton *pushButtonSerialPassThru2;
    QPushButton *pushButtonSerialPassThru3;
    QPushButton *pushButtonSerialPassThru4;

    void setupUi(QWidget *SettingsPorts)
    {
        if (SettingsPorts->objectName().isEmpty())
            SettingsPorts->setObjectName(QString::fromUtf8("SettingsPorts"));
        SettingsPorts->resize(398, 341);
        gridLayout_4 = new QGridLayout(SettingsPorts);
        gridLayout_4->setObjectName(QString::fromUtf8("gridLayout_4"));
        gridLayout_4->setContentsMargins(0, 0, 0, 0);
        formLayout = new QFormLayout();
        formLayout->setObjectName(QString::fromUtf8("formLayout"));
        label = new QLabel(SettingsPorts);
        label->setObjectName(QString::fromUtf8("label"));

        formLayout->setWidget(0, QFormLayout::LabelRole, label);

        comboBoxLpt1 = new QComboBox(SettingsPorts);
        comboBoxLpt1->setObjectName(QString::fromUtf8("comboBoxLpt1"));
        comboBoxLpt1->setMaxVisibleItems(30);

        formLayout->setWidget(0, QFormLayout::FieldRole, comboBoxLpt1);

        label_2 = new QLabel(SettingsPorts);
        label_2->setObjectName(QString::fromUtf8("label_2"));

        formLayout->setWidget(1, QFormLayout::LabelRole, label_2);

        comboBoxLpt2 = new QComboBox(SettingsPorts);
        comboBoxLpt2->setObjectName(QString::fromUtf8("comboBoxLpt2"));
        comboBoxLpt2->setMaxVisibleItems(30);

        formLayout->setWidget(1, QFormLayout::FieldRole, comboBoxLpt2);

        label_3 = new QLabel(SettingsPorts);
        label_3->setObjectName(QString::fromUtf8("label_3"));

        formLayout->setWidget(2, QFormLayout::LabelRole, label_3);

        comboBoxLpt3 = new QComboBox(SettingsPorts);
        comboBoxLpt3->setObjectName(QString::fromUtf8("comboBoxLpt3"));
        comboBoxLpt3->setMaxVisibleItems(30);

        formLayout->setWidget(2, QFormLayout::FieldRole, comboBoxLpt3);

        label_4 = new QLabel(SettingsPorts);
        label_4->setObjectName(QString::fromUtf8("label_4"));

        formLayout->setWidget(3, QFormLayout::LabelRole, label_4);

        comboBoxLpt4 = new QComboBox(SettingsPorts);
        comboBoxLpt4->setObjectName(QString::fromUtf8("comboBoxLpt4"));
        comboBoxLpt4->setMaxVisibleItems(30);

        formLayout->setWidget(3, QFormLayout::FieldRole, comboBoxLpt4);


        gridLayout_4->addLayout(formLayout, 0, 0, 1, 1);

        gridLayout = new QGridLayout();
        gridLayout->setObjectName(QString::fromUtf8("gridLayout"));
        checkBoxParallel2 = new QCheckBox(SettingsPorts);
        checkBoxParallel2->setObjectName(QString::fromUtf8("checkBoxParallel2"));

        gridLayout->addWidget(checkBoxParallel2, 1, 1, 1, 1);

        checkBoxParallel3 = new QCheckBox(SettingsPorts);
        checkBoxParallel3->setObjectName(QString::fromUtf8("checkBoxParallel3"));

        gridLayout->addWidget(checkBoxParallel3, 2, 1, 1, 1);

        checkBoxSerial3 = new QCheckBox(SettingsPorts);
        checkBoxSerial3->setObjectName(QString::fromUtf8("checkBoxSerial3"));

        gridLayout->addWidget(checkBoxSerial3, 2, 0, 1, 1);

        checkBoxSerial1 = new QCheckBox(SettingsPorts);
        checkBoxSerial1->setObjectName(QString::fromUtf8("checkBoxSerial1"));

        gridLayout->addWidget(checkBoxSerial1, 0, 0, 1, 1);

        checkBoxParallel4 = new QCheckBox(SettingsPorts);
        checkBoxParallel4->setObjectName(QString::fromUtf8("checkBoxParallel4"));

        gridLayout->addWidget(checkBoxParallel4, 3, 1, 1, 1);

        checkBoxSerial2 = new QCheckBox(SettingsPorts);
        checkBoxSerial2->setObjectName(QString::fromUtf8("checkBoxSerial2"));

        gridLayout->addWidget(checkBoxSerial2, 1, 0, 1, 1);

        checkBoxParallel1 = new QCheckBox(SettingsPorts);
        checkBoxParallel1->setObjectName(QString::fromUtf8("checkBoxParallel1"));

        gridLayout->addWidget(checkBoxParallel1, 0, 1, 1, 1);

        checkBoxSerial4 = new QCheckBox(SettingsPorts);
        checkBoxSerial4->setObjectName(QString::fromUtf8("checkBoxSerial4"));

        gridLayout->addWidget(checkBoxSerial4, 3, 0, 1, 1);


        gridLayout_4->addLayout(gridLayout, 1, 0, 1, 1);

        gridLayout_5 = new QGridLayout();
        gridLayout_5->setObjectName(QString::fromUtf8("gridLayout_5"));
        gridLayout_5->setSizeConstraint(QLayout::SetDefaultConstraint);
        horizontalSpacer = new QSpacerItem(40, 20, QSizePolicy::Expanding, QSizePolicy::Minimum);

        gridLayout_5->addItem(horizontalSpacer, 4, 0, 1, 1);

        checkBoxSerialPassThru3 = new QCheckBox(SettingsPorts);
        checkBoxSerialPassThru3->setObjectName(QString::fromUtf8("checkBoxSerialPassThru3"));

        gridLayout_5->addWidget(checkBoxSerialPassThru3, 2, 0, 1, 1);

        verticalSpacer = new QSpacerItem(20, 40, QSizePolicy::Minimum, QSizePolicy::Expanding);

        gridLayout_5->addItem(verticalSpacer, 5, 0, 1, 2);

        pushButtonSerialPassThru1 = new QPushButton(SettingsPorts);
        pushButtonSerialPassThru1->setObjectName(QString::fromUtf8("pushButtonSerialPassThru1"));

        gridLayout_5->addWidget(pushButtonSerialPassThru1, 0, 1, 1, 1);

        checkBoxSerialPassThru1 = new QCheckBox(SettingsPorts);
        checkBoxSerialPassThru1->setObjectName(QString::fromUtf8("checkBoxSerialPassThru1"));

        gridLayout_5->addWidget(checkBoxSerialPassThru1, 0, 0, 1, 1);

        checkBoxSerialPassThru2 = new QCheckBox(SettingsPorts);
        checkBoxSerialPassThru2->setObjectName(QString::fromUtf8("checkBoxSerialPassThru2"));

        gridLayout_5->addWidget(checkBoxSerialPassThru2, 1, 0, 1, 1);

        checkBoxSerialPassThru4 = new QCheckBox(SettingsPorts);
        checkBoxSerialPassThru4->setObjectName(QString::fromUtf8("checkBoxSerialPassThru4"));

        gridLayout_5->addWidget(checkBoxSerialPassThru4, 3, 0, 1, 1);

        pushButtonSerialPassThru2 = new QPushButton(SettingsPorts);
        pushButtonSerialPassThru2->setObjectName(QString::fromUtf8("pushButtonSerialPassThru2"));

        gridLayout_5->addWidget(pushButtonSerialPassThru2, 1, 1, 1, 1);

        pushButtonSerialPassThru3 = new QPushButton(SettingsPorts);
        pushButtonSerialPassThru3->setObjectName(QString::fromUtf8("pushButtonSerialPassThru3"));

        gridLayout_5->addWidget(pushButtonSerialPassThru3, 2, 1, 1, 1);

        pushButtonSerialPassThru4 = new QPushButton(SettingsPorts);
        pushButtonSerialPassThru4->setObjectName(QString::fromUtf8("pushButtonSerialPassThru4"));

        gridLayout_5->addWidget(pushButtonSerialPassThru4, 3, 1, 1, 1);


        gridLayout_4->addLayout(gridLayout_5, 3, 0, 1, 1);


        retranslateUi(SettingsPorts);

        QMetaObject::connectSlotsByName(SettingsPorts);
    } // setupUi

    void retranslateUi(QWidget *SettingsPorts)
    {
        SettingsPorts->setWindowTitle(QCoreApplication::translate("SettingsPorts", "Form", nullptr));
        label->setText(QCoreApplication::translate("SettingsPorts", "LPT1 Device:", nullptr));
        label_2->setText(QCoreApplication::translate("SettingsPorts", "LPT2 Device:", nullptr));
        label_3->setText(QCoreApplication::translate("SettingsPorts", "LPT3 Device:", nullptr));
        label_4->setText(QCoreApplication::translate("SettingsPorts", "LPT4 Device:", nullptr));
        checkBoxParallel2->setText(QCoreApplication::translate("SettingsPorts", "Parallel port 2", nullptr));
        checkBoxParallel3->setText(QCoreApplication::translate("SettingsPorts", "Parallel port 3", nullptr));
        checkBoxSerial3->setText(QCoreApplication::translate("SettingsPorts", "Serial port 3", nullptr));
        checkBoxSerial1->setText(QCoreApplication::translate("SettingsPorts", "Serial port 1", nullptr));
        checkBoxParallel4->setText(QCoreApplication::translate("SettingsPorts", "Parallel port 4", nullptr));
        checkBoxSerial2->setText(QCoreApplication::translate("SettingsPorts", "Serial port 2", nullptr));
        checkBoxParallel1->setText(QCoreApplication::translate("SettingsPorts", "Parallel port 1", nullptr));
        checkBoxSerial4->setText(QCoreApplication::translate("SettingsPorts", "Serial port 4", nullptr));
        checkBoxSerialPassThru3->setText(QCoreApplication::translate("SettingsPorts", "Serial port passthrough 3", nullptr));
        pushButtonSerialPassThru1->setText(QCoreApplication::translate("SettingsPorts", "Configure", nullptr));
        checkBoxSerialPassThru1->setText(QCoreApplication::translate("SettingsPorts", "Serial port passthrough 1", nullptr));
        checkBoxSerialPassThru2->setText(QCoreApplication::translate("SettingsPorts", "Serial port passthrough 2", nullptr));
        checkBoxSerialPassThru4->setText(QCoreApplication::translate("SettingsPorts", "Serial port passthrough 4", nullptr));
        pushButtonSerialPassThru2->setText(QCoreApplication::translate("SettingsPorts", "Configure", nullptr));
        pushButtonSerialPassThru3->setText(QCoreApplication::translate("SettingsPorts", "Configure", nullptr));
        pushButtonSerialPassThru4->setText(QCoreApplication::translate("SettingsPorts", "Configure", nullptr));
    } // retranslateUi

};

namespace Ui {
    class SettingsPorts: public Ui_SettingsPorts {};
} // namespace Ui

QT_END_NAMESPACE

#endif // UI_QT_SETTINGSPORTS_H
