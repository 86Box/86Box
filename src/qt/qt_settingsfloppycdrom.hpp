#ifndef QT_SETTINGSFLOPPYCDROM_HPP
#define QT_SETTINGSFLOPPYCDROM_HPP

#include <QWidget>

namespace Ui {
class SettingsFloppyCDROM;
}

class SettingsFloppyCDROM : public QWidget
{
    Q_OBJECT

public:
    explicit SettingsFloppyCDROM(QWidget *parent = nullptr);
    ~SettingsFloppyCDROM();

    void save();

private slots:
    void on_comboBoxChannel_activated(int index);
    void on_comboBoxBus_activated(int index);
    void on_comboBoxSpeed_activated(int index);
    void on_comboBoxBus_currentIndexChanged(int index);
    void on_comboBoxFloppyType_activated(int index);
    void on_checkBoxCheckBPB_stateChanged(int arg1);
    void on_checkBoxTurboTimings_stateChanged(int arg1);
    void onFloppyRowChanged(const QModelIndex &current);
    void onCDROMRowChanged(const QModelIndex &current);

private:
    Ui::SettingsFloppyCDROM *ui;
};

#endif // QT_SETTINGSFLOPPYCDROM_HPP
