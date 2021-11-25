#ifndef QT_SETTINGSSOUND_HPP
#define QT_SETTINGSSOUND_HPP

#include <QWidget>

namespace Ui {
class SettingsSound;
}

class SettingsSound : public QWidget
{
    Q_OBJECT

public:
    explicit SettingsSound(QWidget *parent = nullptr);
    ~SettingsSound();

    void save();

public slots:
    void onCurrentMachineChanged(int machineId);

private slots:
    void on_pushButtonConfigureGUS_clicked();
    void on_pushButtonConfigureCMS_clicked();
    void on_pushButtonConfigureSSI2001_clicked();
    void on_pushButtonConfigureMPU401_clicked();
    void on_checkBoxGUS_stateChanged(int arg1);
    void on_checkBoxCMS_stateChanged(int arg1);
    void on_checkBoxSSI2001_stateChanged(int arg1);
    void on_checkBoxMPU401_stateChanged(int arg1);
    void on_pushButtonConfigureMidiIn_clicked();
    void on_pushButtonConfigureMidiOut_clicked();
    void on_comboBoxMidiIn_currentIndexChanged(int index);
    void on_comboBoxMidiOut_currentIndexChanged(int index);
    void on_pushButtonConfigureSoundCard_clicked();
    void on_comboBoxSoundCard_currentIndexChanged(int index);

private:
    Ui::SettingsSound *ui;
    int machineId = 0;
};

#endif // QT_SETTINGSSOUND_HPP
