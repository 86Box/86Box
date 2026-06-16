#include "qt_vulkanshaderconfig.hpp"
#if QT_CONFIG(vulkan) && defined LIBRA_RUNTIME_VULKAN
#include "ui_qt_vulkanshaderconfig.h"

#include "qt_mainwindow.hpp"

#include "qt-slangp.hpp"

extern MainWindow *main_window;

extern "C" {
#include <86box/86box.h>
#include <86box/plat.h>
#include <86box/ini.h>
#include <86box/config.h>
extern void get_glslp_name(const char *f, char *s, int size);
}
static char s[512];
static char name[512];

void
slangp_write_shader_config(slang_shader& shader)
{
    get_glslp_name(shader.path.c_str(), name, sizeof(name));

    startblit();
    snprintf(s, sizeof(s) - 1, "VK Shaders - %s", name);
    for (unsigned int i = 0; i < shader.param_list.length; i++) {
        config_set_double(s, shader.param_list.parameters[i].name, shader.param_values[i]);
    }
    endblit();
}

void
slangp_read_shader_config(slang_shader& shader)
{
    get_glslp_name(shader.path.c_str(), name, sizeof(name));

    startblit();
    snprintf(s, sizeof(s) - 1, "VK Shaders - %s", name);
    shader.param_values.resize(shader.param_list.length);
    for (unsigned int i = 0; i < shader.param_list.length; i++) {
        std::string shader_name = shader.param_list.parameters[i].name;
        double shader_initial_value = shader.param_list.parameters[i].initial;
        shader.param_values[i] = config_get_double(s, shader_name.c_str(), shader_initial_value);
    }
    endblit();
}

VulkanShaderConfig::VulkanShaderConfig(QWidget *parent, slang_shader *shader)
    : QDialog(parent)
    , ui(new Ui::VulkanShaderConfig)
{
    ui->setupUi(this);

    currentShader = shader;

    connect(ui->buttonBox, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(ui->buttonBox, &QDialogButtonBox::rejected, this, &QDialog::reject);

    slangp_read_shader_config(*currentShader);

    for (unsigned int i = 0; i < currentShader->param_list.length; i++) {
        // XXX: librashader returns duplicated parameters for some reason.
        if (this->findChild<QDoubleSpinBox *>(QString(currentShader->param_list.parameters[i].name))) {
            QFormLayout *layout = (QFormLayout *) ui->scrollAreaWidgetContents->layout();
            layout->removeRow(this->findChild<QDoubleSpinBox *>(QString(currentShader->param_list.parameters[i].name)));
        }
        auto spinBox = new QDoubleSpinBox;
        spinBox->setDecimals(3);
        spinBox->setObjectName(currentShader->param_list.parameters[i].name);
        spinBox->setRange(currentShader->param_list.parameters[i].minimum, currentShader->param_list.parameters[i].maximum);
        spinBox->setValue(currentShader->param_values[i]);
        spinBox->setSingleStep(currentShader->param_list.parameters[i].step);
        QFormLayout *layout = (QFormLayout *) ui->scrollAreaWidgetContents->layout();
        layout->addRow(currentShader->param_list.parameters[i].description, spinBox);
    }
}

VulkanShaderConfig::~VulkanShaderConfig()
{
    delete ui;
}

void
VulkanShaderConfig::on_buttonBox_clicked(QAbstractButton *button)
{
    if (ui->buttonBox->buttonRole(button) == QDialogButtonBox::ResetRole) {
        for (unsigned int i = 0; i < currentShader->param_list.length; i++) {
            QDoubleSpinBox *box = this->findChild<QDoubleSpinBox *>(QString(currentShader->param_list.parameters[i].name));
            if (box) {
                box->setValue(currentShader->param_list.parameters[i].initial);
            }
        }
    } else if (ui->buttonBox->buttonRole(button) == QDialogButtonBox::ApplyRole) {
        startblit();
        for (unsigned int i = 0; i < currentShader->param_list.length; i++) {
            QDoubleSpinBox *box = this->findChild<QDoubleSpinBox *>(QString(currentShader->param_list.parameters[i].name));
            if (box) {
                float val                          = (float) box->value();
                currentShader->param_values[i] = val;
            }
        }
        slangp_write_shader_config(*currentShader);
        config_save();
        endblit();
        main_window->reloadAllRenderers();
    }
}

void
VulkanShaderConfig::on_VulkanShaderConfig_accepted()
{
    startblit();
    for (unsigned int i = 0; i < currentShader->param_list.length; i++) {
        QDoubleSpinBox *box = (QDoubleSpinBox *) this->findChild<QDoubleSpinBox *>(QString(currentShader->param_list.parameters[i].name));
        if (box) {
            float val                          = (float) box->value();
            currentShader->param_values[i] = val;
        }
    }
    slangp_write_shader_config(*currentShader);
    config_save();
    endblit();
    main_window->reloadAllRenderers();
}
#endif
