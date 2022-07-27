/*
 * 86Box	A hypervisor and IBM PC system emulator that specializes in
 *		running old operating systems and software designed for IBM
 *		PC systems and compatibles from 1981 through fairly recent
 *		system designs based on the PCI bus.
 *
 *		This file is part of the 86Box distribution.
 *
 *		Handle the platform-side of CDROM/ZIP/MO drives.
 *
 *
 *
 * Authors:	Sarah Walker, <http://pcem-emulator.co.uk/>
 *		Miran Grca, <mgrca8@gmail.com>
 *		Fred N. van Kempen, <decwiz@yahoo.com>
 *
 *		Copyright 2016-2018 Miran Grca.
 *		Copyright 2017,2018 Fred N. van Kempen.
 */

#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <stdlib.h>
#include <wchar.h>
#include <86box/86box.h>
#include <86box/config.h>
#include <86box/timer.h>
#include <86box/device.h>
#include <86box/cassette.h>
#include <86box/cartridge.h>
#include <86box/fdd.h>
#include <86box/hdd.h>
#include <86box/scsi_device.h>
#include <86box/cdrom.h>
#include <86box/mo.h>
#include <86box/zip.h>
#include <86box/scsi_disk.h>
#include <86box/plat.h>
#include <86box/ui.h>

void
plat_cdrom_ui_update(uint8_t id, uint8_t reload)
{
    cdrom_t *drv = &cdrom[id];

    if (drv->host_drive == 0) {
	ui_sb_update_icon_state(SB_CDROM|id, 1);
    } else {
	ui_sb_update_icon_state(SB_CDROM|id, 0);
    }

    //media_menu_update_cdrom(id);
    ui_sb_update_tip(SB_CDROM|id);
}
