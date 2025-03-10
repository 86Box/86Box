#ifndef QT_OPENGLSHADERCONFIG_HPP
#define QT_OPENGLSHADERCONFIG_HPP

#include <QDialog>
#include <QLabel>
#include <QDoubleSpinBox>
#include <QAbstractButton>

#include <map>
#include <string>

extern "C"
{
#include <86box/qt-glslp-parser.h>
}

namespace Ui {
class OpenGLShaderConfig;
}

class OpenGLShaderConfig : public QDialog {
    Q_OBJECT

public:
    explicit OpenGLShaderConfig(QWidget *parent = nullptr, glslp_t* shader = nullptr);
    ~OpenGLShaderConfig();

private slots:
    void on_buttonBox_clicked(QAbstractButton *button);

    void on_OpenGLShaderConfig_accepted();

private:
    Ui::OpenGLShaderConfig *ui;
    glslp_t* currentShader;

    std::map<std::string, double> defaultValues;
};

#endif // QT_OPENGLSHADERCONFIG_HPP
