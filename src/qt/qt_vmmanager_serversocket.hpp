/*
 * 86Box    A hypervisor and IBM PC system emulator that specializes in
 *          running old operating systems and software designed for IBM
 *          PC systems and compatibles from 1981 through fairly recent
 *          system designs based on the PCI bus.
 *
 *          This file is part of the 86Box distribution.
 *
 *          Header for 86Box VM manager server socket module
 *
 * Authors: cold-brewed
 *
 *          Copyright 2024 cold-brewed
 */
#ifndef QT_VMMANAGER_SERVERSOCKET_H
#define QT_VMMANAGER_SERVERSOCKET_H

#include <QFileInfo>
#include <QLocalServer>
#include <QLocalSocket>
#include <QWidget>
#include <QJsonObject>
#include "qt_vmmanager_protocol.hpp"

// This macro helps give us the required `qHash()` function in order to use the
// enum as a hash key
#define QHASH_FOR_CLASS_ENUM(T)                                                       \
    inline uint qHash(const T &t, uint seed)                                          \
    {                                                                                 \
        return ::qHash(static_cast<typename std::underlying_type<T>::type>(t), seed); \
    }

class VMManagerServerSocket : public QWidget {

    Q_OBJECT

public:
    enum class ServerType {
        Standard,
        Legacy,
    };
    // This macro allows us to do a reverse lookup of the enum with `QMetaEnum`
    Q_ENUM(ServerType)

    QHASH_FOR_CLASS_ENUM(ServerType)

    explicit VMManagerServerSocket(const QFileInfo &config_path, ServerType type = ServerType::Standard);
    ~VMManagerServerSocket() override;

    QFileInfo socket_path;
    QFileInfo config_file;

    QLocalServer *server;
    QLocalSocket *socket;
    ServerType    server_type;
    bool          serverIsRunning;

    // Server functions
    bool        startServer();
    void        serverConnectionReceived();
    void        serverReceivedMessage();
    void        serverSendMessage(VMManagerProtocol::ManagerMessage protocol_message, const QStringList &arguments = QStringList()) const;
    static void serverDisconnected();
    void        jsonReceived(const QJsonObject &json);
    QString     getSocketPath() const;

    static QString serverTypeToString(ServerType server_type_lookup);

    void setupVars();

signals:
    void dataReceived();
    void windowStatusChanged(int status);
    void runningStatusChanged(VMManagerProtocol::RunningState state);
    void configurationChanged();
    void globalConfigurationChanged();
    void winIdReceived(WId id);
};

#endif // QT_VMMANAGER_SERVERSOCKET_H
