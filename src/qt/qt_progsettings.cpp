/*
 * 86Box    A hypervisor and IBM PC system emulator that specializes in
 *          running old operating systems and software designed for IBM
 *          PC systems and compatibles from 1981 through fairly recent
 *          system designs based on the PCI bus.
 *
 *          This file is part of the 86Box distribution.
 *
 *          Program settings UI module.
 *
 * Authors: Cacodemon345
 *
 *          Copyright 2021-2022 Cacodemon345
 */
#include <QDebug>

#include "qt_progsettings.hpp"
#include "ui_qt_progsettings.h"
#include "qt_mainwindow.hpp"
#include "ui_qt_mainwindow.h"
#include "qt_machinestatus.hpp"

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

extern "C" {
#include <86box/86box.h>
#include <86box/version.h>
#include <86box/config.h>
#include <86box/plat.h>
#include <86box/mem.h>
#include <86box/rom.h>
#include <86box/video.h>
}

extern MainWindow *main_window;

ProgSettings::CustomTranslator *ProgSettings::translator   = nullptr;
QTranslator                    *ProgSettings::qtTranslator = nullptr;

QVector<QPair<QString, QString>> ProgSettings::languages = {
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

ProgSettings::ProgSettings(QWidget *parent)
    : QDialog(parent)
    , ui(new Ui::ProgSettings)
{
    ui->setupUi(this);
    ui->comboBoxLanguage->setItemData(0, 0);
    for (int i = 1; i < languages.length(); i++) {
        ui->comboBoxLanguage->addItem(languages[i].second, i);
        if (i == lang_id) {
            ui->comboBoxLanguage->setCurrentIndex(ui->comboBoxLanguage->findData(i));
        }
    }
    ui->comboBoxLanguage->model()->sort(Qt::AscendingOrder);

    mouseSensitivity = mouse_sensitivity;
    ui->horizontalSlider->setValue(mouseSensitivity * 100.);
    ui->openDirUsrPath->setChecked(open_dir_usr_path > 0);
    ui->checkBoxMultimediaKeys->setChecked(inhibit_multimedia_keys);
    ui->checkBoxConfirmExit->setChecked(confirm_exit);
    ui->checkBoxConfirmSave->setChecked(confirm_save);
    ui->checkBoxConfirmHardReset->setChecked(confirm_reset);

    ui->radioButtonSystem->setChecked(color_scheme == 0);
    ui->radioButtonLight->setChecked(color_scheme == 1);
    ui->radioButtonDark->setChecked(color_scheme == 2);

#ifndef Q_OS_WINDOWS
    ui->checkBoxMultimediaKeys->setHidden(true);
    ui->groupBox->setHidden(true);
#endif
}

void
ProgSettings::accept()
{
    auto size               = main_window->centralWidget()->size();
    lang_id                 = ui->comboBoxLanguage->currentData().toInt();
    open_dir_usr_path       = ui->openDirUsrPath->isChecked() ? 1 : 0;
    confirm_exit            = ui->checkBoxConfirmExit->isChecked() ? 1 : 0;
    confirm_save            = ui->checkBoxConfirmSave->isChecked() ? 1 : 0;
    confirm_reset           = ui->checkBoxConfirmHardReset->isChecked() ? 1 : 0;
    inhibit_multimedia_keys = ui->checkBoxMultimediaKeys->isChecked() ? 1 : 0;

    color_scheme = (ui->radioButtonSystem->isChecked()) ? 0 : (ui->radioButtonLight->isChecked() ? 1 : 2);

#ifdef Q_OS_WINDOWS
    extern void selectDarkMode();
    selectDarkMode();
#endif

    loadTranslators(QCoreApplication::instance());
    reloadStrings();
    update_mouse_msg();
    main_window->ui->retranslateUi(main_window);
    QString vmname(vm_name);
    if (vmname.at(vmname.size() - 1) == '"' || vmname.at(vmname.size() - 1) == '\'')
        vmname.truncate(vmname.size() - 1);
    main_window->setWindowTitle(QString("%1 - %2 %3").arg(vmname, EMU_NAME, EMU_VERSION_FULL));
    QString msg = main_window->status->getMessage();
    main_window->status.reset(new MachineStatus(main_window));
    main_window->refreshMediaMenu();
    main_window->status->message(msg);
    connect(main_window, &MainWindow::updateStatusBarTip, main_window->status.get(), &MachineStatus::updateTip);
    connect(main_window, &MainWindow::statusBarMessage, main_window->status.get(), &MachineStatus::message, Qt::QueuedConnection);
    mouse_sensitivity = mouseSensitivity;
    config_save_global();
    QTimer::singleShot(200, [size] () {
        main_window->centralWidget()->setFixedSize(size);
        QApplication::processEvents();
        if (vid_resize == 1) {
            main_window->centralWidget()->setFixedSize(QWIDGETSIZE_MAX, QWIDGETSIZE_MAX);
        }
    });
    QDialog::accept();
}

ProgSettings::~ProgSettings()
{
    delete ui;
}

static QString sys_lang;

#ifdef Q_OS_WINDOWS
/* Returns the standard UI font for Windows, which by default varies for different
   languages. It can also be changed via external tools, if the user wants that.

   We use the message font here since that is what most Windows components and
   other third-party programs use. */
QFont
ProgSettings::getUIFont()
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
ProgSettings::languageCodeToId(QString langCode)
{
    for (int i = 0; i < languages.length(); i++) {
        if (languages[i].first == langCode) {
            return i;
        }
    }
    return 0;
}

QString
ProgSettings::languageIdToCode(int id)
{
    if ((id <= 0) || (id >= languages.length())) {
        return "system";
    }
    return languages[id].first;
}

void
ProgSettings::getSysLang(QObject *parent)
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
ProgSettings::loadTranslators(QObject *parent)
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
ProgSettings::loadQtTranslations(const QString name)
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

void
ProgSettings::on_pushButtonLanguage_released()
{
    ui->comboBoxLanguage->setCurrentIndex(0);
}

void
ProgSettings::on_horizontalSlider_valueChanged(int value)
{
    mouseSensitivity = (double) value / 100.;
}

void
ProgSettings::on_pushButton_2_clicked()
{
    mouseSensitivity = 1.0;
    ui->horizontalSlider->setValue(100);
}
