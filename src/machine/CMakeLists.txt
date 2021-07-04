#
# 86Box		A hypervisor and IBM PC system emulator that specializes in
#		running old operating systems and software designed for IBM
#		PC systems and compatibles from 1981 through fairly recent
#		system designs based on the PCI bus.
#
#		This file is part of the 86Box distribution.
#
#		CMake build script.
#
# Authors:	David Hrdlička, <hrdlickadavid@outlook.com>
#
#		Copyright 2020,2021 David Hrdlička.
#

add_library(mch OBJECT machine.c machine_table.c m_xt.c m_xt_compaq.c
	m_xt_philips.c
	m_xt_t1000.c m_xt_t1000_vid.c m_xt_xi8088.c m_xt_zenith.c m_pcjr.c
	m_amstrad.c m_europc.c m_xt_olivetti.c m_tandy.c m_at.c m_at_commodore.c
	m_at_t3100e.c m_at_t3100e_vid.c m_ps1.c m_ps1_hdc.c m_ps2_isa.c
	m_ps2_mca.c m_at_compaq.c m_at_286_386sx.c m_at_386dx_486.c
	m_at_socket4_5.c m_at_socket7.c m_at_sockets7.c m_at_socket8.c
	m_at_slot1.c m_at_slot2.c m_at_socket370.c m_at_misc.c)

if(HEDAKA)
	target_compile_definitions(mch PRIVATE USE_HEDAKA)
endif()

if(I450KX)
	target_compile_definitions(mch PRIVATE USE_I450KX)
endif()

if(LASERXT)
	target_sources(mch PRIVATE m_xt_laserxt.c)
	target_compile_definitions(mch PRIVATE USE_LASERXT)
endif()

if(NO_SIO)
	target_compile_definitions(mch PRIVATE NO_SIO)
endif()

if(OPEN_AT)
	target_compile_definitions(mch PRIVATE USE_OPEN_AT)
endif()

if(M154X)
	target_compile_definitions(mch PRIVATE USE_M154X)
endif()

if(M6117)
	target_compile_definitions(mch PRIVATE USE_M6117)
endif()