#ifndef QT_SETTINGSOTHERREMOVABLE_HPP
#define QT_SETTINGSOTHERREMOVABLE_HPP

#include <QWidget>

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

    void save();

signals:
    void moChannelChanged();
    void rdiskChannelChanged();

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

private:
    Ui::SettingsOtherRemovable *ui;
    void enableCurrentlySelectedChannel_MO();
    void enableCurrentlySelectedChannel_RDisk();
};

#endif // QT_SETTINGSOTHERREMOVABLE_HPP
