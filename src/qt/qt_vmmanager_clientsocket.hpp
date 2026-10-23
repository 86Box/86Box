/*
 * 86Box    A hypervisor and IBM PC system emulator that specializes in
 *          running old operating systems and software designed for IBM
 *          PC systems and compatibles from 1981 through fairly recent
 *          system designs based on the PCI bus.
 *
 *          This file is part of the 86Box distribution.
 *
 *          Header file for 86Box VM manager client socket module
 *
 * Authors: cold-brewed
 *
 *          Copyright 2024 cold-brewed
 */
#ifndef QT_VMMANAGER_CLIENTSOCKET_HPP
#define QT_VMMANAGER_CLIENTSOCKET_HPP

#include "qt_vmmanager_protocol.hpp"
#include <QEvent>
#include <QLocalSocket>
#include <QObject>
#include <QWidget>

class VMManagerClientSocket final : public QObject {
    Q_OBJECT

public:
    explicit VMManagerClientSocket(QObject *object = nullptr);
    bool IPCConnect(const QString &server);

    void sendWinIdMessage(WId id);

signals:
    void pause();
    void ctrlaltdel();
    void showsettings();
    void resetVM();
    void request_shutdown();
    void force_shutdown();
    void dialogstatus(bool open);
    // Whether the built-in 86Box VM manager supervises this machine changed
    void vmmSupervisedChanged(bool vmm_supervised);
    // The manager changed the global configuration, which was reloaded
    void globalConfigurationReloaded();

public slots:
    void clientRunningStateChanged(VMManagerProtocol::RunningState state) const;
    void configurationChanged() const;
    void globalConfigurationChanged() const;
    void windowActivated() const;

private:
    QString       server_name;
    QLocalSocket *socket;
    bool          server_connected;
    bool          window_blocked = false;
    // Whether the server identified itself as the built-in 86Box VM manager,
    // as opposed to another manager speaking the same protocol
    bool          vmm_supervised = false;
    void          connected() const;
    void          disconnected();
    void          setVmmSupervised(bool new_vmm_supervised);
    static void   connectionError(QLocalSocket::LocalSocketError socketError);

    // Main convenience send function
    void sendMessage(VMManagerProtocol::ClientMessage protocol_message) const;
    // Send message with optional params array convenience function
    void sendMessageWithList(VMManagerProtocol::ClientMessage protocol_message, const QStringList &list) const;
    // Send message with optional json object convenience function
    void sendMessageWithObject(VMManagerProtocol::ClientMessage protocol_message, const QJsonObject &json) const;
    // Full send message function called by all convenience functions
    void sendMessageFull(VMManagerProtocol::ClientMessage protocol_message, const QStringList &list, const QJsonObject &json) const;
    void jsonReceived(const QJsonObject &json);

    void dataReady();

protected:
    bool eventFilter(QObject *obj, QEvent *event) override;
};

#endif // QT_VMMANAGER_CLIENTSOCKET_HPP
