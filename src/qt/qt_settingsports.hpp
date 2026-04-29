#ifndef QT_SETTINGSPORTS_HPP
#define QT_SETTINGSPORTS_HPP

#include <QWidget>

#define SERIAL_MAX_UI 4

namespace Ui {
class SettingsPorts;
}

class SettingsPorts : public QWidget {
    Q_OBJECT

public:
    explicit SettingsPorts(QWidget *parent = nullptr);
    ~SettingsPorts();

    int  changed();

    void restore();
    void save(int soft);

public slots:
    void onCurrentMachineChanged(int machineId);

private slots:
    void on_comboBoxLpt1_currentIndexChanged(int index);
    void on_pushButtonConfigureLpt1_clicked();
    void on_comboBoxLpt2_currentIndexChanged(int index);
    void on_pushButtonConfigureLpt2_clicked();
    void on_comboBoxLpt3_currentIndexChanged(int index);
    void on_pushButtonConfigureLpt3_clicked();
    void on_comboBoxLpt4_currentIndexChanged(int index);
    void on_pushButtonConfigureLpt4_clicked();

    void on_checkBoxParallel1_stateChanged(int state);
    void on_checkBoxParallel2_stateChanged(int state);
    void on_checkBoxParallel3_stateChanged(int state);
    void on_checkBoxParallel4_stateChanged(int state);

    void on_comboBoxCom1_currentIndexChanged(int index);
    void on_pushButtonConfigureCom1_clicked();
    void on_comboBoxCom2_currentIndexChanged(int index);
    void on_pushButtonConfigureCom2_clicked();
    void on_comboBoxCom3_currentIndexChanged(int index);
    void on_pushButtonConfigureCom3_clicked();
    void on_comboBoxCom4_currentIndexChanged(int index);
    void on_pushButtonConfigureCom4_clicked();

    void on_checkBoxSerial1_stateChanged(int state);
    void on_checkBoxSerial2_stateChanged(int state);
    void on_checkBoxSerial3_stateChanged(int state);
    void on_checkBoxSerial4_stateChanged(int state);

private:
    Ui::SettingsPorts *ui;
    int                machineId = 0;

    int                lpt_device_cfg_changed[4] = { 0, 0, 0, 0 };
    int                com_device_cfg_changed[SERIAL_MAX_UI] = { 0 };

    SettingsCompleter *scLpt[4];
    SettingsCompleter *scCom[SERIAL_MAX_UI];
};

#endif // QT_SETTINGSPORTS_HPP
