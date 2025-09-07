#ifndef _WIN32
#    define SOCKET int
#else
#    include <winsock2.h>
#    include <ws2tcpip.h>
#endif

enum net_socket_types {
    /* Only TCP is supported for now. */
    NET_SOCKET_TCP
};

SOCKET plat_netsocket_create(int type);
SOCKET plat_netsocket_create_server(int type, unsigned short port);
void   plat_netsocket_close(SOCKET socket);

SOCKET plat_netsocket_accept(SOCKET socket);
int    plat_netsocket_connected(SOCKET socket); /* Returns -1 on trouble. */
int    plat_netsocket_connect(SOCKET socket, const char *hostname, unsigned short port);

/* Returns 0 in case of inability to send. -1 in case of errors. */
int plat_netsocket_send(SOCKET socket, const unsigned char *data, unsigned int size, int *wouldblock);
int plat_netsocket_receive(SOCKET socket, unsigned char *data, unsigned int size, int *wouldblock);
