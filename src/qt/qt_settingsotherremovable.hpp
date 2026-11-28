#ifndef QT_SETTINGSOTHERREMOVABLE_HPP
#define QT_SETTINGSOTHERREMOVABLE_HPP

#include <cstdint>

#include <QWidget>
#include <QStandardItemModel>

namespace Ui {
class SettingsOtherRemovable;
}

class SettingsCompleter;

class SettingsOtherRemovable : public QWidget {
    Q_OBJECT

public:
    explicit SettingsOtherRemovable(QWidget *parent = nullptr);
    ~SettingsOtherRemovable();
    void reloadBusChannels_MO();
    void reloadBusChannels_RDisk();
    void reloadBusChannels_Tape();
    void reloadBusChannels_MD();

    int  changed();

    void restore();
    void save(int soft);

protected:
    void showEvent(QShowEvent *event) override;

signals:
    void moChannelChanged();
    void rdiskChannelChanged();
    void tapeChannelChanged();
    void mdChannelChanged();

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

    void onMDRowChanged(const QModelIndex &current);
    void on_comboBoxMDBus_currentIndexChanged(int index);
    void on_comboBoxMDBus_activated(int index);
    void on_comboBoxMDChannel_activated(int index);
    void on_comboBoxMDType_activated(int index);

private:
    Ui::SettingsOtherRemovable *ui;

    void setMOBus(QAbstractItemModel *model, const QModelIndex &idx, uint8_t bus, uint8_t channel);
    void setRDiskBus(QAbstractItemModel *model, const QModelIndex &idx, uint8_t bus, uint32_t type, uint8_t channel);
    void setRDiskType(QAbstractItemModel *model, const QModelIndex &idx, uint8_t bus, uint32_t type);
    void setTapeBus(QAbstractItemModel *model, const QModelIndex &idx, uint8_t bus, uint8_t channel);
    void setMDBus(QAbstractItemModel *model, const QModelIndex &idx, uint8_t bus, uint8_t channel);
    void updateRDiskTypeCombo();
    QIcon rdiskIconForType(uint32_t type) const;
    void updateTapeTypeCombo();
    void enableCurrentlySelectedChannel_MO();
    void enableCurrentlySelectedChannel_RDisk();
    void enableCurrentlySelectedChannel_Tape();
    void enableCurrentlySelectedChannel_MD();

    QIcon mo_disabled_icon;
    QIcon mo_icon;
    QIcon rdisk_disabled_icon;
    QIcon rdisk_icon;
    QIcon zip_icon;
    QIcon jaz_icon;
    QIcon syquest_icon;
    QIcon tape_disabled_icon;
    QIcon tape_icon;
    QIcon md_disabled_icon;
    QIcon md_icon;

    SettingsCompleter *scMOType;
    SettingsCompleter *scRDiskType;
    SettingsCompleter *scTapeType;
};

#endif // QT_SETTINGSOTHERREMOVABLE_HPP
