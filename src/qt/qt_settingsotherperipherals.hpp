#ifndef QT_SETTINGSOTHERPERIPHERALS_HPP
#define QT_SETTINGSOTHERPERIPHERALS_HPP

#include <QWidget>

namespace Ui {
class SettingsOtherPeripherals;
}

class SettingsOtherPeripherals : public QWidget
{
    Q_OBJECT

public:
    explicit SettingsOtherPeripherals(QWidget *parent = nullptr);
    ~SettingsOtherPeripherals();

    void save();
private slots:
    void on_pushButtonConfigureCard4_clicked();

private slots:
    void on_comboBoxCard4_currentIndexChanged(int index);

private slots:
    void on_pushButtonConfigureCard3_clicked();

private slots:
    void on_comboBoxCard3_currentIndexChanged(int index);

private slots:
    void on_pushButtonConfigureCard2_clicked();

private slots:
    void on_comboBoxCard2_currentIndexChanged(int index);

private slots:
    void on_pushButtonConfigureCard1_clicked();

private slots:
    void on_comboBoxCard1_currentIndexChanged(int index);

private slots:
    void on_pushButtonConfigureRTC_clicked();

private slots:
    void on_comboBoxRTC_currentIndexChanged(int index);

private:
    Ui::SettingsOtherPeripherals *ui;
};

#endif // QT_SETTINGSOTHERPERIPHERALS_HPP
