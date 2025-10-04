/*
 * 86Box    A hypervisor and IBM PC system emulator that specializes in
 *          running old operating systems and software designed for IBM
 *          PC systems and compatibles from 1981 through fairly recent
 *          system designs based on the PCI bus.
 *
 *          This file is part of the 86Box distribution.
 *
 *          86Box VM manager server socket module
 *
 * Authors: cold-brewed
 *
 *          Copyright 2024 cold-brewed
 */
#include "qt_vmmanager_serversocket.hpp"
#include <QApplication>
#include <QCryptographicHash>
#include <QJsonParseError>
#include <QMetaEnum>
#include <QStandardPaths>
#include <utility>

VMManagerServerSocket::VMManagerServerSocket(const QFileInfo &config_path, const ServerType type)
{
    server_type     = type;
    config_file     = config_path;
    serverIsRunning = false;
    socket          = nullptr;
    server          = new QLocalServer;
    setupVars();
}

VMManagerServerSocket::~VMManagerServerSocket()
{
    delete server;
}

bool
VMManagerServerSocket::startServer()
{

    // Remove socket file (if it exists) in order to start a new one
    qInfo("Socket path is %s", qPrintable(socket_path.filePath()));
    if (socket_path.exists() and !socket_path.isDir()) {
        auto socket_file = new QFile(socket_path.filePath());
        if (!socket_file->remove()) {
            qInfo("Failed to remove the old socket file (Error %i): %s", socket_file->error(), qPrintable(socket_file->errorString()));
            return false;
        }
    }

    if (server->listen(socket_path.fileName())) {
        serverIsRunning = true;
        connect(server, &QLocalServer::newConnection, this, &VMManagerServerSocket::serverConnectionReceived);
        return true;
    } else {
        qInfo("Failed to start server: %s", qPrintable(server->errorString()));
        serverIsRunning = false;
        return false;
    }
}

void
VMManagerServerSocket::serverConnectionReceived()
{
    qDebug("Connection received on %s", qPrintable(socket_path.fileName()));
    socket = server->nextPendingConnection();
    if (!socket) {
        qInfo("Invalid socket when trying to receive the connection");
        return;
    }
    connect(socket, &QLocalSocket::readyRead, this, &VMManagerServerSocket::serverReceivedMessage);
    connect(socket, &QLocalSocket::disconnected, this, &VMManagerServerSocket::serverDisconnected);
}

void
VMManagerServerSocket::serverReceivedMessage()
{

    // Handle legacy socket connections first. These connections only receive
    // information on window status
    if (server_type == VMManagerServerSocket::ServerType::Legacy) {
        QByteArray tempString      = socket->read(1);
        int        window_obscured = tempString.toInt();
        emit       windowStatusChanged(window_obscured);
        return;
    }

    // Normal connections here
    QDataStream stream(socket);
    stream.setVersion(QDataStream::Qt_5_7);
    QByteArray jsonData;
    for (;;) {
        // Start a transaction
        stream.startTransaction();
        // Try to read the data
        stream >> jsonData;
        if (stream.commitTransaction()) {
            QJsonParseError parse_error {};
            // Validate the received data to make sure it's valid json
            const QJsonDocument jsonDoc = QJsonDocument::fromJson(jsonData, &parse_error);
            if (parse_error.error == QJsonParseError::NoError) {
                // The data received was valid json
                if (jsonDoc.isObject()) {
                    // The data is a valid json object
                    emit dataReceived();
                    jsonReceived(jsonDoc.object());
                }
            }
            // The data was not valid json.
            // Loop and try to read more data
        } else {
            // read failed, socket is reverted to its previous state (before the transaction)
            // exit the loop and wait for more data to become available
            break;
        }
    }
}

