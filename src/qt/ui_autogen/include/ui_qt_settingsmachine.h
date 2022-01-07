/********************************************************************************
** Form generated from reading UI file 'qt_settingsmachine.ui'
**
** Created by: Qt User Interface Compiler version 6.2.2
**
** WARNING! All changes made in this file will be lost when recompiling UI file!
********************************************************************************/

#ifndef UI_QT_SETTINGSMACHINE_H
#define UI_QT_SETTINGSMACHINE_H

#include <QtCore/QVariant>
#include <QtWidgets/QApplication>
#include <QtWidgets/QCheckBox>
#include <QtWidgets/QComboBox>
#include <QtWidgets/QFormLayout>
#include <QtWidgets/QGroupBox>
#include <QtWidgets/QHBoxLayout>
#include <QtWidgets/QLabel>
#include <QtWidgets/QPushButton>
#include <QtWidgets/QRadioButton>
#include <QtWidgets/QSpacerItem>
#include <QtWidgets/QSpinBox>
#include <QtWidgets/QVBoxLayout>
#include <QtWidgets/QWidget>

QT_BEGIN_NAMESPACE

class Ui_SettingsMachine
{
public:
    QVBoxLayout *vboxLayout;
    QWidget *widget;
    QFormLayout *formLayout;
    QLabel *label;
    QComboBox *comboBoxMachineType;
    QLabel *label_2;
    QLabel *label_3;
    QLabel *label_4;
    QLabel *label_5;
    QLabel *label_6;
    QComboBox *comboBoxFPU;
    QComboBox *comboBoxWaitStates;
    QSpinBox *spinBoxRAM;
    QWidget *widget_2;
    QHBoxLayout *horizontalLayout;
    QComboBox *comboBoxCPU;
    QLabel *label_7;
    QComboBox *comboBoxSpeed;
    QWidget *widget_3;
    QHBoxLayout *horizontalLayout_2;
    QComboBox *comboBoxMachine;
    QPushButton *pushButtonConfigure;
    QCheckBox *checkBoxDynamicRecompiler;
    QSpacerItem *verticalSpacer;
    QGroupBox *groupBox;
    QVBoxLayout *verticalLayout_2;
    QRadioButton *radioButtonDisabled;
    QRadioButton *radioButtonLocalTime;
    QRadioButton *radioButtonUTC;

