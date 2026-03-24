#ifndef QT_SETTINGSDISPLAY_HPP
#define QT_SETTINGSDISPLAY_HPP

#include <QWidget>

#define VIDEOCARD_MAX 2

namespace Ui {
class SettingsDisplay;
}

class SettingsCompleter;

class SettingsDisplay : public QWidget {
    Q_OBJECT

public:
    explicit SettingsDisplay(QWidget *parent = nullptr);
    ~SettingsDisplay();

    int  changed();

    void restore();
    void save();

public slots:
    void onCurrentMachineChanged(int machineId);

private slots:
    void on_comboBoxVideo_currentIndexChanged(int index);
    void on_pushButtonConfigureVideo_clicked();

    void on_comboBoxVideoSecondary_currentIndexChanged(int index);
    void on_pushButtonConfigureVideoSecondary_clicked();

    void on_checkBoxVoodoo_stateChanged(int state);
    void on_pushButtonConfigureVoodoo_clicked();

    void on_checkBox8514_stateChanged(int state);
    void on_pushButtonConfigure8514_clicked();

    void on_checkBoxXga_stateChanged(int state);
    void on_pushButtonConfigureXga_clicked();

    void on_checkBoxDa2_stateChanged(int state);
    void on_pushButtonConfigureDa2_clicked();

    void on_radioButtonDefault_clicked();

    void on_radioButtonCustom_clicked();

    void on_pushButtonExportDefault_clicked();

private:
    Ui::SettingsDisplay *ui;

    int                  gfxcard_cfg_changed[VIDEOCARD_MAX] = { 0, 0 };
    int                  voodoo_cfg_changed                 = 0;
    int                  ibm8514_cfg_changed                = 0;
    int                  xga_cfg_changed                    = 0;
    int                  ps55da2_cfg_changed                = 0;

    int                  machineId                = 0;
    int                  videoCard[VIDEOCARD_MAX] = { 0, 0 };

    void updateDisplay();

    int cga_hue, cga_saturation, cga_sharpness, cga_brightness, cga_contrast;

    SettingsCompleter   *sc;
    SettingsCompleter   *scSecondary;
};

#endif // QT_SETTINGSDISPLAY_HPP
