#ifndef QT_SETTINGSFLOPPYCDROM_HPP
#define QT_SETTINGSFLOPPYCDROM_HPP

#include <QWidget>
#include <QStandardItemModel>

namespace Ui {
class SettingsFloppyCDROM;
}

class SettingsFloppyCDROM : public QWidget {
    Q_OBJECT

public:
    explicit SettingsFloppyCDROM(QWidget *parent = nullptr);
    ~SettingsFloppyCDROM();
    void reloadBusChannels();

    void save();

signals:
    void cdromChannelChanged();

private slots:
    void onFloppyRowChanged(const QModelIndex &current);
    void on_comboBoxFloppyType_activated(int index);
    void on_checkBoxTurboTimings_stateChanged(int arg1);
    void on_checkBoxCheckBPB_stateChanged(int arg1);
    void on_comboBoxFloppyAudio_activated(int index);

    void onCDROMRowChanged(const QModelIndex &current);
    void on_comboBoxBus_activated(int index);
    void on_comboBoxBus_currentIndexChanged(int index);
    void on_comboBoxChannel_activated(int index);
    void on_comboBoxSpeed_activated(int index);
    void on_comboBoxCDROMType_activated(int index);


private:
    Ui::SettingsFloppyCDROM *ui;
    void setFloppyType(QAbstractItemModel *model, const QModelIndex &idx, int type);
    void setCDROMBus(QAbstractItemModel *model, const QModelIndex &idx, uint8_t bus, uint8_t channel);
    void enableCurrentlySelectedChannel();

    QIcon floppy_disabled_icon;
    QIcon floppy_525_icon;
    QIcon floppy_35_icon;
    QIcon cdrom_disabled_icon;
    QIcon cdrom_icon;
};

#endif // QT_SETTINGSFLOPPYCDROM_HPP
