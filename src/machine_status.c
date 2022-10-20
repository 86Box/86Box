#include <inttypes.h>
#include <stdlib.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>

#include <86box/86box.h>
#include <86box/plat.h>
#include <86box/ui.h>
#include <86box/timer.h>
#include <86box/device.h>
#include <86box/fdd.h>
#include <86box/hdc.h>
#include <86box/scsi.h>
#include <86box/scsi_device.h>
#include <86box/cartridge.h>
#include <86box/cassette.h>
#include <86box/cdrom.h>
#include <86box/zip.h>
#include <86box/mo.h>
#include <86box/hdd.h>
#include <86box/thread.h>
#include <86box/network.h>
#include <86box/machine_status.h>

machine_status_t machine_status;

void
machine_status_init()
{
    for (size_t i = 0; i < FDD_NUM; ++i) {
        machine_status.fdd[i].empty  = (strlen(floppyfns[i]) == 0);
        machine_status.fdd[i].active = false;
    }
    for (size_t i = 0; i < CDROM_NUM; ++i) {
        machine_status.cdrom[i].empty  = cdrom[i].host_drive != 200 || (strlen(cdrom[i].image_path) == 0);
        machine_status.cdrom[i].active = false;
    }
    for (size_t i = 0; i < ZIP_NUM; i++) {
        machine_status.zip[i].empty  = (strlen(zip_drives[i].image_path) == 0);
        machine_status.zip[i].active = false;
    }
    for (size_t i = 0; i < MO_NUM; i++) {
        machine_status.mo[i].empty  = (strlen(mo_drives[i].image_path) == 0);
        machine_status.mo[i].active = false;
    }

    machine_status.cassette.empty = (strlen(cassette_fname) == 0);

    for (size_t i = 0; i < HDD_BUS_USB; i++) {
        machine_status.hdd[i].active = false;
    }

    for (size_t i = 0; i < NET_CARD_MAX; i++) {
        machine_status.net[i].active = false;
        machine_status.net[i].empty  = !network_is_connected(i);
    }
}