#ifndef QT_SETTINGSMACHINE_HPP
#define QT_SETTINGSMACHINE_HPP

#include <QWidget>

namespace Ui {
class SettingsMachine;
}

class SettingsCompleter;

class SettingsMachine : public QWidget {
    Q_OBJECT

public:
    explicit SettingsMachine(QWidget *parent = nullptr);
    ~SettingsMachine();

    int  changed();

    void restore();
    void save(int soft);
    /* The machine selected on the page, saved or not. */
    int  currentMachineId() const;

signals:
    void currentMachineChanged(int machineId);

private slots:
    void on_pushButtonConfigure_clicked();
    void on_comboBoxFPU_currentIndexChanged(int index);
    void on_comboBoxSpeed_currentIndexChanged(int index);
    void on_comboBoxCPU_currentIndexChanged(int index);
    void on_comboBoxMachine_currentIndexChanged(int index);
    void on_comboBoxMachineType_currentIndexChanged(int index);
    void on_checkBoxFPUSoftfloat_stateChanged(int state);
    void on_checkBoxDebugRegs486_stateChanged(int state);
    void on_checkBoxDynamicRecompiler_stateChanged(int state);

    void on_radioButtonSmallerFrames_clicked();

    void on_radioButtonLargerFrames_clicked();

    void on_groupBoxTimeSync_toggled(bool checked);
    void on_spinBoxYearsOffset_valueChanged(int value);
    void on_spinBoxFixedYear_valueChanged(int value);
    void on_radioButtonOffsetYears_toggled(bool checked);
    void on_radioButtonFixedYear_toggled(bool checked);

private:
    void updateGuestYearLabel();
    void updateTimeControlsEnabled();
    void timeSyncSettings(int &sync, int &offset_years, int &fixed_year, int &keep_date) const;
    Ui::SettingsMachine *ui;

    int                  machine_cfg_changed;
    int                  original_years_offset = 0;
    int                  original_fixed_year = 0;
    bool                 years_offset_manipulated = false;
    bool                 fixed_year_manipulated = false;

    SettingsCompleter *  sc;
};

#endif // QT_SETTINGSMACHINE_HPP
