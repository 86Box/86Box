/********************************************************************************
** Form generated from reading UI file 'qt_settingsotherremovable.ui'
**
** Created by: Qt User Interface Compiler version 5.15.10
**
** WARNING! All changes made in this file will be lost when recompiling UI file!
********************************************************************************/

#ifndef UI_QT_SETTINGSOTHERREMOVABLE_H
#define UI_QT_SETTINGSOTHERREMOVABLE_H

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

class Ui_SettingsOtherRemovable
{
public:
    QVBoxLayout *verticalLayout;
    QLabel *label;
    QTableView *tableViewMO;
    QGridLayout *gridLayout;
    QLabel *label_2;
    QLabel *label_7;
    QComboBox *comboBoxMOBus;
    QComboBox *comboBoxMOChannel;
    QLabel *label_4;
    QComboBox *comboBoxMOType;
    QSpacerItem *verticalSpacer;
    QLabel *label_6;
    QTableView *tableViewZIP;
    QHBoxLayout *horizontalLayout_2;
    QLabel *label_3;
    QComboBox *comboBoxZIPBus;
    QLabel *label_5;
    QComboBox *comboBoxZIPChannel;
    QCheckBox *checkBoxZIP250;

    void setupUi(QWidget *SettingsOtherRemovable)
    {
        if (SettingsOtherRemovable->objectName().isEmpty())
            SettingsOtherRemovable->setObjectName(QString::fromUtf8("SettingsOtherRemovable"));
        SettingsOtherRemovable->resize(418, 433);
        verticalLayout = new QVBoxLayout(SettingsOtherRemovable);
        verticalLayout->setObjectName(QString::fromUtf8("verticalLayout"));
        verticalLayout->setContentsMargins(0, 0, 0, 0);
        label = new QLabel(SettingsOtherRemovable);
        label->setObjectName(QString::fromUtf8("label"));

        verticalLayout->addWidget(label);

        tableViewMO = new QTableView(SettingsOtherRemovable);
        tableViewMO->setObjectName(QString::fromUtf8("tableViewMO"));
        tableViewMO->setEditTriggers(QAbstractItemView::NoEditTriggers);
        tableViewMO->setSelectionMode(QAbstractItemView::SingleSelection);
        tableViewMO->setSelectionBehavior(QAbstractItemView::SelectRows);
        tableViewMO->setShowGrid(false);
        tableViewMO->verticalHeader()->setVisible(false);

        verticalLayout->addWidget(tableViewMO);

        gridLayout = new QGridLayout();
        gridLayout->setObjectName(QString::fromUtf8("gridLayout"));
        label_2 = new QLabel(SettingsOtherRemovable);
        label_2->setObjectName(QString::fromUtf8("label_2"));

        gridLayout->addWidget(label_2, 0, 0, 1, 1);

        label_7 = new QLabel(SettingsOtherRemovable);
        label_7->setObjectName(QString::fromUtf8("label_7"));

        gridLayout->addWidget(label_7, 0, 2, 1, 1);

        comboBoxMOBus = new QComboBox(SettingsOtherRemovable);
        comboBoxMOBus->setObjectName(QString::fromUtf8("comboBoxMOBus"));
        comboBoxMOBus->setMaxVisibleItems(30);

        gridLayout->addWidget(comboBoxMOBus, 0, 1, 1, 1);

        comboBoxMOChannel = new QComboBox(SettingsOtherRemovable);
        comboBoxMOChannel->setObjectName(QString::fromUtf8("comboBoxMOChannel"));
        comboBoxMOChannel->setMaxVisibleItems(30);

        gridLayout->addWidget(comboBoxMOChannel, 0, 3, 1, 1);

        label_4 = new QLabel(SettingsOtherRemovable);
        label_4->setObjectName(QString::fromUtf8("label_4"));

        gridLayout->addWidget(label_4, 1, 0, 1, 1);

        comboBoxMOType = new QComboBox(SettingsOtherRemovable);
        comboBoxMOType->setObjectName(QString::fromUtf8("comboBoxMOType"));
        comboBoxMOType->setMaxVisibleItems(30);

        gridLayout->addWidget(comboBoxMOType, 1, 1, 1, 3);


        verticalLayout->addLayout(gridLayout);

        verticalSpacer = new QSpacerItem(20, 40, QSizePolicy::Minimum, QSizePolicy::Expanding);

        verticalLayout->addItem(verticalSpacer);

        label_6 = new QLabel(SettingsOtherRemovable);
        label_6->setObjectName(QString::fromUtf8("label_6"));

        verticalLayout->addWidget(label_6);

        tableViewZIP = new QTableView(SettingsOtherRemovable);
        tableViewZIP->setObjectName(QString::fromUtf8("tableViewZIP"));
        tableViewZIP->setEditTriggers(QAbstractItemView::NoEditTriggers);
        tableViewZIP->setSelectionMode(QAbstractItemView::SingleSelection);
        tableViewZIP->setSelectionBehavior(QAbstractItemView::SelectRows);
        tableViewZIP->setShowGrid(false);
        tableViewZIP->verticalHeader()->setVisible(false);

        verticalLayout->addWidget(tableViewZIP);

        horizontalLayout_2 = new QHBoxLayout();
        horizontalLayout_2->setObjectName(QString::fromUtf8("horizontalLayout_2"));
        label_3 = new QLabel(SettingsOtherRemovable);
        label_3->setObjectName(QString::fromUtf8("label_3"));

        horizontalLayout_2->addWidget(label_3);

        comboBoxZIPBus = new QComboBox(SettingsOtherRemovable);
        comboBoxZIPBus->setObjectName(QString::fromUtf8("comboBoxZIPBus"));
        comboBoxZIPBus->setMaxVisibleItems(30);

        horizontalLayout_2->addWidget(comboBoxZIPBus);

        label_5 = new QLabel(SettingsOtherRemovable);
        label_5->setObjectName(QString::fromUtf8("label_5"));

        horizontalLayout_2->addWidget(label_5);

        comboBoxZIPChannel = new QComboBox(SettingsOtherRemovable);
        comboBoxZIPChannel->setObjectName(QString::fromUtf8("comboBoxZIPChannel"));
        comboBoxZIPChannel->setMaxVisibleItems(30);

        horizontalLayout_2->addWidget(comboBoxZIPChannel);

        checkBoxZIP250 = new QCheckBox(SettingsOtherRemovable);
        checkBoxZIP250->setObjectName(QString::fromUtf8("checkBoxZIP250"));

        horizontalLayout_2->addWidget(checkBoxZIP250);


        verticalLayout->addLayout(horizontalLayout_2);


        retranslateUi(SettingsOtherRemovable);

        QMetaObject::connectSlotsByName(SettingsOtherRemovable);
    } // setupUi

