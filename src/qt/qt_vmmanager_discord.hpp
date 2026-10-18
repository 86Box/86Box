/*
 * 86Box    A hypervisor and IBM PC system emulator that specializes in
 *          running old operating systems and software designed for IBM
 *          PC systems and compatibles from 1981 through fairly recent
 *          system designs based on the PCI bus.
 *
 *          This file is part of the 86Box distribution.
 *
 *          Header for the VM manager Discord Rich Presence module
 *
 * Authors: David Hrdlička, <hrdlickadavid@outlook.com>
 *
 *          Copyright 2026 David Hrdlička.
 */
#ifndef QT_VMMANAGER_DISCORD_HPP
#define QT_VMMANAGER_DISCORD_HPP

#include <QElapsedTimer>
#include <QHash>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QObject>
#include <QPointer>
#include <QTimer>

#include "qt_vmmanager_discordclient.hpp"
#include "qt_vmmanager_model.hpp"

// Decides which running machine is shown in the user's Discord status and
// what is revealed about it.
class VMManagerDiscord final : public QObject {
    Q_OBJECT

public:
    enum class Privacy {
        NameAndHardware = 0,
        HardwareOnly    = 1,
        Minimal         = 2,
    };

    explicit VMManagerDiscord(QObject *parent = nullptr);

    // Must be called again whenever the model is replaced
    void setModel(VMManagerModel *newModel);
    void loadSettings();

private:
    struct Tracked {
        VMManagerSystem::ProcessStatus status = VMManagerSystem::ProcessStatus::Stopped;
        // Wall clock time the machine was started, for the elapsed timer
        qint64 started_at = 0;
        // Monotonic time the machine was last started, resumed or used
        qint64 used_at = 0;
    };

    struct IconCheck {
        bool pending   = false;
        bool available = false;
        // Monotonic time before which the icon isn't checked again
        qint64 retry_at = 0;
    };

    VMManagerDiscordClient            client;
    QPointer<VMManagerModel>          model;
    QHash<VMManagerSystem *, Tracked> tracked;
    QHash<QString, IconCheck>         icon_checks;
    QNetworkAccessManager             network;
    QPointer<VMManagerSystem>         shown;
    QPointer<VMManagerSystem>         focused;
    QTimer                            dwell_timer;
    QElapsedTimer                     clock;
    bool                              enabled = false;
    Privacy                           privacy = Privacy::NameAndHardware;

    void trackSystem(VMManagerSystem *system);
    void statusChanged(VMManagerSystem *system);
    void windowActivated(VMManagerSystem *system);
    void dwellElapsed();
    void update();

    [[nodiscard]] bool           isCandidate(const VMManagerSystem *system) const;
    [[nodiscard]] QJsonObject    buildActivity(VMManagerSystem *system, int others);
    [[nodiscard]] QString        verifiedIconUrl(const QString &icon);
    [[nodiscard]] static QString iconUrl(const QString &icon);
};

#endif // QT_VMMANAGER_DISCORD_HPP
