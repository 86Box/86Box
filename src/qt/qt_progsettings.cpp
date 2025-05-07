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
 *
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

#include <QMap>
#include <QDir>
#include <QFile>
#include <QLibraryInfo>
#ifdef Q_OS_WINDOWS
#    include <QSysInfo>
#    include <QVersionNumber>
#endif

extern "C" {
#include <86box/86box.h>
#include <86box/version.h>
#include <86box/config.h>
#include <86box/plat.h>
#include <86box/mem.h>
#include <86box/rom.h>
}

extern MainWindow            *main_window;

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
    { "en-GB",  "English (United Kingdom)" },
    { "en-US",  "English (United States)"  },
    { "fi-FI",  "Finnish (Finland)"        },
    { "fr-FR",  "French (France)"          },
    { "hu-HU",  "Hungarian (Hungary)"      },
    { "it-IT",  "Italian (Italy)"          },
    { "ja-JP",  "Japanese (Japan)"         },
    { "ko-KR",  "Korean (Korea)"           },
    { "nl-NL",  "Dutch (Netherlands)"      },
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

#ifndef Q_OS_WINDOWS
    ui->checkBoxMultimediaKeys->setHidden(true);
#endif
}

void
ProgSettings::accept()
{
    lang_id                 = ui->comboBoxLanguage->currentData().toInt();
    open_dir_usr_path       = ui->openDirUsrPath->isChecked() ? 1 : 0;
    confirm_exit            = ui->checkBoxConfirmExit->isChecked() ? 1 : 0;
    confirm_save            = ui->checkBoxConfirmSave->isChecked() ? 1 : 0;
    confirm_reset           = ui->checkBoxConfirmHardReset->isChecked() ? 1 : 0;
    inhibit_multimedia_keys = ui->checkBoxMultimediaKeys->isChecked() ? 1 : 0;

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
    QDialog::accept();
}

ProgSettings::~ProgSettings()
{
    delete ui;
}

#ifdef Q_OS_WINDOWS
/* Return the standard font name on Windows, which is overridden per-language
   to prevent CJK fonts with embedded bitmaps being chosen as a fallback. */
QString
ProgSettings::getFontName(int langId)
{
    QString langCode = languageIdToCode(lang_id);
    if (langCode == "ja-JP") {
        /* Check for Windows 10 or later to choose the appropriate system font */
        if (QVersionNumber::fromString(QSysInfo::kernelVersion()).majorVersion() >= 10)
            return "Yu Gothic UI";
        else
            return "Meiryo UI";
    } else if (langCode == "ko-KR")
        return "Malgun Gothic";
    else if (langCode == "zh-CN")
        return "Microsoft YaHei";
    else if (langCode == "zh-TW")
        return "Microsoft JhengHei";
    else
        return "Segoe UI";
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
    if ((id == 0) || (id >= languages.length())) {
        return "system";
    }
    return languages[id].first;
}

void
ProgSettings::loadTranslators(QObject *parent)
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
    if (lang_id == 0 || lang_id >= languages.length()) {
        for (int i = 0; i < QLocale::system().uiLanguages().size(); i++) {
            localetofilename = QLocale::system().uiLanguages()[i];
            if (translator->load(QLatin1String("86box_") + localetofilename, QLatin1String(":/"))) {
                qDebug() << "Translations loaded.\n";
                QCoreApplication::installTranslator(translator);
                if (!qtTranslator->load(QLatin1String("qtbase_") + localetofilename.replace('-', '_'), QLibraryInfo::location(QLibraryInfo::TranslationsPath)))
                    if (!qtTranslator->load(QLatin1String("qtbase_") + localetofilename.left(localetofilename.indexOf('-')), QLibraryInfo::location(QLibraryInfo::TranslationsPath)))
                        if (!qtTranslator->load(QLatin1String("qt_") + localetofilename.replace('-', '_'), QApplication::applicationDirPath() + "/./translations/"))
                            qtTranslator->load(QLatin1String("qt_") + localetofilename.replace('-', '_'), QLatin1String(":/"));
                if (QApplication::installTranslator(qtTranslator)) {
                    qDebug() << "Qt translations loaded."
                             << "\n";
                }
                break;
            }
        }
    } else {
        translator->load(QLatin1String("86box_") + languages[lang_id].first, QLatin1String(":/"));
        QCoreApplication::installTranslator(translator);
        if (!qtTranslator->load(QLatin1String("qtbase_") + QString(languages[lang_id].first).replace('-', '_'), QLibraryInfo::location(QLibraryInfo::TranslationsPath)))
            if (!qtTranslator->load(QLatin1String("qtbase_") + QString(languages[lang_id].first).left(QString(languages[lang_id].first).indexOf('-')), QLibraryInfo::location(QLibraryInfo::TranslationsPath)))
                if(!qtTranslator->load(QLatin1String("qt_") + QString(languages[lang_id].first).replace('-', '_'), QApplication::applicationDirPath() + "/./translations/"))
                    qtTranslator->load(QLatin1String("qt_") + QString(languages[lang_id].first).replace('-', '_'), QLatin1String(":/"));

        QCoreApplication::installTranslator(qtTranslator);
    }
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
