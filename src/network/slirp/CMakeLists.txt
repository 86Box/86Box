add_library(slirp STATIC arp_table.c bootp.c cksum.c dnssearch.c if.c ip_icmp.c
	ip_input.c ip_output.c mbuf.c misc.c sbuf.c slirp.c socket.c tcp_input.c
	tcp_output.c tcp_subr.c tcp_timer.c udp.c util.c version.c)

target_link_libraries(slirp wsock32 iphlpapi)