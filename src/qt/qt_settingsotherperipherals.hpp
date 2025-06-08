#ifndef QT_SETTINGSOTHERPERIPHERALS_HPP
#define QT_SETTINGSOTHERPERIPHERALS_HPP

#include <QWidget>

namespace Ui {
class SettingsOtherPeripherals;
}

class SettingsOtherPeripherals : public QWidget {
    Q_OBJECT

public:
    explicit SettingsOtherPeripherals(QWidget *parent = nullptr);
    ~SettingsOtherPeripherals();

    void save();

public slots:
    void onCurrentMachineChanged(int machineId);

private slots:
    void on_comboBoxRTC_currentIndexChanged(int index);
    void on_pushButtonConfigureRTC_clicked();

    void on_comboBoxIsaMemCard1_currentIndexChanged(int index);
    void on_pushButtonConfigureIsaMemCard1_clicked();
    void on_comboBoxIsaMemCard2_currentIndexChanged(int index);
    void on_pushButtonConfigureIsaMemCard2_clicked();
    void on_comboBoxIsaMemCard3_currentIndexChanged(int index);
    void on_pushButtonConfigureIsaMemCard3_clicked();
    void on_comboBoxIsaMemCard4_currentIndexChanged(int index);
    void on_pushButtonConfigureIsaMemCard4_clicked();

    void on_comboBoxIsaRomCard1_currentIndexChanged(int index);
    void on_pushButtonConfigureIsaRomCard1_clicked();
    void on_comboBoxIsaRomCard2_currentIndexChanged(int index);
    void on_pushButtonConfigureIsaRomCard2_clicked();
    void on_comboBoxIsaRomCard3_currentIndexChanged(int index);
    void on_pushButtonConfigureIsaRomCard3_clicked();
    void on_comboBoxIsaRomCard4_currentIndexChanged(int index);
    void on_pushButtonConfigureIsaRomCard4_clicked();

    void on_checkBoxUnitTester_stateChanged(int arg1);
    void on_pushButtonConfigureUT_clicked();

    void on_checkBoxKeyCard_stateChanged(int arg1);
    void on_pushButtonConfigureKeyCard_clicked();

private:
    Ui::SettingsOtherPeripherals *ui;
    int                           machineId { 0 };
};

#endif // QT_SETTINGSOTHERPERIPHERALS_HPP
