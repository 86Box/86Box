/*
 * 86Box	A hypervisor and IBM PC system emulator that specializes in
 *		running old operating systems and software designed for IBM
 *		PC systems and compatibles from 1981 through fairly recent
 *		system designs based on the PCI bus.
 *
 *		This file is part of the 86Box distribution.
 *
 *		Program settings UI module.
 *
 *
 *
 * Authors: Cacodemon345
 *
 *      Copyright 2021-2022 Cacodemon345
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

extern "C"
{
#include <86box/86box.h>
#include <86box/version.h>
#include <86box/config.h>
#include <86box/plat.h>
}


static QMap<QString, QString> iconset_to_qt;
extern MainWindow* main_window;

ProgSettings::CustomTranslator* ProgSettings::translator = nullptr;
QTranslator* ProgSettings::qtTranslator = nullptr;
QString ProgSettings::getIconSetPath()
{
    QString roms_root;
    if (rom_path[0])
        roms_root = rom_path;
    else {
        roms_root = QString("%1/roms").arg(exe_path);
    }

    if (iconset_to_qt.isEmpty())
    {
        iconset_to_qt.insert("", ":/settings/win/icons");
        QDir dir(roms_root + "/icons/");
        if (dir.isReadable())
        {
            auto dirList = dir.entryList(QDir::AllDirs | QDir::Executable | QDir::Readable);
            for (auto &curIconSet : dirList)
            {
                if (curIconSet == "." || curIconSet == "..") continue;
                iconset_to_qt.insert(curIconSet, (dir.canonicalPath() + '/') + curIconSet);
            }
        }
    }
    return iconset_to_qt[icon_set];
}

QIcon ProgSettings::loadIcon(QString file)
{
    (void)getIconSetPath();
    if (!QFile::exists(iconset_to_qt[icon_set] + file)) return QIcon(iconset_to_qt[""] + file);
    return QIcon(iconset_to_qt[icon_set] + file);
}

ProgSettings::ProgSettings(QWidget *parent) :
    QDialog(parent),
    ui(new Ui::ProgSettings)
{
    ui->setupUi(this);
    (void)getIconSetPath();
    ui->comboBox->setItemData(0, "");
    ui->comboBox->setCurrentIndex(0);
    for (auto i = iconset_to_qt.begin(); i != iconset_to_qt.end(); i++)
    {
        if (i.key() == "") continue;
        QFile iconfile(i.value() + "/iconinfo.txt");
        iconfile.open(QFile::ReadOnly);
        QString friendlyName;
        QString iconsetinfo(iconfile.readAll());
        iconfile.close();
        if (iconsetinfo.isEmpty()) friendlyName = i.key();
        else friendlyName = iconsetinfo.split('\n')[0];
        ui->comboBox->addItem(friendlyName, i.key());
        if (strcmp(icon_set, i.key().toUtf8().data()) == 0)
        {
            ui->comboBox->setCurrentIndex(ui->comboBox->findData(i.key()));
        }
    }
    ui->comboBox->setItemData(0, '(' + tr("Default") + ')', Qt::DisplayRole);

    ui->comboBoxLanguage->setItemData(0, 0xFFFF);
    for (auto i = lcid_langcode.begin(); i != lcid_langcode.end(); i++)
    {
        if (i.key() == 0xFFFF) continue;
        ui->comboBoxLanguage->addItem(lcid_langcode[i.key()].second, i.key());
        if (i.key() == lang_id)
        {
            ui->comboBoxLanguage->setCurrentIndex(ui->comboBoxLanguage->findData(i.key()));
        }
    }
}

void ProgSettings::accept()
{
    strcpy(icon_set, ui->comboBox->currentData().toString().toUtf8().data());
    lang_id = ui->comboBoxLanguage->currentData().toUInt();

    loadTranslators(QCoreApplication::instance());
    reloadStrings();
    update_mouse_msg();
    main_window->ui->retranslateUi(main_window);
    QString vmname(vm_name);
    if (vmname.at(vmname.size() - 1) == '"' || vmname.at(vmname.size() - 1) == '\'') vmname.truncate(vmname.size() - 1);
    main_window->setWindowTitle(QString("%1 - %2 %3").arg(vmname, EMU_NAME, EMU_VERSION_FULL));
    QString msg = main_window->status->getMessage();
    main_window->status.reset(new MachineStatus(main_window));
    main_window->refreshMediaMenu();
    main_window->status->message(msg);
    connect(main_window, &MainWindow::updateStatusBarTip, main_window->status.get(), &MachineStatus::updateTip);
    connect(main_window, &MainWindow::updateStatusBarActivity, main_window->status.get(), &MachineStatus::setActivity);
    connect(main_window, &MainWindow::updateStatusBarEmpty, main_window->status.get(), &MachineStatus::setEmpty);
    connect(main_window, &MainWindow::statusBarMessage, main_window->status.get(), &MachineStatus::message, Qt::QueuedConnection);
    QDialog::accept();
}

ProgSettings::~ProgSettings()
{
    delete ui;
}

void ProgSettings::on_pushButton_released()
{
    ui->comboBox->setCurrentIndex(0);
}

void ProgSettings::loadTranslators(QObject *parent)
{
    if (qtTranslator)
    {
        QApplication::removeTranslator(qtTranslator);
        qtTranslator = nullptr;
    }
    if (translator)
    {
        QApplication::removeTranslator(translator);
        translator = nullptr;
    }
    qtTranslator = new QTranslator(parent);
    translator = new CustomTranslator(parent);
    QString localetofilename = "";
    if (lang_id == 0xFFFF || lcid_langcode.contains(lang_id) == false)
    {
        for (int i = 0; i < QLocale::system().uiLanguages().size(); i++)
        {
            localetofilename = QLocale::system().uiLanguages()[i];
            if (translator->load(QLatin1String("86box_") + localetofilename, QLatin1String(":/")))
            {
                qDebug() << "Translations loaded.\n";
                QCoreApplication::installTranslator(translator);
                if (!qtTranslator->load(QLatin1String("qtbase_") + localetofilename.replace('-', '_'), QLibraryInfo::location(QLibraryInfo::TranslationsPath)))
                    qtTranslator->load(QLatin1String("qt_") + localetofilename.replace('-', '_'), QApplication::applicationDirPath() + "/./translations/");
                if (QApplication::installTranslator(qtTranslator))
                {
                    qDebug() << "Qt translations loaded." << "\n";
                }
                break;
            }
        }
    }
    else
    {
        translator->load(QLatin1String("86box_") + lcid_langcode[lang_id].first, QLatin1String(":/"));
        QCoreApplication::installTranslator(translator);
        if (!qtTranslator->load(QLatin1String("qtbase_") + QString(lcid_langcode[lang_id].first).replace('-', '_'), QLibraryInfo::location(QLibraryInfo::TranslationsPath)))
            qtTranslator->load(QLatin1String("qt_") + QString(lcid_langcode[lang_id].first).replace('-', '_'), QApplication::applicationDirPath() + "/./translations/");
        QCoreApplication::installTranslator(qtTranslator);
    }
}

void ProgSettings::on_pushButtonLanguage_released()
{
    ui->comboBoxLanguage->setCurrentIndex(0);
}
