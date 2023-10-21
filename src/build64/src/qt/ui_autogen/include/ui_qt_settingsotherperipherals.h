/********************************************************************************
** Form generated from reading UI file 'qt_settingsotherperipherals.ui'
**
** Created by: Qt User Interface Compiler version 5.15.10
**
** WARNING! All changes made in this file will be lost when recompiling UI file!
********************************************************************************/

#ifndef UI_QT_SETTINGSOTHERPERIPHERALS_H
#define UI_QT_SETTINGSOTHERPERIPHERALS_H

#include <QtCore/QVariant>
#include <QtWidgets/QApplication>
#include <QtWidgets/QCheckBox>
#include <QtWidgets/QComboBox>
#include <QtWidgets/QGridLayout>
#include <QtWidgets/QGroupBox>
#include <QtWidgets/QHBoxLayout>
#include <QtWidgets/QLabel>
#include <QtWidgets/QPushButton>
#include <QtWidgets/QSpacerItem>
#include <QtWidgets/QVBoxLayout>
#include <QtWidgets/QWidget>

QT_BEGIN_NAMESPACE

class Ui_SettingsOtherPeripherals
{
public:
    QVBoxLayout *verticalLayout;
    QHBoxLayout *horizontalLayout;
    QLabel *label;
    QComboBox *comboBoxRTC;
    QPushButton *pushButtonConfigureRTC;
    QGroupBox *groupBox;
    QGridLayout *gridLayout;
    QPushButton *pushButtonConfigureCard2;
    QComboBox *comboBoxCard2;
    QPushButton *pushButtonConfigureCard3;
    QLabel *label_3;
    QLabel *label_4;
    QPushButton *pushButtonConfigureCard1;
    QComboBox *comboBoxCard1;
    QLabel *label_2;
    QComboBox *comboBoxCard3;
    QComboBox *comboBoxCard4;
    QPushButton *pushButtonConfigureCard4;
    QLabel *label_5;
    QHBoxLayout *horizontalLayout_2;
    QCheckBox *checkBoxISABugger;
    QCheckBox *checkBoxPOSTCard;
    QSpacerItem *verticalSpacer;

