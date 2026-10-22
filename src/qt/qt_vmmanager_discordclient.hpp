/*
 * 86Box    A hypervisor and IBM PC system emulator that specializes in
 *          running old operating systems and software designed for IBM
 *          PC systems and compatibles from 1981 through fairly recent
 *          system designs based on the PCI bus.
 *
 *          This file is part of the 86Box distribution.
 *
 *          Header for the Discord Rich Presence IPC client
 *
 * Authors: David Hrdlička, <hrdlickadavid@outlook.com>
 *
 *          Copyright 2026 David Hrdlička.
 */
#ifndef QT_VMMANAGER_DISCORDCLIENT_HPP
#define QT_VMMANAGER_DISCORDCLIENT_HPP

#include <QElapsedTimer>
#include <QJsonObject>
#include <QLocalSocket>
#include <QObject>
#include <QTimer>

// Talks to the local Discord client over its IPC socket (a named pipe on
// Windows, a Unix domain socket elsewhere) and keeps the activity in sync,
// reconnecting whenever Discord is restarted.
class VMManagerDiscordClient final : public QObject {
    Q_OBJECT

public:
    explicit VMManagerDiscordClient(const QString &clientId, QObject *parent = nullptr);
    ~VMManagerDiscordClient() override;

    void start();
    void stop();
    // An empty object clears the activity
    void setActivity(const QJsonObject &newActivity);

private:
    enum class Opcode : quint32 {
        Handshake = 0,
        Frame     = 1,
        Close     = 2,
        Ping      = 3,
        Pong      = 4,
    };

    enum class State {
        Stopped,
        Waiting,
        Connecting,
        Handshaking,
        Ready,
    };

    QString       client_id;
    QLocalSocket *socket;
    QTimer        reconnect_timer;
    QTimer        update_timer;
    State         state = State::Stopped;
    QStringList   socket_paths;
    int           path_index = 0;
    QByteArray    read_buffer;
    QJsonObject   activity;
    QJsonObject   sent_activity;
    QList<qint64> send_times;
    QElapsedTimer clock;

    static QStringList candidatePaths();

    void beginConnect();
    void tryNextPath();
    void socketConnected();
    void socketError(QLocalSocket::LocalSocketError error);
    void socketDisconnected();
    void connectionLost();
    void readFrames();
    void handleFrame(Opcode opcode, const QJsonObject &payload);
    void sendFrame(Opcode opcode, const QJsonObject &payload);
    void sendActivity();
};

#endif // QT_VMMANAGER_DISCORDCLIENT_HPP
