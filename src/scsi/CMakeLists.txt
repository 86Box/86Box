#
# 86Box     A hypervisor and IBM PC system emulator that specializes in
#           running old operating systems and software designed for IBM
#           PC systems and compatibles from 1981 through fairly recent
#           system designs based on the PCI bus.
#
#           This file is part of the 86Box distribution.
#
#           CMake build script.
#
# Authors:  David Hrdlička, <hrdlickadavid@outlook.com>
#
#           Copyright 2020,2021 David Hrdlička.
#

add_library(scsi OBJECT scsi.c scsi_device.c scsi_cdrom.c scsi_disk.c
    scsi_x54x.c scsi_aha154x.c scsi_buslogic.c scsi_ncr5380.c
    scsi_ncr53c8xx.c scsi_pcscsi.c scsi_spock.c)
