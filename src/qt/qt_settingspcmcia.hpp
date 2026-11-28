/*
 * 86Box       A hypervisor and IBM PC system emulator.
 *
 *             The PCMCIA settings page: the PC Card controller, and the card
 *             in each of its sockets with that card's settings.
 *
 * Authors:    Marcos Alves
 *
 *             Copyright 2026 Marcos Alves.
 *
 *             Released under the GNU General Public License version 2 or
 *             later.  See COPYING for more information.
 */
#ifndef QT_SETTINGSPCMCIA_HPP
#define QT_SETTINGSPCMCIA_HPP

#include <QWidget>

#include <vector>

#include "qt_slirpforwardingdialog.hpp"

extern "C" {
#include <86box/pcmcia.h>
}

class QCheckBox;
class QComboBox;
class QGroupBox;
class QLabel;
class QPushButton;

class SettingsPcmcia : public QWidget {
    Q_OBJECT

public:
    explicit SettingsPcmcia(QWidget *parent = nullptr);

    int  changed();
    void save(int soft);
    int  socketCard(int s) const;   /* the card chosen now; 0 with the controller off */

public slots:
    void onCurrentMachineChanged(int machineId);

private:
    struct Socket {
        QGroupBox   *group;
        QComboBox   *card;
        QPushButton *configure;
        QLabel      *netLabel;
        QComboBox   *netType;
        QLabel      *hostLabel;
        QComboBox   *host;
        QPushButton *slirpFwd;
        int          cfgChanged = 0;
    };

    /* SLiRP port forwarding rules accepted from the dialog, written to
       the configuration only when the settings are saved. */
    bool                              fwdChanged[PCMCIA_SOCKETS] = { false, false };
    std::vector<SlirpForwardingDialog::Rule> fwdNew[PCMCIA_SOCKETS];

    void updateSocket(int s);
    void updateState();
    int  hostIndex(int s) const;
    bool cardSettingsChanged(int s) const;

    int        machineId { 0 };
    QCheckBox *enable;
    Socket     sock[PCMCIA_SOCKETS];
};

#endif // QT_SETTINGSPCMCIA_HPP
