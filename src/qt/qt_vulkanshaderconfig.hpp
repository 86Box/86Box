#ifndef QT_VULKANSHADERCONFIG_HPP
#define QT_VULKANSHADERCONFIG_HPP

#include <QDialog>
#include <QLabel>
#include <QDoubleSpinBox>
#include <QAbstractButton>

#include <map>
#include <string>

#if QT_CONFIG(vulkan) && defined LIBRA_RUNTIME_VULKAN

#include "librashader.h"
#include "librashader_ld.h"
#include "qt-slangp.hpp"

namespace Ui {
class VulkanShaderConfig;
}

class VulkanShaderConfig : public QDialog {
    Q_OBJECT

public:
    explicit VulkanShaderConfig(QWidget *parent = nullptr, slang_shader *shader = nullptr);
    ~VulkanShaderConfig();

private slots:
    void on_buttonBox_clicked(QAbstractButton *button);

    void on_VulkanShaderConfig_accepted();

private:
    Ui::VulkanShaderConfig *ui;
    slang_shader *currentShader;

    std::map<std::string, double> defaultValues;
};
#endif
#endif // QT_VULKANSHADERCONFIG_HPP
