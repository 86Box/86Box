/*
 * 86Box	A hypervisor and IBM PC system emulator that specializes in
 *		running old operating systems and software designed for IBM
 *		PC systems and compatibles from 1981 through fairly recent
 *		system designs based on the PCI bus.
 *
 *		This file is part of the 86Box distribution.
 *
 *		Handling of the emulated flash devices.
 *
 *
 *
 * Author:	Miran Grca, <mgrca8@gmail.com>
 *		Copyright 2020 Miran Grca.
 */

#ifndef EMU_FLASH_H
# define EMU_FLASH_H

extern const device_t catalyst_flash_device;

extern const device_t intel_flash_bxt_ami_device;
extern const device_t intel_flash_bxt_device;
extern const device_t intel_flash_bxb_device;

extern const device_t sst_flash_29ee010_device;
extern const device_t sst_flash_29ee020_device;
extern const device_t winbond_flash_w29c020_device;
extern const device_t sst_flash_39sf010_device;
extern const device_t sst_flash_39sf020_device;
extern const device_t sst_flash_39sf040_device;

#endif /*EMU_FLASH_H*/
