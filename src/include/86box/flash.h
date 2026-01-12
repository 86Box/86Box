/*
 * 86Box    A hypervisor and IBM PC system emulator that specializes in
 *          running old operating systems and software designed for IBM
 *          PC systems and compatibles from 1981 through fairly recent
 *          system designs based on the PCI bus.
 *
 *          This file is part of the 86Box distribution.
 *
 *          Handling of the emulated flash devices.
 *
 * Authors: Miran Grca, <mgrca8@gmail.com>
 *          Jasmine Iwanek, <jriwanek@gmail.com>
 *
 *          Copyright 2020      Miran Grca.
 *          Copyright 2022-2023 Jasmine Iwanek.
 */
#ifndef EMU_FLASH_H
#define EMU_FLASH_H

extern const device_t amd_am28f010_flash_device;
extern const device_t catalyst_flash_device;

extern const device_t intel_flash_bxt_ami_device;
extern const device_t intel_flash_bxt_device;
extern const device_t intel_flash_bxb_device;

extern const device_t sst_flash_29ee010_device;
extern const device_t sst_flash_29ee020_device;

extern const device_t winbond_flash_w29c512_device;
extern const device_t winbond_flash_w29c010_device;
extern const device_t winbond_flash_w29c011a_device;
extern const device_t winbond_flash_w29c020_device;
extern const device_t winbond_flash_w29c040_device;

extern const device_t sst_flash_39sf512_device;
extern const device_t sst_flash_39sf010_device;
extern const device_t sst_flash_39sf020_device;
extern const device_t sst_flash_39sf040_device;

extern const device_t sst_flash_39lf512_device;
extern const device_t sst_flash_39lf010_device;
extern const device_t sst_flash_39lf020_device;
extern const device_t sst_flash_39lf040_device;
extern const device_t sst_flash_39lf080_device;
extern const device_t sst_flash_39lf016_device;

extern const device_t sst_flash_49lf002_device;
extern const device_t sst_flash_49lf020_device;
extern const device_t sst_flash_49lf020a_device;
extern const device_t sst_flash_49lf003_device;
extern const device_t sst_flash_49lf030_device;
extern const device_t sst_flash_49lf004_device;
extern const device_t sst_flash_49lf004c_device;
extern const device_t sst_flash_49lf040_device;
extern const device_t sst_flash_49lf008_device;
extern const device_t sst_flash_49lf008c_device;
extern const device_t sst_flash_49lf080_device;
extern const device_t sst_flash_49lf016_device;
extern const device_t sst_flash_49lf160_device;

extern const device_t amd_flash_29f020a_device;

#endif /*EMU_FLASH_H*/
