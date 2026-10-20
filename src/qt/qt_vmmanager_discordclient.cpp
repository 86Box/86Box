/*
 * 86Box    A hypervisor and IBM PC system emulator that specializes in
 *          running old operating systems and software designed for IBM
 *          PC systems and compatibles from 1981 through fairly recent
 *          system designs based on the PCI bus.
 *
 *          This file is part of the 86Box distribution.
 *
 *          Discord Rich Presence IPC client
 *
 * Authors: David Hrdlička, <hrdlickadavid@outlook.com>
 *
 *          Copyright 2026 David Hrdlička.
 */
#include <QCoreApplication>
#include <QDebug>
#include <QFileInfo>
#include <QJsonDocument>
#include <QtEndian>
#include <QUuid>

#include "qt_vmmanager_discordclient.hpp"

// Delay used to coalesce several activity changes into one update
static constexpr int kUpdateDelay = 250;
// Discord accepts at most 5 activity updates per 20 seconds
static constexpr int    kRateLimitCount  = 5;
static constexpr qint64 kRateLimitWindow = 20000;
// How long to wait before looking for Discord again
static constexpr int kReconnectInterval = 15000;
// Discord's frames are tiny, so anything larger means the stream is out of sync
static constexpr quint32 kMaxFrameSize = 64 * 1024;

VMManagerDiscordClient::VMManagerDiscordClient(const QString &clientId, QObject *parent)
    : QObject(parent)
    , client_id(clientId)
    , socket(new QLocalSocket(this))
{
    clock.start();

    reconnect_timer.setSingleShot(true);
    reconnect_timer.setInterval(kReconnectInterval);
    connect(&reconnect_timer, &QTimer::timeout, this, &VMManagerDiscordClient::beginConnect);

    update_timer.setSingleShot(true);
    connect(&update_timer, &QTimer::timeout, this, &VMManagerDiscordClient::sendActivity);

    connect(socket, &QLocalSocket::connected, this, &VMManagerDiscordClient::socketConnected);
    connect(socket, &QLocalSocket::disconnected, this, &VMManagerDiscordClient::socketDisconnected);
    connect(socket, &QLocalSocket::errorOccurred, this, &VMManagerDiscordClient::socketError);
    connect(socket, &QLocalSocket::readyRead, this, &VMManagerDiscordClient::readFrames);
}

VMManagerDiscordClient::~VMManagerDiscordClient()
{
    stop();
}

void
VMManagerDiscordClient::start()
{
    if (state != State::Stopped)
        return;

    state = State::Waiting;
    beginConnect();
}

void
VMManagerDiscordClient::stop()
{
    // Discord clears the activity by itself once the connection goes away
    state = State::Stopped;
    reconnect_timer.stop();
    update_timer.stop();
    socket->abort();
}

void
VMManagerDiscordClient::setActivity(const QJsonObject &newActivity)
{
    activity = newActivity;
    if ((state == State::Ready) && !update_timer.isActive())
        update_timer.start(kUpdateDelay);
}

QStringList
VMManagerDiscordClient::candidatePaths()
{
    QStringList paths;
#ifdef Q_OS_WINDOWS
    // QLocalSocket resolves bare names to \\.\pipe\<name>
    for (int i = 0; i < 10; i++)
        paths.append(QString("discord-ipc-%1").arg(i));
#else
    QString base;
    for (const auto &var : { "XDG_RUNTIME_DIR", "TMPDIR", "TMP", "TEMP" }) {
        base = qEnvironmentVariable(var);
        if (!base.isEmpty())
            break;
    }
    if (base.isEmpty())
        base = QStringLiteral("/tmp");

    // Native, Flatpak and Snap installs of Discord respectively
    for (const auto &subdir : { "", "app/com.discordapp.Discord/", "snap.discord/" }) {
        for (int i = 0; i < 10; i++) {
            const auto path = QString("%1/%2discord-ipc-%3").arg(base, subdir).arg(i);
            if (QFileInfo::exists(path))
                paths.append(path);
        }
    }
#endif
    return paths;
}

void
VMManagerDiscordClient::beginConnect()
{
    if (state != State::Waiting)
        return;

    socket_paths = candidatePaths();
    path_index   = 0;
    tryNextPath();
}

void
VMManagerDiscordClient::tryNextPath()
{
    if ((state != State::Waiting) && (state != State::Connecting))
        return;

    socket->abort();
    if (path_index >= socket_paths.size()) {
        state = State::Waiting;
        reconnect_timer.start();
        return;
    }

    state = State::Connecting;
    socket->connectToServer(socket_paths.at(path_index++));
}

