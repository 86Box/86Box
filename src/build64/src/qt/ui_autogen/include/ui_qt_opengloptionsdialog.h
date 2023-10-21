/********************************************************************************
** Form generated from reading UI file 'qt_opengloptionsdialog.ui'
**
** Created by: Qt User Interface Compiler version 5.15.10
**
** WARNING! All changes made in this file will be lost when recompiling UI file!
********************************************************************************/

#ifndef UI_QT_OPENGLOPTIONSDIALOG_H
#define UI_QT_OPENGLOPTIONSDIALOG_H

#include <QtCore/QVariant>
#include <QtWidgets/QApplication>
#include <QtWidgets/QCheckBox>
#include <QtWidgets/QDialog>
#include <QtWidgets/QDialogButtonBox>
#include <QtWidgets/QGridLayout>
#include <QtWidgets/QGroupBox>
#include <QtWidgets/QPushButton>
#include <QtWidgets/QRadioButton>
#include <QtWidgets/QSlider>
#include <QtWidgets/QSpacerItem>
#include <QtWidgets/QSpinBox>
#include <QtWidgets/QTextEdit>
#include <QtWidgets/QVBoxLayout>

QT_BEGIN_NAMESPACE

class Ui_OpenGLOptionsDialog
{
public:
    QVBoxLayout *verticalLayout;
    QGroupBox *groupBox;
    QGridLayout *gridLayout_2;
    QRadioButton *syncToFramerate;
    QSpinBox *targetFps;
    QCheckBox *vsync;
    QRadioButton *syncWithVideo;
    QSlider *fpsSlider;
    QGroupBox *groupBox_2;
    QGridLayout *gridLayout;
    QPushButton *removeShader;
    QSpacerItem *verticalSpacer;
    QTextEdit *shader;
    QPushButton *addShader;
    QDialogButtonBox *buttonBox;

    void setupUi(QDialog *OpenGLOptionsDialog)
    {
        if (OpenGLOptionsDialog->objectName().isEmpty())
            OpenGLOptionsDialog->setObjectName(QString::fromUtf8("OpenGLOptionsDialog"));
        OpenGLOptionsDialog->resize(400, 320);
        verticalLayout = new QVBoxLayout(OpenGLOptionsDialog);
        verticalLayout->setObjectName(QString::fromUtf8("verticalLayout"));
        groupBox = new QGroupBox(OpenGLOptionsDialog);
        groupBox->setObjectName(QString::fromUtf8("groupBox"));
        gridLayout_2 = new QGridLayout(groupBox);
        gridLayout_2->setObjectName(QString::fromUtf8("gridLayout_2"));
        syncToFramerate = new QRadioButton(groupBox);
        syncToFramerate->setObjectName(QString::fromUtf8("syncToFramerate"));

        gridLayout_2->addWidget(syncToFramerate, 1, 0, 1, 1);

        targetFps = new QSpinBox(groupBox);
        targetFps->setObjectName(QString::fromUtf8("targetFps"));
        targetFps->setEnabled(false);
        targetFps->setMinimum(15);
        targetFps->setMaximum(240);
        targetFps->setValue(60);

        gridLayout_2->addWidget(targetFps, 2, 1, 1, 1);

        vsync = new QCheckBox(groupBox);
        vsync->setObjectName(QString::fromUtf8("vsync"));

        gridLayout_2->addWidget(vsync, 3, 0, 1, 1);

        syncWithVideo = new QRadioButton(groupBox);
        syncWithVideo->setObjectName(QString::fromUtf8("syncWithVideo"));
        syncWithVideo->setChecked(true);

        gridLayout_2->addWidget(syncWithVideo, 0, 0, 1, 1);

        fpsSlider = new QSlider(groupBox);
        fpsSlider->setObjectName(QString::fromUtf8("fpsSlider"));
        fpsSlider->setEnabled(false);
        fpsSlider->setMinimum(15);
        fpsSlider->setMaximum(240);
        fpsSlider->setValue(60);
        fpsSlider->setOrientation(Qt::Horizontal);
        fpsSlider->setInvertedAppearance(false);
        fpsSlider->setTickPosition(QSlider::NoTicks);

        gridLayout_2->addWidget(fpsSlider, 2, 0, 1, 1);

        gridLayout_2->setColumnStretch(0, 3);
        gridLayout_2->setColumnStretch(1, 1);

        verticalLayout->addWidget(groupBox);

        groupBox_2 = new QGroupBox(OpenGLOptionsDialog);
        groupBox_2->setObjectName(QString::fromUtf8("groupBox_2"));
        gridLayout = new QGridLayout(groupBox_2);
        gridLayout->setObjectName(QString::fromUtf8("gridLayout"));
        removeShader = new QPushButton(groupBox_2);
        removeShader->setObjectName(QString::fromUtf8("removeShader"));

        gridLayout->addWidget(removeShader, 2, 1, 1, 1);

        verticalSpacer = new QSpacerItem(20, 40, QSizePolicy::Minimum, QSizePolicy::Expanding);

        gridLayout->addItem(verticalSpacer, 3, 1, 1, 1);

        shader = new QTextEdit(groupBox_2);
        shader->setObjectName(QString::fromUtf8("shader"));
        shader->setReadOnly(true);

        gridLayout->addWidget(shader, 1, 0, 3, 1);

        addShader = new QPushButton(groupBox_2);
        addShader->setObjectName(QString::fromUtf8("addShader"));

        gridLayout->addWidget(addShader, 1, 1, 1, 1, Qt::AlignTop);

        gridLayout->setColumnStretch(0, 3);
        gridLayout->setColumnStretch(1, 1);

        verticalLayout->addWidget(groupBox_2);

        buttonBox = new QDialogButtonBox(OpenGLOptionsDialog);
        buttonBox->setObjectName(QString::fromUtf8("buttonBox"));
        buttonBox->setOrientation(Qt::Horizontal);
        buttonBox->setStandardButtons(QDialogButtonBox::Cancel|QDialogButtonBox::Ok);

        verticalLayout->addWidget(buttonBox);

        QWidget::setTabOrder(syncWithVideo, syncToFramerate);
        QWidget::setTabOrder(syncToFramerate, fpsSlider);
        QWidget::setTabOrder(fpsSlider, targetFps);
        QWidget::setTabOrder(targetFps, vsync);
        QWidget::setTabOrder(vsync, shader);
        QWidget::setTabOrder(shader, addShader);
        QWidget::setTabOrder(addShader, removeShader);

        retranslateUi(OpenGLOptionsDialog);
        QObject::connect(buttonBox, SIGNAL(accepted()), OpenGLOptionsDialog, SLOT(accept()));
        QObject::connect(buttonBox, SIGNAL(rejected()), OpenGLOptionsDialog, SLOT(reject()));
        QObject::connect(syncToFramerate, SIGNAL(toggled(bool)), targetFps, SLOT(setEnabled(bool)));
        QObject::connect(syncToFramerate, SIGNAL(toggled(bool)), fpsSlider, SLOT(setEnabled(bool)));
        QObject::connect(fpsSlider, SIGNAL(valueChanged(int)), targetFps, SLOT(setValue(int)));
        QObject::connect(targetFps, SIGNAL(valueChanged(int)), fpsSlider, SLOT(setValue(int)));
        QObject::connect(removeShader, SIGNAL(clicked()), shader, SLOT(clear()));

        QMetaObject::connectSlotsByName(OpenGLOptionsDialog);
    } // setupUi

