#ifndef QT_SETTINGSOTHERREMOVABLE_HPP
#define QT_SETTINGSOTHERREMOVABLE_HPP

#include <QWidget>
#include <QStandardItemModel>

namespace Ui {
class SettingsOtherRemovable;
}

class SettingsOtherRemovable : public QWidget {
    Q_OBJECT

public:
    explicit SettingsOtherRemovable(QWidget *parent = nullptr);
    ~SettingsOtherRemovable();
    void reloadBusChannels_MO();
    void reloadBusChannels_RDisk();
    void reloadBusChannels_Tape();

    void save();

signals:
    void moChannelChanged();
    void rdiskChannelChanged();
    void tapeChannelChanged();

private slots:
    void onMORowChanged(const QModelIndex &current);
    void on_comboBoxMOBus_currentIndexChanged(int index);
    void on_comboBoxMOBus_activated(int index);
    void on_comboBoxMOChannel_activated(int index);
    void on_comboBoxMOType_activated(int index);

    void onRDiskRowChanged(const QModelIndex &current);
    void on_comboBoxRDiskBus_currentIndexChanged(int index);
    void on_comboBoxRDiskBus_activated(int index);
    void on_comboBoxRDiskChannel_activated(int index);
    void on_comboBoxRDiskType_activated(int index);

    void onTapeRowChanged(const QModelIndex &current);
    void on_comboBoxTapeBus_currentIndexChanged(int index);
    void on_comboBoxTapeBus_activated(int index);
    void on_comboBoxTapeChannel_activated(int index);
    void on_comboBoxTapeType_activated(int index);

private:
    Ui::SettingsOtherRemovable *ui;

    void setMOBus(QAbstractItemModel *model, const QModelIndex &idx, uint8_t bus, uint8_t channel);
    void setRDiskBus(QAbstractItemModel *model, const QModelIndex &idx, uint8_t bus, uint32_t type, uint8_t channel);
    void setRDiskType(QAbstractItemModel *model, const QModelIndex &idx, uint8_t bus, uint32_t type);
    void setTapeBus(QAbstractItemModel *model, const QModelIndex &idx, uint8_t bus, uint8_t channel);
    void enableCurrentlySelectedChannel_MO();
    void enableCurrentlySelectedChannel_RDisk();
    void enableCurrentlySelectedChannel_Tape();

    QIcon mo_disabled_icon;
    QIcon mo_icon;
    QIcon rdisk_disabled_icon;
    QIcon rdisk_icon;
    QIcon zip_icon;
    QIcon tape_disabled_icon;
    QIcon tape_icon;
};

#endif // QT_SETTINGSOTHERREMOVABLE_HPP
