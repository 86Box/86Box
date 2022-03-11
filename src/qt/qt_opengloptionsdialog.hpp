/*
 * 86Box A hypervisor and IBM PC system emulator that specializes in
 *      running old operating systems and software designed for IBM
 *      PC systems and compatibles from 1981 through fairly recent
 *      system designs based on the PCI bus.
 *
 *      This file is part of the 86Box distribution.
 *
 *      Header for OpenGL renderer options dialog
 *
 * Authors:
 *      Teemu Korhonen
 *
 *      Copyright 2022 Teemu Korhonen
 */

#ifndef QT_OPENGLOPTIONSDIALOG_H
#define QT_OPENGLOPTIONSDIALOG_H

#include <QDialog>

#include <functional>

#include "qt_opengloptions.hpp"

namespace Ui {
class OpenGLOptionsDialog;
}

class OpenGLOptionsDialog : public QDialog {
    Q_OBJECT

public:
    explicit OpenGLOptionsDialog(QWidget *parent, const OpenGLOptions &options, std::function<OpenGLOptions *()> optionsFactory);
    ~OpenGLOptionsDialog();

signals:
    void optionsChanged(OpenGLOptions *options);

public slots:
    void accept() override;

private:
    Ui::OpenGLOptionsDialog *ui;

    std::function<OpenGLOptions *()> createOptions;

private slots:
    void on_addShader_clicked();
};

#endif // QT_OPENGLOPTIONSDIALOG_H