    void retranslateUi(QDialog *OpenGLOptionsDialog)
    {
        OpenGLOptionsDialog->setWindowTitle(QCoreApplication::translate("OpenGLOptionsDialog", "OpenGL 3.0 renderer options", nullptr));
        groupBox->setTitle(QCoreApplication::translate("OpenGLOptionsDialog", "Render behavior", nullptr));
        syncToFramerate->setText(QCoreApplication::translate("OpenGLOptionsDialog", "Use target framerate:", nullptr));
        targetFps->setSuffix(QCoreApplication::translate("OpenGLOptionsDialog", " fps", nullptr));
        vsync->setText(QCoreApplication::translate("OpenGLOptionsDialog", "VSync", nullptr));
#if QT_CONFIG(tooltip)
        syncWithVideo->setToolTip(QCoreApplication::translate("OpenGLOptionsDialog", "<html><head/><body><p>Render each frame immediately, in sync with the emulated display.</p><p><span style=\" font-style:italic;\">This is the recommended option if the shaders in use don't utilize frametime for animated effects.</span></p></body></html>", nullptr));
#endif // QT_CONFIG(tooltip)
        syncWithVideo->setText(QCoreApplication::translate("OpenGLOptionsDialog", "Synchronize with video", nullptr));
        groupBox_2->setTitle(QCoreApplication::translate("OpenGLOptionsDialog", "Shaders", nullptr));
        removeShader->setText(QCoreApplication::translate("OpenGLOptionsDialog", "Remove", nullptr));
        shader->setPlaceholderText(QCoreApplication::translate("OpenGLOptionsDialog", "No shader selected", nullptr));
        addShader->setText(QCoreApplication::translate("OpenGLOptionsDialog", "Browse...", nullptr));
    } // retranslateUi

};

namespace Ui {
    class OpenGLOptionsDialog: public Ui_OpenGLOptionsDialog {};
} // namespace Ui

QT_END_NAMESPACE

#endif // UI_QT_OPENGLOPTIONSDIALOG_H
