/*
 * 86Box    A hypervisor and IBM PC system emulator that specializes in
 *          running old operating systems and software designed for IBM
 *          PC systems and compatibles from 1981 through fairly recent
 *          system designs based on the PCI bus.
 *
 *          This file is part of the 86Box distribution.
 *
 *          Program preferences UI module.
 *
 * Authors: Joakim L. Gilje <jgilje@jgilje.net>
 *          Cacodemon345
 *
 *          Copyright 2021 Joakim L. Gilje
 *          Copyright 2021-2022 Cacodemon345
 */
#include <QDebug>

#include <cstdint>
#include <cstdio>

extern "C" {
#include <86box/86box.h>
#include <86box/config.h>
#include <86box/timer.h>
#include <86box/fdd.h>
#include <86box/hdd.h>
}

#include "qt_settings_completer.hpp"
#include "qt_preferences.hpp"

#include <QStackedWidget>
#include <QStandardItemModel>

#include <QDialog>
#include <QTranslator>

#include <QPushButton>

#include "qt_preferencesgeneral.hpp"
#include "qt_preferencesemulator.hpp"
#include "qt_preferencesinput.hpp"
#include "qt_preferenceskeybindings.hpp"
#include "qt_preferencesmanager.hpp"
#include "qt_defs.hpp"

#include <QDebug>
#include <QMessageBox>
#include <QCheckBox>
#include <QApplication>
#include <QStyle>

#include <QTimer>
#include <QMap>
#include <QDir>
#include <QFile>
#include <QLibraryInfo>
#ifdef Q_OS_WINDOWS
#    include <QSysInfo>
#    include <QVersionNumber>
#    define  WIN32_LEAN_AND_MEAN
#    include <windows.h>
#endif

static const QVector<PagedDialog::Page> preferencesPages = {
    { "General",      "settings"      },
    { "Emulator",     "emulator"      },
    { "Input",        "input_devices" },
    { "Key bindings", "key_bindings"  },
};

static const PagedDialog::Page managerPage = { "VM Manager", "86Box-gray" };

static QVector<PagedDialog::Page>
pageList(bool showManager)
{
    QVector<PagedDialog::Page> pages = preferencesPages;

    if (showManager)
        pages.append(managerPage);

    return pages;
}

Preferences *Preferences::preferences = nullptr;

Preferences::CustomTranslator *Preferences::translator   = nullptr;
QTranslator                   *Preferences::qtTranslator = nullptr;

bool Preferences::translationsLoaded = false;

QVector<QPair<QString, QString>> Preferences::languages = {
    { "system", "(System Default)"         },
    { "ca-ES",  "Català"                   },
    { "cs-CZ",  "Čeština"                  },
    { "de-DE",  "Deutsch"                  },
    { "el-GR",  "Ελληνικά"                 },
    { "en-GB",  "English (United Kingdom)" },
    { "en-US",  "English (United States)"  },
    { "es-ES",  "Español (España)"         },
    { "fi-FI",  "Suomi"                    },
    { "fr-FR",  "Français"                 },
    { "hr-HR",  "Hrvatski"                 },
    { "it-IT",  "Italiano"                 },
    { "ja-JP",  "日本語"                    },
    { "ko-KR",  "한국어"                    },
    { "nb-NO",  "Norsk (bokmål)"           },
    { "nl-NL",  "Nederlands"               },
    { "pl-PL",  "Polski"                   },
    { "pt-BR",  "Português (Brasil)"       },
    { "pt-PT",  "Português (Portugal)"     },
    { "ru-RU",  "Русский"                  },
    { "ro-RO",  "Română"                   },
    { "sk-SK",  "Slovenčina"               },
    { "sl-SI",  "Slovenščina"              },
    { "sv-SE",  "Svenska"                  },
    { "tr-TR",  "Türkçe"                   },
    { "uk-UA",  "Українська"               },
    { "vi-VN",  "Tiếng Việt"               },
    { "zh-CN",  "简体中文"                  },
    { "zh-TW",  "繁體中文"                  },
};

