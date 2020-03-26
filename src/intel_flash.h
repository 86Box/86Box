/*
 * 86Box	A hypervisor and IBM PC system emulator that specializes in
 *		running old operating systems and software designed for IBM
 *		PC systems and compatibles from 1981 through fairly recent
 *		system designs based on the PCI bus.
 *
 *		This file is part of the 86Box distribution.
 *
 *		Implementation of the Intel 1 Mbit 8-bit flash devices.
 *
 *
 *
 * Author:	Sarah Walker, <http://pcem-emulator.co.uk/>
 *		Miran Grca, <mgrca8@gmail.com>
 *		Copyright 2008-2019 Sarah Walker.
 *		Copyright 2016-2019 Miran Grca.
 */

extern const device_t intel_flash_bxt_ami_device;
#if defined(DEV_BRANCH) && defined(USE_TC430HX)
extern const device_t intel_flash_bxtw_ami_device;
#endif
extern const device_t intel_flash_bxt_device;
extern const device_t intel_flash_bxb_device;
