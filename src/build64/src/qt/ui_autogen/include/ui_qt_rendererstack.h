/********************************************************************************
** Form generated from reading UI file 'qt_rendererstack.ui'
**
** Created by: Qt User Interface Compiler version 5.15.10
**
** WARNING! All changes made in this file will be lost when recompiling UI file!
********************************************************************************/

#ifndef UI_QT_RENDERERSTACK_H
#define UI_QT_RENDERERSTACK_H

#include <QtCore/QVariant>
#include <QtWidgets/QApplication>
#include <QtWidgets/QStackedWidget>

QT_BEGIN_NAMESPACE

class Ui_RendererStack
{
public:

    void setupUi(QStackedWidget *RendererStack)
    {
        if (RendererStack->objectName().isEmpty())
            RendererStack->setObjectName(QString::fromUtf8("RendererStack"));
        RendererStack->resize(400, 300);

        retranslateUi(RendererStack);

        QMetaObject::connectSlotsByName(RendererStack);
    } // setupUi

    void retranslateUi(QStackedWidget *RendererStack)
    {
        RendererStack->setWindowTitle(QCoreApplication::translate("RendererStack", "StackedWidget", nullptr));
    } // retranslateUi

};

namespace Ui {
    class RendererStack: public Ui_RendererStack {};
} // namespace Ui

QT_END_NAMESPACE

#endif // UI_QT_RENDERERSTACK_H
