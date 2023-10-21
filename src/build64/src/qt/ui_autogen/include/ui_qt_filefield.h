/********************************************************************************
** Form generated from reading UI file 'qt_filefield.ui'
**
** Created by: Qt User Interface Compiler version 5.15.10
**
** WARNING! All changes made in this file will be lost when recompiling UI file!
********************************************************************************/

#ifndef UI_QT_FILEFIELD_H
#define UI_QT_FILEFIELD_H

#include <QtCore/QVariant>
#include <QtWidgets/QApplication>
#include <QtWidgets/QHBoxLayout>
#include <QtWidgets/QLineEdit>
#include <QtWidgets/QPushButton>
#include <QtWidgets/QWidget>

QT_BEGIN_NAMESPACE

class Ui_FileField
{
public:
    QHBoxLayout *horizontalLayout;
    QLineEdit *label;
    QPushButton *pushButton;

    void setupUi(QWidget *FileField)
    {
        if (FileField->objectName().isEmpty())
            FileField->setObjectName(QString::fromUtf8("FileField"));
        FileField->resize(354, 25);
        QSizePolicy sizePolicy(QSizePolicy::Preferred, QSizePolicy::Preferred);
        sizePolicy.setHorizontalStretch(0);
        sizePolicy.setVerticalStretch(0);
        sizePolicy.setHeightForWidth(FileField->sizePolicy().hasHeightForWidth());
        FileField->setSizePolicy(sizePolicy);
        horizontalLayout = new QHBoxLayout(FileField);
        horizontalLayout->setObjectName(QString::fromUtf8("horizontalLayout"));
        horizontalLayout->setContentsMargins(0, 0, 0, 0);
        label = new QLineEdit(FileField);
        label->setObjectName(QString::fromUtf8("label"));
        QSizePolicy sizePolicy1(QSizePolicy::Expanding, QSizePolicy::Preferred);
        sizePolicy1.setHorizontalStretch(0);
        sizePolicy1.setVerticalStretch(0);
        sizePolicy1.setHeightForWidth(label->sizePolicy().hasHeightForWidth());
        label->setSizePolicy(sizePolicy1);

        horizontalLayout->addWidget(label);

        pushButton = new QPushButton(FileField);
        pushButton->setObjectName(QString::fromUtf8("pushButton"));
        sizePolicy.setHeightForWidth(pushButton->sizePolicy().hasHeightForWidth());
        pushButton->setSizePolicy(sizePolicy);

        horizontalLayout->addWidget(pushButton);

        horizontalLayout->setStretch(0, 3);
        horizontalLayout->setStretch(1, 1);

        retranslateUi(FileField);

        QMetaObject::connectSlotsByName(FileField);
    } // setupUi

    void retranslateUi(QWidget *FileField)
    {
        FileField->setWindowTitle(QCoreApplication::translate("FileField", "Form", nullptr));
        pushButton->setText(QCoreApplication::translate("FileField", "&Specify...", nullptr));
    } // retranslateUi

};

namespace Ui {
    class FileField: public Ui_FileField {};
} // namespace Ui

QT_END_NAMESPACE

#endif // UI_QT_FILEFIELD_H
