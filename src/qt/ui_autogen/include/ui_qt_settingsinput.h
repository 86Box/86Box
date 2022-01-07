/********************************************************************************
** Form generated from reading UI file 'qt_settingsinput.ui'
**
** Created by: Qt User Interface Compiler version 6.2.2
**
** WARNING! All changes made in this file will be lost when recompiling UI file!
********************************************************************************/

#ifndef UI_QT_SETTINGSINPUT_H
#define UI_QT_SETTINGSINPUT_H

#include <QtCore/QVariant>
#include <QtWidgets/QApplication>
#include <QtWidgets/QComboBox>
#include <QtWidgets/QGridLayout>
#include <QtWidgets/QHBoxLayout>
#include <QtWidgets/QLabel>
#include <QtWidgets/QPushButton>
#include <QtWidgets/QSpacerItem>
#include <QtWidgets/QVBoxLayout>
#include <QtWidgets/QWidget>

QT_BEGIN_NAMESPACE

class Ui_SettingsInput
{
public:
    QVBoxLayout *verticalLayout;
    QWidget *widget;
    QGridLayout *gridLayout;
    QLabel *label;
    QComboBox *comboBoxMouse;
    QPushButton *pushButtonConfigureMouse;
    QLabel *label_2;
    QComboBox *comboBoxJoystick;
    QWidget *widget_2;
    QHBoxLayout *horizontalLayout;
    QPushButton *pushButtonJoystick1;
    QPushButton *pushButtonJoystick2;
    QPushButton *pushButtonJoystick3;
    QPushButton *pushButtonJoystick4;
    QSpacerItem *verticalSpacer;

    void setupUi(QWidget *SettingsInput)
    {
        if (SettingsInput->objectName().isEmpty())
            SettingsInput->setObjectName(QString::fromUtf8("SettingsInput"));
        SettingsInput->resize(400, 300);
        verticalLayout = new QVBoxLayout(SettingsInput);
        verticalLayout->setObjectName(QString::fromUtf8("verticalLayout"));
        verticalLayout->setContentsMargins(0, 0, 0, -1);
        widget = new QWidget(SettingsInput);
        widget->setObjectName(QString::fromUtf8("widget"));
        gridLayout = new QGridLayout(widget);
        gridLayout->setObjectName(QString::fromUtf8("gridLayout"));
        gridLayout->setContentsMargins(0, 0, 0, 0);
        label = new QLabel(widget);
        label->setObjectName(QString::fromUtf8("label"));

        gridLayout->addWidget(label, 0, 0, 1, 1);

        comboBoxMouse = new QComboBox(widget);
        comboBoxMouse->setObjectName(QString::fromUtf8("comboBoxMouse"));

        gridLayout->addWidget(comboBoxMouse, 0, 1, 1, 1);

        pushButtonConfigureMouse = new QPushButton(widget);
        pushButtonConfigureMouse->setObjectName(QString::fromUtf8("pushButtonConfigureMouse"));

        gridLayout->addWidget(pushButtonConfigureMouse, 0, 2, 1, 1);

        label_2 = new QLabel(widget);
        label_2->setObjectName(QString::fromUtf8("label_2"));

        gridLayout->addWidget(label_2, 1, 0, 1, 1);

        comboBoxJoystick = new QComboBox(widget);
        comboBoxJoystick->setObjectName(QString::fromUtf8("comboBoxJoystick"));

        gridLayout->addWidget(comboBoxJoystick, 1, 1, 1, 1);


        verticalLayout->addWidget(widget);

        widget_2 = new QWidget(SettingsInput);
        widget_2->setObjectName(QString::fromUtf8("widget_2"));
        horizontalLayout = new QHBoxLayout(widget_2);
        horizontalLayout->setObjectName(QString::fromUtf8("horizontalLayout"));
        horizontalLayout->setContentsMargins(0, 0, 0, 0);
        pushButtonJoystick1 = new QPushButton(widget_2);
        pushButtonJoystick1->setObjectName(QString::fromUtf8("pushButtonJoystick1"));

        horizontalLayout->addWidget(pushButtonJoystick1);

        pushButtonJoystick2 = new QPushButton(widget_2);
        pushButtonJoystick2->setObjectName(QString::fromUtf8("pushButtonJoystick2"));

        horizontalLayout->addWidget(pushButtonJoystick2);

        pushButtonJoystick3 = new QPushButton(widget_2);
        pushButtonJoystick3->setObjectName(QString::fromUtf8("pushButtonJoystick3"));

        horizontalLayout->addWidget(pushButtonJoystick3);

        pushButtonJoystick4 = new QPushButton(widget_2);
        pushButtonJoystick4->setObjectName(QString::fromUtf8("pushButtonJoystick4"));

        horizontalLayout->addWidget(pushButtonJoystick4);


        verticalLayout->addWidget(widget_2);

        verticalSpacer = new QSpacerItem(20, 40, QSizePolicy::Minimum, QSizePolicy::Expanding);

        verticalLayout->addItem(verticalSpacer);


        retranslateUi(SettingsInput);

        QMetaObject::connectSlotsByName(SettingsInput);
    } // setupUi

    void retranslateUi(QWidget *SettingsInput)
    {
        SettingsInput->setWindowTitle(QCoreApplication::translate("SettingsInput", "Form", nullptr));
        label->setText(QCoreApplication::translate("SettingsInput", "Mouse:", nullptr));
        pushButtonConfigureMouse->setText(QCoreApplication::translate("SettingsInput", "Configure", nullptr));
        label_2->setText(QCoreApplication::translate("SettingsInput", "Joystick:", nullptr));
        pushButtonJoystick1->setText(QCoreApplication::translate("SettingsInput", "Joystick 1...", nullptr));
        pushButtonJoystick2->setText(QCoreApplication::translate("SettingsInput", "Joystick 2...", nullptr));
        pushButtonJoystick3->setText(QCoreApplication::translate("SettingsInput", "Joystick 3...", nullptr));
        pushButtonJoystick4->setText(QCoreApplication::translate("SettingsInput", "Joystick 4...", nullptr));
    } // retranslateUi

};

namespace Ui {
    class SettingsInput: public Ui_SettingsInput {};
} // namespace Ui

QT_END_NAMESPACE

#endif // UI_QT_SETTINGSINPUT_H
