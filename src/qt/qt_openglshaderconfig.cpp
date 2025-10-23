#include "qt_openglshaderconfig.hpp"
#include "ui_qt_openglshaderconfig.h"

#include "qt_mainwindow.hpp"

extern MainWindow* main_window;

extern "C"
{
#include <86box/86box.h>
#include <86box/plat.h>
#include <86box/config.h>
}

OpenGLShaderConfig::OpenGLShaderConfig(QWidget *parent, glslp_t* shader)
    : QDialog(parent)
    , ui(new Ui::OpenGLShaderConfig)
{
    ui->setupUi(this);

    currentShader = shader;

    connect(ui->buttonBox, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(ui->buttonBox, &QDialogButtonBox::rejected, this, &QDialog::reject);

    glslp_read_shader_config(currentShader);

    for (int i = 0; i < currentShader->num_parameters; i++) {
        auto spinBox = new QDoubleSpinBox;
        spinBox->setDecimals(3);
        spinBox->setObjectName(currentShader->parameters[i].id);
        spinBox->setRange(currentShader->parameters[i].min, currentShader->parameters[i].max);
        spinBox->setValue(currentShader->parameters[i].value);
        spinBox->setSingleStep(currentShader->parameters[i].step);
        QFormLayout* layout = (QFormLayout*)ui->scrollAreaWidgetContents->layout();
        layout->addRow(currentShader->parameters[i].description, spinBox);
    }
}

OpenGLShaderConfig::~OpenGLShaderConfig()
{
    delete ui;
}

void OpenGLShaderConfig::on_buttonBox_clicked(QAbstractButton *button)
{
    if (ui->buttonBox->buttonRole(button) == QDialogButtonBox::ResetRole) {
        for (int i = 0; i < currentShader->num_parameters; i++) {
            QDoubleSpinBox* box = this->findChild<QDoubleSpinBox*>(QString(currentShader->parameters[i].id));
            if (box) {
                box->setValue(currentShader->parameters[i].default_value);
            }
        }
    } else if (ui->buttonBox->buttonRole(button) == QDialogButtonBox::ApplyRole) {
        startblit();
        for (int i = 0; i < currentShader->num_parameters; i++) {
            QDoubleSpinBox* box = this->findChild<QDoubleSpinBox*>(QString(currentShader->parameters[i].id));
            if (box) {
                float val = (float)box->value();
                currentShader->parameters[i].value = val;
            }
        }
        glslp_write_shader_config(currentShader);
        config_save();
        endblit();
        main_window->reloadAllRenderers();
    }
}


void OpenGLShaderConfig::on_OpenGLShaderConfig_accepted()
{
    startblit();
    for (int i = 0; i < currentShader->num_parameters; i++) {
        QDoubleSpinBox* box = (QDoubleSpinBox*)this->findChild<QDoubleSpinBox*>(QString(currentShader->parameters[i].id));
        if (box) {
            float val = (float)box->value();
            currentShader->parameters[i].value = val;
        }
    }
    glslp_write_shader_config(currentShader);
    config_save();
    endblit();
    main_window->reloadAllRenderers();
}
