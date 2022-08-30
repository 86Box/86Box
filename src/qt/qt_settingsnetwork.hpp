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
    void on_pushButtonConf1_clicked();
    void on_pushButtonConf2_clicked();
    void on_pushButtonConf3_clicked();
    void on_pushButtonConf4_clicked();
    void on_comboIndexChanged(int index);

    void enableElements(Ui::SettingsNetwork *ui);

private:
    Ui::SettingsNetwork *ui;
    int machineId = 0;
};

#endif // QT_SETTINGSNETWORK_HPP
