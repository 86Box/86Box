add_library(net OBJECT network.c net_pcap.c net_slirp.c net_dp8390.c net_3c503.c
	net_ne2000.c net_pcnet.c net_wd8003.c net_plip.c)

add_subdirectory(slirp)
target_link_libraries(86Box slirp)