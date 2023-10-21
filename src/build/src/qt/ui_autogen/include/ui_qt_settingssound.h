/********************************************************************************
** Form generated from reading UI file 'qt_settingssound.ui'
**
** Created by: Qt User Interface Compiler version 5.15.10
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
#include <QtWidgets/QGroupBox>
#include <QtWidgets/QLabel>
#include <QtWidgets/QPushButton>
#include <QtWidgets/QRadioButton>
#include <QtWidgets/QSpacerItem>
#include <QtWidgets/QVBoxLayout>
#include <QtWidgets/QWidget>

QT_BEGIN_NAMESPACE

class Ui_SettingsSound
{
public:
    QGridLayout *gridLayout;
    QLabel *label_3;
    QLabel *label;
    QPushButton *pushButtonConfigureSoundCard1;
    QLabel *label_4;
    QPushButton *pushButtonConfigureSoundCard2;
    QLabel *label_5;
    QPushButton *pushButtonConfigureSoundCard3;
    QLabel *label_6;
    QPushButton *pushButtonConfigureSoundCard4;
    QComboBox *comboBoxMidiIn;
    QLabel *label_2;
    QCheckBox *checkBoxMPU401;
    QPushButton *pushButtonConfigureMPU401;
    QPushButton *pushButtonConfigureMidiIn;
    QComboBox *comboBoxMidiOut;
    QPushButton *pushButtonConfigureMidiOut;
    QCheckBox *checkBoxFloat32;
    QGroupBox *groupBox;
    QVBoxLayout *verticalLayout_1;
    QRadioButton *radioButtonNuked;
    QRadioButton *radioButtonYMFM;
    QSpacerItem *verticalSpacer;
    QComboBox *comboBoxSoundCard1;
    QComboBox *comboBoxSoundCard2;
    QComboBox *comboBoxSoundCard3;
    QComboBox *comboBoxSoundCard4;

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

        gridLayout->addWidget(label_3, 5, 0, 1, 1);

        label = new QLabel(SettingsSound);
        label->setObjectName(QString::fromUtf8("label"));

        gridLayout->addWidget(label, 0, 0, 1, 1);

        pushButtonConfigureSoundCard1 = new QPushButton(SettingsSound);
        pushButtonConfigureSoundCard1->setObjectName(QString::fromUtf8("pushButtonConfigureSoundCard1"));

        gridLayout->addWidget(pushButtonConfigureSoundCard1, 0, 3, 1, 1);

        label_4 = new QLabel(SettingsSound);
        label_4->setObjectName(QString::fromUtf8("label_4"));

        gridLayout->addWidget(label_4, 1, 0, 1, 1);

        pushButtonConfigureSoundCard2 = new QPushButton(SettingsSound);
        pushButtonConfigureSoundCard2->setObjectName(QString::fromUtf8("pushButtonConfigureSoundCard2"));

        gridLayout->addWidget(pushButtonConfigureSoundCard2, 1, 3, 1, 1);

        label_5 = new QLabel(SettingsSound);
        label_5->setObjectName(QString::fromUtf8("label_5"));

        gridLayout->addWidget(label_5, 2, 0, 1, 1);

        pushButtonConfigureSoundCard3 = new QPushButton(SettingsSound);
        pushButtonConfigureSoundCard3->setObjectName(QString::fromUtf8("pushButtonConfigureSoundCard3"));

        gridLayout->addWidget(pushButtonConfigureSoundCard3, 2, 3, 1, 1);

        label_6 = new QLabel(SettingsSound);
        label_6->setObjectName(QString::fromUtf8("label_6"));

        gridLayout->addWidget(label_6, 3, 0, 1, 1);

        pushButtonConfigureSoundCard4 = new QPushButton(SettingsSound);
        pushButtonConfigureSoundCard4->setObjectName(QString::fromUtf8("pushButtonConfigureSoundCard4"));

        gridLayout->addWidget(pushButtonConfigureSoundCard4, 3, 3, 1, 1);

        comboBoxMidiIn = new QComboBox(SettingsSound);
        comboBoxMidiIn->setObjectName(QString::fromUtf8("comboBoxMidiIn"));
        comboBoxMidiIn->setMaxVisibleItems(30);
        QSizePolicy sizePolicy(QSizePolicy::Preferred, QSizePolicy::Fixed);
        sizePolicy.setHorizontalStretch(0);
        sizePolicy.setVerticalStretch(0);
        sizePolicy.setHeightForWidth(comboBoxMidiIn->sizePolicy().hasHeightForWidth());
        comboBoxMidiIn->setSizePolicy(sizePolicy);

        gridLayout->addWidget(comboBoxMidiIn, 5, 1, 1, 1);

        label_2 = new QLabel(SettingsSound);
        label_2->setObjectName(QString::fromUtf8("label_2"));

        gridLayout->addWidget(label_2, 4, 0, 1, 1);

        checkBoxMPU401 = new QCheckBox(SettingsSound);
        checkBoxMPU401->setObjectName(QString::fromUtf8("checkBoxMPU401"));

        gridLayout->addWidget(checkBoxMPU401, 6, 0, 1, 1);

        pushButtonConfigureMPU401 = new QPushButton(SettingsSound);
        pushButtonConfigureMPU401->setObjectName(QString::fromUtf8("pushButtonConfigureMPU401"));

        gridLayout->addWidget(pushButtonConfigureMPU401, 6, 3, 1, 1);

        pushButtonConfigureMidiIn = new QPushButton(SettingsSound);
        pushButtonConfigureMidiIn->setObjectName(QString::fromUtf8("pushButtonConfigureMidiIn"));

        gridLayout->addWidget(pushButtonConfigureMidiIn, 5, 3, 1, 1);

        comboBoxMidiOut = new QComboBox(SettingsSound);
        comboBoxMidiOut->setObjectName(QString::fromUtf8("comboBoxMidiOut"));
        comboBoxMidiOut->setMaxVisibleItems(30);
        sizePolicy.setHeightForWidth(comboBoxMidiOut->sizePolicy().hasHeightForWidth());
        comboBoxMidiOut->setSizePolicy(sizePolicy);

        gridLayout->addWidget(comboBoxMidiOut, 4, 1, 1, 1);

        pushButtonConfigureMidiOut = new QPushButton(SettingsSound);
        pushButtonConfigureMidiOut->setObjectName(QString::fromUtf8("pushButtonConfigureMidiOut"));

        gridLayout->addWidget(pushButtonConfigureMidiOut, 4, 3, 1, 1);

        checkBoxFloat32 = new QCheckBox(SettingsSound);
        checkBoxFloat32->setObjectName(QString::fromUtf8("checkBoxFloat32"));

        gridLayout->addWidget(checkBoxFloat32, 10, 0, 1, 1);

        groupBox = new QGroupBox(SettingsSound);
        groupBox->setObjectName(QString::fromUtf8("groupBox"));
        QSizePolicy sizePolicy1(QSizePolicy::Maximum, QSizePolicy::Preferred);
        sizePolicy1.setHorizontalStretch(0);
        sizePolicy1.setVerticalStretch(0);
        sizePolicy1.setHeightForWidth(groupBox->sizePolicy().hasHeightForWidth());
        groupBox->setSizePolicy(sizePolicy1);
        verticalLayout_1 = new QVBoxLayout(groupBox);
        verticalLayout_1->setObjectName(QString::fromUtf8("verticalLayout_1"));
        radioButtonNuked = new QRadioButton(groupBox);
        radioButtonNuked->setObjectName(QString::fromUtf8("radioButtonNuked"));

        verticalLayout_1->addWidget(radioButtonNuked);

        radioButtonYMFM = new QRadioButton(groupBox);
        radioButtonYMFM->setObjectName(QString::fromUtf8("radioButtonYMFM"));

        verticalLayout_1->addWidget(radioButtonYMFM);


        gridLayout->addWidget(groupBox, 11, 0, 1, 1);

        verticalSpacer = new QSpacerItem(20, 40, QSizePolicy::Minimum, QSizePolicy::Expanding);

        gridLayout->addItem(verticalSpacer, 12, 0, 1, 1);

        comboBoxSoundCard1 = new QComboBox(SettingsSound);
        comboBoxSoundCard1->setObjectName(QString::fromUtf8("comboBoxSoundCard1"));
        comboBoxSoundCard1->setMaxVisibleItems(30);
        QSizePolicy sizePolicy2(QSizePolicy::MinimumExpanding, QSizePolicy::Fixed);
        sizePolicy2.setHorizontalStretch(0);
        sizePolicy2.setVerticalStretch(0);
        sizePolicy2.setHeightForWidth(comboBoxSoundCard1->sizePolicy().hasHeightForWidth());
        comboBoxSoundCard1->setSizePolicy(sizePolicy2);

        gridLayout->addWidget(comboBoxSoundCard1, 0, 1, 1, 1);

        comboBoxSoundCard2 = new QComboBox(SettingsSound);
        comboBoxSoundCard2->setObjectName(QString::fromUtf8("comboBoxSoundCard2"));
        comboBoxSoundCard2->setMaxVisibleItems(30);
        sizePolicy2.setHeightForWidth(comboBoxSoundCard2->sizePolicy().hasHeightForWidth());
        comboBoxSoundCard2->setSizePolicy(sizePolicy2);

        gridLayout->addWidget(comboBoxSoundCard2, 1, 1, 1, 1);

        comboBoxSoundCard3 = new QComboBox(SettingsSound);
        comboBoxSoundCard3->setObjectName(QString::fromUtf8("comboBoxSoundCard3"));
        comboBoxSoundCard3->setMaxVisibleItems(30);
        sizePolicy2.setHeightForWidth(comboBoxSoundCard3->sizePolicy().hasHeightForWidth());
        comboBoxSoundCard3->setSizePolicy(sizePolicy2);

        gridLayout->addWidget(comboBoxSoundCard3, 2, 1, 1, 1);

        comboBoxSoundCard4 = new QComboBox(SettingsSound);
        comboBoxSoundCard4->setObjectName(QString::fromUtf8("comboBoxSoundCard4"));
        comboBoxSoundCard4->setMaxVisibleItems(30);
        sizePolicy2.setHeightForWidth(comboBoxSoundCard4->sizePolicy().hasHeightForWidth());
        comboBoxSoundCard4->setSizePolicy(sizePolicy2);

        gridLayout->addWidget(comboBoxSoundCard4, 3, 1, 1, 1);


        retranslateUi(SettingsSound);

        QMetaObject::connectSlotsByName(SettingsSound);
    } // setupUi

    void retranslateUi(QWidget *SettingsSound)
    {
        SettingsSound->setWindowTitle(QCoreApplication::translate("SettingsSound", "Form", nullptr));
        label_3->setText(QCoreApplication::translate("SettingsSound", "MIDI In Device:", nullptr));
        label->setText(QCoreApplication::translate("SettingsSound", "Sound card #1:", nullptr));
        pushButtonConfigureSoundCard1->setText(QCoreApplication::translate("SettingsSound", "Configure", nullptr));
        label_4->setText(QCoreApplication::translate("SettingsSound", "Sound card #2:", nullptr));
        pushButtonConfigureSoundCard2->setText(QCoreApplication::translate("SettingsSound", "Configure", nullptr));
        label_5->setText(QCoreApplication::translate("SettingsSound", "Sound card #3:", nullptr));
        pushButtonConfigureSoundCard3->setText(QCoreApplication::translate("SettingsSound", "Configure", nullptr));
        label_6->setText(QCoreApplication::translate("SettingsSound", "Sound card #4:", nullptr));
        pushButtonConfigureSoundCard4->setText(QCoreApplication::translate("SettingsSound", "Configure", nullptr));
        label_2->setText(QCoreApplication::translate("SettingsSound", "MIDI Out Device:", nullptr));
        checkBoxMPU401->setText(QCoreApplication::translate("SettingsSound", "Standalone MPU-401", nullptr));
        pushButtonConfigureMPU401->setText(QCoreApplication::translate("SettingsSound", "Configure", nullptr));
        pushButtonConfigureMidiIn->setText(QCoreApplication::translate("SettingsSound", "Configure", nullptr));
        pushButtonConfigureMidiOut->setText(QCoreApplication::translate("SettingsSound", "Configure", nullptr));
        checkBoxFloat32->setText(QCoreApplication::translate("SettingsSound", "Use FLOAT32 sound", nullptr));
        groupBox->setTitle(QCoreApplication::translate("SettingsSound", "FM synth driver", nullptr));
        radioButtonNuked->setText(QCoreApplication::translate("SettingsSound", "Nuked (more accurate)", nullptr));
        radioButtonYMFM->setText(QCoreApplication::translate("SettingsSound", "YMFM (faster)", nullptr));
    } // retranslateUi

};

namespace Ui {
    class SettingsSound: public Ui_SettingsSound {};
} // namespace Ui

QT_END_NAMESPACE

#endif // UI_QT_SETTINGSSOUND_H
