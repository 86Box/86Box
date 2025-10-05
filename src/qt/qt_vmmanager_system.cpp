/*
 * 86Box    A hypervisor and IBM PC system emulator that specializes in
 *          running old operating systems and software designed for IBM
 *          PC systems and compatibles from 1981 through fairly recent
 *          system designs based on the PCI bus.
 *
 *          This file is part of the 86Box distribution.
 *
 *          86Box VM manager system module
 *
 * Authors: cold-brewed
 *
 *          Copyright 2024 cold-brewed
 */
#include <QString>
#include <QDirIterator>
#include <QDebug>
#include <QTimer>
#include <QSettings>
#include <QApplication>
#include <QStandardPaths>
#include <QCryptographicHash>
#include <QtNetwork>
#include <QElapsedTimer>
#include <QMessageBox>
#include <QProgressDialog>
#include <QWindow>
#include "qt_util.hpp"
#include "qt_vmmanager_system.hpp"
// #include "qt_vmmanager_details_section.hpp"
#include "qt_vmmanager_detailsection.hpp"

#ifdef Q_OS_WINDOWS
#include <windows.h>
#endif

extern "C" {
#include <86box/86box.h>
#include <86box/device.h>
#include <86box/video.h>
// #include <86box/vid_xga_device.h>
#include <86box/machine.h>
#include <86box/plat.h>
#include <86box/sound.h>
#include <cpu.h>
#include <86box/thread.h>    // required for network.h
#include <86box/timer.h>     // required for network.h and fdd.h
#include <86box/cdrom.h>
#include <86box/cdrom_interface.h>
#include <86box/scsi.h>
#include <86box/scsi_device.h> // required for rdisk.h and mo.h
#include <86box/rdisk.h>
#include <86box/mo.h>
#include <86box/fdd.h>
#include <86box/fdc_ext.h>
#include <86box/hdc.h>
#include <86box/gameport.h>
#include <86box/isartc.h>
#include <86box/isamem.h>
#include <86box/isarom.h>
#include <86box/lpt.h>
#include <86box/midi.h>
#include <86box/network.h>
#include <86box/keyboard.h>
#include <86box/mouse.h>
}

using namespace VMManager;

VMManagerSystem::VMManagerSystem(const QString &sysconfig_file)  {

    // The 86Box configuration file
    config_file = QFileInfo(sysconfig_file);
    // The default name of the system. This is the name of the directory
    // that contains the 86box configuration file
    config_name = config_file.dir().dirName();
    // The full path of the directory that contains the 86box configuration file
    config_dir = shortened_dir = config_file.dir().absolutePath();
    process_status = ProcessStatus::Stopped;
    // In the configuration file the UUID is used as a unique value
    uuid = util::generateUuid(sysconfig_file);
    // That unique value is used to map the information to each individual system.
    config_settings = new VMManagerConfig(VMManagerConfig::ConfigType::System, uuid);

    // On non-windows platforms, shortened_dir will replace the home directory path with ~
    // and be used as the tool tip in the list view
#if not defined(Q_OS_WINDOWS)
    if (config_dir.startsWith(QDir::homePath())) {
        shortened_dir.replace(QDir::homePath(), "~");
    }
#endif
    loadSettings();
    setupPaths();
    // Paths must be setup before vars!
    setupVars();

    serverIsRunning = false;
    window_obscured = false;

    find86BoxBinary();
    platform = QApplication::platformName();
    process = new QProcess();
    connect(process, &QProcess::stateChanged, this, &VMManagerSystem::processStatusChanged);

    // Server type for this instance (Standard should always be used instead of Legacy)
    socket_server_type = VMManagerServerSocket::ServerType::Standard;
    socket_server = new VMManagerServerSocket(config_file, socket_server_type);

    // NOTE: When unique names or UUIDs are written to the individual VM config file, use that
    // here instead of the auto-generated unique_name
    // Save settings once everything is initialized
    saveSettings();
}

VMManagerSystem::~VMManagerSystem() {
    delete socket_server;
}

QVector<VMManagerSystem *>
VMManagerSystem::scanForConfigs(QWidget* parent, const QString &searchPath)
{
    QProgressDialog progDialog(parent);
    unsigned int found = 0;
    progDialog.setCancelButton(nullptr);
    progDialog.setWindowTitle(tr("Searching for VMs..."));
    progDialog.setMinimumDuration(0);
    progDialog.setValue(0);
    progDialog.setMinimum(0);
    progDialog.setMaximum(0);
    progDialog.setWindowFlags(progDialog.windowFlags() & ~Qt::WindowCloseButtonHint);
    progDialog.setMinimumSize(progDialog.sizeHint());
    progDialog.setMaximumSize(progDialog.sizeHint());
    QElapsedTimer scanTimer;
    scanTimer.start();
    QVector<VMManagerSystem *> system_configs;

    const auto config_file_name = QString(CONFIG_FILE);
    const QStringList filters = {config_file_name};
    QStringList matches;
    QString search_directory;

    search_directory = searchPath.isEmpty()? vmm_path : searchPath;

    if(!QDir(search_directory).exists()) {
        //qWarning() << "Path" << search_directory << "does not exist. Cannot continue";
        QDir(search_directory).mkpath(".");
        //return {};
    }

    QDirIterator dir_iterator(search_directory, filters, QDir::Files, QDirIterator::Subdirectories);

    qInfo("Searching %s for %s", qPrintable(search_directory), qPrintable(config_file_name));

    QElapsedTimer timer;
    timer.start();
    while (dir_iterator.hasNext()) {
        found++;
        progDialog.setLabelText(tr("Found %1").arg(QString::number(found)));
        QApplication::processEvents();
        QString   filename = dir_iterator.next();
        matches.append(filename);
    }

    const auto scanElapsed = timer.elapsed();
    qDebug().noquote().nospace() << "Found " << matches.size() << " configs in " << search_directory <<". Scan took " << scanElapsed << " ms";

    timer.restart();
    // foreach (QFileInfo hit, matches) {
    //     system_configs.append(new VMManagerSystem(hit));
    // }
    progDialog.setMaximum(found);
    progDialog.setValue(0);
    unsigned int appended = 0;
    for (const auto &filename : matches) {
        system_configs.append(new VMManagerSystem(filename));
        appended++;
        progDialog.setLabelText(system_configs.last()->displayName);
        progDialog.setValue(appended);
        QApplication::processEvents();
    }
    if (matches.size()) {
        auto elapsed = timer.elapsed();
        qDebug() << "Load loop took" << elapsed << "ms for" << matches.size() << "loads";
        qDebug() << "Overall scan time was" << scanTimer.elapsed() << "ms, average" << elapsed / matches.size() << "ms / load";
    }
    return system_configs;
}

