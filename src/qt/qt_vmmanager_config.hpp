/*
 * 86Box    A hypervisor and IBM PC system emulator that specializes in
 *          running old operating systems and software designed for IBM
 *          PC systems and compatibles from 1981 through fairly recent
 *          system designs based on the PCI bus.
 *
 *          This file is part of the 86Box distribution.
 *
 *          Header for the 86Box VM manager configuration module
 *
 * Authors: cold-brewed
 *
 *          Copyright 2024 cold-brewed
 */
#ifndef QT_VMMANAGER_CONFIG_H
#define QT_VMMANAGER_CONFIG_H

#include <QSettings>

class VMManagerConfig : QObject {
    Q_OBJECT

public:
    enum class ConfigType {
        General,
        System,
    };
    Q_ENUM(ConfigType);

    explicit VMManagerConfig(ConfigType type, const QString& section = {});
    ~VMManagerConfig() override;
    [[nodiscard]] QString getStringValue(const QString& key) const;
    void setStringValue(const QString& key, const QString& value) const;
    void remove(const QString &key) const;

    void sync() const;

    QSettings *settings;
    ConfigType config_type;
    QString system_name;
};

#endif // QT_VMMANAGER_CONFIG_H