#include "qt_progsettings.hpp"
#include "ui_qt_progsettings.h"
#include "qt_mainwindow.hpp"
#include "ui_qt_mainwindow.h"
#include "qt_machinestatus.hpp"

#include <QMap>
#include <QDir>
#include <QFile>

extern "C"
{
#include <86box/86box.h>
#include <86box/config.h>
#include <86box/plat.h>
}


static QMap<QString, QString> iconset_to_qt;
extern MainWindow* main_window;

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
}

void ProgSettings::accept()
{
    strcpy(icon_set, ui->comboBox->currentData().toString().toUtf8().data());

    QString msg = main_window->status->getMessage();
    main_window->status.reset(new MachineStatus(main_window));
    main_window->refreshMediaMenu();
    main_window->status->message(msg);
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

