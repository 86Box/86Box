/********************************************************************************
** Form generated from reading UI file 'qt_soundgain.ui'
**
** Created by: Qt User Interface Compiler version 6.2.2
**
** WARNING! All changes made in this file will be lost when recompiling UI file!
********************************************************************************/

#ifndef UI_QT_SOUNDGAIN_H
#define UI_QT_SOUNDGAIN_H

#include <QtCore/QVariant>
#include <QtWidgets/QAbstractButton>
#include <QtWidgets/QApplication>
#include <QtWidgets/QDialog>
#include <QtWidgets/QDialogButtonBox>
#include <QtWidgets/QLabel>
#include <QtWidgets/QSlider>

QT_BEGIN_NAMESPACE

class Ui_SoundGain
{
public:
    QDialogButtonBox *buttonBox;
    QSlider *verticalSlider;
    QLabel *label;

    void setupUi(QDialog *SoundGain)
    {
        if (SoundGain->objectName().isEmpty())
            SoundGain->setObjectName(QString::fromUtf8("SoundGain"));
        SoundGain->resize(262, 279);
        buttonBox = new QDialogButtonBox(SoundGain);
        buttonBox->setObjectName(QString::fromUtf8("buttonBox"));
        buttonBox->setGeometry(QRect(140, 20, 101, 241));
        buttonBox->setOrientation(Qt::Vertical);
        buttonBox->setStandardButtons(QDialogButtonBox::Cancel|QDialogButtonBox::Ok);
        verticalSlider = new QSlider(SoundGain);
        verticalSlider->setObjectName(QString::fromUtf8("verticalSlider"));
        verticalSlider->setGeometry(QRect(30, 30, 31, 231));
        verticalSlider->setMaximum(18);
        verticalSlider->setSingleStep(2);
        verticalSlider->setPageStep(4);
        verticalSlider->setOrientation(Qt::Vertical);
        verticalSlider->setInvertedAppearance(false);
        verticalSlider->setTickPosition(QSlider::TicksBothSides);
        label = new QLabel(SoundGain);
        label->setObjectName(QString::fromUtf8("label"));
        label->setGeometry(QRect(-20, 10, 131, 20));
        label->setAlignment(Qt::AlignCenter);

        retranslateUi(SoundGain);
        QObject::connect(buttonBox, &QDialogButtonBox::accepted, SoundGain, qOverload<>(&QDialog::accept));
        QObject::connect(buttonBox, &QDialogButtonBox::rejected, SoundGain, qOverload<>(&QDialog::reject));

        QMetaObject::connectSlotsByName(SoundGain);
    } // setupUi

    void retranslateUi(QDialog *SoundGain)
    {
        SoundGain->setWindowTitle(QCoreApplication::translate("SoundGain", "Sound Gain", nullptr));
        label->setText(QCoreApplication::translate("SoundGain", "Gain", nullptr));
    } // retranslateUi

};

namespace Ui {
    class SoundGain: public Ui_SoundGain {};
} // namespace Ui

QT_END_NAMESPACE

#endif // UI_QT_SOUNDGAIN_H