void
VMManagerDiscordClient::socketConnected()
{
    if (state != State::Connecting)
        return;

    qDebug().noquote() << "Discord: connected to" << socket->fullServerName();
    state = State::Handshaking;
    read_buffer.clear();
    sendFrame(Opcode::Handshake, { { "v", 1 }, { "client_id", client_id } });
}

void
VMManagerDiscordClient::socketError(QLocalSocket::LocalSocketError error)
{
    if (state == State::Connecting) {
        // This can be emitted from within connectToServer(), so move on to
        // the next candidate only once control is back in the event loop
        QMetaObject::invokeMethod(this, &VMManagerDiscordClient::tryNextPath, Qt::QueuedConnection);
    } else if ((state == State::Handshaking) || (state == State::Ready)) {
        qDebug().noquote() << "Discord: connection error" << error << socket->errorString();
        connectionLost();
    }
}

void
VMManagerDiscordClient::socketDisconnected()
{
    if ((state == State::Handshaking) || (state == State::Ready))
        connectionLost();
}

void
VMManagerDiscordClient::connectionLost()
{
    qDebug("Discord: connection lost");
    state = State::Waiting;
    update_timer.stop();
    socket->abort();
    reconnect_timer.start();
}

void
VMManagerDiscordClient::readFrames()
{
    read_buffer.append(socket->readAll());

    // Each frame is a little endian opcode and length followed by a JSON payload
    while (read_buffer.size() >= 8) {
        const auto opcode = qFromLittleEndian<quint32>(read_buffer.constData());
        const auto length = qFromLittleEndian<quint32>(read_buffer.constData() + 4);
        if (length > kMaxFrameSize) {
            qWarning("Discord: invalid frame of %u bytes received", length);
            connectionLost();
            return;
        }
        if (read_buffer.size() < static_cast<qsizetype>(8 + length))
            break;

        const auto payload = QJsonDocument::fromJson(read_buffer.mid(8, length)).object();
        read_buffer.remove(0, 8 + length);
        handleFrame(static_cast<Opcode>(opcode), payload);

        // The frame may have closed the connection
        if ((state != State::Handshaking) && (state != State::Ready))
            return;
    }
}

void
VMManagerDiscordClient::handleFrame(Opcode opcode, const QJsonObject &payload)
{
    switch (opcode) {
        case Opcode::Frame:
            if (payload.value("evt").toString() == "READY") {
                qDebug("Discord: ready");
                state = State::Ready;
                // A fresh connection has no activity set
                sent_activity = QJsonObject();
                sendActivity();
            } else if (payload.value("evt").toString() == "ERROR") {
                const auto data = payload.value("data").toObject();
                qWarning().noquote() << "Discord: error" << data.value("code").toInt() << data.value("message").toString();
            }
            break;
        case Opcode::Ping:
            sendFrame(Opcode::Pong, payload);
            break;
        case Opcode::Close:
            qWarning().noquote() << "Discord: connection closed:" << payload.value("message").toString();
            connectionLost();
            break;
        default:
            break;
    }
}

void
VMManagerDiscordClient::sendFrame(Opcode opcode, const QJsonObject &payload)
{
    const auto json = QJsonDocument(payload).toJson(QJsonDocument::Compact);
    QByteArray frame(8, Qt::Uninitialized);
    qToLittleEndian<quint32>(static_cast<quint32>(opcode), frame.data());
    qToLittleEndian<quint32>(static_cast<quint32>(json.size()), frame.data() + 4);
    frame.append(json);
    socket->write(frame);
}

void
VMManagerDiscordClient::sendActivity()
{
    if ((state != State::Ready) || (activity == sent_activity))
        return;

    // Stay within the rate limit; whatever the activity is by then gets sent
    const auto now = clock.elapsed();
    while (!send_times.isEmpty() && ((now - send_times.first()) >= kRateLimitWindow))
        send_times.removeFirst();
    if (send_times.size() >= kRateLimitCount) {
        update_timer.start(static_cast<int>(send_times.first() + kRateLimitWindow - now));
        return;
    }

    QJsonObject args { { "pid", QCoreApplication::applicationPid() } };
    if (!activity.isEmpty())
        args["activity"] = activity;
    qDebug().noquote() << "Discord: setting activity" << QJsonDocument(activity).toJson(QJsonDocument::Compact);

    sendFrame(Opcode::Frame, {
                                 { "cmd", "SET_ACTIVITY" },
                                 { "args", args },
                                 { "nonce", QUuid::createUuid().toString(QUuid::WithoutBraces) },
                             });
    send_times.append(now);
    sent_activity = activity;
}