Preferences::Preferences(bool showManager, bool machinesRunning, QWidget *parent)
    : PagedDialog(tr("Preferences"), QSize(600, 0), pageList(showManager), parent)
{
    general                   = new PreferencesGeneral(this);
    emulator                  = new PreferencesEmulator(this);
    input                     = new PreferencesInput(this);
    key_bindings              = new PreferencesKeyBindings(this);

    pageStack()->addWidget(general);
    pageStack()->addWidget(emulator);
    pageStack()->addWidget(input);
    pageStack()->addWidget(key_bindings);

    if (showManager) {
        manager = new PreferencesManager(machinesRunning, this);
        pageStack()->addWidget(manager);
    }

    showFirstPage();

    Preferences::preferences = this;
}

Preferences::~Preferences()
{
    Preferences::preferences = nullptr;
}

void
Preferences::save()
{
    general->save();
    emulator->save();
    input->save();
    key_bindings->save();
    if (manager)
        manager->save();
}

void
Preferences::accept()
{
    save();
    config_save_global();
    QDialog::accept();
}

static QString sys_lang;

#ifdef Q_OS_WINDOWS
/* Returns the standard UI font for Windows, which by default varies for different
   languages. It can also be changed via external tools, if the user wants that.

   We use the message font here since that is what most Windows components and
   other third-party programs use. */
QFont
Preferences::getUIFont()
{
    QString langCode = languageIdToCode(lang_id);

    if ((langCode != sys_lang) && ((langCode == "ja-JP") || (langCode == "ko-KR") ||
                                   (langCode == "zh-CN") || (langCode == "zh-TW"))) {
        /*
           Work around Windows' inappropriate, ugly default fonts when using an East Asian
           language when it is not also the system language.
         */
        if (langCode == "ja-JP") {
            /* Check for Windows 10 or later to choose the appropriate system font */
            if (QVersionNumber::fromString(QSysInfo::kernelVersion()).majorVersion() >= 10)
                return QFont("Yu Gothic UI", 9);
            else
                return QFont("Meiryo UI", 9);
        } else if (langCode == "ko-KR")
            return QFont("Malgun Gothic", 9);
        else if (langCode == "zh-CN")
            return QFont("Microsoft YaHei", 9);
        else if (langCode == "zh-TW")
            return QFont("Microsoft JhengHei", 9);
    }

    // Get the system (primary monitor) DPI. The font returned by
    // SystemParametersInfo is scaled according to this and we need
    // to get the font size in points to pass into QFont's constructor.
    HDC hdc = GetDC(NULL);
    int systemDpi = GetDeviceCaps(hdc, LOGPIXELSY);
    ReleaseDC(NULL, hdc);

    // Get the font metrics.
    NONCLIENTMETRICSW ncm = {};
    ncm.cbSize = sizeof(ncm);
    // This should never happen, but just to be safe, return Segoe UI if
    // SPI fails.
    if (!SystemParametersInfoW(SPI_GETNONCLIENTMETRICS, sizeof(ncm), &ncm, 0))
    {
        return QFont("Segoe UI", 9);
    }

    QString fontName = QString::fromWCharArray(ncm.lfMessageFont.lfFaceName);
    // Windows' conversion from points to pixels goes as follows:
    // 
    //     -MulDiv(PointSize, GetDeviceCaps(hDC, LOGPIXELSY), 72)
    // 
    // (source: https://learn.microsoft.com/en-us/windows/win32/api/wingdi/nf-wingdi-createfontw)
    //
    // Let's reverse that calculation to get the point size from the message font.
    int fontSize = -MulDiv(ncm.lfMessageFont.lfHeight, 72, systemDpi);
    
    return QFont(fontName, fontSize);
}
#endif

int
Preferences::languageCodeToId(QString langCode)
{
    for (int i = 0; i < languages.length(); i++) {
        if (languages[i].first == langCode) {
            return i;
        }
    }
    return 0;
}

QString
Preferences::languageIdToCode(int id)
{
    if ((id <= 0) || (id >= languages.length())) {
        return "system";
    }
    return languages[id].first;
}

