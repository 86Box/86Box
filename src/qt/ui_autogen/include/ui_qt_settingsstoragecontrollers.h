/********************************************************************************
** Form generated from reading UI file 'qt_settingsstoragecontrollers.ui'
**
** Created by: Qt User Interface Compiler version 6.2.2
**
** WARNING! All changes made in this file will be lost when recompiling UI file!
********************************************************************************/

#ifndef UI_QT_SETTINGSSTORAGECONTROLLERS_H
#define UI_QT_SETTINGSSTORAGECONTROLLERS_H

#include <QtCore/QVariant>
#include <QtWidgets/QApplication>
#include <QtWidgets/QCheckBox>
#include <QtWidgets/QComboBox>
#include <QtWidgets/QGridLayout>
#include <QtWidgets/QGroupBox>
#include <QtWidgets/QLabel>
#include <QtWidgets/QPushButton>
#include <QtWidgets/QSpacerItem>
#include <QtWidgets/QVBoxLayout>
#include <QtWidgets/QWidget>

QT_BEGIN_NAMESPACE

class Ui_SettingsStorageControllers
{
public:
    QVBoxLayout *verticalLayout;
    QGridLayout *gridLayout;
    QLabel *label;
    QPushButton *pushButtonFD;
    QLabel *label_2;
    QComboBox *comboBoxHD;
    QPushButton *pushButtonHD;
    QComboBox *comboBoxFD;
    QCheckBox *checkBoxTertiaryIDE;
    QCheckBox *checkBoxQuaternaryIDE;
    QPushButton *pushButtonTertiaryIDE;
    QPushButton *pushButtonQuaternaryIDE;
    QGroupBox *groupBox;
    QGridLayout *gridLayout_3;
    QComboBox *comboBoxSCSI4;
    QPushButton *pushButtonSCSI4;
    QLabel *label_3;
    QComboBox *comboBoxSCSI1;
    QComboBox *comboBoxSCSI2;
    QLabel *label_5;
    QLabel *label_4;
    QPushButton *pushButtonSCSI1;
    QPushButton *pushButtonSCSI2;
    QLabel *label_6;
    QComboBox *comboBoxSCSI3;
    QPushButton *pushButtonSCSI3;
    QCheckBox *checkBoxCassette;
    QSpacerItem *verticalSpacer;

