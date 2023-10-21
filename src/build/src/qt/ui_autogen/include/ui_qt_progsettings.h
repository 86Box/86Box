/********************************************************************************
** Form generated from reading UI file 'qt_progsettings.ui'
**
** Created by: Qt User Interface Compiler version 5.15.10
**
** WARNING! All changes made in this file will be lost when recompiling UI file!
********************************************************************************/

#ifndef UI_QT_PROGSETTINGS_H
#define UI_QT_PROGSETTINGS_H

#include <QtCore/QVariant>
#include <QtWidgets/QApplication>
#include <QtWidgets/QCheckBox>
#include <QtWidgets/QComboBox>
#include <QtWidgets/QDialog>
#include <QtWidgets/QDialogButtonBox>
#include <QtWidgets/QGridLayout>
#include <QtWidgets/QLabel>
#include <QtWidgets/QPushButton>
#include <QtWidgets/QSlider>
#include <QtWidgets/QSpacerItem>

QT_BEGIN_NAMESPACE

class Ui_ProgSettings
{
public:
    QGridLayout *gridLayout;
    QComboBox *comboBox;
    QLabel *label_3;
    QLabel *label;
    QPushButton *pushButton;
    QSpacerItem *horizontalSpacer_3;
    QDialogButtonBox *buttonBox;
    QSlider *horizontalSlider;
    QComboBox *comboBoxLanguage;
    QLabel *label_2;
    QPushButton *pushButtonLanguage;
    QPushButton *pushButton_2;
    QSpacerItem *horizontalSpacer_2;
    QSpacerItem *horizontalSpacer;
    QCheckBox *openDirUsrPath;

    void setupUi(QDialog *ProgSettings)
    {
        if (ProgSettings->objectName().isEmpty())
            ProgSettings->setObjectName(QString::fromUtf8("ProgSettings"));
        ProgSettings->resize(458, 374);
        ProgSettings->setMinimumSize(QSize(0, 0));
        ProgSettings->setMaximumSize(QSize(16777215, 16777215));
        gridLayout = new QGridLayout(ProgSettings);
        gridLayout->setObjectName(QString::fromUtf8("gridLayout"));
        gridLayout->setSizeConstraint(QLayout::SetFixedSize);
        comboBox = new QComboBox(ProgSettings);
        comboBox->addItem(QString());
        comboBox->setObjectName(QString::fromUtf8("comboBox"));
        comboBox->setEditable(false);
        comboBox->setMaxVisibleItems(30);

        gridLayout->addWidget(comboBox, 1, 0, 1, 2);

        label_3 = new QLabel(ProgSettings);
        label_3->setObjectName(QString::fromUtf8("label_3"));

        gridLayout->addWidget(label_3, 6, 0, 1, 1);

        label = new QLabel(ProgSettings);
        label->setObjectName(QString::fromUtf8("label"));

        gridLayout->addWidget(label, 0, 0, 1, 2);

        pushButton = new QPushButton(ProgSettings);
        pushButton->setObjectName(QString::fromUtf8("pushButton"));

        gridLayout->addWidget(pushButton, 2, 1, 1, 1);

        horizontalSpacer_3 = new QSpacerItem(40, 20, QSizePolicy::Expanding, QSizePolicy::Minimum);

        gridLayout->addItem(horizontalSpacer_3, 8, 0, 1, 1);

        buttonBox = new QDialogButtonBox(ProgSettings);
        buttonBox->setObjectName(QString::fromUtf8("buttonBox"));
        buttonBox->setOrientation(Qt::Horizontal);
        buttonBox->setStandardButtons(QDialogButtonBox::Cancel|QDialogButtonBox::Ok);

        gridLayout->addWidget(buttonBox, 12, 0, 1, 2);

        horizontalSlider = new QSlider(ProgSettings);
        horizontalSlider->setObjectName(QString::fromUtf8("horizontalSlider"));
        horizontalSlider->setMinimum(10);
        horizontalSlider->setMaximum(200);
        horizontalSlider->setSingleStep(10);
        horizontalSlider->setPageStep(20);
        horizontalSlider->setValue(100);
        horizontalSlider->setOrientation(Qt::Horizontal);

        gridLayout->addWidget(horizontalSlider, 7, 0, 1, 2);

        comboBoxLanguage = new QComboBox(ProgSettings);
        comboBoxLanguage->addItem(QString());
        comboBoxLanguage->setObjectName(QString::fromUtf8("comboBoxLanguage"));
        comboBoxLanguage->setMaxVisibleItems(30);

        gridLayout->addWidget(comboBoxLanguage, 4, 0, 1, 2);

        label_2 = new QLabel(ProgSettings);
        label_2->setObjectName(QString::fromUtf8("label_2"));

        gridLayout->addWidget(label_2, 3, 0, 1, 2);

        pushButtonLanguage = new QPushButton(ProgSettings);
        pushButtonLanguage->setObjectName(QString::fromUtf8("pushButtonLanguage"));

        gridLayout->addWidget(pushButtonLanguage, 5, 1, 1, 1);

        pushButton_2 = new QPushButton(ProgSettings);
        pushButton_2->setObjectName(QString::fromUtf8("pushButton_2"));

        gridLayout->addWidget(pushButton_2, 8, 1, 1, 1);

        horizontalSpacer_2 = new QSpacerItem(40, 20, QSizePolicy::Expanding, QSizePolicy::Minimum);

        gridLayout->addItem(horizontalSpacer_2, 5, 0, 1, 1);

        horizontalSpacer = new QSpacerItem(40, 20, QSizePolicy::Expanding, QSizePolicy::Minimum);

        gridLayout->addItem(horizontalSpacer, 2, 0, 1, 1);

        openDirUsrPath = new QCheckBox(ProgSettings);
        openDirUsrPath->setObjectName(QString::fromUtf8("openDirUsrPath"));

        gridLayout->addWidget(openDirUsrPath, 9, 0, 1, 1);


        retranslateUi(ProgSettings);
        QObject::connect(buttonBox, SIGNAL(accepted()), ProgSettings, SLOT(accept()));
        QObject::connect(buttonBox, SIGNAL(rejected()), ProgSettings, SLOT(reject()));

        QMetaObject::connectSlotsByName(ProgSettings);
    } // setupUi