QString
VMManagerSystem::generateTemporaryFilename()
{
    QTemporaryFile tempFile;
    // File will be closed once the QTemporaryFile object goes out of scope
    tempFile.setAutoRemove(true);
    tempFile.open();
    return tempFile.fileName();
}

QFileInfoList
VMManagerSystem::getScreenshots() {

    // Don't bother unless the directory exists
    if(!screenshot_directory.exists()) {
        return {};
    }

    auto screen_scan_dir = QDir(screenshot_directory.path(), "Monitor_1*", QDir::SortFlag::LocaleAware | QDir::SortFlag::IgnoreCase, QDir::Files);
    auto screenshot_files = screen_scan_dir.entryInfoList();
    return screenshot_files;
}

void
VMManagerSystem::loadSettings()
{
    // First, load the information from the 86box.cfg
    QSettings settings(config_file.filePath(), QSettings::IniFormat);
    if (settings.status() != QSettings::NoError) {
        qWarning() << "Error loading" << config_file.path() << " status:" << settings.status();
    }
    // qInfo() << "Loaded "<< config_file.filePath() << "status:" << settings.status();

#if QT_VERSION < QT_VERSION_CHECK(6, 0, 0)
    settings.setIniCodec("UTF-8");
#endif
    // Clear out the config hash in case the config is reloaded
    for (const auto &outer_key : config_hash.keys()) {
        config_hash[outer_key].clear();
    }

    // General
    for (const auto &key_name : settings.childKeys()) {
        config_hash["General"][key_name] = settings.value(key_name).toString();
    }

    for (auto &group_name : settings.childGroups()) {
        settings.beginGroup(group_name);
        for (const auto &key_name : settings.allKeys()) {
            QString setting_value;
            // QSettings will interpret lines with commas as QStringList.
            // Check for it and join them back to a string.
#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
            if (settings.value(key_name).typeId() == QMetaType::QStringList) {
#else
            if (settings.value(key_name).type() == QVariant::StringList) {
#endif
                setting_value = settings.value(key_name).toStringList().join(", ");
            } else {
                setting_value = settings.value(key_name).toString();
            }
            config_hash[group_name][key_name] = setting_value;
        }
        settings.endGroup();
    }

    // Next, load the information from the vmm config for this system
    // Display name
    auto loadedDisplayName = config_settings->getStringValue("display_name");
    if (!loadedDisplayName.isEmpty()) {
        displayName = loadedDisplayName;
    } else {
        displayName = config_name;
    }
    // Notes
    auto loadedNotes = config_settings->getStringValue("notes");
    if (!loadedNotes.isEmpty()) {
        notes = loadedNotes;
    }
    // Timestamp
    auto loadedTimestamp = config_settings->getStringValue("timestamp");
    if (!loadedTimestamp.isEmpty()) {
        // Make sure it is valid
        if (auto newTimestamp = QDateTime::fromString(loadedTimestamp, Qt::ISODate); newTimestamp.isValid()) {
            lastUsedTimestamp = newTimestamp;
        }
    }
    // Icon
    auto loadedIcon = config_settings->getStringValue("icon");
    if (!loadedIcon.isEmpty()) {
        icon = loadedIcon;
    }
}
void
VMManagerSystem::saveSettings()
{
    if(!isValid()) {
        return;
    }
    config_settings->setStringValue("system_name", config_name);
    config_settings->setStringValue("config_file", config_file.canonicalFilePath());
    config_settings->setStringValue("config_dir", config_file.canonicalPath());
    if (displayName != config_name) {
        config_settings->setStringValue("display_name", displayName);
    } else {
        config_settings->remove("display_name");
    }

    config_settings->setStringValue("notes", notes);
    if(lastUsedTimestamp.isValid()) {
        config_settings->setStringValue("timestamp", lastUsedTimestamp.toString(Qt::ISODate));
    }
    config_settings->setStringValue("icon", icon);
    generateSearchTerms();
}
void
VMManagerSystem::generateSearchTerms()
{
    searchTerms.clear();
    for (const auto &config_key : config_hash.keys()) {
        // searchTerms.append(config_hash[config_key].values());
        // brute force temporarily don't add paths
        for(const auto &value: config_hash[config_key].values()) {
            if(!value.startsWith("/")) {
                searchTerms.append(value);
            }
        }
    }
    searchTerms.append(display_table.values());
    searchTerms.append(displayName);
    searchTerms.append(config_name);
    QRegularExpression whitespaceRegex("\\s+");
    searchTerms.append(notes.split(whitespaceRegex));
}
void
VMManagerSystem::updateTimestamp()
{
    lastUsedTimestamp = QDateTime::currentDateTimeUtc();
    saveSettings();
}

QString
VMManagerSystem::getAll(const QString& category) const {
    auto value = config_hash[category].keys().join(", ");
    return value;
}

QHash<QString, QHash<QString, QString>>
VMManagerSystem::getConfigHash() const
{
    return config_hash;
}

void
VMManagerSystem::setDisplayName(const QString &newDisplayName)
{
    // If blank, reset to the default
    if (newDisplayName.isEmpty()) {
        displayName = config_name;
    } else {
        displayName = newDisplayName;
    }
    saveSettings();
}
void
VMManagerSystem::setNotes(const QString &newNotes)
{
    notes = newNotes;
    saveSettings();
}
bool
VMManagerSystem::isValid() const
{
    return config_file.exists() && config_file.isFile() && config_file.size() != 0;
}

bool
VMManagerSystem::isProcessRunning() const
{
    return process->processId() != 0;
}

qint64
VMManagerSystem::processId() const
{
    return process->processId();
}

QHash<QString, QString>
VMManagerSystem::getCategory(const QString &category) const {
    return config_hash[category];
}

void
VMManagerSystem::find86BoxBinary() {
    // We'll use our own self to launch the VMs
    main_binary = QFileInfo(QCoreApplication::applicationFilePath());
}

bool
VMManagerSystem::has86BoxBinary() {
    return main_binary.exists();
}

void
VMManagerSystem::launchMainProcess() {

    if(!has86BoxBinary()) {
        qWarning("No binary found! returning");
        return;
    }

    // start the server first to get the socket name
    if (!serverIsRunning) {
        if(!startServer()) {
            // FIXME: Better error handling
            qInfo("Failed to start VM Manager server");
            return;
        }
    }
    // If the system is already running, bring it to front
    if (process->processId() != 0) {
#ifdef Q_OS_WINDOWS
        if (this->id) {
            SetForegroundWindow((HWND)this->id);
        }
#endif
        return;
    }
    setProcessEnvVars();
    QString program = main_binary.filePath();
    QStringList args;
    args << "--vmpath" << config_dir;
    args << "--vmname" << displayName;
    if (rom_path[0] != '\0')
        args << "--rompath" << QString(rom_path);
    if (global_cfg_overridden)
        args << "--global" << QString(global_cfg_path);
    if (!hook_enabled)
        args << "--nohook";
    if (start_in_fullscreen)
        args << "--fullscreen";
    if (!confirm_exit_cmdl)
        args << "--noconfirm";
    process->setProgram(program);
    process->setArguments(args);
    qDebug() << Q_FUNC_INFO << " Full Command:" << process->program() << " " << process->arguments();
    process->start();
    updateTimestamp();

    connect(process, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished),
    [=](const int exitCode, const QProcess::ExitStatus exitStatus){
        if (exitCode != 0 || exitStatus != QProcess::NormalExit) {
            qInfo().nospace().noquote() << "Abnormal program termination while launching main process: exit code " <<  exitCode << ", exit status " << exitStatus;
            QMessageBox::critical(this, tr("Virtual machine crash"),
                                        tr("The virtual machine \"%1\"'s process has unexpectedly terminated with exit code %2.").arg(displayName, QString::number(exitCode)));
            return;
        }
    });
}