    void setupUi(QWidget *SettingsStorageControllers)
    {
        if (SettingsStorageControllers->objectName().isEmpty())
            SettingsStorageControllers->setObjectName(QString::fromUtf8("SettingsStorageControllers"));
        SettingsStorageControllers->resize(496, 449);
        verticalLayout = new QVBoxLayout(SettingsStorageControllers);
        verticalLayout->setObjectName(QString::fromUtf8("verticalLayout"));
        verticalLayout->setContentsMargins(0, 0, 0, 0);
        gridLayout = new QGridLayout();
        gridLayout->setObjectName(QString::fromUtf8("gridLayout"));
        label = new QLabel(SettingsStorageControllers);
        label->setObjectName(QString::fromUtf8("label"));

        gridLayout->addWidget(label, 0, 0, 1, 1);

        pushButtonFD = new QPushButton(SettingsStorageControllers);
        pushButtonFD->setObjectName(QString::fromUtf8("pushButtonFD"));

        gridLayout->addWidget(pushButtonFD, 1, 2, 1, 1);

        label_2 = new QLabel(SettingsStorageControllers);
        label_2->setObjectName(QString::fromUtf8("label_2"));

        gridLayout->addWidget(label_2, 1, 0, 1, 1);

        comboBoxHD = new QComboBox(SettingsStorageControllers);
        comboBoxHD->setObjectName(QString::fromUtf8("comboBoxHD"));

        gridLayout->addWidget(comboBoxHD, 0, 1, 1, 1);

        pushButtonHD = new QPushButton(SettingsStorageControllers);
        pushButtonHD->setObjectName(QString::fromUtf8("pushButtonHD"));

        gridLayout->addWidget(pushButtonHD, 0, 2, 1, 1);

        comboBoxFD = new QComboBox(SettingsStorageControllers);
        comboBoxFD->setObjectName(QString::fromUtf8("comboBoxFD"));

        gridLayout->addWidget(comboBoxFD, 1, 1, 1, 1);

        checkBoxTertiaryIDE = new QCheckBox(SettingsStorageControllers);
        checkBoxTertiaryIDE->setObjectName(QString::fromUtf8("checkBoxTertiaryIDE"));

        gridLayout->addWidget(checkBoxTertiaryIDE, 2, 0, 1, 1);

        checkBoxQuaternaryIDE = new QCheckBox(SettingsStorageControllers);
        checkBoxQuaternaryIDE->setObjectName(QString::fromUtf8("checkBoxQuaternaryIDE"));

        gridLayout->addWidget(checkBoxQuaternaryIDE, 3, 0, 1, 1);

        pushButtonTertiaryIDE = new QPushButton(SettingsStorageControllers);
        pushButtonTertiaryIDE->setObjectName(QString::fromUtf8("pushButtonTertiaryIDE"));
        pushButtonTertiaryIDE->setEnabled(false);

        gridLayout->addWidget(pushButtonTertiaryIDE, 2, 2, 1, 1);

        pushButtonQuaternaryIDE = new QPushButton(SettingsStorageControllers);
        pushButtonQuaternaryIDE->setObjectName(QString::fromUtf8("pushButtonQuaternaryIDE"));
        pushButtonQuaternaryIDE->setEnabled(false);

        gridLayout->addWidget(pushButtonQuaternaryIDE, 3, 2, 1, 1);


        verticalLayout->addLayout(gridLayout);

        groupBox = new QGroupBox(SettingsStorageControllers);
        groupBox->setObjectName(QString::fromUtf8("groupBox"));
        gridLayout_3 = new QGridLayout(groupBox);
        gridLayout_3->setObjectName(QString::fromUtf8("gridLayout_3"));
        comboBoxSCSI4 = new QComboBox(groupBox);
        comboBoxSCSI4->setObjectName(QString::fromUtf8("comboBoxSCSI4"));

        gridLayout_3->addWidget(comboBoxSCSI4, 3, 2, 1, 1);

        pushButtonSCSI4 = new QPushButton(groupBox);
        pushButtonSCSI4->setObjectName(QString::fromUtf8("pushButtonSCSI4"));

        gridLayout_3->addWidget(pushButtonSCSI4, 3, 3, 1, 1);

        label_3 = new QLabel(groupBox);
        label_3->setObjectName(QString::fromUtf8("label_3"));

        gridLayout_3->addWidget(label_3, 0, 0, 1, 1);

        comboBoxSCSI1 = new QComboBox(groupBox);
        comboBoxSCSI1->setObjectName(QString::fromUtf8("comboBoxSCSI1"));

        gridLayout_3->addWidget(comboBoxSCSI1, 0, 2, 1, 1);

        comboBoxSCSI2 = new QComboBox(groupBox);
        comboBoxSCSI2->setObjectName(QString::fromUtf8("comboBoxSCSI2"));

        gridLayout_3->addWidget(comboBoxSCSI2, 1, 2, 1, 1);

        label_5 = new QLabel(groupBox);
        label_5->setObjectName(QString::fromUtf8("label_5"));

        gridLayout_3->addWidget(label_5, 3, 0, 2, 1);

        label_4 = new QLabel(groupBox);
        label_4->setObjectName(QString::fromUtf8("label_4"));

        gridLayout_3->addWidget(label_4, 1, 0, 1, 1);

        pushButtonSCSI1 = new QPushButton(groupBox);
        pushButtonSCSI1->setObjectName(QString::fromUtf8("pushButtonSCSI1"));

        gridLayout_3->addWidget(pushButtonSCSI1, 0, 3, 1, 1);

        pushButtonSCSI2 = new QPushButton(groupBox);
        pushButtonSCSI2->setObjectName(QString::fromUtf8("pushButtonSCSI2"));

        gridLayout_3->addWidget(pushButtonSCSI2, 1, 3, 1, 1);

        label_6 = new QLabel(groupBox);
        label_6->setObjectName(QString::fromUtf8("label_6"));

        gridLayout_3->addWidget(label_6, 2, 0, 1, 1);

        comboBoxSCSI3 = new QComboBox(groupBox);
        comboBoxSCSI3->setObjectName(QString::fromUtf8("comboBoxSCSI3"));

        gridLayout_3->addWidget(comboBoxSCSI3, 2, 2, 1, 1);

        pushButtonSCSI3 = new QPushButton(groupBox);
        pushButtonSCSI3->setObjectName(QString::fromUtf8("pushButtonSCSI3"));

        gridLayout_3->addWidget(pushButtonSCSI3, 2, 3, 1, 1);


        verticalLayout->addWidget(groupBox);

        checkBoxCassette = new QCheckBox(SettingsStorageControllers);
        checkBoxCassette->setObjectName(QString::fromUtf8("checkBoxCassette"));

        verticalLayout->addWidget(checkBoxCassette);

        verticalSpacer = new QSpacerItem(20, 40, QSizePolicy::Minimum, QSizePolicy::Expanding);

        verticalLayout->addItem(verticalSpacer);


        retranslateUi(SettingsStorageControllers);

        QMetaObject::connectSlotsByName(SettingsStorageControllers);
    } // setupUi

