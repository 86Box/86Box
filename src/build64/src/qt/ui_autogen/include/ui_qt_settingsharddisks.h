/********************************************************************************
** Form generated from reading UI file 'qt_settingsharddisks.ui'
**
** Created by: Qt User Interface Compiler version 5.15.10
**
** WARNING! All changes made in this file will be lost when recompiling UI file!
********************************************************************************/

#ifndef UI_QT_SETTINGSHARDDISKS_H
#define UI_QT_SETTINGSHARDDISKS_H

#include <QtCore/QVariant>
#include <QtWidgets/QApplication>
#include <QtWidgets/QComboBox>
#include <QtWidgets/QHBoxLayout>
#include <QtWidgets/QHeaderView>
#include <QtWidgets/QLabel>
#include <QtWidgets/QPushButton>
#include <QtWidgets/QTableView>
#include <QtWidgets/QVBoxLayout>
#include <QtWidgets/QWidget>

QT_BEGIN_NAMESPACE

class Ui_SettingsHarddisks
{
public:
    QVBoxLayout *verticalLayout;
    QTableView *tableView;
    QHBoxLayout *horizontalLayout_2;
    QLabel *labelBus;
    QComboBox *comboBoxBus;
    QLabel *labelChannel;
    QComboBox *comboBoxChannel;
    QLabel *labelSpeed;
    QComboBox *comboBoxSpeed;
    QHBoxLayout *horizontalLayout;
    QPushButton *pushButtonNew;
    QPushButton *pushButtonExisting;
    QPushButton *pushButtonRemove;

    void setupUi(QWidget *SettingsHarddisks)
    {
        if (SettingsHarddisks->objectName().isEmpty())
            SettingsHarddisks->setObjectName(QString::fromUtf8("SettingsHarddisks"));
        SettingsHarddisks->resize(400, 300);
        verticalLayout = new QVBoxLayout(SettingsHarddisks);
        verticalLayout->setObjectName(QString::fromUtf8("verticalLayout"));
        verticalLayout->setContentsMargins(0, 0, 0, 0);
        tableView = new QTableView(SettingsHarddisks);
        tableView->setObjectName(QString::fromUtf8("tableView"));
        tableView->setEditTriggers(QAbstractItemView::NoEditTriggers);
        tableView->setSelectionMode(QAbstractItemView::SingleSelection);
        tableView->setSelectionBehavior(QAbstractItemView::SelectRows);
        tableView->setShowGrid(false);
        tableView->verticalHeader()->setVisible(false);

        verticalLayout->addWidget(tableView);

        horizontalLayout_2 = new QHBoxLayout();
        horizontalLayout_2->setObjectName(QString::fromUtf8("horizontalLayout_2"));
        labelBus = new QLabel(SettingsHarddisks);
        labelBus->setObjectName(QString::fromUtf8("labelBus"));

        horizontalLayout_2->addWidget(labelBus);

        comboBoxBus = new QComboBox(SettingsHarddisks);
        comboBoxBus->setObjectName(QString::fromUtf8("comboBoxBus"));
        comboBoxBus->setMaxVisibleItems(30);

        horizontalLayout_2->addWidget(comboBoxBus);

        labelChannel = new QLabel(SettingsHarddisks);
        labelChannel->setObjectName(QString::fromUtf8("labelChannel"));

        horizontalLayout_2->addWidget(labelChannel);

        comboBoxChannel = new QComboBox(SettingsHarddisks);
        comboBoxChannel->setObjectName(QString::fromUtf8("comboBoxChannel"));
        comboBoxChannel->setMaxVisibleItems(30);

        horizontalLayout_2->addWidget(comboBoxChannel);

        labelSpeed = new QLabel(SettingsHarddisks);
        labelSpeed->setObjectName(QString::fromUtf8("labelSpeed"));

        horizontalLayout_2->addWidget(labelSpeed);

        comboBoxSpeed = new QComboBox(SettingsHarddisks);
        comboBoxSpeed->setObjectName(QString::fromUtf8("comboBoxSpeed"));
        comboBoxSpeed->setMaxVisibleItems(30);

        horizontalLayout_2->addWidget(comboBoxSpeed);


        verticalLayout->addLayout(horizontalLayout_2);

        horizontalLayout = new QHBoxLayout();
        horizontalLayout->setObjectName(QString::fromUtf8("horizontalLayout"));
        pushButtonNew = new QPushButton(SettingsHarddisks);
        pushButtonNew->setObjectName(QString::fromUtf8("pushButtonNew"));

        horizontalLayout->addWidget(pushButtonNew);

        pushButtonExisting = new QPushButton(SettingsHarddisks);
        pushButtonExisting->setObjectName(QString::fromUtf8("pushButtonExisting"));

        horizontalLayout->addWidget(pushButtonExisting);

        pushButtonRemove = new QPushButton(SettingsHarddisks);
        pushButtonRemove->setObjectName(QString::fromUtf8("pushButtonRemove"));

        horizontalLayout->addWidget(pushButtonRemove);


        verticalLayout->addLayout(horizontalLayout);


        retranslateUi(SettingsHarddisks);

        QMetaObject::connectSlotsByName(SettingsHarddisks);
    } // setupUi

    void retranslateUi(QWidget *SettingsHarddisks)
    {
        SettingsHarddisks->setWindowTitle(QCoreApplication::translate("SettingsHarddisks", "Form", nullptr));
        labelBus->setText(QCoreApplication::translate("SettingsHarddisks", "Bus:", nullptr));
        labelChannel->setText(QCoreApplication::translate("SettingsHarddisks", "ID:", nullptr));
        labelSpeed->setText(QCoreApplication::translate("SettingsHarddisks", "Speed:", nullptr));
        pushButtonNew->setText(QCoreApplication::translate("SettingsHarddisks", "&New...", nullptr));
        pushButtonExisting->setText(QCoreApplication::translate("SettingsHarddisks", "&Existing...", nullptr));
        pushButtonRemove->setText(QCoreApplication::translate("SettingsHarddisks", "&Remove", nullptr));
    } // retranslateUi

};

namespace Ui {
    class SettingsHarddisks: public Ui_SettingsHarddisks {};
} // namespace Ui

QT_END_NAMESPACE

#endif // UI_QT_SETTINGSHARDDISKS_H