void
VMManagerSystem::startButtonPressed() {
    launchMainProcess();
}

void
VMManagerSystem::launchSettings() {
    if(!has86BoxBinary()) {
        qWarning("No binary found! returning");
        return;
    }

    // start the server first to get the socket name
    if (!serverIsRunning) {
        if(!startServer()) {
            // FIXME: Better error handling
            qInfo("Failed to start VM Manager server");
            return;
        }
    }

    // If the system is already running, instruct it to show settings
    if (process->processId() != 0) {
#ifdef Q_OS_WINDOWS
        if (this->id) {
            SetForegroundWindow((HWND)this->id);
        }
#endif
        socket_server->serverSendMessage(VMManagerProtocol::ManagerMessage::ShowSettings);
        return;
    }

    // Otherwise, launch the system with the settings parameter
    setProcessEnvVars();
    window_obscured = true;
    QString program = main_binary.filePath();
    QStringList open_command_args;
    QStringList args;
    args << "--vmpath" << config_dir << "--settings";
    if (rom_path[0] != '\0')
        args << "--rompath" << QString(rom_path);
    if (global_cfg_overridden)
        args << "--global" << QString(global_cfg_path);
    process->setProgram(program);
    process->setArguments(args);
    qDebug() << Q_FUNC_INFO << " Full Command:" << process->program() << " " << process->arguments();
    process->start();

    connect(process, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished),
    [=](const int exitCode, const QProcess::ExitStatus exitStatus){
        if (exitCode != 0 || exitStatus != QProcess::NormalExit) {
            qInfo().nospace().noquote() << "Abnormal program termination while launching settings: exit code " <<  exitCode << ", exit status " << exitStatus;
            QMessageBox::critical(this, tr("Virtual machine crash"),
                                        tr("The virtual machine \"%1\"'s process has unexpectedly terminated with exit code %2.").arg(displayName, QString("%1 (0x%2)").arg(QString::number(exitCode), QString::number(exitCode, 16))));
            return;
        }

        configurationChangeReceived();
    });
}

void
VMManagerSystem::setupPaths() {
    // application_temp_directory.setPath(QStandardPaths::writableLocation(QStandardPaths::TempLocation));
    // standard_temp_directory.setPath(QStandardPaths::writableLocation(QStandardPaths::TempLocation));
    // QString temp_subdir = QApplication::applicationName();
    // if (!application_temp_directory.exists(temp_subdir)) {
    //     // FIXME: error checking
    //     application_temp_directory.mkdir(temp_subdir);
    // }
    // // QT always replaces `/` with native separators, so it is safe to use here for all platforms
    // application_temp_directory.setPath(application_temp_directory.path() + "/" + temp_subdir);
    // app_data_directory.setPath(QStandardPaths::writableLocation(QStandardPaths::AppDataLocation));
    // // TODO: come back here and update with the new plat_get_global_*
    // if (!app_data_directory.exists()) {
    //     // FIXME: Error checking
    //     app_data_directory.mkpath(app_data_directory.path());
    // }
    screenshot_directory.setPath(config_dir + "/" + "screenshots");
}

