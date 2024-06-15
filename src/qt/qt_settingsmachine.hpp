#ifndef QT_SETTINGSMACHINE_HPP
#define QT_SETTINGSMACHINE_HPP

#include <QWidget>

namespace Ui {
class SettingsMachine;
}

class SettingsMachine : public QWidget {
    Q_OBJECT

public:
    explicit SettingsMachine(QWidget *parent = nullptr);
    ~SettingsMachine();

    void save();

signals:
    void currentMachineChanged(int machineId);
private slots:
    void on_pushButtonConfigure_clicked();

private slots:
    void on_comboBoxFPU_currentIndexChanged(int index);

private slots:
    void on_comboBoxSpeed_currentIndexChanged(int index);

private slots:
    void on_comboBoxCPU_currentIndexChanged(int index);

private slots:
    void on_comboBoxMachine_currentIndexChanged(int index);

private slots:
    void on_comboBoxMachineType_currentIndexChanged(int index);
    void on_checkBoxFPUSoftfloat_stateChanged(int state);

private:
    Ui::SettingsMachine *ui;
};

#endif // QT_SETTINGSMACHINE_HPP
