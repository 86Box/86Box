/*
 * 86Box    A hypervisor and IBM PC system emulator that specializes in
 *          running old operating systems and software designed for IBM
 *          PC systems and compatibles from 1981 through fairly recent
 *          system designs based on the PCI bus.
 *
 *          This file is part of the 86Box distribution.
 *
 *          Header for 86Box VM manager system module
 *
 * Authors: cold-brewed
 *
 *          Copyright 2024 cold-brewed
 */
#ifndef QT_VMMANAGER_SYSTEM_H
#define QT_VMMANAGER_SYSTEM_H

#include <QFileInfo>
#include <QDir>
#include <QProcess>
#include <QLocalServer>
#include <QWidget>
#include "qt_vmmanager_serversocket.hpp"
#include "qt_vmmanager_config.hpp"
#include "qt_deviceconfig.hpp"

// This macro helps give us the required `qHash()` function in order to use the
// enum as a hash key
#define QHASH_FOR_CLASS_ENUM(T)                                                   \
inline uint qHash(const T &t, uint seed) {                                        \
    return ::qHash(static_cast<typename std::underlying_type<T>::type>(t), seed); \
}

namespace VMManager {
Q_NAMESPACE
namespace Display {
Q_NAMESPACE
enum class Name {
    Machine,
    CPU,
    Memory,
    Video,
    Disks,
    Floppy,
    CD,
    RDisk,
    MO,
    SCSIController,
    StorageController,
    MidiOut,
    Joystick,
    Serial,
    Parallel,
    Audio,
    Voodoo,
    NIC,
    Keyboard,
    Mouse,
    IsaRtc,
    IsaMem,
    IsaRom,
    Unknown
};
Q_ENUM_NS(Name)
QHASH_FOR_CLASS_ENUM(Name)
}
}

class VMManagerSystem : public QWidget {
    Q_OBJECT

    typedef QHash<VMManager::Display::Name, QString> display_table_t;
    typedef QHash <QString, QHash <QString, QString>> config_hash_t;

public:

    enum class ProcessStatus {
        Stopped,
        Running,
        Paused,
        PausedWaiting,
        RunningWaiting,
        Unknown,
    };
    Q_ENUM(ProcessStatus);

    explicit VMManagerSystem(const QString &sysconfig_file);
    // Default constructor will generate a temporary filename as the config file
    // but it will not be valid (isValid() will return false)
    VMManagerSystem() : VMManagerSystem(generateTemporaryFilename()) {}

    ~VMManagerSystem() override;

    static QVector<VMManagerSystem *> scanForConfigs(QWidget* parent = nullptr, const QString &searchPath = {});
    static QString generateTemporaryFilename();

    QFileInfo   config_file;
    QString     config_name;
    QString     config_dir;
    QString     shortened_dir;
    QString     uuid;
    QString     displayName;
    QString     notes;
    QString     icon;
    QStringList searchTerms;

    config_hash_t config_hash;

    [[nodiscard]] QString getAll(const QString& category) const;
    [[nodiscard]] QHash <QString, QString> getCategory(const QString& category) const;
    [[nodiscard]] QHash <QString, QHash <QString, QString>> getConfigHash() const;

    void setDisplayName(const QString& newDisplayName);
    void setNotes(const QString& newNotes);

    [[nodiscard]] bool isValid() const;
    [[nodiscard]] bool isProcessRunning() const;
    [[nodiscard]] qint64 processId() const;
public slots:
    void launchMainProcess();
    void launchSettings();
    void startButtonPressed();
    void restartButtonPressed();
    void pauseButtonPressed();
    void shutdownRequestButtonPressed();
    void shutdownForceButtonPressed();
    void cadButtonPressed();
    void reloadConfig();
    void sendGlobalConfigurationChanged();
public:
    QDateTime timestamp();
    void setIcon(const QString &newIcon);

    QProcess *process = new QProcess();

    bool window_obscured;
    bool config_signal_connected = false;

    QString getDisplayValue(VMManager::Display::Name key);
    QFileInfoList getScreenshots();

    inline bool operator==(const VMManagerSystem &rhs) const
    {
        return config_file.filePath() == rhs.config_file.filePath();
    }

    static QString
    processStatusToString(VMManagerSystem::ProcessStatus status) ;
    ProcessStatus process_status;
    [[nodiscard]] QString getProcessStatusString() const;
    [[nodiscard]] ProcessStatus getProcessStatus() const;

signals:
    void windowStatusChanged();
    void itemDataChanged();
    void clientProcessStatusChanged();
    void configurationChanged(const QString &uuid);
    void globalConfigurationChanged();

private:
    void loadSettings();
    void saveSettings();
    void generateSearchTerms();
    void updateTimestamp();

    display_table_t display_table;

    QFileInfo main_binary;
    QString platform;

    // QDir application_temp_directory;
    // QDir standard_temp_directory;
    // QDir app_data_directory;
    QDir screenshot_directory;

    QString unique_name;
    QDateTime   lastUsedTimestamp;

    VMManagerServerSocket            *socket_server;
    VMManagerServerSocket::ServerType socket_server_type;

    // Configuration file settings
    VMManagerConfig *config_settings;

    WId id;

    bool serverIsRunning;
    bool startServer();

    bool has86BoxBinary();
    void find86BoxBinary();
    void setupPaths();
    void setupVars();
    void setProcessEnvVars();

    void dataReceived();
    void windowStatusChangeReceived(int status);
    void runningStatusChangeReceived(VMManagerProtocol::RunningState state);
    void configurationChangeReceived();
    void processStatusChanged();
    void statusRefresh();
};

#endif //QT_VMMANAGER_SYSTEM_H