void
VMManagerSystem::setupVars() {
    unique_name = QCryptographicHash::hash(config_file.path().toUtf8().constData(), QCryptographicHash::Algorithm::Sha256).toHex().right(9);
    // unique_name = "aaaaaa";
    // Set up the display vars
    // This will likely get moved out to its own class
    // This will likely get moved out to its own class
    auto machine_config      = getCategory("Machine");
    auto video_config        = getCategory("Video");
    auto disk_config         = getCategory("Hard disks");
    auto audio_config        = getCategory("Sound");
    auto network_config      = getCategory("Network");
    auto input_config        = getCategory("Input devices");
    auto floppy_cdrom_config = getCategory("Floppy and CD-ROM drives");
    auto rdisk_mo_config     = getCategory("Other removable devices");
    auto storage_config      = getCategory("Storage controllers");
    auto ports_config        = getCategory("Ports (COM & LPT)");
    auto other_config        = getCategory("Other peripherals");
    // auto general_config = getCategory("General");
    // auto config_uuid = QString("Not set");
    // if(!general_config["uuid"].isEmpty()) {
    //     config_uuid = general_config["uuid"];
    //     qDebug() << "btw config dir:" << config_dir;
    // }
    // qDebug() << "Generated UUID:" << uuid;
    // qDebug() << "Config file UUID:" << config_uuid;
    auto machine_name = QString();
    int i = 0;
    int ram_granularity = 0;
    // Machine
    for (int ci = 0; ci < machine_count(); ++ci) {
        if (machine_available(ci)) {
                if (machines[ci].internal_name == machine_config["machine"]) {
                    machine_name = machines[ci].name;
                    ram_granularity = machines[ci].ram.step;
                }
        }
    }
    display_table[VMManager::Display::Name::Machine] = machine_name;

    // CPU: Combine name with speed and FPU
    QString cpu_name = "Unknown";
    while (cpu_families[i].package != 0) {
        if (cpu_families[i].internal_name == machine_config["cpu_family"]) {
            int j = 0;
            cpu_name = QString("%1 %2").arg(cpu_families[i].manufacturer, cpu_families[i].name);
            while (cpu_families[i].cpus[j].cpu_type != 0) {
                if (cpu_families[i].cpus[j].rspeed == machine_config["cpu_speed"].toUInt()) {
                    auto cpu_speed = QString(cpu_families[i].cpus[j].name).split("/").at(0).split(" (").at(0);
                    cpu_name.append(cpu_speed.prepend(" / "));
                    cpu_name.append(QCoreApplication::translate("", "MHz").prepend(' '));
                    if (machine_config.contains("fpu_type") && (machine_config["fpu_type"] != QString("none")) && (machine_config["fpu_type"] != QString("internal"))) {
                        int k = 0;
                        while (cpu_families[i].cpus[j].fpus[k].internal_name != nullptr) {
                            if (QString(cpu_families[i].cpus[j].fpus[k].internal_name) == machine_config["fpu_type"]) {
                                cpu_name.append(QString(cpu_families[i].cpus[j].fpus[k].name).prepend(", "));
                                cpu_name.append(QCoreApplication::translate("", "FPU").prepend(' '));
                                break;
                            }
                            k++;
                        }
                    }
                    break;
                }
                j++;
            }
            break;
        }
        i++;
    }
//    int speed_display = machine_config["cpu_speed"].toInt() / 1000000;
//    cpu_name.append(QString::number(speed_display).prepend(" / "));
//    cpu_name.append(QCoreApplication::translate("", "MHz").prepend(' '));
    display_table[VMManager::Display::Name::CPU] = cpu_name;

    // Memory
    int divisor = (ram_granularity < 1024) ? 1 : 1024;
    QString display_unit = (divisor == 1) ? "KB" : "MB";
    auto mem_display = QString::number(machine_config["mem_size"].toInt() / divisor);
    mem_display.append(QCoreApplication::translate("", display_unit.toUtf8().constData()).prepend(' '));
    display_table[VMManager::Display::Name::Memory] = mem_display;

    // Video card
    int video_int = video_get_video_from_internal_name(video_config["gfxcard"].toUtf8().data());
    const device_t* video_dev = video_card_getdevice(video_int);
    display_table[VMManager::Display::Name::Video] = DeviceConfig::DeviceName(video_dev, video_get_internal_name(video_int), 1);

    // Secondary video
    if (video_config.contains("gfxcard_2")) {
        int video2_int = video_get_video_from_internal_name(video_config["gfxcard_2"].toUtf8().data());
        const device_t* video2_dev = video_card_getdevice(video2_int);
        display_table[VMManager::Display::Name::Video].append(DeviceConfig::DeviceName(video2_dev, video_get_internal_name(video2_int), 1).prepend(VMManagerDetailSection::sectionSeparator));
    }

    // Add-on video that's not Voodoo
    if (video_config.contains("8514a") && (video_config["8514a"].toInt() != 0))
        display_table[VMManager::Display::Name::Video].append(tr("IBM 8514/A Graphics").prepend(VMManagerDetailSection::sectionSeparator));
    if (video_config.contains("xga") && (video_config["xga"].toInt() != 0))
        display_table[VMManager::Display::Name::Video].append(tr("XGA Graphics").prepend(VMManagerDetailSection::sectionSeparator));
    if (video_config.contains("da2") && (video_config["da2"].toInt() != 0))
        display_table[VMManager::Display::Name::Video].append(tr("IBM PS/55 Display Adapter Graphics").prepend(VMManagerDetailSection::sectionSeparator));

    // Voodoo
    QString voodoo_name = "";
    if (video_config.contains("voodoo") && (video_config["voodoo"].toInt() != 0)) {
        char temp[512];
        device_get_name(&voodoo_device, 0, temp);
        auto voodoo_config = getCategory(QString(temp));
        int voodoo_type = voodoo_config["type"].toInt();
        switch (voodoo_type) {
            case 0:
            default:
                voodoo_name = tr("3Dfx Voodoo Graphics");
                break;
            case 1:
                voodoo_name = tr("Obsidian SB50 + Amethyst (2 TMUs)");
                break;
            case 2:
                voodoo_name = tr("3Dfx Voodoo 2");
                break;
        }

        if (voodoo_config["sli"].toInt() == 1)
            voodoo_name.append(" (SLI)");
    }
    display_table[VMManager::Display::Name::Voodoo] = voodoo_name;

    // Drives
    // First the number of disks
    QMap<QString, int> disks;
    for(const auto& key: disk_config.keys()) {
        // Assuming the format hdd_NN_*
        QStringList pieces = key.split('_');
        QString disk = QString("%1_%2").arg(pieces.at(0), pieces.at(1));
        if(!disk.isEmpty()) {
            disks[disk] = 1;
        }
    }
    // Next, the types
    QHash<QString, int> bus_types;
    for (const auto& key: disks.keys()) {
        auto        disk_parameter_key = QString("%1_parameters").arg(key);
        QStringList pieces = disk_config[disk_parameter_key].split(",");
        QString bus_type = pieces.value(pieces.length() - 1).trimmed();
        bus_types[bus_type] = 1;
    }
    QString disks_display = tr("%n disk(s)", "", disks.count());
    if (disks.count()) {
        disks_display.append(" / ").append(bus_types.keys().join(", ").toUpper());
    }
//    display_table[VMManager::Display::Name::Disks] = disks_display;

    // Drives
    QString new_disk_display;
    for (const auto& key: disks.keys()) {
        auto        disk_parameter_key = QString("%1_parameters").arg(key);
        // Converting a string to an int back to a string to remove the zero (e.g. 01 to 1)
        auto disk_number = QString::number(key.split("_").last().toInt());
        QStringList pieces = disk_config[disk_parameter_key].split(",");
        QString sectors = pieces.value(0).trimmed();
        QString heads = pieces.value(1).trimmed();
        QString cylinders = pieces.value(2).trimmed();
        QString bus_type = pieces.value(pieces.length() - 1).trimmed();
        // Add separator for each subsequent value, skipping the first
        if(!new_disk_display.isEmpty()) {
            new_disk_display.append(QString("%1").arg(VMManagerDetailSection::sectionSeparator));
        }
        int diskSizeRaw = (cylinders.toInt() * heads.toInt() * sectors.toInt()) >> 11;
        QString diskSizeFinal;
        QString unit = tr("MiB");
        if(diskSizeRaw > 1000) {
            unit = tr("GiB");
            diskSizeFinal = QString::number(diskSizeRaw * 1.0 / 1000, 'f', 1);
        } else {
            diskSizeFinal = QString::number(diskSizeRaw);
        }
        // Only prefix each disk when there are multiple disks
        QString diskNumberDisplay = disks.count() > 1 ? tr("Disk %1: ").arg(disk_number) : "";
        new_disk_display.append(QString("%1%2 %3 (%4)").arg(diskNumberDisplay, diskSizeFinal, unit, bus_type.toUpper()));
    }
    if(new_disk_display.isEmpty()) {
        new_disk_display = tr("No disks");
    }
    display_table[VMManager::Display::Name::Disks] = new_disk_display;

    // Floppy & CD-ROM
    QStringList floppyDevices;
    QStringList cdromDevices;
    // Special case: first two 5.25" 360k FDDs which don't get saved to the .cfg
    for (int i = 0; i < 2; i++) {
        if (!floppy_cdrom_config.contains(QString("fdd_0%1_type").arg(i + 1)))
            floppyDevices.append(QString(fdd_getname(fdd_get_from_internal_name((char *) "525_2dd"))));
    }

    static auto floppy_match = QRegularExpression("fdd_\\d\\d_type", QRegularExpression::CaseInsensitiveOption);
    static auto cdrom_match  = QRegularExpression("cdrom_\\d\\d_parameters", QRegularExpression::CaseInsensitiveOption);
    for(const auto& key: floppy_cdrom_config.keys()) {
        if(key.contains(floppy_match)) {
            // auto device_number = key.split("_").at(1);
            auto floppy_internal_name = QString(floppy_cdrom_config[key]);
            // Not interested in the nones
            if(floppy_internal_name == "none") {
                continue;
            }
            auto floppy_type = fdd_get_from_internal_name(floppy_internal_name.toUtf8().data());
            if(auto fddName = QString(fdd_getname(floppy_type)); !fddName.isEmpty()) {
                floppyDevices.append(fddName);
            }
        }
        if(key.contains(cdrom_match)) {
            auto device_number = key.split("_").at(1);
            auto cdrom_parameters = QString(floppy_cdrom_config[key]);
            auto cdrom_bus = cdrom_parameters.split(",").at(1).trimmed().toUpper();

            auto cdrom_type_key = QString("cdrom_%1_type").arg(device_number);
            auto cdrom_internal_name = QString(floppy_cdrom_config[cdrom_type_key]);
            if (cdrom_internal_name.isEmpty())
                cdrom_internal_name = "86cd";
            auto cdrom_type = cdrom_get_from_internal_name(cdrom_internal_name.toUtf8().data());

            auto cdrom_speed_key = QString("cdrom_%1_speed").arg(device_number);
            auto cdrom_speed = QString(floppy_cdrom_config[cdrom_speed_key]);
            if (cdrom_speed.isEmpty())
                cdrom_speed = "8";

            if ((cdrom_bus != "NONE") && (cdrom_type != -1)) {
                cdrom_speed = QString("%1x ").arg(cdrom_speed);
                cdrom_bus = QString(" (%1)").arg(cdrom_bus);
                cdromDevices.append(QString("%1%2 %3 %4%5").arg(cdrom_speed, cdrom_drive_types[cdrom_type].vendor, cdrom_drive_types[cdrom_type].model, cdrom_drive_types[cdrom_type].revision, cdrom_bus));
            }
        }
    }

    display_table[VMManager::Display::Name::Floppy] = floppyDevices.join(VMManagerDetailSection::sectionSeparator);
    display_table[VMManager::Display::Name::CD]     = cdromDevices.join(VMManagerDetailSection::sectionSeparator);

    // Removable disks & MO
    QStringList rdiskDevices;
    QStringList moDevices;
    static auto rdisk_match = QRegularExpression("rdisk_\\d\\d_parameters", QRegularExpression::CaseInsensitiveOption);
    static auto zip_match   = QRegularExpression("zip_\\d\\d_parameters", QRegularExpression::CaseInsensitiveOption); // Legacy ZIP drive entries
    static auto mo_match    = QRegularExpression("mo_\\d\\d_parameters", QRegularExpression::CaseInsensitiveOption);
    for(const auto& key: rdisk_mo_config.keys()) {
        if(key.contains(rdisk_match) || key.contains(zip_match)) {
            auto device_number = key.split("_").at(1);
            auto rdisk_parameters = QString(rdisk_mo_config[key]);
            auto rdisk_type = rdisk_parameters.split(",").at(0).toInt();
            if (key.contains(zip_match))
                rdisk_type++;
            auto rdisk_bus =  rdisk_parameters.split(",").at(1).trimmed().toUpper();

            if((rdisk_type >= 0) && (rdisk_type < KNOWN_RDISK_DRIVE_TYPES)) {
                if(!rdisk_bus.isEmpty())
                    rdisk_bus = QString(" (%1)").arg(rdisk_bus);
                rdiskDevices.append(QString("%1 %2%3").arg(rdisk_drive_types[rdisk_type].vendor, rdisk_drive_types[rdisk_type].model, rdisk_bus));
            }
        }
        if(key.contains(mo_match)) {
            auto device_number = key.split("_").at(1);
            auto mo_parameters = QString(rdisk_mo_config[key]);
            auto mo_type = mo_parameters.split(",").at(0).toInt();
            auto mo_bus  = mo_parameters.split(",").at(1).trimmed().toUpper();

            if((mo_type >= 0) && (mo_type < KNOWN_MO_DRIVE_TYPES)) {
                if(!mo_bus.isEmpty())
                    mo_bus = QString(" (%1)").arg(mo_bus);
                moDevices.append(QString("%1 %2%3").arg(mo_drive_types[mo_type].vendor, mo_drive_types[mo_type].model, mo_bus));
            }
        }
    }

    display_table[VMManager::Display::Name::RDisk] = rdiskDevices.join(VMManagerDetailSection::sectionSeparator);
    display_table[VMManager::Display::Name::MO]    = moDevices.join(VMManagerDetailSection::sectionSeparator);


    // SCSI controllers
    QStringList scsiControllers;
    static auto scsi_match = QRegularExpression("scsicard_\\d", QRegularExpression::CaseInsensitiveOption);
    for(const auto& key: storage_config.keys()) {
        if(key.contains(scsi_match)) {
            auto device_number = key.split("_").at(1);
            auto scsi_internal_name = QString(storage_config[key]);
            auto scsi_id = scsi_card_get_from_internal_name(scsi_internal_name.toUtf8().data());
            auto scsi_device = scsi_card_getdevice(scsi_id);
            auto scsi_name = DeviceConfig::DeviceName(scsi_device, scsi_card_get_internal_name(scsi_id), 1);
            if(!scsi_name.isEmpty()) {
                scsiControllers.append(scsi_name);
            }
        }
    }
    display_table[VMManager::Display::Name::SCSIController] = scsiControllers.join(VMManagerDetailSection::sectionSeparator);

    // Hard and floppy disk controllers
    QStringList storageControllers;
    static auto fdc_match = QRegularExpression("fdc(_\\d)?", QRegularExpression::CaseInsensitiveOption); // futureproofing
    static auto hdc_match = QRegularExpression("hdc(_\\d)?", QRegularExpression::CaseInsensitiveOption);
    for(const auto& key: storage_config.keys()) {
        if(key.contains(fdc_match)) {
            QString device_number;
            if (!key.contains('_'))
                device_number = "1";
            else // futureproofing
             device_number = key.split("_").at(1);
            auto fdc_internal_name = QString(storage_config[key]);
            if (!fdc_internal_name.isEmpty() && (fdc_internal_name != "none") && (fdc_internal_name != "internal")) {
                auto fdc_id = fdc_card_get_from_internal_name(fdc_internal_name.toUtf8().data());
                auto fdc_device = fdc_card_getdevice(fdc_id);
                auto fdc_name = DeviceConfig::DeviceName(fdc_device, fdc_card_get_internal_name(fdc_id), 1);
                if(!fdc_name.isEmpty()) {
                    storageControllers.append(fdc_name);
                }
            }
        }
        if(key.contains(hdc_match)) {
            QString device_number;
            if (!key.contains('_')) // legacy hdc entry
                device_number = "1";
            else
             device_number = key.split("_").at(1);
            auto hdc_internal_name = QString(storage_config[key]);
            if (!hdc_internal_name.isEmpty() && (hdc_internal_name != "none") && (hdc_internal_name != "internal")) {
                auto hdc_id = hdc_get_from_internal_name(hdc_internal_name.toUtf8().data());
                auto hdc_device = hdc_get_device(hdc_id);
                auto hdc_name = DeviceConfig::DeviceName(hdc_device, hdc_get_internal_name(hdc_id), 1);
                if(!hdc_name.isEmpty()) {
                    storageControllers.append(hdc_name);
                }
            }
        }
    }

    // CD-ROM controller
    if (storage_config.contains("cdrom_interface")) {
        auto cdrom_intf_internal_name = storage_config["cdrom_interface"];
        if (!cdrom_intf_internal_name.isEmpty() && (cdrom_intf_internal_name != "none") && (cdrom_intf_internal_name != "internal")) {
            auto cdrom_intf_dev = cdrom_interface_get_from_internal_name(cdrom_intf_internal_name.toUtf8().data());
            auto cdrom_intf_dev_name = DeviceConfig::DeviceName(cdrom_interface_get_device(cdrom_intf_dev), cdrom_interface_get_internal_name(cdrom_intf_dev), 1);
            storageControllers.append(cdrom_intf_dev_name);
        }
    }

    // Legacy tertiary/quaternary IDE
    QString ide_ter_internal_name = "ide_ter";
    QString ide_qua_internal_name = "ide_qua";
    if (storage_config.contains(ide_ter_internal_name) && (storage_config[ide_ter_internal_name].toInt() != 0))
        storageControllers.append(DeviceConfig::DeviceName(hdc_get_device(hdc_get_from_internal_name(ide_ter_internal_name.toUtf8().data())), ide_ter_internal_name.toUtf8().constData(), 1));
    if (storage_config.contains(ide_qua_internal_name) && (storage_config[ide_qua_internal_name].toInt() != 0))
        storageControllers.append(DeviceConfig::DeviceName(hdc_get_device(hdc_get_from_internal_name(ide_qua_internal_name.toUtf8().data())), ide_qua_internal_name.toUtf8().constData(), 1));

    display_table[VMManager::Display::Name::StorageController] = storageControllers.join(VMManagerDetailSection::sectionSeparator);

    // Audio
    QStringList sndCards;
    static auto sndcard_match = QRegularExpression("sndcard\\d?", QRegularExpression::CaseInsensitiveOption);
    for(const auto& key: audio_config.keys()) {
        if(key.contains(sndcard_match)) {
            auto device_number = key.right(1);
            if(device_number == "d") // card #1 has no number
                device_number = "1";
            auto audio_internal_name = QString(audio_config[key]);
            auto audio_id = sound_card_get_from_internal_name(audio_internal_name.toUtf8().data());
            auto audio_device = sound_card_getdevice(audio_id);
            auto audio_name = DeviceConfig::DeviceName(audio_device, sound_card_get_internal_name(audio_id), 1);
            if(!audio_name.isEmpty()) {
                sndCards.append(audio_name);
            }
        }
    }
    if(audio_config.contains("mpu401_standalone")) {
        sndCards.append(tr("Standalone MPU-401"));
    }
    if(sndCards.isEmpty()) {
        sndCards.append(tr("None"));
    }
    display_table[VMManager::Display::Name::Audio] = sndCards.join(VMManagerDetailSection::sectionSeparator);

    // MIDI
    QString midiOutDev;
    if (audio_config.contains("midi_device")) {
        auto midi_out_device = QString(audio_config["midi_device"]);
        auto midi_device_int = midi_out_device_get_from_internal_name(midi_out_device.toUtf8().data());
        auto midi_out = midi_out_device_getdevice(midi_device_int);
        if(auto midiDevName = QString(midi_out->name);  !midiDevName.isEmpty()) {
            midiOutDev = midiDevName;
        }
    }
    display_table[VMManager::Display::Name::MidiOut] = midiOutDev;

    // midi_device = mt32 (output)
    // mpu401_standalone = 1
    // midi_in_device (input)

    // Network
    QStringList nicList;
    static auto nic_match = QRegularExpression("net_\\d\\d_card", QRegularExpression::CaseInsensitiveOption);
    for(const auto& key: network_config.keys()) {
        if(key.contains(nic_match)) {
            auto device_number = key.split("_").at(1);
            auto nic_internal_name = QString(network_config[key]);
            auto nic_id = network_card_get_from_internal_name(nic_internal_name.toUtf8().data());
            auto nic = network_card_getdevice(nic_id);
            auto nic_name = DeviceConfig::DeviceName(nic, network_card_get_internal_name(nic_id), 1);
            auto net_type_key = QString("net_%1_net_type").arg(device_number);
            auto net_type = network_config[net_type_key];
            if (!net_type.isEmpty()) {
                if (net_type == "slirp")
                    net_type = "SLiRP";
                else if (net_type == "pcap")
                    net_type = "PCap";
                else if (net_type == "nmswitch")
                    net_type = tr("Local Switch");
                else if (net_type == "nrswitch")
                    net_type = tr("Remote Switch");
                else
                    net_type = net_type.toUpper();
                nicList.append(nic_name + " (" + net_type + ")");
            } else {
                nicList.append(nic_name);
            }

        }
    }
    if(nicList.isEmpty()) {
        nicList.append(tr("None"));
    }
    display_table[VMManager::Display::Name::NIC] = nicList.join(VMManagerDetailSection::sectionSeparator);

    // Input (Keyboard)
    if (input_config.contains("keyboard_type")) {
        auto keyboard_internal_name = input_config["keyboard_type"];
        auto keyboard_dev = keyboard_get_from_internal_name(keyboard_internal_name.toUtf8().data());
        auto keyboard_dev_name = DeviceConfig::DeviceName(keyboard_get_device(keyboard_dev), keyboard_get_internal_name(keyboard_dev), 0);
        display_table[VMManager::Display::Name::Keyboard] = keyboard_dev_name;
    }

    // Input (Mouse)
    auto mouse_internal_name = input_config["mouse_type"];
    auto mouse_dev = mouse_get_from_internal_name(mouse_internal_name.toUtf8().data());
    auto mouse_dev_name = DeviceConfig::DeviceName(mouse_get_device(mouse_dev), mouse_get_internal_name(mouse_dev), 0);
    display_table[VMManager::Display::Name::Mouse] = mouse_dev_name;

    // Input (joystick)
    QString joystickDevice;
    if(input_config.contains("joystick_type")) {
        auto joystick_internal = QString(input_config["joystick_type"]);
        auto joystick_dev = joystick_get_from_internal_name(joystick_internal.toUtf8().data());
        if (auto joystickName = tr(joystick_get_name(joystick_dev)); !joystickName.isEmpty()) {
            joystickDevice = joystickName;
        }
    }
    display_table[VMManager::Display::Name::Joystick] = joystickDevice;

    // # Ports
    // Serial
    // By default serial 1 and 2 are enabled unless otherwise specified
    static auto serial_match = QRegularExpression("serial\\d_enabled", QRegularExpression::CaseInsensitiveOption);
    QList<bool> serial_enabled = {true, true, false, false, false, false, false, false};
    // Parallel
    // By default lpt 1 is enabled unless otherwise specified
    static auto lpt_match = QRegularExpression("lpt\\d_enabled", QRegularExpression::CaseInsensitiveOption);
    QList<bool> lpt_enabled = {true, false, false, false};
    for (const auto &key: ports_config.keys()) {
        if (key.contains(serial_match)) {
            if (auto serial_dev = key.split("_").at(0); !serial_dev.isEmpty()) {
                auto serial_num = serial_dev.at(serial_dev.size() - 1);
                // qDebug() << "serial is set" << key << ":" << ports_config[key];
                if(serial_num.isDigit() && serial_num.digitValue() >= 1 && serial_num.digitValue() <= 4) {
                    // Already verified that it is a digit with isDigit()
                    serial_enabled[serial_num.digitValue() - 1] = ports_config[key].toInt() == 1;
                }
            }
        }
        if (key.contains(lpt_match)) {
            if (auto lpt_dev = key.split("_").at(0); !lpt_dev.isEmpty()) {
                auto lpt_num = lpt_dev.at(lpt_dev.size() - 1);
                // qDebug() << "lpt is set" << key << ":" << ports_config[key];
                if (lpt_num.isDigit() && lpt_num.digitValue() >= 1 && lpt_num.digitValue() <= 4) {
                    lpt_enabled[lpt_num.digitValue() - 1] = ports_config[key].toInt() == 1;
                }
            }
        }
    }
    // qDebug() << "ports final" << serial_enabled << lpt_enabled;
    QStringList serialFinal;
    QStringList lptFinal;
    int portIndex = 0;
    while (true) {
        if (serial_enabled[portIndex])
            serialFinal.append(QString("COM%1").arg(portIndex + 1));
        ++portIndex;
        if (portIndex == SERIAL_MAX)
            break;
    }
    portIndex = 0;
    bool hasLptDevices = false;
    while (true) {
        if (lpt_enabled[portIndex]) {
            auto lpt_device_key = QString("lpt%1_device").arg(portIndex + 1);
            QString lpt_device_name = "";
            if (ports_config.contains(lpt_device_key)) {
                auto lpt_internal_name = QString(ports_config[lpt_device_key]);
                auto lpt_id = lpt_device_get_from_internal_name(lpt_internal_name.toUtf8().data());
                lpt_device_name = " (" + tr(lpt_device_get_name(lpt_id)) + ")";
                hasLptDevices = true;
            }
            lptFinal.append(QString("LPT%1%2").arg(portIndex + 1).arg(lpt_device_name));
        }
        ++portIndex;
        if (portIndex == PARALLEL_MAX)
            break;
    }
    display_table[VMManager::Display::Name::Serial]   = (serialFinal.empty() ?  tr("None") : serialFinal.join(", "));
    display_table[VMManager::Display::Name::Parallel] = (lptFinal.empty()    ?  tr("None") : lptFinal.join((hasLptDevices ? VMManagerDetailSection::sectionSeparator : ", ")));

    // ISA RTC
    QString isartc_dev_name = "";
    if (other_config.contains("isartc_type")) {
        auto isartc_internal_name = other_config["isartc_type"];
        auto isartc_dev = isartc_get_from_internal_name(isartc_internal_name.toUtf8().data());
        isartc_dev_name = DeviceConfig::DeviceName(isartc_get_device(isartc_dev), isartc_get_internal_name(isartc_dev), 0);
    }
    display_table[VMManager::Display::Name::IsaRtc] = isartc_dev_name;

    // ISA RAM
    QStringList IsaMemCards;
    static auto isamem_match = QRegularExpression("isamem\\d_type", QRegularExpression::CaseInsensitiveOption);
    for(const auto& key: other_config.keys()) {
        if(key.contains(isamem_match)) {
            auto device_number = QString("%1").arg(key.split("_").at(0).right(1).toInt() + 1);
            auto isamem_internal_name = QString(other_config[key]);
            auto isamem_id = isamem_get_from_internal_name(isamem_internal_name.toUtf8().data());
            auto isamem_device = isamem_get_device(isamem_id);
            auto isamem_name = DeviceConfig::DeviceName(isamem_device, isamem_get_internal_name(isamem_id), 0);
            if(!isamem_name.isEmpty()) {
                IsaMemCards.append(isamem_name);
            }
        }
    }
    display_table[VMManager::Display::Name::IsaMem] = IsaMemCards.join(VMManagerDetailSection::sectionSeparator);

    // ISA ROM
    QStringList IsaRomCards;
    static auto isarom_match = QRegularExpression("isarom\\d_type", QRegularExpression::CaseInsensitiveOption);
    for(const auto& key: other_config.keys()) {
        if(key.contains(isarom_match)) {
            auto device_number = QString("%1").arg(key.split("_").at(0).right(1).toInt() + 1);
            auto isarom_internal_name = QString(other_config[key]);
            auto isarom_id = isarom_get_from_internal_name(isarom_internal_name.toUtf8().data());
            auto isarom_device = isarom_get_device(isarom_id);
            auto isarom_name = DeviceConfig::DeviceName(isarom_device, isarom_get_internal_name(isarom_id), 0);
            if(!isarom_name.isEmpty()) {
                IsaRomCards.append(isarom_name);
            }
        }
    }
    display_table[VMManager::Display::Name::IsaRom] = IsaRomCards.join(VMManagerDetailSection::sectionSeparator);
}

