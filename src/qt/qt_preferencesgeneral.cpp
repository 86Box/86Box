/*
 * 86Box    A hypervisor and IBM PC system emulator that specializes in
 *          running old operating systems and software designed for IBM
 *          PC systems and compatibles from 1981 through fairly recent
 *          system designs based on the PCI bus.
 *
 *          This file is part of the 86Box distribution.
 *
 *          General preferences UI module, shared by the emulator and the
 *          VM manager.
 *
 * Authors: Joakim L. Gilje <jgilje@jgilje.net>
 *          David Hrdlička, <hrdlickadavid@outlook.com>
 *
 *          Copyright 2021 Joakim L. Gilje
 *          Copyright 2026 David Hrdlička
 */
#include "qt_settings_completer.hpp"
#include "qt_preferences.hpp"
#include "qt_preferencesgeneral.hpp"
#include "qt_util.hpp"
#include "ui_qt_preferencesgeneral.h"

#include <QStyleFactory>

#include <algorithm>
#include <cstring>

extern "C" {
#include <86box/86box.h>
#include <86box/sound.h>
}


PreferencesGeneral::PreferencesGeneral(QWidget *parent)
    : QWidget(parent)
    , ui(new Ui::PreferencesGeneral)
{
    ui->setupUi(this);

    scLanguage = new SettingsCompleter(ui->comboBoxLanguage, nullptr);

    ui->comboBoxLanguage->setItemData(0, 0);
    for (int i = 1; i < Preferences::languages.length(); i++) {
        ui->comboBoxLanguage->addItem(Preferences::languages[i].second, i);
        scLanguage->addDevice(nullptr, Preferences::languages[i].second);

        if (i == lang_id)
            ui->comboBoxLanguage->setCurrentIndex(ui->comboBoxLanguage->findData(i));
    }
    ui->comboBoxLanguage->model()->sort(Qt::AscendingOrder);

    ui->comboBoxStyle->setItemData(0, QString());
    QList<QPair<QString, QString>> styles;
    for (const auto &style : QStyleFactory::keys())
        styles.append({ styleDisplayName(style), style });
    std::sort(styles.begin(), styles.end(), [](const auto &a, const auto &b) {
        return a.first.localeAwareCompare(b.first) < 0;
    });
    for (const auto &style : std::as_const(styles))
        ui->comboBoxStyle->addItem(style.first, style.second);
    const int styleIndex = ui->comboBoxStyle->findData(QString::fromUtf8(ui_style));
    ui->comboBoxStyle->setCurrentIndex((styleIndex >= 0) ? styleIndex : 0);

    populatingBackend = true;
    for (int i = 0; i < sound_backend_get_count(); i++) {
        ui->comboBoxSoundBackend->addItem(QString::fromUtf8(sound_backend_get_name(i)), i);
        if (i == sound_backend_current)
            ui->comboBoxSoundBackend->setCurrentIndex(ui->comboBoxSoundBackend->count() - 1);
    }
    populatingBackend = false;
    populateAudioOutputDevices(sound_backend_current);

    ui->radioButtonSystem->setChecked(color_scheme == 0);
    ui->radioButtonLight->setChecked(color_scheme == 1);
    ui->radioButtonDark->setChecked(color_scheme == 2);

#ifndef HAS_COLOR_SCHEME_HINT
    /* Qt can only switch color schemes on its own from 6.8 on */
    ui->groupBoxColorScheme->setHidden(true);
#endif
}

PreferencesGeneral::~PreferencesGeneral()
{
    delete scLanguage;

    delete ui;
}

void
PreferencesGeneral::save()
{
    lang_id      = ui->comboBoxLanguage->currentData().toInt();
    color_scheme = (ui->radioButtonSystem->isChecked()) ? 0 : (ui->radioButtonLight->isChecked() ? 1 : 2);
    qstrncpy(ui_style, ui->comboBoxStyle->currentData().toString().toUtf8().constData(), sizeof(ui_style));

    const int      backend   = ui->comboBoxSoundBackend->currentData().toInt();
    const QString  outDev    = ui->comboBoxAudioOutputDevice->currentData().toString();
    const bool     outMoved  = (backend != sound_backend_current) || (outDev != QString(sound_output_device));

    sound_backend_current = backend;
    qstrncpy(sound_output_device, outDev.toUtf8().constData(), sizeof(sound_output_device));

    /* The manager has no sound running, only the emulator needs to switch. */
    if (outMoved && sound_output_is_open())
        sound_reopen_output();
}

void
PreferencesGeneral::populateAudioOutputDevices(const int backend)
{
    /* The selected device only means something to the backend it was selected for. */
    const QString outName = (backend == sound_backend_current) ? QString(sound_output_device) : QString();

    ui->comboBoxAudioOutputDevice->clear();
    ui->comboBoxAudioOutputDevice->addItem(tr("System Default"), QString(""));
    int selected = 0;

    const char *devList = sound_backend_get_output_devices_of(backend);
    if (devList != nullptr) {
        for (const char *dev = devList; *dev != '\0'; dev += strlen(dev) + 1) {
            const QString devName = QString::fromUtf8(dev);
            ui->comboBoxAudioOutputDevice->addItem(devName, devName);
            if (devName == outName)
                selected = ui->comboBoxAudioOutputDevice->count() - 1;
        }
    }

    ui->comboBoxAudioOutputDevice->setCurrentIndex(selected);
}

void
PreferencesGeneral::on_comboBoxSoundBackend_currentIndexChanged(int index)
{
    if ((index < 0) || populatingBackend)
        return;

    populateAudioOutputDevices(ui->comboBoxSoundBackend->currentData().toInt());
}

/* Qt names the Windows styles after their internal keys, so give them friendlier names */
QString
PreferencesGeneral::styleDisplayName(const QString &style)
{
    if (!style.compare("windows11", Qt::CaseInsensitive))
        return tr("Windows 11");
    if (!style.compare("windowsvista", Qt::CaseInsensitive))
        return tr("Windows Native");
    if (!style.compare("windows", Qt::CaseInsensitive))
        return tr("Windows Classic");

    return style;
}

void
PreferencesGeneral::on_pushButtonLanguage_released()
{
    ui->comboBoxLanguage->setCurrentIndex(0);
}
