/*
 * 86Box A hypervisor and IBM PC system emulator that specializes in
 *      running old operating systems and software designed for IBM
 *      PC systems and compatibles from 1981 through fairly recent
 *      system designs based on the PCI bus.
 *
 *      This file is part of the 86Box distribution.
 *
 *      OpenGL renderer options dialog for Qt
 *
 * Authors:
 *      Teemu Korhonen
 *
 *      Copyright 2022 Teemu Korhonen
 */

#include <QFileDialog>
#include <QMessageBox>
#include <QStringBuilder>

#include <stdexcept>

#include "qt_opengloptionsdialog.hpp"
#include "qt_util.hpp"
#include "ui_qt_opengloptionsdialog.h"

OpenGLOptionsDialog::OpenGLOptionsDialog(QWidget *parent, const OpenGLOptions &options, std::function<OpenGLOptions *()> optionsFactory)
    : QDialog(parent)
    , ui(new Ui::OpenGLOptionsDialog)
    , createOptions(optionsFactory)
{
    ui->setupUi(this);

    if (options.renderBehavior() == OpenGLOptions::SyncWithVideo)
        ui->syncWithVideo->setChecked(true);
    else {
        ui->syncToFramerate->setChecked(true);
        ui->targetFps->setValue(options.framerate());
    }

    ui->vsync->setChecked(options.vSync());

    if (!options.shaders().isEmpty()) {
        auto path = options.shaders().first().path();
        if (!path.isEmpty())
            ui->shader->setPlainText(path);
    }
}

OpenGLOptionsDialog::~OpenGLOptionsDialog()
{
    delete ui;
}

void
OpenGLOptionsDialog::accept()
{
    auto options = createOptions();

    options->setRenderBehavior(
        ui->syncWithVideo->isChecked()
            ? OpenGLOptions::SyncWithVideo
            : OpenGLOptions::TargetFramerate);

    options->setFrameRate(ui->targetFps->value());

    options->setVSync(ui->vsync->isChecked());

    auto shader = ui->shader->toPlainText();

    try {

        if (!shader.isEmpty())
            options->addShader(shader);
        else
            options->addDefaultShader();

    } catch (const std::runtime_error &e) {
        delete options;

        QMessageBox msgBox(this);
        msgBox.setWindowTitle(tr("Shader error"));
        msgBox.setText(tr("Could not load shaders."));
        msgBox.setInformativeText(tr("More information in details."));
        msgBox.setDetailedText(e.what());
        msgBox.setIcon(QMessageBox::Critical);
        msgBox.setStandardButtons(QMessageBox::Close);
        msgBox.setDefaultButton(QMessageBox::Close);
        msgBox.setStyleSheet("QTextEdit { min-width: 45em; }");
        msgBox.exec();

        return;
    }

    options->save();

    emit optionsChanged(options);

    QDialog::accept();
}

void
OpenGLOptionsDialog::on_addShader_clicked()
{
    auto shader = QFileDialog::getOpenFileName(
        this,
        QString(),
        QString(),
        tr("OpenGL Shaders") % util::DlgFilter({ "glsl" }, true));

    if (shader.isNull())
        return;

    ui->shader->setPlainText(shader);
}
