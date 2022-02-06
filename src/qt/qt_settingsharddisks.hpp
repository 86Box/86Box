#ifndef QT_SETTINGSHARDDISKS_HPP
#define QT_SETTINGSHARDDISKS_HPP

#include <QWidget>

namespace Ui {
class SettingsHarddisks;
}

class SettingsHarddisks : public QWidget
{
    Q_OBJECT

public:
    explicit SettingsHarddisks(QWidget *parent = nullptr);
    ~SettingsHarddisks();

    void save();

private slots:
    void on_comboBoxChannel_currentIndexChanged(int index);

private slots:
    void on_pushButtonRemove_clicked();
    void on_pushButtonExisting_clicked();
    void on_pushButtonNew_clicked();
    void on_comboBoxBus_currentIndexChanged(int index);

    void onTableRowChanged(const QModelIndex& current);

private:
    Ui::SettingsHarddisks *ui;
    bool buschangeinprogress = false;
};

#endif // QT_SETTINGSHARDDISKS_HPP
