/********************************************************************************
** Form generated from reading UI file 'qt_settingssound.ui'
**
** Created by: Qt User Interface Compiler version 6.2.2
**
** WARNING! All changes made in this file will be lost when recompiling UI file!
********************************************************************************/

#ifndef UI_QT_SETTINGSSOUND_H
#define UI_QT_SETTINGSSOUND_H

#include <QtCore/QVariant>
#include <QtWidgets/QApplication>
#include <QtWidgets/QCheckBox>
#include <QtWidgets/QComboBox>
#include <QtWidgets/QGridLayout>
#include <QtWidgets/QLabel>
#include <QtWidgets/QPushButton>
#include <QtWidgets/QSpacerItem>
#include <QtWidgets/QWidget>

QT_BEGIN_NAMESPACE

class Ui_SettingsSound
{
public:
    QGridLayout *gridLayout;
    QLabel *label_3;
    QCheckBox *checkBoxSSI2001;
    QCheckBox *checkBoxGUS;
    QLabel *label;
    QPushButton *pushButtonConfigureSoundCard;
    QComboBox *comboBoxMidiIn;
    QLabel *label_2;
    QCheckBox *checkBoxMPU401;
    QPushButton *pushButtonConfigureMPU401;
    QPushButton *pushButtonConfigureMidiIn;
    QPushButton *pushButtonConfigureSSI2001;
    QCheckBox *checkBoxCMS;
    QComboBox *comboBoxSoundCard;
    QComboBox *comboBoxMidiOut;
    QPushButton *pushButtonConfigureMidiOut;
    QCheckBox *checkBoxFloat32;
    QPushButton *pushButtonConfigureCMS;
    QPushButton *pushButtonConfigureGUS;
    QSpacerItem *verticalSpacer;

    void setupUi(QWidget *SettingsSound)
    {
        if (SettingsSound->objectName().isEmpty())
            SettingsSound->setObjectName(QString::fromUtf8("SettingsSound"));
        SettingsSound->resize(387, 332);
        gridLayout = new QGridLayout(SettingsSound);
        gridLayout->setObjectName(QString::fromUtf8("gridLayout"));
        gridLayout->setContentsMargins(0, 0, 0, 0);
        label_3 = new QLabel(SettingsSound);
        label_3->setObjectName(QString::fromUtf8("label_3"));

        gridLayout->addWidget(label_3, 2, 0, 1, 1);

        checkBoxSSI2001 = new QCheckBox(SettingsSound);
        checkBoxSSI2001->setObjectName(QString::fromUtf8("checkBoxSSI2001"));

        gridLayout->addWidget(checkBoxSSI2001, 4, 0, 1, 2);

        checkBoxGUS = new QCheckBox(SettingsSound);
        checkBoxGUS->setObjectName(QString::fromUtf8("checkBoxGUS"));

        gridLayout->addWidget(checkBoxGUS, 6, 0, 1, 1);

        label = new QLabel(SettingsSound);
        label->setObjectName(QString::fromUtf8("label"));

        gridLayout->addWidget(label, 0, 0, 1, 1);

        pushButtonConfigureSoundCard = new QPushButton(SettingsSound);
        pushButtonConfigureSoundCard->setObjectName(QString::fromUtf8("pushButtonConfigureSoundCard"));

        gridLayout->addWidget(pushButtonConfigureSoundCard, 0, 3, 1, 1);

        comboBoxMidiIn = new QComboBox(SettingsSound);
        comboBoxMidiIn->setObjectName(QString::fromUtf8("comboBoxMidiIn"));

        gridLayout->addWidget(comboBoxMidiIn, 2, 1, 1, 1);

        label_2 = new QLabel(SettingsSound);
        label_2->setObjectName(QString::fromUtf8("label_2"));

        gridLayout->addWidget(label_2, 1, 0, 1, 1);

        checkBoxMPU401 = new QCheckBox(SettingsSound);
        checkBoxMPU401->setObjectName(QString::fromUtf8("checkBoxMPU401"));

        gridLayout->addWidget(checkBoxMPU401, 3, 0, 1, 1);

        pushButtonConfigureMPU401 = new QPushButton(SettingsSound);
        pushButtonConfigureMPU401->setObjectName(QString::fromUtf8("pushButtonConfigureMPU401"));

        gridLayout->addWidget(pushButtonConfigureMPU401, 3, 3, 1, 1);

        pushButtonConfigureMidiIn = new QPushButton(SettingsSound);
        pushButtonConfigureMidiIn->setObjectName(QString::fromUtf8("pushButtonConfigureMidiIn"));

        gridLayout->addWidget(pushButtonConfigureMidiIn, 2, 3, 1, 1);

        pushButtonConfigureSSI2001 = new QPushButton(SettingsSound);
        pushButtonConfigureSSI2001->setObjectName(QString::fromUtf8("pushButtonConfigureSSI2001"));

        gridLayout->addWidget(pushButtonConfigureSSI2001, 4, 3, 1, 1);

        checkBoxCMS = new QCheckBox(SettingsSound);
        checkBoxCMS->setObjectName(QString::fromUtf8("checkBoxCMS"));

        gridLayout->addWidget(checkBoxCMS, 5, 0, 1, 1);

        comboBoxSoundCard = new QComboBox(SettingsSound);
        comboBoxSoundCard->setObjectName(QString::fromUtf8("comboBoxSoundCard"));

        gridLayout->addWidget(comboBoxSoundCard, 0, 1, 1, 1);

        comboBoxMidiOut = new QComboBox(SettingsSound);
        comboBoxMidiOut->setObjectName(QString::fromUtf8("comboBoxMidiOut"));

        gridLayout->addWidget(comboBoxMidiOut, 1, 1, 1, 1);

        pushButtonConfigureMidiOut = new QPushButton(SettingsSound);
        pushButtonConfigureMidiOut->setObjectName(QString::fromUtf8("pushButtonConfigureMidiOut"));

        gridLayout->addWidget(pushButtonConfigureMidiOut, 1, 3, 1, 1);

        checkBoxFloat32 = new QCheckBox(SettingsSound);
        checkBoxFloat32->setObjectName(QString::fromUtf8("checkBoxFloat32"));

        gridLayout->addWidget(checkBoxFloat32, 7, 0, 1, 1);

        pushButtonConfigureCMS = new QPushButton(SettingsSound);
        pushButtonConfigureCMS->setObjectName(QString::fromUtf8("pushButtonConfigureCMS"));

        gridLayout->addWidget(pushButtonConfigureCMS, 5, 3, 1, 1);

        pushButtonConfigureGUS = new QPushButton(SettingsSound);
        pushButtonConfigureGUS->setObjectName(QString::fromUtf8("pushButtonConfigureGUS"));

        gridLayout->addWidget(pushButtonConfigureGUS, 6, 3, 1, 1);

        verticalSpacer = new QSpacerItem(20, 40, QSizePolicy::Minimum, QSizePolicy::Expanding);

        gridLayout->addItem(verticalSpacer, 8, 0, 1, 1);


        retranslateUi(SettingsSound);

        QMetaObject::connectSlotsByName(SettingsSound);
    } // setupUi

