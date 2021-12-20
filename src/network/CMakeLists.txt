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

add_library(net OBJECT network.c net_pcap.c net_slirp.c net_dp8390.c net_3c503.c
    net_ne2000.c net_pcnet.c net_wd8003.c net_plip.c)

add_subdirectory(slirp)
target_link_libraries(86Box slirp)