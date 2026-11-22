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
#include <QBuffer>
#include <QClipboard>
#include <QImage>
#include <QImageWriter>
#include <QMimeData>
#include <QStyleHints>
#if QT_VERSION <= QT_VERSION_CHECK(5, 14, 0)
#    include <QDesktopWidget>
#endif
#include <QUuid>
#include <QDebug>
#include <QPalette>
#include <QProxyStyle>
#include <QStyle>
#include <QStyleFactory>
#include "qt_styleoverride.hpp"
#include "qt_util.hpp"

#ifdef Q_OS_WINDOWS
#    include <windows.h>
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

int
logicalSizeForPixels(int pixels, qreal dpr)
{
    if (dpr <= 0.)
        return pixels;

    /* Qt rounds a window's size to whole device pixels, but a widget drawn into its
       parent covers the size multiplied out, so don't count on the rounding. */
    int size = static_cast<int>(pixels / dpr);
    while ((size * dpr) < (pixels - 0.001))
        size++;

    return size;
}

#ifdef HAS_COLOR_SCHEME_HINT
void
applyColorScheme(void)
{
    switch (color_scheme) {
        case 1:
            QGuiApplication::styleHints()->setColorScheme(Qt::ColorScheme::Light);
            break;
        case 2:
            QGuiApplication::styleHints()->setColorScheme(Qt::ColorScheme::Dark);
            break;
        default:
            /* Follow the system */
            QGuiApplication::styleHints()->unsetColorScheme();
            break;
    }
}
#endif

bool
selectionUsesHighlightedText(const QStyle *style)
{
    const auto *proxy = qobject_cast<const QProxyStyle *>(style);
    if (proxy != nullptr)
        style = proxy->baseStyle();
    if (style == nullptr)
        return true;

#if QT_VERSION >= QT_VERSION_CHECK(6, 1, 0)
    const QString name = style->name();
#else
    const QString name = style->objectName();
#endif
    /* The native Windows styles draw a light selection behind the regular text color */
    return name.compare("windowsvista", Qt::CaseInsensitive) && name.compare("windows11", Qt::CaseInsensitive);
}

#ifdef NEUTRAL_DARK_ACCENT
bool
neutralDarkAccent(const QStyle *style)
{
    if ((style == nullptr) || (QGuiApplication::styleHints()->colorScheme() != Qt::ColorScheme::Dark))
        return false;

    const QString name = style->name();
    return !name.compare("fusion", Qt::CaseInsensitive) || !name.compare("windows", Qt::CaseInsensitive);
}

/* Replaces the accent color of alternating rows with a neutral one where needed */
static void
applyPaletteOverrides(void)
{
    static bool overridden = false;

    const auto  *proxy = qobject_cast<QProxyStyle *>(qApp->style());
    const QStyle *base = (proxy != nullptr) ? proxy->baseStyle() : qApp->style();

    if (neutralDarkAccent(base)) {
        /* Only the roles set here are overridden, the rest keeps following the system */
        QPalette palette;
        palette.setColor(QPalette::AlternateBase, qApp->palette().color(QPalette::Base).lighter(115));
        qApp->setPalette(palette);
        overridden = true;
    } else if (overridden) {
        /* An empty palette brings back the system one */
        qApp->setPalette(QPalette());
        overridden = false;
    }
}
#endif

/* Returns the style to use, empty for the default one of the platform */
static QString
effectiveStyleName(const QString &defaultName)
{
    const QString name = QString::fromUtf8(ui_style);

#if defined(Q_OS_WINDOWS) && defined(HAS_COLOR_SCHEME_HINT)
    /* The windowsvista style can't draw dark controls, so default to Fusion in dark mode */
    if (name.isEmpty() && !defaultName.compare("windowsvista", Qt::CaseInsensitive)
        && (QGuiApplication::styleHints()->colorScheme() == Qt::ColorScheme::Dark))
        return QStringLiteral("fusion");
#else
    Q_UNUSED(defaultName);
#endif

    return name;
}

