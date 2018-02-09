/*
 * 86Box	A hypervisor and IBM PC system emulator that specializes in
 *		running old operating systems and software designed for IBM
 *		PC systems and compatibles from 1981 through fairly recent
 *		system designs based on the PCI bus.
 *
 *		This file is part of the 86Box distribution.
 *
 *		Emulation of the S3 Trio32, S3 Trio64, and S3 Vision864
 *		graphics cards.
 *
 * Version:	@(#)vid_s3.h	1.0.1	2018/02/09
 *
 * Author:	Sarah Walker, <http://pcem-emulator.co.uk/>
 *		Miran Grca, <mgrca8@gmail.com>
 *		Copyright 2008-2018 Sarah Walker.
 *		Copyright 2016-2018 Miran Grca.
 */

device_t s3_bahamas64_vlb_device;
device_t s3_bahamas64_pci_device;
device_t s3_9fx_vlb_device;
device_t s3_9fx_pci_device;
device_t s3_phoenix_trio32_vlb_device;
device_t s3_phoenix_trio32_pci_device;
device_t s3_phoenix_trio64_vlb_device;
device_t s3_phoenix_trio64_onboard_pci_device;
device_t s3_phoenix_trio64_pci_device;
device_t s3_phoenix_vision864_pci_device;
device_t s3_phoenix_vision864_vlb_device;
device_t s3_diamond_stealth64_pci_device;
device_t s3_diamond_stealth64_vlb_device;
/* device_t s3_miro_vision964_device; */