bool
VMManagerSystem::startServer() {
    if (socket_server->startServer()) {
        serverIsRunning = true;
        connect(socket_server, &VMManagerServerSocket::dataReceived, this, &VMManagerSystem::dataReceived);
        connect(socket_server, &VMManagerServerSocket::windowStatusChanged, this, &VMManagerSystem::windowStatusChangeReceived);
        connect(socket_server, &VMManagerServerSocket::runningStatusChanged, this, &VMManagerSystem::runningStatusChangeReceived);
        connect(socket_server, &VMManagerServerSocket::configurationChanged, this, &VMManagerSystem::configurationChangeReceived);
        connect(socket_server, &VMManagerServerSocket::globalConfigurationChanged, this, &VMManagerSystem::globalConfigurationChanged);
        connect(socket_server, &VMManagerServerSocket::winIdReceived, this, [this] (WId id) { this->id = id; });
        return true;
    } else {
        return false;
    }
}

void
VMManagerSystem::setProcessEnvVars() {
    QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
    QString env_var_name = (socket_server_type == VMManagerServerSocket::ServerType::Standard) ? "VMM_86BOX_SOCKET" : "86BOX_MANAGER_SOCKET";
    env.insert(env_var_name, socket_server->getSocketPath());
    process->setProcessEnvironment(env);
}

