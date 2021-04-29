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

add_library(slirp STATIC tinyglib.c arp_table.c bootp.c cksum.c dnssearch.c if.c ip_icmp.c
	ip_input.c ip_output.c mbuf.c misc.c sbuf.c slirp.c socket.c tcp_input.c
	tcp_output.c tcp_subr.c tcp_timer.c udp.c util.c version.c)

target_link_libraries(slirp wsock32 iphlpapi)