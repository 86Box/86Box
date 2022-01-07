/********************************************************************************
** Form generated from reading UI file 'qt_settingsports.ui'
**
** Created by: Qt User Interface Compiler version 6.2.2
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
#include <QtWidgets/QSpacerItem>
#include <QtWidgets/QVBoxLayout>
#include <QtWidgets/QWidget>

QT_BEGIN_NAMESPACE

class Ui_SettingsPorts
{
public:
    QVBoxLayout *verticalLayout;
    QFormLayout *formLayout;
    QLabel *label;
    QComboBox *comboBoxLpt1;
    QLabel *label_2;
    QComboBox *comboBoxLpt2;
    QLabel *label_3;
    QComboBox *comboBoxLpt3;
    QGridLayout *gridLayout;
    QCheckBox *checkBoxSerial1;
    QCheckBox *checkBoxParallel1;
    QCheckBox *checkBoxSerial2;
    QCheckBox *checkBoxParallel2;
    QCheckBox *checkBoxSerial3;
    QCheckBox *checkBoxParallel3;
    QCheckBox *checkBoxSerial4;
    QSpacerItem *verticalSpacer;

    void setupUi(QWidget *SettingsPorts)
    {
        if (SettingsPorts->objectName().isEmpty())
            SettingsPorts->setObjectName(QString::fromUtf8("SettingsPorts"));
        SettingsPorts->resize(398, 341);
        verticalLayout = new QVBoxLayout(SettingsPorts);
        verticalLayout->setObjectName(QString::fromUtf8("verticalLayout"));
        verticalLayout->setContentsMargins(0, 0, 0, 0);
        formLayout = new QFormLayout();
        formLayout->setObjectName(QString::fromUtf8("formLayout"));
        label = new QLabel(SettingsPorts);
        label->setObjectName(QString::fromUtf8("label"));

        formLayout->setWidget(0, QFormLayout::LabelRole, label);

        comboBoxLpt1 = new QComboBox(SettingsPorts);
        comboBoxLpt1->setObjectName(QString::fromUtf8("comboBoxLpt1"));

        formLayout->setWidget(0, QFormLayout::FieldRole, comboBoxLpt1);

        label_2 = new QLabel(SettingsPorts);
        label_2->setObjectName(QString::fromUtf8("label_2"));

        formLayout->setWidget(1, QFormLayout::LabelRole, label_2);

        comboBoxLpt2 = new QComboBox(SettingsPorts);
        comboBoxLpt2->setObjectName(QString::fromUtf8("comboBoxLpt2"));

        formLayout->setWidget(1, QFormLayout::FieldRole, comboBoxLpt2);

        label_3 = new QLabel(SettingsPorts);
        label_3->setObjectName(QString::fromUtf8("label_3"));

        formLayout->setWidget(2, QFormLayout::LabelRole, label_3);

        comboBoxLpt3 = new QComboBox(SettingsPorts);
        comboBoxLpt3->setObjectName(QString::fromUtf8("comboBoxLpt3"));

        formLayout->setWidget(2, QFormLayout::FieldRole, comboBoxLpt3);


        verticalLayout->addLayout(formLayout);

        gridLayout = new QGridLayout();
        gridLayout->setObjectName(QString::fromUtf8("gridLayout"));
        checkBoxSerial1 = new QCheckBox(SettingsPorts);
        checkBoxSerial1->setObjectName(QString::fromUtf8("checkBoxSerial1"));

        gridLayout->addWidget(checkBoxSerial1, 0, 0, 1, 1);

        checkBoxParallel1 = new QCheckBox(SettingsPorts);
        checkBoxParallel1->setObjectName(QString::fromUtf8("checkBoxParallel1"));

        gridLayout->addWidget(checkBoxParallel1, 0, 1, 1, 1);

        checkBoxSerial2 = new QCheckBox(SettingsPorts);
        checkBoxSerial2->setObjectName(QString::fromUtf8("checkBoxSerial2"));

        gridLayout->addWidget(checkBoxSerial2, 1, 0, 1, 1);

        checkBoxParallel2 = new QCheckBox(SettingsPorts);
        checkBoxParallel2->setObjectName(QString::fromUtf8("checkBoxParallel2"));

        gridLayout->addWidget(checkBoxParallel2, 1, 1, 1, 1);

        checkBoxSerial3 = new QCheckBox(SettingsPorts);
        checkBoxSerial3->setObjectName(QString::fromUtf8("checkBoxSerial3"));

        gridLayout->addWidget(checkBoxSerial3, 2, 0, 1, 1);

        checkBoxParallel3 = new QCheckBox(SettingsPorts);
        checkBoxParallel3->setObjectName(QString::fromUtf8("checkBoxParallel3"));

        gridLayout->addWidget(checkBoxParallel3, 2, 1, 1, 1);

        checkBoxSerial4 = new QCheckBox(SettingsPorts);
        checkBoxSerial4->setObjectName(QString::fromUtf8("checkBoxSerial4"));

        gridLayout->addWidget(checkBoxSerial4, 3, 0, 1, 1);


        verticalLayout->addLayout(gridLayout);

        verticalSpacer = new QSpacerItem(20, 40, QSizePolicy::Minimum, QSizePolicy::Expanding);

        verticalLayout->addItem(verticalSpacer);


        retranslateUi(SettingsPorts);

        QMetaObject::connectSlotsByName(SettingsPorts);
    } // setupUi

    void retranslateUi(QWidget *SettingsPorts)
    {
        SettingsPorts->setWindowTitle(QCoreApplication::translate("SettingsPorts", "Form", nullptr));
        label->setText(QCoreApplication::translate("SettingsPorts", "LPT1 Device:", nullptr));
        label_2->setText(QCoreApplication::translate("SettingsPorts", "LPT2 Device:", nullptr));
        label_3->setText(QCoreApplication::translate("SettingsPorts", "LPT3 Device:", nullptr));
        checkBoxSerial1->setText(QCoreApplication::translate("SettingsPorts", "Serial port 1", nullptr));
        checkBoxParallel1->setText(QCoreApplication::translate("SettingsPorts", "Parallel port 1", nullptr));
        checkBoxSerial2->setText(QCoreApplication::translate("SettingsPorts", "Serial port 2", nullptr));
        checkBoxParallel2->setText(QCoreApplication::translate("SettingsPorts", "Parallel port 2", nullptr));
        checkBoxSerial3->setText(QCoreApplication::translate("SettingsPorts", "Serial port 3", nullptr));
        checkBoxParallel3->setText(QCoreApplication::translate("SettingsPorts", "Parallel port 3", nullptr));
        checkBoxSerial4->setText(QCoreApplication::translate("SettingsPorts", "Serial port 4", nullptr));
    } // retranslateUi

};

namespace Ui {
    class SettingsPorts: public Ui_SettingsPorts {};
} // namespace Ui

QT_END_NAMESPACE

#endif // UI_QT_SETTINGSPORTS_H