void
VMManagerSystem::restartButtonPressed() {
    socket_server->serverSendMessage(VMManagerProtocol::ManagerMessage::ResetVM);

}

void
VMManagerSystem::pauseButtonPressed() {
    socket_server->serverSendMessage(VMManagerProtocol::ManagerMessage::Pause);
}
void
VMManagerSystem::dataReceived()
{
    qInfo() << Q_FUNC_INFO << "Note: Respond to data received events here.";
}
void
VMManagerSystem::windowStatusChangeReceived(int status)
{
    window_obscured = status;
    emit windowStatusChanged();
    processStatusChanged();
}
QString
VMManagerSystem::getDisplayValue(VMManager::Display::Name key)
{
    return (display_table.contains(key)) ? display_table[key] : "";
}

void
VMManagerSystem::sendGlobalConfigurationChanged()
{
    socket_server->serverSendMessage(VMManagerProtocol::ManagerMessage::GlobalConfigurationChanged);
}

void
VMManagerSystem::shutdownRequestButtonPressed()
{
    socket_server->serverSendMessage(VMManagerProtocol::ManagerMessage::RequestShutdown);
}

void
VMManagerSystem::shutdownForceButtonPressed()
{
    socket_server->serverSendMessage(VMManagerProtocol::ManagerMessage::ForceShutdown);
}

