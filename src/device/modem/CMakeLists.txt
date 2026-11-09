#
# 86Box    A hypervisor and IBM PC system emulator that specializes in
#          running old operating systems and IBM PC systems and compatibles
#          from 1981 through fairly recent system designs.
#
#          This file is part of the 86Box distribution.
#
#          CMake build script for the emulated modem.
#
# Authors: Jasmine Iwanek, <jriwanek@gmail.com>
#
#          Copyright 2025-2026 Jasmine Iwanek.
#

target_sources(dev PRIVATE
    modem_crypto.c
    modem_ppp.c
    modem_ras.c
    modem_pptp.c
    modem_lapb.c
    modem_pap.c
    modem_chap.c
    modem_ccp.c
    modem_ccp_codec.c
    modem_mppe.c
    modem_eap.c
    modem_ipcp.c
    modem_ipxcp.c
    modem_ipv6cp.c
    modem_nbfcp.c
    modem_cslip.c
    modem_mppp.c
    modem_slip.c
    modem_slip_auth.c
    modem_sound.c
    modem_voice.c
    modem_profile.c
    modem_pnp.c
    modem_flow.c
)

if(ENABLE_MODEM_LOG)
    target_compile_definitions(dev PRIVATE ENABLE_MODEM_LOG)
endif()

if(ENABLE_MODEM_DEBUG)
    target_compile_definitions(dev PRIVATE ENABLE_MODEM_DEBUG)
    target_compile_definitions(dev PRIVATE ENABLE_SLIRP_LOG=1)
endif()