void
Preferences::getSysLang(QObject *parent)
{
    if (qtTranslator) {
        QApplication::removeTranslator(qtTranslator);
        qtTranslator = nullptr;
    }
    if (translator) {
        QApplication::removeTranslator(translator);
        translator = nullptr;
    }
    qtTranslator             = new QTranslator(parent);
    translator               = new CustomTranslator(parent);
    QString localetofilename = "";
    for (int i = 0; i < QLocale::system().uiLanguages().size(); i++) {
        localetofilename = QLocale::system().uiLanguages()[i];
        if (translator->load(QLatin1String("86box_") + localetofilename, QLatin1String(":/"))) {
            qDebug() << "Translations loaded.";
            QCoreApplication::installTranslator(translator);
            /* First try qtbase */
            if (!loadQtTranslations(QLatin1String("qtbase_") + localetofilename.replace('-', '_')))
                /* If that fails, try legacy qt_* translations */
                if (!loadQtTranslations(QLatin1String("qt_") + localetofilename.replace('-', '_')))
                    qDebug() << "Failed to find Qt translations!";
            if (QCoreApplication::installTranslator(qtTranslator))
                qDebug() << "Qt translations loaded.";
            sys_lang = localetofilename;
            break;
        }
    }
}

void
Preferences::loadTranslators(QObject *parent)
{
    getSysLang(parent);
    if (qtTranslator) {
        QApplication::removeTranslator(qtTranslator);
        qtTranslator = nullptr;
    }
    if (translator) {
        QApplication::removeTranslator(translator);
        translator = nullptr;
    }
    qtTranslator             = new QTranslator(parent);
    translator               = new CustomTranslator(parent);
    QString localetofilename = "";
    if (lang_id == 0 || lang_id >= languages.length()) {
        for (int i = 0; i < QLocale::system().uiLanguages().size(); i++) {
            localetofilename = QLocale::system().uiLanguages()[i];
            if (translator->load(QLatin1String("86box_") + localetofilename, QLatin1String(":/"))) {
                qDebug() << "Translations loaded.";
                QCoreApplication::installTranslator(translator);
                /* First try qtbase */
                if (!loadQtTranslations(QLatin1String("qtbase_") + localetofilename.replace('-', '_')))
                    /* If that fails, try legacy qt_* translations */
                    if (!loadQtTranslations(QLatin1String("qt_") + localetofilename.replace('-', '_')))
                        qDebug() << "Failed to find Qt translations!";
                if (QCoreApplication::installTranslator(qtTranslator))
                    qDebug() << "Qt translations loaded.";
                break;
            }
        }
    } else {
        if (translator->load(QLatin1String("86box_") + languages[lang_id].first, QLatin1String(":/")))
            qDebug() << "Translations loaded.";
        QCoreApplication::installTranslator(translator);
        /* First try qtbase */
        if (!loadQtTranslations(QLatin1String("qtbase_") + QString(languages[lang_id].first).replace('-', '_')))
            /* If that fails, try legacy qt_* translations */
            if (!loadQtTranslations(QLatin1String("qt_") + QString(languages[lang_id].first).replace('-', '_')))
                qDebug() << "Failed to find Qt translations!";

        if (QCoreApplication::installTranslator(qtTranslator))
            qDebug() << "Qt translations loaded.";
    }
    translationsLoaded = true;
}

bool
Preferences::loadQtTranslations(const QString name)
{
    QString name_lang_only = name.left(name.indexOf('_'));
    /* System-wide translations */
#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
    if (qtTranslator->load(name, QLibraryInfo::path(QLibraryInfo::TranslationsPath)))
#else
    if (qtTranslator->load(name, QLibraryInfo::location(QLibraryInfo::TranslationsPath)))
#endif
        return true;
#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
    else if (qtTranslator->load(name_lang_only, QLibraryInfo::path(QLibraryInfo::TranslationsPath)))
#else
    else if (qtTranslator->load(name_lang_only, QLibraryInfo::location(QLibraryInfo::TranslationsPath)))
#endif
        return true;
    /* Bundled translations (embedded) */
    else if (qtTranslator->load(name, QLatin1String(":/")))
        return true;
    else if (qtTranslator->load(name_lang_only, QLatin1String(":/")))
        return true;
    /* Bundled translations (external) */
    else if (qtTranslator->load(name, QApplication::applicationDirPath() + "/./translations/"))
        return true;
    else if (qtTranslator->load(name_lang_only, QApplication::applicationDirPath() + "/./translations/"))
        return true;
    else
        return false;
}