    void retranslateUi(QDialog *ProgSettings)
    {
        ProgSettings->setWindowTitle(QCoreApplication::translate("ProgSettings", "Preferences", nullptr));
        comboBox->setItemText(0, QCoreApplication::translate("ProgSettings", "(Default)", nullptr));

        label_3->setText(QCoreApplication::translate("ProgSettings", "Mouse sensitivity:", nullptr));
        label->setText(QCoreApplication::translate("ProgSettings", "Icon set:", nullptr));
        pushButton->setText(QCoreApplication::translate("ProgSettings", "Default", nullptr));
        comboBoxLanguage->setItemText(0, QCoreApplication::translate("ProgSettings", "(System Default)", nullptr));

        label_2->setText(QCoreApplication::translate("ProgSettings", "Language:", nullptr));
        pushButtonLanguage->setText(QCoreApplication::translate("ProgSettings", "Default", nullptr));
        pushButton_2->setText(QCoreApplication::translate("ProgSettings", "Default", nullptr));
#if QT_CONFIG(tooltip)
        openDirUsrPath->setToolTip(QCoreApplication::translate("ProgSettings", "<html><head/><body><p>When selecting media images (CD-ROM, floppy, etc.) the open dialog will start in the same directory as the 86Box configuration file. This setting will likely only make a difference on macOS.</p></body></html>", nullptr));
#endif // QT_CONFIG(tooltip)
        openDirUsrPath->setText(QCoreApplication::translate("ProgSettings", "Select media images from program working directory", nullptr));
    } // retranslateUi

};

namespace Ui {
    class ProgSettings: public Ui_ProgSettings {};
} // namespace Ui

QT_END_NAMESPACE

#endif // UI_QT_PROGSETTINGS_H
