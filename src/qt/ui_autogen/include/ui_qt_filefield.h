/********************************************************************************
** Form generated from reading UI file 'qt_filefield.ui'
**
** Created by: Qt User Interface Compiler version 6.2.2
**
** WARNING! All changes made in this file will be lost when recompiling UI file!
********************************************************************************/

#ifndef UI_QT_FILEFIELD_H
#define UI_QT_FILEFIELD_H

#include <QtCore/QVariant>
#include <QtWidgets/QApplication>
#include <QtWidgets/QHBoxLayout>
#include <QtWidgets/QLabel>
#include <QtWidgets/QPushButton>
#include <QtWidgets/QWidget>

QT_BEGIN_NAMESPACE

class Ui_FileField
{
public:
    QHBoxLayout *horizontalLayout;
    QLabel *label;
    QPushButton *pushButton;

    void setupUi(QWidget *FileField)
    {
        if (FileField->objectName().isEmpty())
            FileField->setObjectName(QString::fromUtf8("FileField"));
        FileField->resize(400, 300);
        horizontalLayout = new QHBoxLayout(FileField);
        horizontalLayout->setObjectName(QString::fromUtf8("horizontalLayout"));
        horizontalLayout->setContentsMargins(0, 0, 0, 0);
        label = new QLabel(FileField);
        label->setObjectName(QString::fromUtf8("label"));

        horizontalLayout->addWidget(label);

        pushButton = new QPushButton(FileField);
        pushButton->setObjectName(QString::fromUtf8("pushButton"));

        horizontalLayout->addWidget(pushButton);

        horizontalLayout->setStretch(0, 3);
        horizontalLayout->setStretch(1, 1);

        retranslateUi(FileField);

        QMetaObject::connectSlotsByName(FileField);
    } // setupUi

    void retranslateUi(QWidget *FileField)
    {
        FileField->setWindowTitle(QCoreApplication::translate("FileField", "Form", nullptr));
        label->setText(QString());
        pushButton->setText(QCoreApplication::translate("FileField", "&Specify...", nullptr));
    } // retranslateUi

};

namespace Ui {
    class FileField: public Ui_FileField {};
} // namespace Ui

QT_END_NAMESPACE

#endif // UI_QT_FILEFIELD_H
