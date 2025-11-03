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
#include <86box/plat.h>
}

VMManagerConfig::VMManagerConfig(const ConfigType type, const QString& section)
{
    char BUF[256];
    plat_get_global_config_dir(BUF, 255);
    const auto configDir = QString(BUF);
    const auto configFile = QDir::cleanPath(configDir + "/" + "vmm.ini");

    config_type = type;

    settings = new QSettings(configFile, QSettings::IniFormat, this);
#if QT_VERSION < QT_VERSION_CHECK(6, 0, 0)
    settings->setIniCodec("UTF-8");
#endif
    settings->setFallbacksEnabled(false);
    if(type == ConfigType::System && !section.isEmpty()) {
        settings->beginGroup(section);
    }
}

VMManagerConfig::~VMManagerConfig() {
    settings->endGroup();
}

QString
VMManagerConfig::getStringValue(const QString& key) const
{
    const auto value = settings->value(key);
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

