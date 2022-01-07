/********************************************************************************
** Form generated from reading UI file 'qt_progsettings.ui'
**
** Created by: Qt User Interface Compiler version 6.2.2
**
** WARNING! All changes made in this file will be lost when recompiling UI file!
********************************************************************************/

#ifndef UI_QT_PROGSETTINGS_H
#define UI_QT_PROGSETTINGS_H

#include <QtCore/QVariant>
#include <QtWidgets/QAbstractButton>
#include <QtWidgets/QApplication>
#include <QtWidgets/QComboBox>
#include <QtWidgets/QDialog>
#include <QtWidgets/QDialogButtonBox>
#include <QtWidgets/QLabel>
#include <QtWidgets/QPushButton>

QT_BEGIN_NAMESPACE

class Ui_ProgSettings
{
public:
    QDialogButtonBox *buttonBox;
    QComboBox *comboBox;
    QLabel *label;
    QPushButton *pushButton;

    void setupUi(QDialog *ProgSettings)
    {
        if (ProgSettings->objectName().isEmpty())
            ProgSettings->setObjectName(QString::fromUtf8("ProgSettings"));
        ProgSettings->resize(400, 300);
        buttonBox = new QDialogButtonBox(ProgSettings);
        buttonBox->setObjectName(QString::fromUtf8("buttonBox"));
        buttonBox->setGeometry(QRect(30, 240, 341, 32));
        buttonBox->setOrientation(Qt::Horizontal);
        buttonBox->setStandardButtons(QDialogButtonBox::Cancel|QDialogButtonBox::Ok);
        comboBox = new QComboBox(ProgSettings);
        comboBox->addItem(QString());
        comboBox->setObjectName(QString::fromUtf8("comboBox"));
        comboBox->setGeometry(QRect(20, 80, 351, 25));
        comboBox->setEditable(false);
        label = new QLabel(ProgSettings);
        label->setObjectName(QString::fromUtf8("label"));
        label->setGeometry(QRect(20, 60, 54, 17));
        pushButton = new QPushButton(ProgSettings);
        pushButton->setObjectName(QString::fromUtf8("pushButton"));
        pushButton->setGeometry(QRect(290, 110, 80, 25));

        retranslateUi(ProgSettings);
        QObject::connect(buttonBox, &QDialogButtonBox::accepted, ProgSettings, qOverload<>(&QDialog::accept));
        QObject::connect(buttonBox, &QDialogButtonBox::rejected, ProgSettings, qOverload<>(&QDialog::reject));

        QMetaObject::connectSlotsByName(ProgSettings);
    } // setupUi

    void retranslateUi(QDialog *ProgSettings)
    {
        ProgSettings->setWindowTitle(QCoreApplication::translate("ProgSettings", "Preferences", nullptr));
        comboBox->setItemText(0, QCoreApplication::translate("ProgSettings", "(Default)", nullptr));

        label->setText(QCoreApplication::translate("ProgSettings", "Icon set:", nullptr));
        pushButton->setText(QCoreApplication::translate("ProgSettings", "Default", nullptr));
    } // retranslateUi

};

namespace Ui {
    class ProgSettings: public Ui_ProgSettings {};
} // namespace Ui

QT_END_NAMESPACE

#endif // UI_QT_PROGSETTINGS_H
