#define _XOPEN_SOURCE 500
#include <stdio.h>
#include <fcntl.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <wchar.h>

#include <86box/86box.h>
#include <86box/log.h>
#include <86box/timer.h>
#include <86box/plat.h>
#include <86box/device.h>
#include <86box/plat_netsocket.h>
#include <86box/ui.h>

#include <arpa/inet.h>
#include <errno.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <netdb.h>
#include <sys/time.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/select.h>
#include <unistd.h>

SOCKET
plat_netsocket_create(int type)
{
    SOCKET fd  = -1;
    int    yes = 1;

    if (type != NET_SOCKET_TCP)
        return -1;

    fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd == -1)
        return -1;

    fcntl(fd, F_SETFL, fcntl(fd, F_GETFL, 0) | O_NONBLOCK);
    setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, (char *) &yes, sizeof(yes));

    return fd;
}

SOCKET
plat_netsocket_create_server(int type, unsigned short port)
{
    struct sockaddr_in sock_addr;
    SOCKET             fd  = -1;
    int                yes = 1;

    if (type != NET_SOCKET_TCP)
        return -1;

    fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd == -1)
        return -1;

    memset(&sock_addr, 0, sizeof(struct sockaddr_in));

    sock_addr.sin_family      = AF_INET;
    sock_addr.sin_addr.s_addr = INADDR_ANY;
    sock_addr.sin_port        = htons(port);

    if (bind(fd, (struct sockaddr *) &sock_addr, sizeof(struct sockaddr_in)) == -1) {
        plat_netsocket_close(fd);
        return (SOCKET) -1;
    }

    if (listen(fd, 5) == -1) {
        plat_netsocket_close(fd);
        return (SOCKET) -1;
    }

    fcntl(fd, F_SETFL, fcntl(fd, F_GETFL, 0) | O_NONBLOCK);
    setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, (char *) &yes, sizeof(yes));

    return fd;
}

void
plat_netsocket_close(SOCKET socket)
{
    close((SOCKET) socket);
}

SOCKET
plat_netsocket_accept(SOCKET socket)
{
    SOCKET clientsocket = accept(socket, NULL, NULL);

    if (clientsocket == -1)
        return -1;

    fcntl(clientsocket, F_SETFL, fcntl(clientsocket, F_GETFL, 0) | O_NONBLOCK);

    return clientsocket;
}

int
plat_netsocket_connected(SOCKET socket)
{
    struct sockaddr addr;
    socklen_t       len = sizeof(struct sockaddr);
    fd_set          wrfds;
    struct timeval  tv;
    int             res    = -1;
    int             status = 0;
    socklen_t       optlen = 4;

    FD_ZERO(&wrfds);
    FD_SET(socket, &wrfds);

    tv.tv_sec  = 0;
    tv.tv_usec = 0;

    res = select(socket + 1, NULL, &wrfds, NULL, &tv);

    if (res == -1)
        return -1;

    if (res == 0 || !(res >= 1 && FD_ISSET(socket, &wrfds)))
        return 0;

    res = getsockopt(socket, SOL_SOCKET, SO_ERROR, (char *) &status, &optlen);

    if (res == -1)
        return -1;

    if (status != 0)
        return -1;

    if (getpeername(socket, &addr, &len) == -1)
        return -1;

    return 1;
}

int
plat_netsocket_connect(SOCKET socket, const char *hostname, unsigned short port)
{
    struct sockaddr_in sock_addr;
    int                res = -1;

    sock_addr.sin_family      = AF_INET;
    sock_addr.sin_addr.s_addr = inet_addr(hostname);
    sock_addr.sin_port        = htons(port);

    if (sock_addr.sin_addr.s_addr == ((in_addr_t) -1) || sock_addr.sin_addr.s_addr == 0) {
        struct hostent *hp;

        hp = gethostbyname(hostname);

        if (hp)
            memcpy(&sock_addr.sin_addr.s_addr, hp->h_addr_list[0], hp->h_length);
        else
            return -1;
    }

    res = connect(socket, (struct sockaddr *) &sock_addr, sizeof(struct sockaddr_in));

    if (res == -1) {
        int error = errno;

        if (error == EISCONN || error == EWOULDBLOCK || error == EAGAIN || error == EINPROGRESS)
            return 0;

        res = -1;
    }
    return res;
}

int
plat_netsocket_send(SOCKET socket, const unsigned char *data, unsigned int size, int *wouldblock)
{
    int res = send(socket, (const char *) data, size, 0);

    if (res == -1) {
        int error = errno;

        if (wouldblock)
            *wouldblock = !!(error == EWOULDBLOCK || error == EAGAIN);

        return -1;
    }
    return res;
}

int
plat_netsocket_receive(SOCKET socket, unsigned char *data, unsigned int size, int *wouldblock)
{
    int res = recv(socket, (char *) data, size, 0);

    if (res == -1) {
        int error = errno;

        if (wouldblock)
            *wouldblock = !!(error == EWOULDBLOCK || error == EAGAIN);

        return -1;
    }
    return res;
}
