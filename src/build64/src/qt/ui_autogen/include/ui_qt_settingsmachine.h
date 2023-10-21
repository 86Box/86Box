/********************************************************************************
** Form generated from reading UI file 'qt_settingsmachine.ui'
**
** Created by: Qt User Interface Compiler version 5.15.10
**
** WARNING! All changes made in this file will be lost when recompiling UI file!
********************************************************************************/

#ifndef UI_QT_SETTINGSMACHINE_H
#define UI_QT_SETTINGSMACHINE_H

#include <QtCore/QVariant>
#include <QtWidgets/QApplication>
#include <QtWidgets/QCheckBox>
#include <QtWidgets/QComboBox>
#include <QtWidgets/QGridLayout>
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
    QGridLayout *gridLayout;
    QLabel *label_5;
    QComboBox *comboBoxMachineType;
    QLabel *label_6;
    QComboBox *comboBoxFPU;
    QWidget *widget_4;
    QHBoxLayout *horizontalLayout_3;
    QComboBox *comboBoxWaitStates;
    QLabel *label_8;
    QComboBox *comboBoxPitMode;
    QLabel *label_3;
    QWidget *widget_2;
    QHBoxLayout *horizontalLayout;
    QComboBox *comboBoxCPU;
    QLabel *label_7;
    QComboBox *comboBoxSpeed;
    QSpinBox *spinBoxRAM;
    QWidget *widget_3;
    QHBoxLayout *horizontalLayout_2;
    QComboBox *comboBoxMachine;
    QPushButton *pushButtonConfigure;
    QLabel *label;
    QLabel *label_2;
    QLabel *label_4;
    QCheckBox *checkBoxDynamicRecompiler;
    QCheckBox *checkBoxFPUSoftfloat;
    QGroupBox *groupBox;
    QVBoxLayout *verticalLayout_2;
    QRadioButton *radioButtonDisabled;
    QRadioButton *radioButtonLocalTime;
    QRadioButton *radioButtonUTC;
    QSpacerItem *verticalSpacer;

    void setupUi(QWidget *SettingsMachine)
    {
        if (SettingsMachine->objectName().isEmpty())
            SettingsMachine->setObjectName(QString::fromUtf8("SettingsMachine"));
        SettingsMachine->resize(458, 434);
        vboxLayout = new QVBoxLayout(SettingsMachine);
        vboxLayout->setObjectName(QString::fromUtf8("vboxLayout"));
        vboxLayout->setContentsMargins(0, 0, 0, 0);
        widget = new QWidget(SettingsMachine);
        widget->setObjectName(QString::fromUtf8("widget"));
        gridLayout = new QGridLayout(widget);
        gridLayout->setObjectName(QString::fromUtf8("gridLayout"));
        gridLayout->setContentsMargins(0, 0, 0, 0);
        label_5 = new QLabel(widget);
        label_5->setObjectName(QString::fromUtf8("label_5"));

        gridLayout->addWidget(label_5, 4, 0, 1, 1);

        comboBoxMachineType = new QComboBox(widget);
        comboBoxMachineType->setObjectName(QString::fromUtf8("comboBoxMachineType"));
        comboBoxMachineType->setMaxVisibleItems(30);

        gridLayout->addWidget(comboBoxMachineType, 0, 1, 1, 1);

        label_6 = new QLabel(widget);
        label_6->setObjectName(QString::fromUtf8("label_6"));

        gridLayout->addWidget(label_6, 5, 0, 1, 1);

        comboBoxFPU = new QComboBox(widget);
        comboBoxFPU->setObjectName(QString::fromUtf8("comboBoxFPU"));
        comboBoxFPU->setMaxVisibleItems(30);

        gridLayout->addWidget(comboBoxFPU, 3, 1, 1, 1);

        widget_4 = new QWidget(widget);
        widget_4->setObjectName(QString::fromUtf8("widget_4"));
        horizontalLayout_3 = new QHBoxLayout(widget_4);
        horizontalLayout_3->setObjectName(QString::fromUtf8("horizontalLayout_3"));
        horizontalLayout_3->setContentsMargins(0, 0, 0, 0);
        comboBoxWaitStates = new QComboBox(widget_4);
        comboBoxWaitStates->setObjectName(QString::fromUtf8("comboBoxWaitStates"));
        comboBoxWaitStates->setMaxVisibleItems(30);
        QSizePolicy sizePolicy(QSizePolicy::MinimumExpanding, QSizePolicy::Fixed);
        sizePolicy.setHorizontalStretch(0);
        sizePolicy.setVerticalStretch(0);
        sizePolicy.setHeightForWidth(comboBoxWaitStates->sizePolicy().hasHeightForWidth());
        comboBoxWaitStates->setSizePolicy(sizePolicy);

        horizontalLayout_3->addWidget(comboBoxWaitStates);

        label_8 = new QLabel(widget_4);
        label_8->setObjectName(QString::fromUtf8("label_8"));

        horizontalLayout_3->addWidget(label_8);

        comboBoxPitMode = new QComboBox(widget_4);
        comboBoxPitMode->setObjectName(QString::fromUtf8("comboBoxPitMode"));
        comboBoxPitMode->setMaxVisibleItems(30);
        QSizePolicy sizePolicy1(QSizePolicy::Expanding, QSizePolicy::Fixed);
        sizePolicy1.setHorizontalStretch(0);
        sizePolicy1.setVerticalStretch(0);
        sizePolicy1.setHeightForWidth(comboBoxPitMode->sizePolicy().hasHeightForWidth());
        comboBoxPitMode->setSizePolicy(sizePolicy1);

        horizontalLayout_3->addWidget(comboBoxPitMode);


        gridLayout->addWidget(widget_4, 4, 1, 1, 1);

        label_3 = new QLabel(widget);
        label_3->setObjectName(QString::fromUtf8("label_3"));

        gridLayout->addWidget(label_3, 2, 0, 1, 1);

        widget_2 = new QWidget(widget);
        widget_2->setObjectName(QString::fromUtf8("widget_2"));
        horizontalLayout = new QHBoxLayout(widget_2);
        horizontalLayout->setObjectName(QString::fromUtf8("horizontalLayout"));
        horizontalLayout->setContentsMargins(0, 0, 0, 0);
        comboBoxCPU = new QComboBox(widget_2);
        comboBoxCPU->setObjectName(QString::fromUtf8("comboBoxCPU"));
        comboBoxCPU->setMaxVisibleItems(30);
        sizePolicy.setHeightForWidth(comboBoxCPU->sizePolicy().hasHeightForWidth());
        comboBoxCPU->setSizePolicy(sizePolicy);

        horizontalLayout->addWidget(comboBoxCPU);

        label_7 = new QLabel(widget_2);
        label_7->setObjectName(QString::fromUtf8("label_7"));
        label_7->setAlignment(Qt::AlignLeading|Qt::AlignLeft|Qt::AlignVCenter);

        horizontalLayout->addWidget(label_7);

        comboBoxSpeed = new QComboBox(widget_2);
        comboBoxSpeed->setObjectName(QString::fromUtf8("comboBoxSpeed"));
        comboBoxSpeed->setMaxVisibleItems(30);
        sizePolicy1.setHeightForWidth(comboBoxSpeed->sizePolicy().hasHeightForWidth());
        comboBoxSpeed->setSizePolicy(sizePolicy1);

        horizontalLayout->addWidget(comboBoxSpeed);


        gridLayout->addWidget(widget_2, 2, 1, 1, 1);

        spinBoxRAM = new QSpinBox(widget);
        spinBoxRAM->setObjectName(QString::fromUtf8("spinBoxRAM"));
        QSizePolicy sizePolicy2(QSizePolicy::Maximum, QSizePolicy::Fixed);
        sizePolicy2.setHorizontalStretch(0);
        sizePolicy2.setVerticalStretch(0);
        sizePolicy2.setHeightForWidth(spinBoxRAM->sizePolicy().hasHeightForWidth());
        spinBoxRAM->setSizePolicy(sizePolicy2);

        gridLayout->addWidget(spinBoxRAM, 5, 1, 1, 1);

        widget_3 = new QWidget(widget);
        widget_3->setObjectName(QString::fromUtf8("widget_3"));
        horizontalLayout_2 = new QHBoxLayout(widget_3);
        horizontalLayout_2->setObjectName(QString::fromUtf8("horizontalLayout_2"));
        horizontalLayout_2->setContentsMargins(0, 0, 0, 0);
        comboBoxMachine = new QComboBox(widget_3);
        comboBoxMachine->setObjectName(QString::fromUtf8("comboBoxMachine"));
        comboBoxMachine->setMaxVisibleItems(30);

        horizontalLayout_2->addWidget(comboBoxMachine);

        pushButtonConfigure = new QPushButton(widget_3);
        pushButtonConfigure->setObjectName(QString::fromUtf8("pushButtonConfigure"));
        sizePolicy2.setHeightForWidth(pushButtonConfigure->sizePolicy().hasHeightForWidth());
        pushButtonConfigure->setSizePolicy(sizePolicy2);

        horizontalLayout_2->addWidget(pushButtonConfigure);


        gridLayout->addWidget(widget_3, 1, 1, 1, 1);

        label = new QLabel(widget);
        label->setObjectName(QString::fromUtf8("label"));

        gridLayout->addWidget(label, 0, 0, 1, 1);

        label_2 = new QLabel(widget);
        label_2->setObjectName(QString::fromUtf8("label_2"));

        gridLayout->addWidget(label_2, 1, 0, 1, 1);

        label_4 = new QLabel(widget);
        label_4->setObjectName(QString::fromUtf8("label_4"));

        gridLayout->addWidget(label_4, 3, 0, 1, 1);


        vboxLayout->addWidget(widget);

        checkBoxDynamicRecompiler = new QCheckBox(SettingsMachine);
        checkBoxDynamicRecompiler->setObjectName(QString::fromUtf8("checkBoxDynamicRecompiler"));
        QSizePolicy sizePolicy3(QSizePolicy::Minimum, QSizePolicy::Fixed);
        sizePolicy3.setHorizontalStretch(2);
        sizePolicy3.setVerticalStretch(2);
        sizePolicy3.setHeightForWidth(checkBoxDynamicRecompiler->sizePolicy().hasHeightForWidth());
        checkBoxDynamicRecompiler->setSizePolicy(sizePolicy3);

        vboxLayout->addWidget(checkBoxDynamicRecompiler);

        checkBoxFPUSoftfloat = new QCheckBox(SettingsMachine);
        checkBoxFPUSoftfloat->setObjectName(QString::fromUtf8("checkBoxFPUSoftfloat"));
        QSizePolicy sizePolicy4(QSizePolicy::Minimum, QSizePolicy::Fixed);
        sizePolicy4.setHorizontalStretch(3);
        sizePolicy4.setVerticalStretch(3);
        sizePolicy4.setHeightForWidth(checkBoxFPUSoftfloat->sizePolicy().hasHeightForWidth());
        checkBoxFPUSoftfloat->setSizePolicy(sizePolicy4);

        vboxLayout->addWidget(checkBoxFPUSoftfloat);

        groupBox = new QGroupBox(SettingsMachine);
        groupBox->setObjectName(QString::fromUtf8("groupBox"));
        QSizePolicy sizePolicy5(QSizePolicy::Maximum, QSizePolicy::Preferred);
        sizePolicy5.setHorizontalStretch(0);
        sizePolicy5.setVerticalStretch(0);
        sizePolicy5.setHeightForWidth(groupBox->sizePolicy().hasHeightForWidth());
        groupBox->setSizePolicy(sizePolicy5);
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

        verticalSpacer = new QSpacerItem(20, 40, QSizePolicy::Minimum, QSizePolicy::Expanding);

        vboxLayout->addItem(verticalSpacer);


        retranslateUi(SettingsMachine);

        QMetaObject::connectSlotsByName(SettingsMachine);
    } // setupUi

    void retranslateUi(QWidget *SettingsMachine)
    {
        SettingsMachine->setWindowTitle(QCoreApplication::translate("SettingsMachine", "Form", nullptr));
        label_5->setText(QCoreApplication::translate("SettingsMachine", "Wait states:", nullptr));
        label_6->setText(QCoreApplication::translate("SettingsMachine", "Memory:", nullptr));
        label_8->setText(QCoreApplication::translate("SettingsMachine", "PIT Mode:", nullptr));
        label_3->setText(QCoreApplication::translate("SettingsMachine", "CPU type:", nullptr));
        label_7->setText(QCoreApplication::translate("SettingsMachine", "Speed:", nullptr));
        pushButtonConfigure->setText(QCoreApplication::translate("SettingsMachine", "Configure", nullptr));
        label->setText(QCoreApplication::translate("SettingsMachine", "Machine type:", nullptr));
        label_2->setText(QCoreApplication::translate("SettingsMachine", "Machine:", nullptr));
        label_4->setText(QCoreApplication::translate("SettingsMachine", "FPU:", nullptr));
        checkBoxDynamicRecompiler->setText(QCoreApplication::translate("SettingsMachine", "Dynamic Recompiler", nullptr));
        checkBoxFPUSoftfloat->setText(QCoreApplication::translate("SettingsMachine", "Softfloat FPU", nullptr));
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
