/********************************************************************************
** Form generated from reading UI file 'qt_settingsinput.ui'
**
** Created by: Qt User Interface Compiler version 5.15.10
**
** WARNING! All changes made in this file will be lost when recompiling UI file!
********************************************************************************/

#ifndef UI_QT_SETTINGSINPUT_H
#define UI_QT_SETTINGSINPUT_H

#include <QtCore/QVariant>
#include <QtWidgets/QApplication>
#include <QtWidgets/QComboBox>
#include <QtWidgets/QGridLayout>
#include <QtWidgets/QLabel>
#include <QtWidgets/QPushButton>
#include <QtWidgets/QSpacerItem>
#include <QtWidgets/QWidget>

QT_BEGIN_NAMESPACE

class Ui_SettingsInput
{
public:
    QGridLayout *gridLayout;
    QPushButton *pushButtonJoystick2;
    QLabel *label_2;
    QPushButton *pushButtonJoystick4;
    QLabel *label;
    QPushButton *pushButtonJoystick3;
    QSpacerItem *verticalSpacer;
    QPushButton *pushButtonJoystick1;
    QPushButton *pushButtonConfigureMouse;
    QComboBox *comboBoxMouse;
    QComboBox *comboBoxJoystick;

    void setupUi(QWidget *SettingsInput)
    {
        if (SettingsInput->objectName().isEmpty())
            SettingsInput->setObjectName(QString::fromUtf8("SettingsInput"));
        SettingsInput->resize(400, 300);
        gridLayout = new QGridLayout(SettingsInput);
        gridLayout->setObjectName(QString::fromUtf8("gridLayout"));
        gridLayout->setContentsMargins(0, 0, 0, -1);
        pushButtonJoystick2 = new QPushButton(SettingsInput);
        pushButtonJoystick2->setObjectName(QString::fromUtf8("pushButtonJoystick2"));

        gridLayout->addWidget(pushButtonJoystick2, 2, 1, 1, 1);

        label_2 = new QLabel(SettingsInput);
        label_2->setObjectName(QString::fromUtf8("label_2"));

        gridLayout->addWidget(label_2, 1, 0, 1, 1);

        pushButtonJoystick4 = new QPushButton(SettingsInput);
        pushButtonJoystick4->setObjectName(QString::fromUtf8("pushButtonJoystick4"));

        gridLayout->addWidget(pushButtonJoystick4, 2, 3, 1, 1);

        label = new QLabel(SettingsInput);
        label->setObjectName(QString::fromUtf8("label"));

        gridLayout->addWidget(label, 0, 0, 1, 1);

        pushButtonJoystick3 = new QPushButton(SettingsInput);
        pushButtonJoystick3->setObjectName(QString::fromUtf8("pushButtonJoystick3"));

        gridLayout->addWidget(pushButtonJoystick3, 2, 2, 1, 1);

        verticalSpacer = new QSpacerItem(20, 40, QSizePolicy::Minimum, QSizePolicy::Expanding);

        gridLayout->addItem(verticalSpacer, 3, 0, 1, 1);

        pushButtonJoystick1 = new QPushButton(SettingsInput);
        pushButtonJoystick1->setObjectName(QString::fromUtf8("pushButtonJoystick1"));

        gridLayout->addWidget(pushButtonJoystick1, 2, 0, 1, 1);

        pushButtonConfigureMouse = new QPushButton(SettingsInput);
        pushButtonConfigureMouse->setObjectName(QString::fromUtf8("pushButtonConfigureMouse"));
        QSizePolicy sizePolicy(QSizePolicy::Minimum, QSizePolicy::Fixed);
        sizePolicy.setHorizontalStretch(0);
        sizePolicy.setVerticalStretch(0);
        sizePolicy.setHeightForWidth(pushButtonConfigureMouse->sizePolicy().hasHeightForWidth());
        pushButtonConfigureMouse->setSizePolicy(sizePolicy);

        gridLayout->addWidget(pushButtonConfigureMouse, 0, 3, 1, 1);

        comboBoxMouse = new QComboBox(SettingsInput);
        comboBoxMouse->setObjectName(QString::fromUtf8("comboBoxMouse"));
        comboBoxMouse->setMaxVisibleItems(30);
        QSizePolicy sizePolicy1(QSizePolicy::Expanding, QSizePolicy::Fixed);
        sizePolicy1.setHorizontalStretch(0);
        sizePolicy1.setVerticalStretch(0);
        sizePolicy1.setHeightForWidth(comboBoxMouse->sizePolicy().hasHeightForWidth());
        comboBoxMouse->setSizePolicy(sizePolicy1);

        gridLayout->addWidget(comboBoxMouse, 0, 1, 1, 2);

        comboBoxJoystick = new QComboBox(SettingsInput);
        comboBoxJoystick->setObjectName(QString::fromUtf8("comboBoxJoystick"));
        comboBoxJoystick->setMaxVisibleItems(30);

        gridLayout->addWidget(comboBoxJoystick, 1, 1, 1, 2);


        retranslateUi(SettingsInput);

        QMetaObject::connectSlotsByName(SettingsInput);
    } // setupUi

    void retranslateUi(QWidget *SettingsInput)
    {
        SettingsInput->setWindowTitle(QCoreApplication::translate("SettingsInput", "Form", nullptr));
        pushButtonJoystick2->setText(QCoreApplication::translate("SettingsInput", "Joystick 2...", nullptr));
        label_2->setText(QCoreApplication::translate("SettingsInput", "Joystick:", nullptr));
        pushButtonJoystick4->setText(QCoreApplication::translate("SettingsInput", "Joystick 4...", nullptr));
        label->setText(QCoreApplication::translate("SettingsInput", "Mouse:", nullptr));
        pushButtonJoystick3->setText(QCoreApplication::translate("SettingsInput", "Joystick 3...", nullptr));
        pushButtonJoystick1->setText(QCoreApplication::translate("SettingsInput", "Joystick 1...", nullptr));
        pushButtonConfigureMouse->setText(QCoreApplication::translate("SettingsInput", "Configure", nullptr));
    } // retranslateUi

};

namespace Ui {
    class SettingsInput: public Ui_SettingsInput {};
} // namespace Ui

QT_END_NAMESPACE

#endif // UI_QT_SETTINGSINPUT_H
