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

#include <winsock2.h>
#include <windows.h>
#include <ws2tcpip.h>
#include <winerror.h>

SOCKET
plat_netsocket_create(int type)
{
    SOCKET socket = -1;
    u_long yes    = 1;

    if (type != NET_SOCKET_TCP)
        return -1;

    socket = WSASocketA(AF_INET, SOCK_STREAM, IPPROTO_TCP, NULL, 0, WSA_FLAG_OVERLAPPED);
    if (socket == INVALID_SOCKET)
        return -1;

    ioctlsocket(socket, FIONBIO, &yes);

    return socket;
}

SOCKET
plat_netsocket_create_server(int type, unsigned short port)
{
    struct sockaddr_in sock_addr;
    SOCKET             socket = -1;
    u_long             yes    = 1;

    if (type != NET_SOCKET_TCP)
        return -1;

    socket = WSASocketA(AF_INET, SOCK_STREAM, IPPROTO_TCP, NULL, 0, WSA_FLAG_OVERLAPPED);
    if (socket == INVALID_SOCKET)
        return -1;

    memset(&sock_addr, 0, sizeof(struct sockaddr_in));

    sock_addr.sin_family      = AF_INET;
    sock_addr.sin_addr.s_addr = INADDR_ANY;
    sock_addr.sin_port        = htons(port);

    if (bind(socket, (struct sockaddr *) &sock_addr, sizeof(struct sockaddr_in)) == SOCKET_ERROR) {
        plat_netsocket_close(socket);
        return (SOCKET) -1;
    }

    if (listen(socket, 5) == SOCKET_ERROR) {
        plat_netsocket_close(socket);
        return (SOCKET) -1;
    }

    ioctlsocket(socket, FIONBIO, &yes);

    return socket;
}

void
plat_netsocket_close(SOCKET socket)
{
    closesocket((SOCKET) socket);
}

SOCKET
plat_netsocket_accept(SOCKET socket)
{
    SOCKET clientsocket = accept(socket, NULL, NULL);
    u_long yes          = 1;

    if (clientsocket == INVALID_SOCKET)
        return -1;

    ioctlsocket(clientsocket, FIONBIO, &yes);
    return clientsocket;
}

int
plat_netsocket_connected(SOCKET socket)
{
    struct sockaddr addr;
    socklen_t       len = sizeof(struct sockaddr);
    fd_set          wrfds, exfds;
    struct timeval  tv;
    int             res    = SOCKET_ERROR;
    int             status = 0;
    int             optlen = 4;

    FD_ZERO(&wrfds);
    FD_ZERO(&exfds);
    FD_SET(socket, &wrfds);
    FD_SET(socket, &exfds);

    tv.tv_sec  = 0;
    tv.tv_usec = 0;

    res = select(socket + 1, NULL, &wrfds, &exfds, &tv);

    if (res == SOCKET_ERROR)
        return -1;

    if (res >= 1 && FD_ISSET(socket, &exfds)) {
        res = getsockopt(socket, SOL_SOCKET, SO_ERROR, (char *) &status, &optlen);
        pclog("Socket error %d\n", status);
        return -1;
    }

    if (res == 0 || !(res >= 1 && FD_ISSET(socket, &wrfds)))
        return 0;

    res = getsockopt(socket, SOL_SOCKET, SO_ERROR, (char *) &status, &optlen);

    if (res == SOCKET_ERROR)
        return -1;

    if (status != 0)
        return -1;

    if (getpeername(socket, &addr, &len) == SOCKET_ERROR)
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

    if (sock_addr.sin_addr.s_addr == INADDR_ANY || sock_addr.sin_addr.s_addr == INADDR_NONE) {
        struct hostent *hp;

        hp = gethostbyname(hostname);

        if (hp)
            memcpy(&sock_addr.sin_addr.s_addr, hp->h_addr_list[0], hp->h_length);
        else
            return -1;
    }

    res = connect(socket, (struct sockaddr *) &sock_addr, sizeof(struct sockaddr_in));

    if (res == SOCKET_ERROR) {
        int error = WSAGetLastError();

        if (error == WSAEISCONN || error == WSAEWOULDBLOCK)
            return 0;

        res = -1;
    }
    return res;
}

int
plat_netsocket_send(SOCKET socket, const unsigned char *data, unsigned int size, int *wouldblock)
{
    int res = send(socket, (const char *) data, size, 0);

    if (res == SOCKET_ERROR) {
        int error = WSAGetLastError();

        if (wouldblock)
            *wouldblock = !!(error == WSAEWOULDBLOCK);

        return -1;
    }
    return res;
}

int
plat_netsocket_receive(SOCKET socket, unsigned char *data, unsigned int size, int *wouldblock)
{
    int res = recv(socket, (char *) data, size, 0);

    if (res == SOCKET_ERROR) {
        int error = WSAGetLastError();

        if (wouldblock)
            *wouldblock = !!(error == WSAEWOULDBLOCK);

        return -1;
    }
    return res;
}