void
VMManagerServerSocket::serverSendMessage(VMManagerProtocol::ManagerMessage protocol_message, const QStringList &arguments) const
{
    if (!socket) {
        qInfo("Cannot send message: Invalid socket");
        return;
    }

    // Regular connection
    QDataStream stream(socket);
    stream.setVersion(QDataStream::Qt_5_7);
    auto packet      = new VMManagerProtocol(VMManagerProtocol::Sender::Manager);
    auto jsonMessage = packet->protocolManagerMessage(protocol_message);
    stream << QJsonDocument(jsonMessage).toJson(QJsonDocument::Compact);
}

void
VMManagerServerSocket::serverDisconnected()
{
    qInfo("Connection disconnected");
}
void
VMManagerServerSocket::jsonReceived(const QJsonObject &json)
{
    // The serialization portion has already validated the message as json.
    // Now ensure it has the required fields.
    if (!VMManagerProtocol::hasRequiredFields(json)) {
        // TODO: Error handling of some sort, emit signals
        qDebug("Invalid message received from client: required fields missing. Object:");
        qDebug() << json;
        return;
    }
    //    qDebug().noquote() << Q_FUNC_INFO << json;
    QJsonObject params_object;

    auto message_type = VMManagerProtocol::getClientMessageType(json);
    switch (message_type) {
        case VMManagerProtocol::ClientMessage::WinIdMessage:
            qDebug("WinId message received from client");
            params_object = VMManagerProtocol::getParams(json);
            if (!params_object.isEmpty()) {
                // valid object
                if (params_object.value("params").type() == QJsonValue::Double) {
                    emit winIdReceived(params_object.value("params").toVariant().toULongLong());
                }
            }
            break;
        case VMManagerProtocol::ClientMessage::Status:
            qDebug("Status message received from client");
            break;
        case VMManagerProtocol::ClientMessage::WindowBlocked:
            qDebug("Window blocked message received from client");
            emit windowStatusChanged(static_cast<int>(VMManagerProtocol::WindowStatus::WindowBlocked));
            break;
        case VMManagerProtocol::ClientMessage::WindowUnblocked:
            qDebug("Window unblocked received from client");
            emit windowStatusChanged(static_cast<int>(VMManagerProtocol::WindowStatus::WindowUnblocked));
            break;
        case VMManagerProtocol::ClientMessage::RunningStateChanged:
            qDebug("Running state change received from client");
            params_object = VMManagerProtocol::getParams(json);
            if (!params_object.isEmpty()) {
                // valid object
                if (params_object.value("status").type() == QJsonValue::Double) {
                    // has status key, value is an int (qt assigns it as Double)
                    emit runningStatusChanged(static_cast<VMManagerProtocol::RunningState>(params_object.value("status").toInt()));
                }
            }
            break;
        case VMManagerProtocol::ClientMessage::ConfigurationChanged:
            qDebug("Configuration change received from client");
            emit configurationChanged();
            break;
        case VMManagerProtocol::ClientMessage::GlobalConfigurationChanged:
            qDebug("Global configuration change received from client");
            emit globalConfigurationChanged();
            break;
        default:
            qDebug("Unknown client message type received:");
            qDebug() << json;
            return;
    }
}

void
VMManagerServerSocket::setupVars()
{
    QString unique_name = QCryptographicHash::hash(config_file.path().toUtf8().constData(), QCryptographicHash::Algorithm::Sha256).toHex().right(6);
    socket_path.setFile(QStandardPaths::writableLocation(QStandardPaths::TempLocation) + "/" + QApplication::applicationName() + ".socket." + unique_name);
}

QString
VMManagerServerSocket::getSocketPath() const
{
    if (server)
        return server->fullServerName();

    return {};
}

QString
VMManagerServerSocket::serverTypeToString(VMManagerServerSocket::ServerType server_type_lookup)
{
    QMetaEnum qme = QMetaEnum::fromType<VMManagerServerSocket::ServerType>();

    return qme.valueToKey(static_cast<int>(server_type_lookup));
}