void
applyStyle(void)
{
    static bool    applied = false;
    static QString defaultName;
    static QString appliedName;

    if (!applied) {
#if defined(Q_OS_WINDOWS) && defined(HAS_COLOR_SCHEME_HINT)
        /* Remember the default style of the platform before it gets replaced */
        defaultName = qApp->style()->name();
#endif
#if QT_VERSION >= QT_VERSION_CHECK(6, 5, 0)
        /* The default style and the palette may depend on the color scheme, and the
           palette still is the old one when the change is signaled */
        QObject::connect(QGuiApplication::styleHints(), &QStyleHints::colorSchemeChanged, qApp,
                         [] { applyStyle(); }, Qt::QueuedConnection);
#endif
    }

    const QString name = effectiveStyleName(defaultName);

    /* Changing the style repolishes every widget, so only do it when needed */
    if (!applied || name.compare(appliedName, Qt::CaseInsensitive)) {
        QStyle *base = nullptr;
        if (!name.isEmpty()) {
            base = QStyleFactory::create(name);
            if (base == nullptr)
                qWarning() << "Qt style" << name << "is not available, using the default one";
        }

        /* Without a base style, the override goes over the default style of the platform */
        qApp->setStyle(new StyleOverride(base));

        applied     = true;
        appliedName = name;
    }

#ifdef NEUTRAL_DARK_ACCENT
    applyPaletteOverrides();
#endif
}

#ifdef Q_OS_WINDOWS

bool
isWindowsLightTheme(void)
{
    /* Go by the palette in use, as the windowsvista style stays light even in dark mode */
    const QPalette palette = QApplication::palette();
    return palette.window().color().value() > palette.windowText().color().value();
}

void
setWin11RoundedCorners(WId hwnd, bool enable)
{
    auto cornerPreference = (enable ? DWMWCP_DEFAULT : DWMWCP_DONOTROUND);
    DwmSetWindowAttribute((HWND) hwnd, DWMWA_WINDOW_CORNER_PREFERENCE, (LPCVOID) &cornerPreference, sizeof(cornerPreference));
}
#endif

QString
DlgFilter(QStringList extensions, bool last)
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

QString
DlgFilter(std::initializer_list<QString> extensions, bool last)
{
    QStringList temp(extensions);

    return DlgFilter(temp, last);
}

QString
currentUuid()
{
    return generateUuid(QString(cfg_path));
}

QString
generateUuid(const QString &path)
{
    auto dirPath = QFileInfo(path).dir().canonicalPath();
    if (!dirPath.endsWith("/")) {
        dirPath.append("/");
    }
    return QUuid::createUuidV5(QUuid {}, dirPath).toString(QUuid::WithoutBraces);
}

bool
compareUuid()
{
    // A uuid not set in the config file will have a zero length.
    // Any uuid that is lower than the minimum length will be considered invalid
    // and a new one will be generated
    if (const auto currentUuidLength = QString(uuid).length(); currentUuidLength < UUID_MIN_LENGTH) {
        storeCurrentUuid();
        return true;
    }
    // Do not prompt on mismatch if the system does not have any configured NICs. Just update the uuid
    if (!hasConfiguredNICs() && uuid != currentUuid()) {
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

void
copyImageToClipboard(const QImage &image)
{
    /* QImage does not take ownership of externally allocated pixels and the
       clipboard only encodes the image once another application asks for it,
       so hand over a copy the caller cannot invalidate. */
    const QImage img = image.copy();

    QByteArray png;
    QBuffer    buf(&png);
    buf.open(QIODevice::WriteOnly);
    QImageWriter writer(&buf, "png");
    writer.setCompression(100); /* Qt 6 maps 0-100 onto zlib levels 0-9 */
    writer.setQuality(0);       /* Qt 5 uses quality as the compression lever */
    writer.write(img);
    buf.close();

    /* Qt offers the clipboard image in every format the image plugins can
       write, and encodes on demand. The first format offered is whichever
       one was named first, and application/x-qt-image - which Qt's Wayland
       backend serialises as an uncompressed BMP - otherwise leads the list.
       Applications that take the first format on offer then receive a bitmap
       several times the size of a PNG of the same screen. Naming image/png
       before the image data puts it at the head of the list instead, while
       still offering the image itself for the platforms and applications
       that want it. */
    auto *mime = new QMimeData;
    mime->setData(QStringLiteral("image/png"), png);
    mime->setImageData(img);
    QApplication::clipboard()->setMimeData(mime, QClipboard::Clipboard);
}

}
