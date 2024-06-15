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
    void on_pushButtonConfigureCard4_clicked();
    void on_comboBoxCard4_currentIndexChanged(int index);
    void on_pushButtonConfigureCard3_clicked();
    void on_comboBoxCard3_currentIndexChanged(int index);
    void on_pushButtonConfigureCard2_clicked();
    void on_comboBoxCard2_currentIndexChanged(int index);
    void on_pushButtonConfigureCard1_clicked();
    void on_comboBoxCard1_currentIndexChanged(int index);
    void on_pushButtonConfigureRTC_clicked();
    void on_comboBoxRTC_currentIndexChanged(int index);
    void on_checkBoxUnitTester_stateChanged(int arg1);
    void on_pushButtonConfigureUT_clicked();

    void on_pushButtonConfigureKeyCard_clicked();

    void on_checkBoxKeyCard_stateChanged(int arg1);

private:
    Ui::SettingsOtherPeripherals *ui;
    int                           machineId { 0 };
};

#endif // QT_SETTINGSOTHERPERIPHERALS_HPP
