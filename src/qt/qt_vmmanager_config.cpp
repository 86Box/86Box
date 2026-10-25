/*
 * 86Box    A hypervisor and IBM PC system emulator that specializes in
 *          running old operating systems and software designed for IBM
 *          PC systems and compatibles from 1981 through fairly recent
 *          system designs based on the PCI bus.
 *
 *          This file is part of the 86Box distribution.
 *
 *          86Box VM manager configuration module
 *
 * Authors: cold-brewed
 *
 *          Copyright 2024 cold-brewed
 */
#include <QDebug>
#include <QDir>
#include "qt_vmmanager_config.hpp"

extern "C" {
#include <86box/86box.h>
#include <86box/config.h>
#include <86box/plat.h>
#include <86box/version.h>
}

QVariantHash VMManagerConfig::generalDefaults = {
    { "hide_tool_bar",   0 }
};

VMManagerConfig::VMManagerConfig(const ConfigType type, const QString &section)
{
    char BUF[256];
    plat_get_global_config_dir(BUF, 255);
    const auto configDir  = QString(BUF);
    const auto configFile = QDir::cleanPath(configDir + "/" + "vmm.ini");

    config_type = type;

    settings = new QSettings(configFile, QSettings::IniFormat, this);
#if QT_VERSION < QT_VERSION_CHECK(6, 0, 0)
    settings->setIniCodec("UTF-8");
#endif
    settings->setFallbacksEnabled(false);
    if (type == ConfigType::System && !section.isEmpty()) {
        settings->beginGroup(section);
    } else {
        settings->beginGroup("");
    }
}

VMManagerConfig::~VMManagerConfig()
{
    settings->endGroup();
}

QString
VMManagerConfig::getStringValue(const QString &key) const
{
    auto defaultValue = QVariant();
    if ((config_type == ConfigType::General) && (generalDefaults.contains(key)))
        defaultValue = generalDefaults[key];
    const auto value = settings->value(key, defaultValue);
    // An invalid QVariant with toString will give a default QString value which is blank.
    // Therefore any variables that do not exist will return blank strings
    return value.toString();
}

void
VMManagerConfig::setStringValue(const QString &key, const QString &value) const
{
    if (value.isEmpty()) {
        remove(key);
        return;
    }
    settings->setValue(key, value);
}

void
VMManagerConfig::remove(const QString &key) const
{
    settings->remove(key);
}

void
VMManagerConfig::sync() const
{
    settings->sync();
}

/* Moves the preferences kept here to the "Manager" section of the global
   configuration. A preference set there is newer, so one is only taken from
   here while it still has its default value there; either way, it is then
   removed from here. */
void
VMManagerConfig::migratePreferences()
{
    const struct {
        const char *key;
        int        *value;
        int         defaultValue;
    } preferences[] = {
        { "update_check",    &vmm_update_check,    1 },
        { "regex_search",    &vmm_regex_search,    0 },
        { "window_remember", &vmm_window_remember, 0 },
        { "delete_to_trash", &vmm_delete_to_trash, 0 },
    };

    const VMManagerConfig config(ConfigType::General);
    QStringList           migrated;

    for (const auto &preference : preferences) {
        if (!config.settings->contains(preference.key))
            continue;

        if (*preference.value == preference.defaultValue)
            *preference.value = config.getStringValue(preference.key).toInt();
        /* Only this preference is saved, keeping the rest of the file. */
        config_save_global_setting("Manager", preference.key);
        migrated.append(preference.key);
    }

    if (migrated.isEmpty())
        return;

    for (const auto &key : migrated)
        config.remove(key);
    config.sync();
}