    void retranslateUi(QWidget *SettingsStorageControllers)
    {
        SettingsStorageControllers->setWindowTitle(QCoreApplication::translate("SettingsStorageControllers", "Form", nullptr));
        label->setText(QCoreApplication::translate("SettingsStorageControllers", "HD Controller:", nullptr));
        pushButtonFD->setText(QCoreApplication::translate("SettingsStorageControllers", "Configure", nullptr));
        label_2->setText(QCoreApplication::translate("SettingsStorageControllers", "FD Controller:", nullptr));
        pushButtonHD->setText(QCoreApplication::translate("SettingsStorageControllers", "Configure", nullptr));
        checkBoxTertiaryIDE->setText(QCoreApplication::translate("SettingsStorageControllers", "Tertiary IDE Controller", nullptr));
        checkBoxQuaternaryIDE->setText(QCoreApplication::translate("SettingsStorageControllers", "Quaternary IDE Controller", nullptr));
        pushButtonTertiaryIDE->setText(QCoreApplication::translate("SettingsStorageControllers", "Configure", nullptr));
        pushButtonQuaternaryIDE->setText(QCoreApplication::translate("SettingsStorageControllers", "Configure", nullptr));
        groupBox->setTitle(QCoreApplication::translate("SettingsStorageControllers", "SCSI", nullptr));
        pushButtonSCSI4->setText(QCoreApplication::translate("SettingsStorageControllers", "Configure", nullptr));
        label_3->setText(QCoreApplication::translate("SettingsStorageControllers", "Controller 1:", nullptr));
        label_5->setText(QCoreApplication::translate("SettingsStorageControllers", "Controller 4:", nullptr));
        label_4->setText(QCoreApplication::translate("SettingsStorageControllers", "Controller 2:", nullptr));
        pushButtonSCSI1->setText(QCoreApplication::translate("SettingsStorageControllers", "Configure", nullptr));
        pushButtonSCSI2->setText(QCoreApplication::translate("SettingsStorageControllers", "Configure", nullptr));
        label_6->setText(QCoreApplication::translate("SettingsStorageControllers", "Controller 3:", nullptr));
        pushButtonSCSI3->setText(QCoreApplication::translate("SettingsStorageControllers", "Configure", nullptr));
        checkBoxCassette->setText(QCoreApplication::translate("SettingsStorageControllers", "Cassette", nullptr));
    } // retranslateUi

};

namespace Ui {
    class SettingsStorageControllers: public Ui_SettingsStorageControllers {};
} // namespace Ui

QT_END_NAMESPACE

#endif // UI_QT_SETTINGSSTORAGECONTROLLERS_H
