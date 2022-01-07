/********************************************************************************
** Form generated from reading UI file 'qt_specifydimensions.ui'
**
** Created by: Qt User Interface Compiler version 6.2.2
**
** WARNING! All changes made in this file will be lost when recompiling UI file!
********************************************************************************/

#ifndef UI_QT_SPECIFYDIMENSIONS_H
#define UI_QT_SPECIFYDIMENSIONS_H

#include <QtCore/QVariant>
#include <QtWidgets/QAbstractButton>
#include <QtWidgets/QApplication>
#include <QtWidgets/QCheckBox>
#include <QtWidgets/QDialog>
#include <QtWidgets/QDialogButtonBox>
#include <QtWidgets/QLabel>
#include <QtWidgets/QSpinBox>

QT_BEGIN_NAMESPACE

class Ui_SpecifyDimensions
{
public:
    QDialogButtonBox *buttonBox;
    QSpinBox *spinBoxWidth;
    QLabel *labelWidth;
    QCheckBox *checkBox;
    QLabel *labelHeight;
    QSpinBox *spinBoxHeight;

    void setupUi(QDialog *SpecifyDimensions)
    {
        if (SpecifyDimensions->objectName().isEmpty())
            SpecifyDimensions->setObjectName(QString::fromUtf8("SpecifyDimensions"));
        SpecifyDimensions->resize(388, 158);
        buttonBox = new QDialogButtonBox(SpecifyDimensions);
        buttonBox->setObjectName(QString::fromUtf8("buttonBox"));
        buttonBox->setGeometry(QRect(20, 110, 361, 32));
        buttonBox->setOrientation(Qt::Horizontal);
        buttonBox->setStandardButtons(QDialogButtonBox::Cancel|QDialogButtonBox::Ok);
        spinBoxWidth = new QSpinBox(SpecifyDimensions);
        spinBoxWidth->setObjectName(QString::fromUtf8("spinBoxWidth"));
        spinBoxWidth->setGeometry(QRect(70, 50, 81, 21));
        labelWidth = new QLabel(SpecifyDimensions);
        labelWidth->setObjectName(QString::fromUtf8("labelWidth"));
        labelWidth->setGeometry(QRect(30, 50, 41, 21));
        checkBox = new QCheckBox(SpecifyDimensions);
        checkBox->setObjectName(QString::fromUtf8("checkBox"));
        checkBox->setGeometry(QRect(20, 90, 131, 23));
        labelHeight = new QLabel(SpecifyDimensions);
        labelHeight->setObjectName(QString::fromUtf8("labelHeight"));
        labelHeight->setGeometry(QRect(200, 50, 51, 21));
        spinBoxHeight = new QSpinBox(SpecifyDimensions);
        spinBoxHeight->setObjectName(QString::fromUtf8("spinBoxHeight"));
        spinBoxHeight->setGeometry(QRect(250, 50, 81, 21));

        retranslateUi(SpecifyDimensions);
        QObject::connect(buttonBox, &QDialogButtonBox::accepted, SpecifyDimensions, qOverload<>(&QDialog::accept));
        QObject::connect(buttonBox, &QDialogButtonBox::rejected, SpecifyDimensions, qOverload<>(&QDialog::reject));

        QMetaObject::connectSlotsByName(SpecifyDimensions);
    } // setupUi

    void retranslateUi(QDialog *SpecifyDimensions)
    {
        SpecifyDimensions->setWindowTitle(QCoreApplication::translate("SpecifyDimensions", "Specify Main Window Dimensions", nullptr));
        labelWidth->setText(QCoreApplication::translate("SpecifyDimensions", "Width:", nullptr));
        checkBox->setText(QCoreApplication::translate("SpecifyDimensions", "Lock to this size", nullptr));
        labelHeight->setText(QCoreApplication::translate("SpecifyDimensions", "Height:", nullptr));
    } // retranslateUi

};

namespace Ui {
    class SpecifyDimensions: public Ui_SpecifyDimensions {};
} // namespace Ui

QT_END_NAMESPACE

#endif // UI_QT_SPECIFYDIMENSIONS_H
