#ifndef QT_SETTINGSNETWORK_HPP
#define QT_SETTINGSNETWORK_HPP

#include <QWidget>

#include <vector>

#include "qt_slirpforwardingdialog.hpp"

namespace Ui {
class SettingsNetwork;
}

class SettingsNetwork : public QWidget {
    Q_OBJECT

public:
    explicit SettingsNetwork(QWidget *parent = nullptr);
    ~SettingsNetwork();

    int  changed();
    /* The card chosen in row i of the page, as the settings stand now. */
    int netCard(int i) const;

    void restore();
    void save(int soft);

public slots:
    void onCurrentMachineChanged(int machineId);

private slots:
    void on_pushButtonConf1_clicked();
    void on_pushButtonConf2_clicked();
    void on_pushButtonConf3_clicked();
    void on_pushButtonConf4_clicked();
    void on_pushButtonSlirpFwd1_clicked();
    void on_pushButtonSlirpFwd2_clicked();
    void on_pushButtonSlirpFwd3_clicked();
    void on_pushButtonSlirpFwd4_clicked();
    void on_comboIndexChanged(int index);

    void enableElements(Ui::SettingsNetwork *ui);

private:
    void initializeHostOptions();

    Ui::SettingsNetwork *ui;
    int                  machineId = 0;

    int                  net_card_cfg_changed[4] = { 0, 0, 0, 0 };

    /* SLiRP port forwarding rules accepted from the dialog, written to
       the configuration only when the settings are saved. */
    bool                          slirp_fwd_changed[4] = { false, false, false, false };
    std::vector<SlirpForwardingDialog::Rule> slirp_fwd_new[4];

    SettingsCompleter   *sc[4];
    SettingsCompleter   *scDevice[4];
};

#endif // QT_SETTINGSNETWORK_HPP