    void setupUi(QWidget *SettingsOtherPeripherals)
    {
        if (SettingsOtherPeripherals->objectName().isEmpty())
            SettingsOtherPeripherals->setObjectName(QString::fromUtf8("SettingsOtherPeripherals"));
        SettingsOtherPeripherals->resize(421, 458);
        verticalLayout = new QVBoxLayout(SettingsOtherPeripherals);
        verticalLayout->setObjectName(QString::fromUtf8("verticalLayout"));
        verticalLayout->setContentsMargins(0, 0, 0, 0);
        horizontalLayout = new QHBoxLayout();
        horizontalLayout->setObjectName(QString::fromUtf8("horizontalLayout"));
        label = new QLabel(SettingsOtherPeripherals);
        label->setObjectName(QString::fromUtf8("label"));

        horizontalLayout->addWidget(label);

        comboBoxRTC = new QComboBox(SettingsOtherPeripherals);
        comboBoxRTC->setObjectName(QString::fromUtf8("comboBoxRTC"));
        comboBoxRTC->setMaxVisibleItems(30);
        QSizePolicy sizePolicy(QSizePolicy::MinimumExpanding, QSizePolicy::Fixed);
        sizePolicy.setHorizontalStretch(0);
        sizePolicy.setVerticalStretch(0);
        sizePolicy.setHeightForWidth(comboBoxRTC->sizePolicy().hasHeightForWidth());
        comboBoxRTC->setSizePolicy(sizePolicy);

        horizontalLayout->addWidget(comboBoxRTC);

        pushButtonConfigureRTC = new QPushButton(SettingsOtherPeripherals);
        pushButtonConfigureRTC->setObjectName(QString::fromUtf8("pushButtonConfigureRTC"));

        horizontalLayout->addWidget(pushButtonConfigureRTC);


        verticalLayout->addLayout(horizontalLayout);

        groupBox = new QGroupBox(SettingsOtherPeripherals);
        groupBox->setObjectName(QString::fromUtf8("groupBox"));
        gridLayout = new QGridLayout(groupBox);
        gridLayout->setObjectName(QString::fromUtf8("gridLayout"));
        pushButtonConfigureCard2 = new QPushButton(groupBox);
        pushButtonConfigureCard2->setObjectName(QString::fromUtf8("pushButtonConfigureCard2"));

        gridLayout->addWidget(pushButtonConfigureCard2, 1, 2, 1, 1);

        comboBoxCard2 = new QComboBox(groupBox);
        comboBoxCard2->setObjectName(QString::fromUtf8("comboBoxCard2"));
        comboBoxCard2->setMaxVisibleItems(30);
        sizePolicy.setHeightForWidth(comboBoxCard2->sizePolicy().hasHeightForWidth());
        comboBoxCard2->setSizePolicy(sizePolicy);

        gridLayout->addWidget(comboBoxCard2, 1, 1, 1, 1);

        pushButtonConfigureCard3 = new QPushButton(groupBox);
        pushButtonConfigureCard3->setObjectName(QString::fromUtf8("pushButtonConfigureCard3"));

        gridLayout->addWidget(pushButtonConfigureCard3, 2, 2, 1, 1);

        label_3 = new QLabel(groupBox);
        label_3->setObjectName(QString::fromUtf8("label_3"));

        gridLayout->addWidget(label_3, 1, 0, 1, 1);

        label_4 = new QLabel(groupBox);
        label_4->setObjectName(QString::fromUtf8("label_4"));

        gridLayout->addWidget(label_4, 2, 0, 1, 1);

        pushButtonConfigureCard1 = new QPushButton(groupBox);
        pushButtonConfigureCard1->setObjectName(QString::fromUtf8("pushButtonConfigureCard1"));

        gridLayout->addWidget(pushButtonConfigureCard1, 0, 2, 1, 1);

        comboBoxCard1 = new QComboBox(groupBox);
        comboBoxCard1->setObjectName(QString::fromUtf8("comboBoxCard1"));
        comboBoxCard1->setMaxVisibleItems(30);
        sizePolicy.setHeightForWidth(comboBoxCard1->sizePolicy().hasHeightForWidth());
        comboBoxCard1->setSizePolicy(sizePolicy);

        gridLayout->addWidget(comboBoxCard1, 0, 1, 1, 1);

        label_2 = new QLabel(groupBox);
        label_2->setObjectName(QString::fromUtf8("label_2"));

        gridLayout->addWidget(label_2, 0, 0, 1, 1);

        comboBoxCard3 = new QComboBox(groupBox);
        comboBoxCard3->setObjectName(QString::fromUtf8("comboBoxCard3"));
        comboBoxCard3->setMaxVisibleItems(30);
        sizePolicy.setHeightForWidth(comboBoxCard3->sizePolicy().hasHeightForWidth());
        comboBoxCard3->setSizePolicy(sizePolicy);

        gridLayout->addWidget(comboBoxCard3, 2, 1, 1, 1);

        comboBoxCard4 = new QComboBox(groupBox);
        comboBoxCard4->setObjectName(QString::fromUtf8("comboBoxCard4"));
        comboBoxCard4->setMaxVisibleItems(30);
        sizePolicy.setHeightForWidth(comboBoxCard4->sizePolicy().hasHeightForWidth());
        comboBoxCard4->setSizePolicy(sizePolicy);

        gridLayout->addWidget(comboBoxCard4, 3, 1, 1, 1);

        pushButtonConfigureCard4 = new QPushButton(groupBox);
        pushButtonConfigureCard4->setObjectName(QString::fromUtf8("pushButtonConfigureCard4"));

        gridLayout->addWidget(pushButtonConfigureCard4, 3, 2, 1, 1);

        label_5 = new QLabel(groupBox);
        label_5->setObjectName(QString::fromUtf8("label_5"));

        gridLayout->addWidget(label_5, 3, 0, 1, 1);


        verticalLayout->addWidget(groupBox);

        horizontalLayout_2 = new QHBoxLayout();
        horizontalLayout_2->setObjectName(QString::fromUtf8("horizontalLayout_2"));
        checkBoxISABugger = new QCheckBox(SettingsOtherPeripherals);
        checkBoxISABugger->setObjectName(QString::fromUtf8("checkBoxISABugger"));

        horizontalLayout_2->addWidget(checkBoxISABugger);

        checkBoxPOSTCard = new QCheckBox(SettingsOtherPeripherals);
        checkBoxPOSTCard->setObjectName(QString::fromUtf8("checkBoxPOSTCard"));

        horizontalLayout_2->addWidget(checkBoxPOSTCard);


        verticalLayout->addLayout(horizontalLayout_2);

        verticalSpacer = new QSpacerItem(20, 40, QSizePolicy::Minimum, QSizePolicy::Expanding);

        verticalLayout->addItem(verticalSpacer);


        retranslateUi(SettingsOtherPeripherals);

        QMetaObject::connectSlotsByName(SettingsOtherPeripherals);
    } // setupUi

    void retranslateUi(QWidget *SettingsOtherPeripherals)
    {
        SettingsOtherPeripherals->setWindowTitle(QCoreApplication::translate("SettingsOtherPeripherals", "Form", nullptr));
        label->setText(QCoreApplication::translate("SettingsOtherPeripherals", "ISA RTC:", nullptr));
        pushButtonConfigureRTC->setText(QCoreApplication::translate("SettingsOtherPeripherals", "Configure", nullptr));
        groupBox->setTitle(QCoreApplication::translate("SettingsOtherPeripherals", "ISA Memory Expansion", nullptr));
        pushButtonConfigureCard2->setText(QCoreApplication::translate("SettingsOtherPeripherals", "Configure", nullptr));
        pushButtonConfigureCard3->setText(QCoreApplication::translate("SettingsOtherPeripherals", "Configure", nullptr));
        label_3->setText(QCoreApplication::translate("SettingsOtherPeripherals", "Card 2:", nullptr));
        label_4->setText(QCoreApplication::translate("SettingsOtherPeripherals", "Card 3:", nullptr));
        pushButtonConfigureCard1->setText(QCoreApplication::translate("SettingsOtherPeripherals", "Configure", nullptr));
        label_2->setText(QCoreApplication::translate("SettingsOtherPeripherals", "Card 1:", nullptr));
        pushButtonConfigureCard4->setText(QCoreApplication::translate("SettingsOtherPeripherals", "Configure", nullptr));
        label_5->setText(QCoreApplication::translate("SettingsOtherPeripherals", "Card 4:", nullptr));
        checkBoxISABugger->setText(QCoreApplication::translate("SettingsOtherPeripherals", "ISABugger device", nullptr));
        checkBoxPOSTCard->setText(QCoreApplication::translate("SettingsOtherPeripherals", "POST card", nullptr));
    } // retranslateUi

};

namespace Ui {
    class SettingsOtherPeripherals: public Ui_SettingsOtherPeripherals {};
} // namespace Ui

QT_END_NAMESPACE

#endif // UI_QT_SETTINGSOTHERPERIPHERALS_H
