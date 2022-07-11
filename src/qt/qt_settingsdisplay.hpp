#ifndef QT_SETTINGSDISPLAY_HPP
#define QT_SETTINGSDISPLAY_HPP

#include <QWidget>

namespace Ui {
class SettingsDisplay;
}

class SettingsDisplay : public QWidget
{
    Q_OBJECT

public:
    explicit SettingsDisplay(QWidget *parent = nullptr);
    ~SettingsDisplay();

    void save();

public slots:
    void onCurrentMachineChanged(int machineId);

private slots:
    void on_pushButtonConfigureSecondary_clicked();

private slots:
    void on_comboBoxVideoSecondary_currentIndexChanged(int index);

private slots:
    void on_checkBoxVoodoo_stateChanged(int state);
    void on_checkBoxXga_stateChanged(int state);
    void on_comboBoxVideo_currentIndexChanged(int index);
    void on_pushButtonConfigureVoodoo_clicked();
    void on_pushButtonConfigureXga_clicked();
    void on_pushButtonConfigure_clicked();

private:
    Ui::SettingsDisplay *ui;
    int machineId = 0;
};

#endif // QT_SETTINGSDISPLAY_HPP
