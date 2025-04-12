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

ProgSettings::ProgSettings(QWidget *parent)
    : QDialog(parent)
    , ui(new Ui::ProgSettings)
{
    ui->setupUi(this);
    ui->comboBoxLanguage->setItemData(0, 0xFFFF);
    for (auto i = lcid_langcode.begin(); i != lcid_langcode.end(); i++) {
        if (i.key() == 0xFFFF)
            continue;
        ui->comboBoxLanguage->addItem(lcid_langcode[i.key()].second, i.key());
        if (i.key() == lang_id) {
            ui->comboBoxLanguage->setCurrentIndex(ui->comboBoxLanguage->findData(i.key()));
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
    ui->checkBoxFullscreenFirst->setChecked(video_fullscreen_first);

#ifndef Q_OS_WINDOWS
    ui->checkBoxMultimediaKeys->setHidden(true);
#endif
}

void
ProgSettings::accept()
{
    lang_id                 = ui->comboBoxLanguage->currentData().toUInt();
    open_dir_usr_path       = ui->openDirUsrPath->isChecked() ? 1 : 0;
    confirm_exit            = ui->checkBoxConfirmExit->isChecked() ? 1 : 0;
    confirm_save            = ui->checkBoxConfirmSave->isChecked() ? 1 : 0;
    confirm_reset           = ui->checkBoxConfirmHardReset->isChecked() ? 1 : 0;
    video_fullscreen_first  = ui->checkBoxFullscreenFirst->isChecked() ? 1 : 0;
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
ProgSettings::getFontName(uint32_t lcid)
{
    switch (lcid) {
        case 0x0404: /* zh-TW */
            return "Microsoft JhengHei";
        case 0x0411: /* ja-JP */
            return "Meiryo UI";
        case 0x0412: /* ko-KR */
            return "Malgun Gothic";
        case 0x0804: /* zh-CN */
            return "Microsoft YaHei";
        default:
            return "Segoe UI";
    }
}
#endif

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
    if (lang_id == 0xFFFF || lcid_langcode.contains(lang_id) == false) {
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
        translator->load(QLatin1String("86box_") + lcid_langcode[lang_id].first, QLatin1String(":/"));
        QCoreApplication::installTranslator(translator);
        if (!qtTranslator->load(QLatin1String("qtbase_") + QString(lcid_langcode[lang_id].first).replace('-', '_'), QLibraryInfo::location(QLibraryInfo::TranslationsPath)))
            if (!qtTranslator->load(QLatin1String("qtbase_") + QString(lcid_langcode[lang_id].first).left(QString(lcid_langcode[lang_id].first).indexOf('-')), QLibraryInfo::location(QLibraryInfo::TranslationsPath)))
                if(!qtTranslator->load(QLatin1String("qt_") + QString(lcid_langcode[lang_id].first).replace('-', '_'), QApplication::applicationDirPath() + "/./translations/"))
                    qtTranslator->load(QLatin1String("qt_") + QString(lcid_langcode[lang_id].first).replace('-', '_'), QLatin1String(":/"));

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
