/********************************************************************************
** Form generated from reading UI file 'qt_joystickconfiguration.ui'
**
** Created by: Qt User Interface Compiler version 5.15.10
**
** WARNING! All changes made in this file will be lost when recompiling UI file!
********************************************************************************/

#ifndef UI_QT_JOYSTICKCONFIGURATION_H
#define UI_QT_JOYSTICKCONFIGURATION_H

#include <QtCore/QVariant>
#include <QtWidgets/QApplication>
#include <QtWidgets/QComboBox>
#include <QtWidgets/QDialog>
#include <QtWidgets/QDialogButtonBox>
#include <QtWidgets/QGridLayout>
#include <QtWidgets/QLabel>
#include <QtWidgets/QSpacerItem>

QT_BEGIN_NAMESPACE

class Ui_JoystickConfiguration
{
public:
    QGridLayout *gridLayout;
    QGridLayout *ct;
    QDialogButtonBox *buttonBox;
    QComboBox *comboBoxDevice;
    QLabel *label_2;
    QSpacerItem *verticalSpacer;

    void setupUi(QDialog *JoystickConfiguration)
    {
        if (JoystickConfiguration->objectName().isEmpty())
            JoystickConfiguration->setObjectName(QString::fromUtf8("JoystickConfiguration"));
        JoystickConfiguration->resize(400, 300);
        gridLayout = new QGridLayout(JoystickConfiguration);
        gridLayout->setObjectName(QString::fromUtf8("gridLayout"));
        ct = new QGridLayout();
        ct->setObjectName(QString::fromUtf8("ct"));

        gridLayout->addLayout(ct, 1, 0, 1, 2);

        buttonBox = new QDialogButtonBox(JoystickConfiguration);
        buttonBox->setObjectName(QString::fromUtf8("buttonBox"));
        buttonBox->setStandardButtons(QDialogButtonBox::Cancel|QDialogButtonBox::Ok);

        gridLayout->addWidget(buttonBox, 3, 1, 1, 1);

        comboBoxDevice = new QComboBox(JoystickConfiguration);
        comboBoxDevice->setObjectName(QString::fromUtf8("comboBoxDevice"));
        comboBoxDevice->setMaxVisibleItems(30);

        gridLayout->addWidget(comboBoxDevice, 0, 1, 1, 1);

        label_2 = new QLabel(JoystickConfiguration);
        label_2->setObjectName(QString::fromUtf8("label_2"));

        gridLayout->addWidget(label_2, 0, 0, 1, 1);

        verticalSpacer = new QSpacerItem(20, 40, QSizePolicy::Minimum, QSizePolicy::Expanding);

        gridLayout->addItem(verticalSpacer, 2, 1, 1, 1);


        retranslateUi(JoystickConfiguration);
        QObject::connect(buttonBox, SIGNAL(accepted()), JoystickConfiguration, SLOT(accept()));
        QObject::connect(buttonBox, SIGNAL(rejected()), JoystickConfiguration, SLOT(reject()));

        QMetaObject::connectSlotsByName(JoystickConfiguration);
    } // setupUi

    void retranslateUi(QDialog *JoystickConfiguration)
    {
        JoystickConfiguration->setWindowTitle(QCoreApplication::translate("JoystickConfiguration", "Joystick configuration", nullptr));
        label_2->setText(QCoreApplication::translate("JoystickConfiguration", "Device", nullptr));
    } // retranslateUi

};

namespace Ui {
    class JoystickConfiguration: public Ui_JoystickConfiguration {};
} // namespace Ui

QT_END_NAMESPACE

#endif // UI_QT_JOYSTICKCONFIGURATION_H
