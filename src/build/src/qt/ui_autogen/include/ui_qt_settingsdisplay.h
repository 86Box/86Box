/********************************************************************************
** Form generated from reading UI file 'qt_settingsdisplay.ui'
**
** Created by: Qt User Interface Compiler version 5.15.10
**
** WARNING! All changes made in this file will be lost when recompiling UI file!
********************************************************************************/

#ifndef UI_QT_SETTINGSDISPLAY_H
#define UI_QT_SETTINGSDISPLAY_H

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

class Ui_SettingsDisplay
{
public:
    QGridLayout *gridLayout;
    QPushButton *pushButtonConfigure;
    QCheckBox *checkBoxXga;
    QLabel *label;
    QComboBox *comboBoxVideo;
    QPushButton *pushButtonConfigureVoodoo;
    QLabel *label_2;
    QCheckBox *checkBox8514;
    QCheckBox *checkBoxVoodoo;
    QPushButton *pushButtonConfigureXga;
    QPushButton *pushButtonConfigureSecondary;
    QComboBox *comboBoxVideoSecondary;
    QSpacerItem *verticalSpacer;

    void setupUi(QWidget *SettingsDisplay)
    {
        if (SettingsDisplay->objectName().isEmpty())
            SettingsDisplay->setObjectName(QString::fromUtf8("SettingsDisplay"));
        SettingsDisplay->resize(400, 300);
        gridLayout = new QGridLayout(SettingsDisplay);
        gridLayout->setObjectName(QString::fromUtf8("gridLayout"));
        gridLayout->setContentsMargins(0, 0, 0, 0);
        pushButtonConfigure = new QPushButton(SettingsDisplay);
        pushButtonConfigure->setObjectName(QString::fromUtf8("pushButtonConfigure"));
        QSizePolicy sizePolicy(QSizePolicy::Minimum, QSizePolicy::Fixed);
        sizePolicy.setHorizontalStretch(0);
        sizePolicy.setVerticalStretch(0);
        sizePolicy.setHeightForWidth(pushButtonConfigure->sizePolicy().hasHeightForWidth());
        pushButtonConfigure->setSizePolicy(sizePolicy);

        gridLayout->addWidget(pushButtonConfigure, 1, 2, 1, 1);

        checkBoxXga = new QCheckBox(SettingsDisplay);
        checkBoxXga->setObjectName(QString::fromUtf8("checkBoxXga"));

        gridLayout->addWidget(checkBoxXga, 6, 0, 1, 2);

        label = new QLabel(SettingsDisplay);
        label->setObjectName(QString::fromUtf8("label"));
        QSizePolicy sizePolicy1(QSizePolicy::Minimum, QSizePolicy::Preferred);
        sizePolicy1.setHorizontalStretch(0);
        sizePolicy1.setVerticalStretch(0);
        sizePolicy1.setHeightForWidth(label->sizePolicy().hasHeightForWidth());
        label->setSizePolicy(sizePolicy1);

        gridLayout->addWidget(label, 0, 0, 1, 1);

        comboBoxVideo = new QComboBox(SettingsDisplay);
        comboBoxVideo->setObjectName(QString::fromUtf8("comboBoxVideo"));
        comboBoxVideo->setMaxVisibleItems(30);
        QSizePolicy sizePolicy2(QSizePolicy::Expanding, QSizePolicy::Fixed);
        sizePolicy2.setHorizontalStretch(0);
        sizePolicy2.setVerticalStretch(0);
        sizePolicy2.setHeightForWidth(comboBoxVideo->sizePolicy().hasHeightForWidth());
        comboBoxVideo->setSizePolicy(sizePolicy2);

        gridLayout->addWidget(comboBoxVideo, 1, 0, 1, 2);

        pushButtonConfigureVoodoo = new QPushButton(SettingsDisplay);
        pushButtonConfigureVoodoo->setObjectName(QString::fromUtf8("pushButtonConfigureVoodoo"));

        gridLayout->addWidget(pushButtonConfigureVoodoo, 4, 2, 1, 1);

        label_2 = new QLabel(SettingsDisplay);
        label_2->setObjectName(QString::fromUtf8("label_2"));
        sizePolicy1.setHeightForWidth(label_2->sizePolicy().hasHeightForWidth());
        label_2->setSizePolicy(sizePolicy1);

        gridLayout->addWidget(label_2, 2, 0, 1, 1);

        checkBox8514 = new QCheckBox(SettingsDisplay);
        checkBox8514->setObjectName(QString::fromUtf8("checkBox8514"));

        gridLayout->addWidget(checkBox8514, 5, 0, 1, 2);

        checkBoxVoodoo = new QCheckBox(SettingsDisplay);
        checkBoxVoodoo->setObjectName(QString::fromUtf8("checkBoxVoodoo"));

        gridLayout->addWidget(checkBoxVoodoo, 4, 0, 1, 2);

        pushButtonConfigureXga = new QPushButton(SettingsDisplay);
        pushButtonConfigureXga->setObjectName(QString::fromUtf8("pushButtonConfigureXga"));

        gridLayout->addWidget(pushButtonConfigureXga, 6, 2, 1, 1);

        pushButtonConfigureSecondary = new QPushButton(SettingsDisplay);
        pushButtonConfigureSecondary->setObjectName(QString::fromUtf8("pushButtonConfigureSecondary"));

        gridLayout->addWidget(pushButtonConfigureSecondary, 3, 2, 1, 1);

        comboBoxVideoSecondary = new QComboBox(SettingsDisplay);
        comboBoxVideoSecondary->setObjectName(QString::fromUtf8("comboBoxVideoSecondary"));
        comboBoxVideoSecondary->setMaxVisibleItems(30);
        sizePolicy2.setHeightForWidth(comboBoxVideoSecondary->sizePolicy().hasHeightForWidth());
        comboBoxVideoSecondary->setSizePolicy(sizePolicy2);

        gridLayout->addWidget(comboBoxVideoSecondary, 3, 0, 1, 2);

        verticalSpacer = new QSpacerItem(20, 40, QSizePolicy::Minimum, QSizePolicy::Expanding);

        gridLayout->addItem(verticalSpacer, 7, 0, 1, 3);

        gridLayout->setColumnStretch(0, 2);
        gridLayout->setColumnStretch(1, 1);
        gridLayout->setColumnStretch(2, 1);

        retranslateUi(SettingsDisplay);

        QMetaObject::connectSlotsByName(SettingsDisplay);
    } // setupUi

