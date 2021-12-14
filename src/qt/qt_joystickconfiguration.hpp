#ifndef QT_JOYSTICKCONFIGURATION_HPP
#define QT_JOYSTICKCONFIGURATION_HPP

#include <QDialog>

namespace Ui {
class JoystickConfiguration;
}

class JoystickConfiguration : public QDialog
{
    Q_OBJECT

public:
    explicit JoystickConfiguration(int type, int joystick_nr, QWidget *parent = nullptr);
    ~JoystickConfiguration();

    int selectedDevice();
    int selectedAxis(int axis);
    int selectedButton(int button);
    int selectedPov(int pov);
private slots:
    void on_comboBoxDevice_currentIndexChanged(int index);

private:
    Ui::JoystickConfiguration *ui;
    QList<QWidget*> widgets;
    int type;
    int joystick_nr;
};

#endif // QT_JOYSTICKCONFIGURATION_HPP
