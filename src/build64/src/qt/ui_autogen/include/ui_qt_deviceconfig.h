/********************************************************************************
** Form generated from reading UI file 'qt_deviceconfig.ui'
**
** Created by: Qt User Interface Compiler version 5.15.10
**
** WARNING! All changes made in this file will be lost when recompiling UI file!
********************************************************************************/

#ifndef UI_QT_DEVICECONFIG_H
#define UI_QT_DEVICECONFIG_H

#include <QtCore/QVariant>
#include <QtWidgets/QApplication>
#include <QtWidgets/QDialog>
#include <QtWidgets/QDialogButtonBox>
#include <QtWidgets/QFormLayout>
#include <QtWidgets/QFrame>
#include <QtWidgets/QVBoxLayout>

QT_BEGIN_NAMESPACE

class Ui_DeviceConfig
{
public:
    QVBoxLayout *verticalLayout;
    QFormLayout *formLayout;
    QFrame *line;
    QDialogButtonBox *buttonBox;

    void setupUi(QDialog *DeviceConfig)
    {
        if (DeviceConfig->objectName().isEmpty())
            DeviceConfig->setObjectName(QString::fromUtf8("DeviceConfig"));
        DeviceConfig->resize(400, 300);
        verticalLayout = new QVBoxLayout(DeviceConfig);
        verticalLayout->setObjectName(QString::fromUtf8("verticalLayout"));
        formLayout = new QFormLayout();
        formLayout->setObjectName(QString::fromUtf8("formLayout"));

        verticalLayout->addLayout(formLayout);

        line = new QFrame(DeviceConfig);
        line->setObjectName(QString::fromUtf8("line"));
        line->setFrameShape(QFrame::HLine);
        line->setFrameShadow(QFrame::Sunken);

        verticalLayout->addWidget(line);

        buttonBox = new QDialogButtonBox(DeviceConfig);
        buttonBox->setObjectName(QString::fromUtf8("buttonBox"));
        buttonBox->setOrientation(Qt::Horizontal);
        buttonBox->setStandardButtons(QDialogButtonBox::Cancel|QDialogButtonBox::Ok);

        verticalLayout->addWidget(buttonBox);


        retranslateUi(DeviceConfig);
        QObject::connect(buttonBox, SIGNAL(accepted()), DeviceConfig, SLOT(accept()));
        QObject::connect(buttonBox, SIGNAL(rejected()), DeviceConfig, SLOT(reject()));

        QMetaObject::connectSlotsByName(DeviceConfig);
    } // setupUi

    void retranslateUi(QDialog *DeviceConfig)
    {
        DeviceConfig->setWindowTitle(QCoreApplication::translate("DeviceConfig", "Dialog", nullptr));
    } // retranslateUi

};

namespace Ui {
    class DeviceConfig: public Ui_DeviceConfig {};
} // namespace Ui

QT_END_NAMESPACE

#endif // UI_QT_DEVICECONFIG_H
