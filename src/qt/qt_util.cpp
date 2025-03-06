/*
 * 86Box    A hypervisor and IBM PC system emulator that specializes in
 *          running old operating systems and software designed for IBM
 *          PC systems and compatibles from 1981 through fairly recent
 *          system designs based on the PCI bus.
 *
 *          This file is part of the 86Box distribution.
 *
 *          Utility functions.
 *
 *
 *
 * Authors: Teemu Korhonen
 *
 *          Copyright 2022 Teemu Korhonen
 */
#include <QDir>
#include <QFileInfo>
#include <QStringBuilder>
#include <QStringList>
#include <QWidget>
#include <QApplication>
#if QT_VERSION <= QT_VERSION_CHECK(5, 14, 0)
#    include <QDesktopWidget>
#endif
#include <QUuid>
#include "qt_util.hpp"

#ifdef Q_OS_WINDOWS
#    include <dwmapi.h>
#    ifndef DWMWA_WINDOW_CORNER_PREFERENCE
#        define DWMWA_WINDOW_CORNER_PREFERENCE 33
#    endif
#    ifndef DWMWCP_DEFAULT
#        define DWMWCP_DEFAULT 0
#    endif
#    ifndef DWMWCP_DONOTROUND
#        define DWMWCP_DONOTROUND 1
#    endif
#endif

extern "C" {
#include <86box/86box.h>
#include <86box/config.h>
#include <86box/device.h>
#include <86box/ini.h>
#include <86box/random.h>
#include <86box/thread.h>
#include <86box/timer.h>
#include <86box/network.h>
}

namespace util {
QScreen *
screenOfWidget(QWidget *widget)
{
#if QT_VERSION <= QT_VERSION_CHECK(5, 14, 0)
    return QApplication::screens()[QApplication::desktop()->screenNumber(widget) == -1 ? 0 : QApplication::desktop()->screenNumber(widget)];
#else
    return widget->screen();
#endif
}

#ifdef Q_OS_WINDOWS
void
setWin11RoundedCorners(WId hwnd, bool enable)
{
    auto cornerPreference = (enable ? DWMWCP_DEFAULT : DWMWCP_DONOTROUND);
    DwmSetWindowAttribute((HWND) hwnd, DWMWA_WINDOW_CORNER_PREFERENCE, (LPCVOID) &cornerPreference, sizeof(cornerPreference));
}
#endif

QString
DlgFilter(std::initializer_list<QString> extensions, bool last)
{
    QStringList temp;

    for (auto ext : extensions) {
#ifdef Q_OS_UNIX
        if (ext == "*") {
            temp.append("*");
            continue;
        }
        temp.append("*." % ext.toUpper());
#endif
        temp.append("*." % ext);
    }

#ifdef Q_OS_UNIX
    temp.removeDuplicates();
#endif
    return " (" % temp.join(' ') % ")" % (!last ? ";;" : "");
}

QString currentUuid()
{
    auto configPath = QFileInfo(cfg_path).dir().canonicalPath();
    if(!configPath.endsWith("/")) {
        configPath.append("/");
    }
    return QUuid::createUuidV5(QUuid{}, configPath).toString(QUuid::WithoutBraces);
}

bool compareUuid()
{
    // A uuid not set in the config file will have a zero length.
    // Any uuid that is lower than the minimum length will be considered invalid
    // and a new one will be generated
    if (const auto currentUuidLength = QString(uuid).length(); currentUuidLength < UUID_MIN_LENGTH) {
        storeCurrentUuid();
        return true;
    }
    // Do not prompt on mismatch if the system does not have any configured NICs. Just update the uuid
    if(!hasConfiguredNICs() && uuid != currentUuid()) {
        storeCurrentUuid();
        return true;
    }
    // The uuid appears to be a valid, at least by length.
    // Compare with a simple string match
    return uuid == currentUuid();
}

void
storeCurrentUuid()
{
    strncpy(uuid, currentUuid().toUtf8().constData(), sizeof(uuid) - 1);
}

void
generateNewMacAdresses()
{
    for (int i = 0; i < NET_CARD_MAX; ++i) {
        auto net_card = net_cards_conf[i];
        if (net_card.device_num != 0) {
            const auto       network_device = network_card_getdevice(net_card.device_num);
            device_context_t device_context;
            device_set_context(&device_context, network_device, i + 1);
            auto generatedMac = QString::asprintf("%02X:%02X:%02X", random_generate(), random_generate(), random_generate()).toLower();
            config_set_string(device_context.name, "mac", generatedMac.toUtf8().constData());
        }
    }
}

bool
hasConfiguredNICs()
{
    for (int i = 0; i < NET_CARD_MAX; ++i) {
        if (const auto net_card = net_cards_conf[i]; net_card.device_num != 0) {
            return true;
        }
    }
    return false;
}

}