void
VMManagerSystem::cadButtonPressed()
{
    socket_server->serverSendMessage(VMManagerProtocol::ManagerMessage::CtrlAltDel);
}

void
VMManagerSystem::processStatusChanged()
{
    // set to running if the process is running and the state is stopped
    if (process->state() == QProcess::ProcessState::Running) {
        if (process_status == VMManagerSystem::ProcessStatus::Stopped) {
            process_status = VMManagerSystem::ProcessStatus::Running;
        }
    } else if (process->state() == QProcess::ProcessState::NotRunning) {
        process_status = VMManagerSystem::ProcessStatus::Stopped;
        window_obscured = false;
    }
    emit itemDataChanged();
    emit clientProcessStatusChanged();
}
void
VMManagerSystem::statusRefresh()
{
    processStatusChanged();
}
QString
VMManagerSystem::processStatusToString(VMManagerSystem::ProcessStatus status)
{
//    QMetaEnum qme = QMetaEnum::fromType<VMManagerSystem::ProcessStatus>();
//    return qme.valueToKey(static_cast<int>(status));
        switch (status) {
            case VMManagerSystem::ProcessStatus::Stopped:
                return tr("Powered Off");
            case VMManagerSystem::ProcessStatus::Running:
                return tr("Running");
            case VMManagerSystem::ProcessStatus::Paused:
                return tr("Paused");
            case VMManagerSystem::ProcessStatus::PausedWaiting:
            case VMManagerSystem::ProcessStatus::RunningWaiting:
                return QString("%1 (%2)").arg(tr("Paused"), tr("Waiting"));
            default:
                return tr("Unknown Status");
        }
}

