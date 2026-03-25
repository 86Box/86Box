#ifndef QT_SETTINGSHARDDISKS_HPP
#define QT_SETTINGSHARDDISKS_HPP

#include <QWidget>

namespace Ui {
class SettingsHarddisks;
}

class SettingsHarddisks : public QWidget {
    Q_OBJECT

public:
    explicit SettingsHarddisks(QWidget *parent = nullptr);
    ~SettingsHarddisks();
    void reloadBusChannels();

    int  changed();

    void restore();
    void save();

signals:
    void driveChannelChanged();

private slots:
    void on_comboBoxBus_currentIndexChanged(int index);
    void on_comboBoxChannel_currentIndexChanged(int index);
    void on_comboBoxSpeed_currentIndexChanged(int index);
    void on_comboBoxAudio_currentIndexChanged(int index);

    void on_pushButtonNew_clicked();
    void on_pushButtonExisting_clicked();
    void on_pushButtonRemove_clicked();

    void onTableRowChanged(const QModelIndex &current);

private:
    Ui::SettingsHarddisks *ui;
    void                   enableCurrentlySelectedChannel();
    void                   populateAudioProfiles();
    void                   addRow(QAbstractItemModel *model, void *priv);
    void                   addDriveFromDialog(Ui::SettingsHarddisks *ui, const HarddiskDialog &dlg);
    bool                   buschangeinprogress = false;

    int                    org_rows = 0;

    SettingsCompleter     *scSpeed;
};

#endif // QT_SETTINGSHARDDISKS_HPP