    void retranslateUi(QWidget *SettingsSound)
    {
        SettingsSound->setWindowTitle(QCoreApplication::translate("SettingsSound", "Form", nullptr));
        label_3->setText(QCoreApplication::translate("SettingsSound", "MIDI In Device:", nullptr));
        checkBoxSSI2001->setText(QCoreApplication::translate("SettingsSound", "Innovation SSI-2001", nullptr));
        checkBoxGUS->setText(QCoreApplication::translate("SettingsSound", "Gravis Ultrasound", nullptr));
        label->setText(QCoreApplication::translate("SettingsSound", "Sound card:", nullptr));
        pushButtonConfigureSoundCard->setText(QCoreApplication::translate("SettingsSound", "Configure", nullptr));
        label_2->setText(QCoreApplication::translate("SettingsSound", "MIDI Out Device:", nullptr));
        checkBoxMPU401->setText(QCoreApplication::translate("SettingsSound", "Standalone MPU-401", nullptr));
        pushButtonConfigureMPU401->setText(QCoreApplication::translate("SettingsSound", "Configure", nullptr));
        pushButtonConfigureMidiIn->setText(QCoreApplication::translate("SettingsSound", "Configure", nullptr));
        pushButtonConfigureSSI2001->setText(QCoreApplication::translate("SettingsSound", "Configure", nullptr));
        checkBoxCMS->setText(QCoreApplication::translate("SettingsSound", "CMS / Game Blaster", nullptr));
        pushButtonConfigureMidiOut->setText(QCoreApplication::translate("SettingsSound", "Configure", nullptr));
        checkBoxFloat32->setText(QCoreApplication::translate("SettingsSound", "Use FLOAT32 sound", nullptr));
        pushButtonConfigureCMS->setText(QCoreApplication::translate("SettingsSound", "Configure", nullptr));
        pushButtonConfigureGUS->setText(QCoreApplication::translate("SettingsSound", "Configure", nullptr));
    } // retranslateUi

};

namespace Ui {
    class SettingsSound: public Ui_SettingsSound {};
} // namespace Ui

QT_END_NAMESPACE

#endif // UI_QT_SETTINGSSOUND_H
