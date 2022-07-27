#ifndef QT_SETTINGSNETWORK_HPP
#define QT_SETTINGSNETWORK_HPP

#include <QWidget>

namespace Ui {
class SettingsNetwork;
}

class SettingsNetwork : public QWidget
{
    Q_OBJECT

public:
    explicit SettingsNetwork(QWidget *parent = nullptr);
    ~SettingsNetwork();

    void save();

public slots:
    void onCurrentMachineChanged(int machineId);

private slots:
    void on_pushButtonConfigure_clicked();
    void on_comboBoxAdapter_currentIndexChanged(int index);
    void on_comboBoxNetwork_currentIndexChanged(int index);

    void on_comboBoxPcap_currentIndexChanged(int index);

private:
    Ui::SettingsNetwork *ui;
    int machineId = 0;
};

#endif // QT_SETTINGSNETWORK_HPP
