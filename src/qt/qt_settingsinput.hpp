#ifndef QT_SETTINGSINPUT_HPP
#define QT_SETTINGSINPUT_HPP

#include <QWidget>

namespace Ui {
class SettingsInput;
}

class SettingsInput : public QWidget
{
    Q_OBJECT

public:
    explicit SettingsInput(QWidget *parent = nullptr);
    ~SettingsInput();

    void save();

public slots:
    void onCurrentMachineChanged(int machineId);

private slots:
    void on_pushButtonConfigureMouse_clicked();
    void on_comboBoxJoystick_currentIndexChanged(int index);
    void on_comboBoxMouse_currentIndexChanged(int index);

private:
    Ui::SettingsInput *ui;
    int machineId = 0;
};

#endif // QT_SETTINGSINPUT_HPP
