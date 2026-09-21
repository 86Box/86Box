/*
 * 86Box    A hypervisor and IBM PC system emulator that specializes in
 *          running old operating systems and software designed for IBM
 *          PC systems and compatibles from 1981 through fairly recent
 *          system designs based on the PCI bus.
 *
 *          This file is part of the 86Box distribution.
 *
 *          Header for 86Box VM manager protocol module
 *
 * Authors: cold-brewed
 *
 *          Copyright 2024 cold-brewed
 */
#ifndef QT_VMMANAGER_PROTOCOL_H
#define QT_VMMANAGER_PROTOCOL_H

#include <QJsonObject>
extern "C" {
#include <86box/version.h>
}

static QVector<QString> ProtocolRequiredFields = { "type", "message" };

class VMManagerProtocol : QObject {
    Q_OBJECT

public:
    enum class Sender {
        Manager,
        Client,
    };
    Q_ENUM(Sender);

    enum class ManagerMessage {
        RequestStatus,
        Pause,
        CtrlAltDel,
        ShowSettings,
        ResetVM,
        RequestShutdown,
        ForceShutdown,
        GlobalConfigurationChanged,
        UnknownMessage,
    };

    // This macro allows us to do a reverse lookup of the enum with `QMetaEnum`
    Q_ENUM(ManagerMessage);

    enum class ClientMessage {
        Status,
        WindowBlocked,
        WindowUnblocked,
        RunningStateChanged,
        ConfigurationChanged,
        WinIdMessage,
        GlobalConfigurationChanged,
        UnknownMessage,
    };
    Q_ENUM(ClientMessage);

    enum class WindowStatus {
        WindowUnblocked = 0,
        WindowBlocked,
    };

    enum class RunningState {
        Running = 0,
        Paused,
        RunningWaiting,
        PausedWaiting,
        Unknown,
    };
    Q_ENUM(RunningState);

    explicit VMManagerProtocol(Sender sender);
    ~VMManagerProtocol();

    QJsonObject    protocolManagerMessage(ManagerMessage message_type);
    QJsonObject    protocolClientMessage(ClientMessage message_type);
    static QString managerMessageTypeToString(ManagerMessage message);
    static QString clientMessageTypeToString(ClientMessage message);

    static bool           hasRequiredFields(const QJsonObject &json_document);
    static QJsonObject    getParams(const QJsonObject &json_document);
    static QJsonObject    getStatus(const QJsonObject &json_document);
    static ClientMessage  getClientMessageType(const QJsonObject &json_document);
    static ManagerMessage getManagerMessageType(const QJsonObject &json_document);

private:
    Sender             message_class;
    static QJsonObject constructDefaultObject(VMManagerProtocol::Sender type);
};

#endif // QT_VMMANAGER_PROTOCOL_H
