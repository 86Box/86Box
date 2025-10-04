/*
 * 86Box    A hypervisor and IBM PC system emulator that specializes in
 *          running old operating systems and software designed for IBM
 *          PC systems and compatibles from 1981 through fairly recent
 *          system designs based on the PCI bus.
 *
 *          This file is part of the 86Box distribution.
 *
 *          86Box VM manager protocol module
 *
 * Authors: cold-brewed
 *
 *          Copyright 2024 cold-brewed
 */
#include "qt_vmmanager_protocol.hpp"
#include <QJsonDocument>
#include <QMetaEnum>

VMManagerProtocol::VMManagerProtocol(VMManagerProtocol::Sender sender)
{
    message_class = sender;
}

VMManagerProtocol::~VMManagerProtocol()
    = default;

QJsonObject
VMManagerProtocol::protocolManagerMessage(VMManagerProtocol::ManagerMessage message_type)
{
    auto json_message       = constructDefaultObject(VMManagerProtocol::Sender::Manager);
    json_message["message"] = managerMessageTypeToString(message_type);
    return json_message;
}

QJsonObject
VMManagerProtocol::protocolClientMessage(VMManagerProtocol::ClientMessage message_type)
{
    auto json_message       = constructDefaultObject(VMManagerProtocol::Sender::Client);
    json_message["message"] = clientMessageTypeToString(message_type);
    return json_message;
}

QString
VMManagerProtocol::managerMessageTypeToString(VMManagerProtocol::ManagerMessage message)
{
    QMetaEnum qme = QMetaEnum::fromType<VMManagerProtocol::ManagerMessage>();
    return qme.valueToKey(static_cast<int>(message));
}

QString
VMManagerProtocol::clientMessageTypeToString(VMManagerProtocol::ClientMessage message)
{
    QMetaEnum qme = QMetaEnum::fromType<VMManagerProtocol::ClientMessage>();
    return qme.valueToKey(static_cast<int>(message));
}

QJsonObject
VMManagerProtocol::constructDefaultObject(VMManagerProtocol::Sender type)
{
    QJsonObject json_message;
    QString     sender_type = (type == VMManagerProtocol::Sender::Client) ? "Client" : "VMManager";
    json_message["type"]    = QString(sender_type);
    json_message["version"] = QStringLiteral(EMU_VERSION);
    return json_message;
}

bool
VMManagerProtocol::hasRequiredFields(const QJsonObject &json_document)
{
    for (const auto &field : ProtocolRequiredFields) {
        if (!json_document.contains(field)) {
            qDebug("Received json missing field \"%s\"", qPrintable(field));
            return false;
        }
    }
    return true;
}

VMManagerProtocol::ClientMessage
VMManagerProtocol::getClientMessageType(const QJsonObject &json_document)
{
    // FIXME: This key ("message") is hardcoded here. Make a hash which maps these
    // required values.
    QString message_type = json_document.value("message").toString();
    // Can't use switch with strings, manual compare
    if (message_type == "Status")
        return VMManagerProtocol::ClientMessage::Status;
    else if (message_type == "WindowBlocked")
        return VMManagerProtocol::ClientMessage::WindowBlocked;
    else if (message_type == "WindowUnblocked")
        return VMManagerProtocol::ClientMessage::WindowUnblocked;
    else if (message_type == "RunningStateChanged")
        return VMManagerProtocol::ClientMessage::RunningStateChanged;
    else if (message_type == "ConfigurationChanged")
        return VMManagerProtocol::ClientMessage::ConfigurationChanged;
    else if (message_type == "WinIdMessage")
        return VMManagerProtocol::ClientMessage::WinIdMessage;
    else if (message_type == "GlobalConfigurationChanged")
        return VMManagerProtocol::ClientMessage::GlobalConfigurationChanged;

    return VMManagerProtocol::ClientMessage::UnknownMessage;
}

VMManagerProtocol::ManagerMessage
VMManagerProtocol::getManagerMessageType(const QJsonObject &json_document)
{
    // FIXME: This key ("message") is hardcoded here. Make a hash which maps these
    // required values.
    QString message_type = json_document.value("message").toString();
    // Can't use switch with strings, manual compare
    if (message_type == "RequestStatus")
        return VMManagerProtocol::ManagerMessage::RequestStatus;
    else if (message_type == "Pause")
        return VMManagerProtocol::ManagerMessage::Pause;

    if (message_type == "CtrlAltDel")
        return VMManagerProtocol::ManagerMessage::CtrlAltDel;

    if (message_type == "ShowSettings")
        return VMManagerProtocol::ManagerMessage::ShowSettings;

    if (message_type == "ResetVM")
        return VMManagerProtocol::ManagerMessage::ResetVM;

    if (message_type == "RequestShutdown")
        return VMManagerProtocol::ManagerMessage::RequestShutdown;

    if (message_type == "ForceShutdown")
        return VMManagerProtocol::ManagerMessage::ForceShutdown;

    if (message_type == "GlobalConfigurationChanged")
        return VMManagerProtocol::ManagerMessage::GlobalConfigurationChanged;

    return VMManagerProtocol::ManagerMessage::UnknownMessage;
}

QJsonObject
VMManagerProtocol::getParams(const QJsonObject &json_document)
{
    // FIXME: This key ("params") is hardcoded here. Make a hash which maps these
    // required values.
    auto params_object = json_document.value("params");
    if (params_object.type() != QJsonValue::Object)
        return {};

    return params_object.toObject();
}
