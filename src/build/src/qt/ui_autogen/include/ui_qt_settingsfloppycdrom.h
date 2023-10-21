/********************************************************************************
** Form generated from reading UI file 'qt_settingsfloppycdrom.ui'
**
** Created by: Qt User Interface Compiler version 5.15.10
**
** WARNING! All changes made in this file will be lost when recompiling UI file!
********************************************************************************/

#ifndef UI_QT_SETTINGSFLOPPYCDROM_H
#define UI_QT_SETTINGSFLOPPYCDROM_H

#include <QtCore/QVariant>
#include <QtWidgets/QApplication>
#include <QtWidgets/QCheckBox>
#include <QtWidgets/QComboBox>
#include <QtWidgets/QGridLayout>
#include <QtWidgets/QHBoxLayout>
#include <QtWidgets/QHeaderView>
#include <QtWidgets/QLabel>
#include <QtWidgets/QSpacerItem>
#include <QtWidgets/QTableView>
#include <QtWidgets/QVBoxLayout>
#include <QtWidgets/QWidget>

QT_BEGIN_NAMESPACE

class Ui_SettingsFloppyCDROM
{
public:
    QVBoxLayout *verticalLayout;
    QLabel *label;
    QTableView *tableViewFloppy;
    QHBoxLayout *horizontalLayout;
    QLabel *label_2;
    QComboBox *comboBoxFloppyType;
    QCheckBox *checkBoxTurboTimings;
    QCheckBox *checkBoxCheckBPB;
    QSpacerItem *verticalSpacer;
    QLabel *label_6;
    QTableView *tableViewCDROM;
    QGridLayout *gridLayout;
    QLabel *label_3;
    QLabel *label_7;
    QLabel *label_4;
    QLabel *label_5;
    QComboBox *comboBoxBus;
    QComboBox *comboBoxChannel;
    QComboBox *comboBoxSpeed;
    QComboBox *comboBoxCDROMType;

