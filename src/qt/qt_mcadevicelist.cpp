#include "qt_mcadevicelist.hpp"
#include "ui_qt_mcadevicelist.h"

extern "C"
{
#include <86box/86box.h>
#include <86box/video.h>
#include <86box/mca.h>
#include <86box/plat.h>
}

MCADeviceList::MCADeviceList(QWidget *parent) :
    QDialog(parent),
    ui(new Ui::MCADeviceList)
{
    ui->setupUi(this);

    startblit();
    if (mca_get_nr_cards() == 0)
    {
        ui->listWidget->addItem(QObject::tr("No MCA devices."));
        ui->listWidget->setDisabled(true);
    }
    else
    {
        for (int i = 0; i < mca_get_nr_cards(); i++)
        {
            uint32_t deviceId = (mca_read_index(0x00, i) | (mca_read_index(0x01, i) << 8));
            if (deviceId != 0xFFFF)
            {
                QString hexRepresentation = QString::number(deviceId, 16).toUpper();
                ui->listWidget->addItem(QString("Slot %1: 0x%2 (@%3.ADF)").arg(i + 1).arg(hexRepresentation, hexRepresentation));
            }
        }
    }
    endblit();
}

MCADeviceList::~MCADeviceList()
{
    delete ui;
}
