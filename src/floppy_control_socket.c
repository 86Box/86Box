#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/un.h>
#include <unistd.h>

#include <86box/86box.h>
#include <86box/floppy_control_socket.h>
#include <86box/thread.h>

typedef struct floppy_control_server_t {
    int       server_fd;
    int       stopping;
    char      socket_path[1024];
    thread_t *thread;
    mutex_t  *state_mutex;
} floppy_control_server_t;

static floppy_control_server_t fcs = {
    .server_fd = -1
};

static void fcs_handle_client(int client_fd);

static int
fcs_create_server_socket(const char *path)
{
    struct sockaddr_un addr;
    int                fd;

    if (!path || path[0] == '\0')
        return -1;

    if (strlen(path) >= sizeof(addr.sun_path)) {
        pclog("Floppy control socket path too long: %s\n", path);
        return -1;
    }

    fd = socket(AF_UNIX, SOCK_STREAM, 0);
    if (fd < 0) {
        pclog("Floppy control socket: socket failed: %s\n", strerror(errno));
        return -1;
    }

    unlink(path);
    memset(&addr, 0, sizeof(addr));
    addr.sun_family = AF_UNIX;
    strncpy(addr.sun_path, path, sizeof(addr.sun_path) - 1);

    if (bind(fd, (struct sockaddr *) &addr, sizeof(addr)) < 0) {
        pclog("Floppy control socket: bind %s failed: %s\n", path, strerror(errno));
        close(fd);
        return -1;
    }

    chmod(path, 0600);

    if (listen(fd, 4) < 0) {
        pclog("Floppy control socket: listen failed: %s\n", strerror(errno));
        close(fd);
        unlink(path);
        return -1;
    }

    return fd;
}

static void
fcs_server_thread(void *param)
{
    floppy_control_server_t *server = (floppy_control_server_t *) param;

    while (!server->stopping) {
        int client_fd = accept(server->server_fd, NULL, NULL);
        if (client_fd < 0) {
            if (!server->stopping)
                pclog("Floppy control socket: accept failed: %s\n", strerror(errno));
            continue;
        }

        fcs_handle_client(client_fd);
        close(client_fd);
    }
}

void
floppy_control_socket_init(void)
{
    if (!floppy_control_socket_enabled)
        return;

    memset(&fcs, 0, sizeof(fcs));
    fcs.server_fd = -1;
    strncpy(fcs.socket_path, floppy_control_socket_path, sizeof(fcs.socket_path) - 1);

    fcs.state_mutex = thread_create_mutex();
    fcs.server_fd = fcs_create_server_socket(fcs.socket_path);
    if (fcs.server_fd < 0) {
        thread_close_mutex(fcs.state_mutex);
        fcs.state_mutex = NULL;
        return;
    }

    fcs.thread = thread_create(fcs_server_thread, &fcs);
}

void
floppy_control_socket_close(void)
{
    if (fcs.server_fd < 0)
        return;

    fcs.stopping = 1;
    shutdown(fcs.server_fd, SHUT_RDWR);
    close(fcs.server_fd);
    fcs.server_fd = -1;

    if (fcs.thread) {
        thread_wait(fcs.thread);
        free(fcs.thread);
        fcs.thread = NULL;
    }

    if (fcs.socket_path[0])
        unlink(fcs.socket_path);

    if (fcs.state_mutex) {
        thread_close_mutex(fcs.state_mutex);
        fcs.state_mutex = NULL;
    }
}

static void
fcs_handle_client(int client_fd)
{
    (void) client_fd;
}
