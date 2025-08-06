#ifndef QT_SETTINGSSOUND_HPP
#define QT_SETTINGSSOUND_HPP

#include <QWidget>

namespace Ui {
class SettingsSound;
}

class SettingsSound : public QWidget {
    Q_OBJECT

public:
    explicit SettingsSound(QWidget *parent = nullptr);
    ~SettingsSound();

    void save();

public slots:
    void onCurrentMachineChanged(int machineId);

private slots:
    void on_comboBoxSoundCard1_currentIndexChanged(int index);
    void on_pushButtonConfigureSoundCard1_clicked();

    void on_comboBoxSoundCard2_currentIndexChanged(int index);
    void on_pushButtonConfigureSoundCard2_clicked();

    void on_comboBoxSoundCard3_currentIndexChanged(int index);
    void on_pushButtonConfigureSoundCard3_clicked();

    void on_comboBoxSoundCard4_currentIndexChanged(int index);
    void on_pushButtonConfigureSoundCard4_clicked();

    void on_comboBoxMidiOut_currentIndexChanged(int index);
    void on_pushButtonConfigureMidiOut_clicked();

    void on_comboBoxMidiIn_currentIndexChanged(int index);
    void on_pushButtonConfigureMidiIn_clicked();

    void on_checkBoxMPU401_stateChanged(int arg1);
    void on_pushButtonConfigureMPU401_clicked();

private:
    Ui::SettingsSound *ui;
    int                machineId = 0;
};

#endif // QT_SETTINGSSOUND_HPP