    void setupUi(QWidget *SettingsFloppyCDROM)
    {
        if (SettingsFloppyCDROM->objectName().isEmpty())
            SettingsFloppyCDROM->setObjectName(QString::fromUtf8("SettingsFloppyCDROM"));
        SettingsFloppyCDROM->resize(544, 617);
        verticalLayout = new QVBoxLayout(SettingsFloppyCDROM);
        verticalLayout->setObjectName(QString::fromUtf8("verticalLayout"));
        verticalLayout->setContentsMargins(0, 0, 0, 0);
        label = new QLabel(SettingsFloppyCDROM);
        label->setObjectName(QString::fromUtf8("label"));

        verticalLayout->addWidget(label);

        tableViewFloppy = new QTableView(SettingsFloppyCDROM);
        tableViewFloppy->setObjectName(QString::fromUtf8("tableViewFloppy"));
        tableViewFloppy->setEditTriggers(QAbstractItemView::NoEditTriggers);
        tableViewFloppy->setSelectionMode(QAbstractItemView::SingleSelection);
        tableViewFloppy->setSelectionBehavior(QAbstractItemView::SelectRows);
        tableViewFloppy->setShowGrid(false);
        tableViewFloppy->verticalHeader()->setVisible(false);

        verticalLayout->addWidget(tableViewFloppy);

        horizontalLayout = new QHBoxLayout();
        horizontalLayout->setObjectName(QString::fromUtf8("horizontalLayout"));
        label_2 = new QLabel(SettingsFloppyCDROM);
        label_2->setObjectName(QString::fromUtf8("label_2"));

        horizontalLayout->addWidget(label_2);

        comboBoxFloppyType = new QComboBox(SettingsFloppyCDROM);
        comboBoxFloppyType->setObjectName(QString::fromUtf8("comboBoxFloppyType"));
        comboBoxFloppyType->setMaxVisibleItems(30);

        horizontalLayout->addWidget(comboBoxFloppyType);

        checkBoxTurboTimings = new QCheckBox(SettingsFloppyCDROM);
        checkBoxTurboTimings->setObjectName(QString::fromUtf8("checkBoxTurboTimings"));

        horizontalLayout->addWidget(checkBoxTurboTimings);

        checkBoxCheckBPB = new QCheckBox(SettingsFloppyCDROM);
        checkBoxCheckBPB->setObjectName(QString::fromUtf8("checkBoxCheckBPB"));

        horizontalLayout->addWidget(checkBoxCheckBPB);


        verticalLayout->addLayout(horizontalLayout);

        verticalSpacer = new QSpacerItem(20, 40, QSizePolicy::Minimum, QSizePolicy::Expanding);

        verticalLayout->addItem(verticalSpacer);

        label_6 = new QLabel(SettingsFloppyCDROM);
        label_6->setObjectName(QString::fromUtf8("label_6"));

        verticalLayout->addWidget(label_6);

        tableViewCDROM = new QTableView(SettingsFloppyCDROM);
        tableViewCDROM->setObjectName(QString::fromUtf8("tableViewCDROM"));
        tableViewCDROM->setEditTriggers(QAbstractItemView::NoEditTriggers);
        tableViewCDROM->setSelectionMode(QAbstractItemView::SingleSelection);
        tableViewCDROM->setSelectionBehavior(QAbstractItemView::SelectRows);
        tableViewCDROM->setShowGrid(false);
        tableViewCDROM->verticalHeader()->setVisible(false);

        verticalLayout->addWidget(tableViewCDROM);

        gridLayout = new QGridLayout();
        gridLayout->setObjectName(QString::fromUtf8("gridLayout"));
        label_3 = new QLabel(SettingsFloppyCDROM);
        label_3->setObjectName(QString::fromUtf8("label_3"));

        gridLayout->addWidget(label_3, 0, 0, 1, 1);

        label_7 = new QLabel(SettingsFloppyCDROM);
        label_7->setObjectName(QString::fromUtf8("label_7"));

        gridLayout->addWidget(label_7, 0, 2, 1, 1);

        label_4 = new QLabel(SettingsFloppyCDROM);
        label_4->setObjectName(QString::fromUtf8("label_4"));

        gridLayout->addWidget(label_4, 1, 0, 1, 1);

        label_5 = new QLabel(SettingsFloppyCDROM);
        label_5->setObjectName(QString::fromUtf8("label_5"));

        gridLayout->addWidget(label_5, 2, 0, 1, 1);

        comboBoxBus = new QComboBox(SettingsFloppyCDROM);
        comboBoxBus->setObjectName(QString::fromUtf8("comboBoxBus"));
        comboBoxBus->setMaxVisibleItems(30);

        gridLayout->addWidget(comboBoxBus, 0, 1, 1, 1);

        comboBoxChannel = new QComboBox(SettingsFloppyCDROM);
        comboBoxChannel->setObjectName(QString::fromUtf8("comboBoxChannel"));
        comboBoxChannel->setMaxVisibleItems(30);

        gridLayout->addWidget(comboBoxChannel, 0, 3, 1, 1);

        comboBoxSpeed = new QComboBox(SettingsFloppyCDROM);
        comboBoxSpeed->setObjectName(QString::fromUtf8("comboBoxSpeed"));
        comboBoxSpeed->setMaxVisibleItems(30);

        gridLayout->addWidget(comboBoxSpeed, 1, 1, 1, 1);

        comboBoxCDROMType = new QComboBox(SettingsFloppyCDROM);
        comboBoxCDROMType->setObjectName(QString::fromUtf8("comboBoxCDROMType"));
        comboBoxCDROMType->setMaxVisibleItems(30);

        gridLayout->addWidget(comboBoxCDROMType, 2, 1, 1, 3);


        verticalLayout->addLayout(gridLayout);


        retranslateUi(SettingsFloppyCDROM);

        QMetaObject::connectSlotsByName(SettingsFloppyCDROM);
    } // setupUi

    void retranslateUi(QWidget *SettingsFloppyCDROM)
    {
        SettingsFloppyCDROM->setWindowTitle(QCoreApplication::translate("SettingsFloppyCDROM", "Form", nullptr));
        label->setText(QCoreApplication::translate("SettingsFloppyCDROM", "Floppy drives:", nullptr));
        label_2->setText(QCoreApplication::translate("SettingsFloppyCDROM", "Type:", nullptr));
        checkBoxTurboTimings->setText(QCoreApplication::translate("SettingsFloppyCDROM", "Turbo timings", nullptr));
        checkBoxCheckBPB->setText(QCoreApplication::translate("SettingsFloppyCDROM", "Check BPB", nullptr));
        label_6->setText(QCoreApplication::translate("SettingsFloppyCDROM", "CD-ROM drives:", nullptr));
        label_3->setText(QCoreApplication::translate("SettingsFloppyCDROM", "Bus:", nullptr));
        label_7->setText(QCoreApplication::translate("SettingsFloppyCDROM", "Channel:", nullptr));
        label_4->setText(QCoreApplication::translate("SettingsFloppyCDROM", "Speed:", nullptr));
        label_5->setText(QCoreApplication::translate("SettingsFloppyCDROM", "Type:", nullptr));
    } // retranslateUi

};

namespace Ui {
    class SettingsFloppyCDROM: public Ui_SettingsFloppyCDROM {};
} // namespace Ui

QT_END_NAMESPACE

#endif // UI_QT_SETTINGSFLOPPYCDROM_H