    void retranslateUi(QWidget *SettingsOtherRemovable)
    {
        SettingsOtherRemovable->setWindowTitle(QCoreApplication::translate("SettingsOtherRemovable", "Form", nullptr));
        label->setText(QCoreApplication::translate("SettingsOtherRemovable", "MO drives:", nullptr));
        label_2->setText(QCoreApplication::translate("SettingsOtherRemovable", "Bus:", nullptr));
        label_7->setText(QCoreApplication::translate("SettingsOtherRemovable", "Channel:", nullptr));
        label_4->setText(QCoreApplication::translate("SettingsOtherRemovable", "Type:", nullptr));
        label_6->setText(QCoreApplication::translate("SettingsOtherRemovable", "ZIP drives:", nullptr));
        label_3->setText(QCoreApplication::translate("SettingsOtherRemovable", "Bus:", nullptr));
        label_5->setText(QCoreApplication::translate("SettingsOtherRemovable", "Channel:", nullptr));
        checkBoxZIP250->setText(QCoreApplication::translate("SettingsOtherRemovable", "ZIP 250", nullptr));
    } // retranslateUi

};

namespace Ui {
    class SettingsOtherRemovable: public Ui_SettingsOtherRemovable {};
} // namespace Ui

QT_END_NAMESPACE

#endif // UI_QT_SETTINGSOTHERREMOVABLE_H
