#
# 86Box    A hypervisor and IBM PC system emulator that specializes in
#          running old operating systems and software designed for IBM
#          PC systems and compatibles from 1981 through fairly recent
#          system designs based on the PCI bus.
#
#          This file is part of the 86Box distribution.
#
#          CMake build script.
#
# Authors: Jasmine Iwanek, <jriwanek@gmail.com>
#
#          Copyright 2025-2026 Jasmine Iwanek.
#
target_sources(net PRIVATE
    net_browser.c
    net_ipx.c
    net_nbf.c
    net_nbns.c
    net_smb.c
    net_smb_fs.c
    net_smb_rap.c
    net_vserver.c
    net_vserver_eth.c
)

if(ENABLE_VSERVER_FTP)
    target_sources(net PRIVATE net_ftp.c)
endif()

if(ENABLE_VSERVER_TFTP)
    target_sources(net PRIVATE net_tftp.c)
endif()
