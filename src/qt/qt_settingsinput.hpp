#ifndef QT_SETTINGSINPUT_HPP
#define QT_SETTINGSINPUT_HPP

#include <QWidget>
#include <QtGui/QStandardItemModel>
#include <QtGui/QStandardItem>
#include <QItemDelegate>
#include <QPainter>
#include <QVariant>
#include <QTableWidget>

namespace Ui {
class SettingsInput;
}

class SettingsInput : public QWidget {
    Q_OBJECT

public:
    explicit SettingsInput(QWidget *parent = nullptr);
    ~SettingsInput();

    int  changed();

    void restore();
    void save();

public slots:
    void onCurrentMachineChanged(int machineId);

private slots:
    void on_comboBoxKeyboard_currentIndexChanged(int index);
    void on_pushButtonConfigureKeyboard_clicked();

    void on_comboBoxMouse_currentIndexChanged(int index);
    void on_pushButtonConfigureMouse_clicked();

    void on_comboBoxJoystick0_currentIndexChanged(int index);
    void on_pushButtonJoystick01_clicked();
    void on_pushButtonJoystick02_clicked();
    void on_pushButtonJoystick03_clicked();
    void on_pushButtonJoystick04_clicked();

private:
    Ui::SettingsInput *ui;

    int                kbd_config_changed   = 0;
    int                mouse_config_changed = 0;

    int                machineId = 0;
};

#endif // QT_SETTINGSINPUT_HPP