    void setupUi(QWidget *SettingsMachine)
    {
        if (SettingsMachine->objectName().isEmpty())
            SettingsMachine->setObjectName(QString::fromUtf8("SettingsMachine"));
        SettingsMachine->resize(458, 390);
        vboxLayout = new QVBoxLayout(SettingsMachine);
        vboxLayout->setObjectName(QString::fromUtf8("vboxLayout"));
        vboxLayout->setContentsMargins(0, 0, 0, 0);
        widget = new QWidget(SettingsMachine);
        widget->setObjectName(QString::fromUtf8("widget"));
        formLayout = new QFormLayout(widget);
        formLayout->setObjectName(QString::fromUtf8("formLayout"));
        formLayout->setContentsMargins(0, 0, 0, 0);
        label = new QLabel(widget);
        label->setObjectName(QString::fromUtf8("label"));

        formLayout->setWidget(0, QFormLayout::LabelRole, label);

        comboBoxMachineType = new QComboBox(widget);
        comboBoxMachineType->setObjectName(QString::fromUtf8("comboBoxMachineType"));

        formLayout->setWidget(0, QFormLayout::FieldRole, comboBoxMachineType);

        label_2 = new QLabel(widget);
        label_2->setObjectName(QString::fromUtf8("label_2"));

        formLayout->setWidget(2, QFormLayout::LabelRole, label_2);

        label_3 = new QLabel(widget);
        label_3->setObjectName(QString::fromUtf8("label_3"));

        formLayout->setWidget(3, QFormLayout::LabelRole, label_3);

        label_4 = new QLabel(widget);
        label_4->setObjectName(QString::fromUtf8("label_4"));

        formLayout->setWidget(4, QFormLayout::LabelRole, label_4);

        label_5 = new QLabel(widget);
        label_5->setObjectName(QString::fromUtf8("label_5"));

        formLayout->setWidget(5, QFormLayout::LabelRole, label_5);

        label_6 = new QLabel(widget);
        label_6->setObjectName(QString::fromUtf8("label_6"));

        formLayout->setWidget(6, QFormLayout::LabelRole, label_6);

        comboBoxFPU = new QComboBox(widget);
        comboBoxFPU->setObjectName(QString::fromUtf8("comboBoxFPU"));

        formLayout->setWidget(4, QFormLayout::FieldRole, comboBoxFPU);

        comboBoxWaitStates = new QComboBox(widget);
        comboBoxWaitStates->setObjectName(QString::fromUtf8("comboBoxWaitStates"));

        formLayout->setWidget(5, QFormLayout::FieldRole, comboBoxWaitStates);

        spinBoxRAM = new QSpinBox(widget);
        spinBoxRAM->setObjectName(QString::fromUtf8("spinBoxRAM"));

        formLayout->setWidget(6, QFormLayout::FieldRole, spinBoxRAM);

        widget_2 = new QWidget(widget);
        widget_2->setObjectName(QString::fromUtf8("widget_2"));
        horizontalLayout = new QHBoxLayout(widget_2);
        horizontalLayout->setObjectName(QString::fromUtf8("horizontalLayout"));
        horizontalLayout->setContentsMargins(0, 0, 0, 0);
        comboBoxCPU = new QComboBox(widget_2);
        comboBoxCPU->setObjectName(QString::fromUtf8("comboBoxCPU"));

        horizontalLayout->addWidget(comboBoxCPU);

        label_7 = new QLabel(widget_2);
        label_7->setObjectName(QString::fromUtf8("label_7"));
        label_7->setAlignment(Qt::AlignRight|Qt::AlignTrailing|Qt::AlignVCenter);

        horizontalLayout->addWidget(label_7);

        comboBoxSpeed = new QComboBox(widget_2);
        comboBoxSpeed->setObjectName(QString::fromUtf8("comboBoxSpeed"));

        horizontalLayout->addWidget(comboBoxSpeed);


        formLayout->setWidget(3, QFormLayout::FieldRole, widget_2);

        widget_3 = new QWidget(widget);
        widget_3->setObjectName(QString::fromUtf8("widget_3"));
        horizontalLayout_2 = new QHBoxLayout(widget_3);
        horizontalLayout_2->setObjectName(QString::fromUtf8("horizontalLayout_2"));
        horizontalLayout_2->setContentsMargins(0, 0, 0, 0);
        comboBoxMachine = new QComboBox(widget_3);
        comboBoxMachine->setObjectName(QString::fromUtf8("comboBoxMachine"));

        horizontalLayout_2->addWidget(comboBoxMachine);

        pushButtonConfigure = new QPushButton(widget_3);
        pushButtonConfigure->setObjectName(QString::fromUtf8("pushButtonConfigure"));
        QSizePolicy sizePolicy(QSizePolicy::Maximum, QSizePolicy::Fixed);
        sizePolicy.setHorizontalStretch(0);
        sizePolicy.setVerticalStretch(0);
        sizePolicy.setHeightForWidth(pushButtonConfigure->sizePolicy().hasHeightForWidth());
        pushButtonConfigure->setSizePolicy(sizePolicy);

        horizontalLayout_2->addWidget(pushButtonConfigure);


        formLayout->setWidget(2, QFormLayout::FieldRole, widget_3);


        vboxLayout->addWidget(widget);

        checkBoxDynamicRecompiler = new QCheckBox(SettingsMachine);
        checkBoxDynamicRecompiler->setObjectName(QString::fromUtf8("checkBoxDynamicRecompiler"));
        QSizePolicy sizePolicy1(QSizePolicy::Minimum, QSizePolicy::Fixed);
        sizePolicy1.setHorizontalStretch(2);
        sizePolicy1.setVerticalStretch(2);
        sizePolicy1.setHeightForWidth(checkBoxDynamicRecompiler->sizePolicy().hasHeightForWidth());
        checkBoxDynamicRecompiler->setSizePolicy(sizePolicy1);

        vboxLayout->addWidget(checkBoxDynamicRecompiler);

        verticalSpacer = new QSpacerItem(20, 40, QSizePolicy::Minimum, QSizePolicy::Expanding);

        vboxLayout->addItem(verticalSpacer);

        groupBox = new QGroupBox(SettingsMachine);
        groupBox->setObjectName(QString::fromUtf8("groupBox"));
        verticalLayout_2 = new QVBoxLayout(groupBox);
        verticalLayout_2->setObjectName(QString::fromUtf8("verticalLayout_2"));
        radioButtonDisabled = new QRadioButton(groupBox);
        radioButtonDisabled->setObjectName(QString::fromUtf8("radioButtonDisabled"));

        verticalLayout_2->addWidget(radioButtonDisabled);

        radioButtonLocalTime = new QRadioButton(groupBox);
        radioButtonLocalTime->setObjectName(QString::fromUtf8("radioButtonLocalTime"));

        verticalLayout_2->addWidget(radioButtonLocalTime);

        radioButtonUTC = new QRadioButton(groupBox);
        radioButtonUTC->setObjectName(QString::fromUtf8("radioButtonUTC"));

        verticalLayout_2->addWidget(radioButtonUTC);


        vboxLayout->addWidget(groupBox);


        retranslateUi(SettingsMachine);

        QMetaObject::connectSlotsByName(SettingsMachine);
    } // setupUi

    void retranslateUi(QWidget *SettingsMachine)
    {
        SettingsMachine->setWindowTitle(QCoreApplication::translate("SettingsMachine", "Form", nullptr));
        label->setText(QCoreApplication::translate("SettingsMachine", "Machine type:", nullptr));
        label_2->setText(QCoreApplication::translate("SettingsMachine", "Machine:", nullptr));
        label_3->setText(QCoreApplication::translate("SettingsMachine", "CPU type:", nullptr));
        label_4->setText(QCoreApplication::translate("SettingsMachine", "FPU:", nullptr));
        label_5->setText(QCoreApplication::translate("SettingsMachine", "Wait states:", nullptr));
        label_6->setText(QCoreApplication::translate("SettingsMachine", "Memory:", nullptr));
        label_7->setText(QCoreApplication::translate("SettingsMachine", "Speed:", nullptr));
        pushButtonConfigure->setText(QCoreApplication::translate("SettingsMachine", "Configure", nullptr));
        checkBoxDynamicRecompiler->setText(QCoreApplication::translate("SettingsMachine", "Dynamic Recompiler", nullptr));
        groupBox->setTitle(QCoreApplication::translate("SettingsMachine", "Time synchronization", nullptr));
        radioButtonDisabled->setText(QCoreApplication::translate("SettingsMachine", "Disabled", nullptr));
        radioButtonLocalTime->setText(QCoreApplication::translate("SettingsMachine", "Enabled (local time)", nullptr));
        radioButtonUTC->setText(QCoreApplication::translate("SettingsMachine", "Enabled (UTC)", nullptr));
    } // retranslateUi

};

namespace Ui {
    class SettingsMachine: public Ui_SettingsMachine {};
} // namespace Ui

QT_END_NAMESPACE

#endif // UI_QT_SETTINGSMACHINE_H