    void retranslateUi(QWidget *SettingsDisplay)
    {
        SettingsDisplay->setWindowTitle(QCoreApplication::translate("SettingsDisplay", "Form", nullptr));
        pushButtonConfigure->setText(QCoreApplication::translate("SettingsDisplay", "Configure", nullptr));
        checkBoxXga->setText(QCoreApplication::translate("SettingsDisplay", "XGA", nullptr));
        label->setText(QCoreApplication::translate("SettingsDisplay", "Video:", nullptr));
        pushButtonConfigureVoodoo->setText(QCoreApplication::translate("SettingsDisplay", "Configure", nullptr));
        label_2->setText(QCoreApplication::translate("SettingsDisplay", "Video #2:", nullptr));
        checkBox8514->setText(QCoreApplication::translate("SettingsDisplay", "8514/A", nullptr));
        checkBoxVoodoo->setText(QCoreApplication::translate("SettingsDisplay", "Voodoo Graphics", nullptr));
        pushButtonConfigureXga->setText(QCoreApplication::translate("SettingsDisplay", "Configure", nullptr));
        pushButtonConfigureSecondary->setText(QCoreApplication::translate("SettingsDisplay", "Configure", nullptr));
    } // retranslateUi

};

namespace Ui {
    class SettingsDisplay: public Ui_SettingsDisplay {};
} // namespace Ui

QT_END_NAMESPACE

#endif // UI_QT_SETTINGSDISPLAY_H
