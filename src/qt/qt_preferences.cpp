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
#include "ui_qt_preferences.h"
#include "qt_mainwindow.hpp"
#include "ui_qt_mainwindow.h"
#include "qt_machinestatus.hpp"

#include <QStandardItemModel>

#include <QDialog>
#include <QTranslator>

#include <QPushButton>

#include "qt_preferencesemulator.hpp"
#include "qt_preferencesinput.hpp"
#include "qt_preferenceskeybindings.hpp"
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

extern MainWindow *main_window;

class PreferencesModel : public QAbstractListModel {
public:
    PreferencesModel(QObject *parent)
        : QAbstractListModel(parent)
    {
#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
        fontHeight = QFontMetrics(qApp->font()).height();
#else
        fontHeight = QApplication::fontMetrics().height();
#endif
    }

    QVariant data(const QModelIndex &index, int role = Qt::DisplayRole) const override;
    int      rowCount(const QModelIndex &parent = QModelIndex()) const override;

private:
    QStringList pages = {
        "Emulator",
        "Input",
        "Key bindings",
    };
    QStringList page_icons = {
        "emulator",
        "input_devices",
        "key_bindings",
    };
    int fontHeight;
};

QVariant
PreferencesModel::data(const QModelIndex &index, int role) const
{
    Q_ASSERT(checkIndex(index, QAbstractItemModel::CheckIndexOption::IndexIsValid | QAbstractItemModel::CheckIndexOption::ParentIsInvalid));

    switch (role) {
        case Qt::DisplayRole:
            return tr(pages.at(index.row()).toUtf8().data());
        case Qt::DecorationRole:
            return QIcon(QString(":/settings/qt/icons/%1.ico").arg(page_icons[index.row()]));
        case Qt::SizeHintRole:
            return QSize(-1, fontHeight * 2);
        default:
            return {};
    }
}

int
PreferencesModel::rowCount(const QModelIndex &parent) const
{
    (void) parent;
    return pages.size();
}

Preferences *Preferences::preferences = nullptr;
;
Preferences::CustomTranslator *Preferences::translator   = nullptr;
QTranslator                   *Preferences::qtTranslator = nullptr;

QVector<QPair<QString, QString>> Preferences::languages = {
    { "system", "(System Default)"         },
    { "ca-ES",  "Catalan (Spain)"          },
    { "zh-CN",  "Chinese (Simplified)"     },
    { "zh-TW",  "Chinese (Traditional)"    },
    { "hr-HR",  "Croatian (Croatia)"       },
    { "cs-CZ",  "Czech (Czech Republic)"   },
    { "de-DE",  "German (Germany)"         },
    { "el-GR",  "Greek (Greece)"           },
    { "en-GB",  "English (United Kingdom)" },
    { "en-US",  "English (United States)"  },
    { "fi-FI",  "Finnish (Finland)"        },
    { "fr-FR",  "French (France)"          },
    { "it-IT",  "Italian (Italy)"          },
    { "ja-JP",  "Japanese (Japan)"         },
    { "ko-KR",  "Korean (Korea)"           },
    { "nl-NL",  "Dutch (Netherlands)"      },
    { "nb-NO",  "Norwegian (Bokmål)"       },
    { "pl-PL",  "Polish (Poland)"          },
    { "pt-BR",  "Portuguese (Brazil)"      },
    { "pt-PT",  "Portuguese (Portugal)"    },
    { "ru-RU",  "Russian (Russia)"         },
    { "sk-SK",  "Slovak (Slovakia)"        },
    { "sl-SI",  "Slovenian (Slovenia)"     },
    { "sv-SE",  "Swedish (Sweden)"         },
    { "es-ES",  "Spanish (Spain)"          },
    { "tr-TR",  "Turkish (Turkey)"         },
    { "uk-UA",  "Ukrainian (Ukraine)"      },
    { "vi-VN",  "Vietnamese (Vietnam)"     },
};

Preferences::Preferences(QWidget *parent)
    : QDialog(parent)
    , ui(new Ui::Preferences)
{
    ui->setupUi(this);
    auto *model = new PreferencesModel(this);
    ui->listView->setModel(model);

    emulator                  = new PreferencesEmulator(this);
    input                     = new PreferencesInput(this);
    key_bindings              = new PreferencesKeyBindings(this);

    ui->stackedWidget->addWidget(emulator);
    ui->stackedWidget->addWidget(input);
    ui->stackedWidget->addWidget(key_bindings);

    connect(ui->listView->selectionModel(), &QItemSelectionModel::currentChanged, this,
            [this](const QModelIndex &current, const QModelIndex &previous) {
                ui->stackedWidget->setCurrentIndex(current.row());
                ui->headerIcon->setPixmap(qvariant_cast<QIcon>(ui->listView->model()->data(current, Qt::DecorationRole)).pixmap(QSize(16, 16)));
                ui->headerLabel->setText(ui->listView->model()->data(current, Qt::DisplayRole).toString());
            });

    ui->listView->setCurrentIndex(model->index(0, 0));

    Preferences::preferences = this;
}

Preferences::~Preferences()
{
    delete ui;

    Preferences::preferences = nullptr;
}

void
Preferences::save()
{
    emulator->save();
    input->save();
    key_bindings->save();
}

void
Preferences::accept()
{
    auto size               = main_window->centralWidget()->size();

    save();
    config_save_global();
    config_changed = 2;
    QTimer::singleShot(200, [size] () {
        main_window->centralWidget()->setFixedSize(size);
        QApplication::processEvents();
        if (vid_resize == 1) {
            main_window->centralWidget()->setFixedSize(QWIDGETSIZE_MAX, QWIDGETSIZE_MAX);
        }
    });
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
