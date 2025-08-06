#ifndef QT_SETTINGSPORTS_HPP
#define QT_SETTINGSPORTS_HPP

#include <QWidget>

namespace Ui {
class SettingsPorts;
}

class SettingsPorts : public QWidget {
    Q_OBJECT

public:
    explicit SettingsPorts(QWidget *parent = nullptr);
    ~SettingsPorts();

    void save();

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

    void on_checkBoxSerial1_stateChanged(int state);
    void on_checkBoxSerial2_stateChanged(int state);
    void on_checkBoxSerial3_stateChanged(int state);
    void on_checkBoxSerial4_stateChanged(int state);
#if 0
    void on_checkBoxSerial5_stateChanged(int state);
    void on_checkBoxSerial6_stateChanged(int state);
    void on_checkBoxSerial7_stateChanged(int state);
#endif
    void on_checkBoxSerialPassThru1_stateChanged(int state);
    void on_pushButtonSerialPassThru1_clicked();

    void on_checkBoxSerialPassThru2_stateChanged(int state);
    void on_pushButtonSerialPassThru2_clicked();

    void on_checkBoxSerialPassThru3_stateChanged(int state);
    void on_pushButtonSerialPassThru3_clicked();

    void on_checkBoxSerialPassThru4_stateChanged(int state);
    void on_pushButtonSerialPassThru4_clicked();

#if 0
    void on_checkBoxSerialPassThru5_stateChanged(int state);
    void on_pushButtonSerialPassThru5_clicked();

    void on_checkBoxSerialPassThru6_stateChanged(int state);
    void on_pushButtonSerialPassThru6_clicked();

    void on_checkBoxSerialPassThru7_stateChanged(int state);
    void on_pushButtonSerialPassThru7_clicked();
#endif

private:
    Ui::SettingsPorts *ui;
    int                machineId = 0;
};

#endif // QT_SETTINGSPORTS_HPP
