/********************************************************************************
** Form generated from reading UI file 'qt_settingsnetwork.ui'
**
** Created by: Qt User Interface Compiler version 6.2.2
**
** WARNING! All changes made in this file will be lost when recompiling UI file!
********************************************************************************/

#ifndef UI_QT_SETTINGSNETWORK_H
#define UI_QT_SETTINGSNETWORK_H

#include <QtCore/QVariant>
#include <QtWidgets/QApplication>
#include <QtWidgets/QComboBox>
#include <QtWidgets/QGridLayout>
#include <QtWidgets/QHBoxLayout>
#include <QtWidgets/QLabel>
#include <QtWidgets/QPushButton>
#include <QtWidgets/QSpacerItem>
#include <QtWidgets/QWidget>

QT_BEGIN_NAMESPACE

class Ui_SettingsNetwork
{
public:
    QGridLayout *gridLayout;
    QLabel *label_2;
    QLabel *label_3;
    QComboBox *comboBoxPcap;
    QSpacerItem *verticalSpacer;
    QComboBox *comboBoxNetwork;
    QLabel *label;
    QWidget *widget;
    QHBoxLayout *horizontalLayout;
    QComboBox *comboBoxAdapter;
    QPushButton *pushButtonConfigure;

    void setupUi(QWidget *SettingsNetwork)
    {
        if (SettingsNetwork->objectName().isEmpty())
            SettingsNetwork->setObjectName(QString::fromUtf8("SettingsNetwork"));
        SettingsNetwork->resize(400, 300);
        gridLayout = new QGridLayout(SettingsNetwork);
        gridLayout->setObjectName(QString::fromUtf8("gridLayout"));
        gridLayout->setContentsMargins(0, 0, 0, 0);
        label_2 = new QLabel(SettingsNetwork);
        label_2->setObjectName(QString::fromUtf8("label_2"));

        gridLayout->addWidget(label_2, 1, 0, 1, 1);

        label_3 = new QLabel(SettingsNetwork);
        label_3->setObjectName(QString::fromUtf8("label_3"));

        gridLayout->addWidget(label_3, 2, 0, 1, 1);

        comboBoxPcap = new QComboBox(SettingsNetwork);
        comboBoxPcap->setObjectName(QString::fromUtf8("comboBoxPcap"));

        gridLayout->addWidget(comboBoxPcap, 1, 1, 1, 1);

        verticalSpacer = new QSpacerItem(20, 40, QSizePolicy::Minimum, QSizePolicy::Expanding);

        gridLayout->addItem(verticalSpacer, 3, 0, 1, 1);

        comboBoxNetwork = new QComboBox(SettingsNetwork);
        comboBoxNetwork->setObjectName(QString::fromUtf8("comboBoxNetwork"));

        gridLayout->addWidget(comboBoxNetwork, 0, 1, 1, 1);

        label = new QLabel(SettingsNetwork);
        label->setObjectName(QString::fromUtf8("label"));

        gridLayout->addWidget(label, 0, 0, 1, 1);

        widget = new QWidget(SettingsNetwork);
        widget->setObjectName(QString::fromUtf8("widget"));
        horizontalLayout = new QHBoxLayout(widget);
        horizontalLayout->setObjectName(QString::fromUtf8("horizontalLayout"));
        horizontalLayout->setContentsMargins(0, 0, 0, 0);
        comboBoxAdapter = new QComboBox(widget);
        comboBoxAdapter->setObjectName(QString::fromUtf8("comboBoxAdapter"));

        horizontalLayout->addWidget(comboBoxAdapter);

        pushButtonConfigure = new QPushButton(widget);
        pushButtonConfigure->setObjectName(QString::fromUtf8("pushButtonConfigure"));

        horizontalLayout->addWidget(pushButtonConfigure);


        gridLayout->addWidget(widget, 2, 1, 1, 1);


        retranslateUi(SettingsNetwork);

        QMetaObject::connectSlotsByName(SettingsNetwork);
    } // setupUi

    void retranslateUi(QWidget *SettingsNetwork)
    {
        SettingsNetwork->setWindowTitle(QCoreApplication::translate("SettingsNetwork", "Form", nullptr));
        label_2->setText(QCoreApplication::translate("SettingsNetwork", "PCap device:", nullptr));
        label_3->setText(QCoreApplication::translate("SettingsNetwork", "Network adapter:", nullptr));
        label->setText(QCoreApplication::translate("SettingsNetwork", "Network type:", nullptr));
        pushButtonConfigure->setText(QCoreApplication::translate("SettingsNetwork", "Configure", nullptr));
    } // retranslateUi

};

namespace Ui {
    class SettingsNetwork: public Ui_SettingsNetwork {};
} // namespace Ui

QT_END_NAMESPACE

#endif // UI_QT_SETTINGSNETWORK_H