QString
VMManagerSystem::getProcessStatusString() const
{
    return processStatusToString(process_status);
}

VMManagerSystem::ProcessStatus
VMManagerSystem::getProcessStatus() const
{
    return process_status;
}
// Maps VMManagerProtocol::RunningState to VMManagerSystem::ProcessStatus
void
VMManagerSystem::runningStatusChangeReceived(VMManagerProtocol::RunningState state)
{
    if(state == VMManagerProtocol::RunningState::Running) {
        process_status = VMManagerSystem::ProcessStatus::Running;
        window_obscured = false;
        windowStatusChanged();
    } else if(state == VMManagerProtocol::RunningState::Paused) {
        process_status = VMManagerSystem::ProcessStatus::Paused;
        window_obscured = false;
        windowStatusChanged();
    } else if(state == VMManagerProtocol::RunningState::RunningWaiting) {
        process_status = VMManagerSystem::ProcessStatus::RunningWaiting;
        window_obscured = true;
        windowStatusChanged();
    } else if(state == VMManagerProtocol::RunningState::PausedWaiting) {
        process_status = VMManagerSystem::ProcessStatus::PausedWaiting;
        window_obscured = true;
        windowStatusChanged();
    } else {
        process_status = VMManagerSystem::ProcessStatus::Unknown;
    }
    processStatusChanged();
}
void
VMManagerSystem::configurationChangeReceived()
{
    reloadConfig();
    emit configurationChanged(this->uuid);
}
void
VMManagerSystem::reloadConfig()
{
    loadSettings();
    setupVars();
}

QDateTime
VMManagerSystem::timestamp()
{
    return lastUsedTimestamp;
}
void
VMManagerSystem::setIcon(const QString &newIcon)
{
    icon = newIcon;
    saveSettings();
    emit itemDataChanged();
}
