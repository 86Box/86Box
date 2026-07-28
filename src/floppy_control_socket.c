#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/un.h>
#include <unistd.h>

#include <cJSON.h>
#include <86box/86box.h>
#include <86box/timer.h>
#include <86box/fdd.h>
#include <86box/floppy_control_socket.h>
#include <86box/thread.h>

#define FCS_MAX_FRAME 65536

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

static cJSON *
fcs_create_drive_status(int drive)
{
    cJSON *obj = cJSON_CreateObject();

    cJSON_AddNumberToObject(obj, "drive", drive);
    cJSON_AddBoolToObject(obj, "inserted", fdd_runtime_inserted(drive));
    if (fdd_runtime_inserted(drive))
        cJSON_AddStringToObject(obj, "path", floppyfns[drive]);
    else
        cJSON_AddNullToObject(obj, "path");
    cJSON_AddBoolToObject(obj, "read_only", fdd_get_effective_read_only(drive));
    cJSON_AddBoolToObject(obj, "busy", fdd_is_busy(drive));
    cJSON_AddBoolToObject(obj, "changed", !!fdd_changed[drive]);

    return obj;
}

static cJSON *
fcs_success_response(const cJSON *id, cJSON *result)
{
    cJSON *response = cJSON_CreateObject();

    if (cJSON_IsString(id))
        cJSON_AddStringToObject(response, "id", id->valuestring);
    else
        cJSON_AddNullToObject(response, "id");
    cJSON_AddBoolToObject(response, "ok", 1);
    cJSON_AddItemToObject(response, "result", result);

    return response;
}

static cJSON *
fcs_error_response(const cJSON *id, const char *code, const char *message)
{
    cJSON *response = cJSON_CreateObject();
    cJSON *error    = cJSON_CreateObject();

    if (cJSON_IsString(id))
        cJSON_AddStringToObject(response, "id", id->valuestring);
    else
        cJSON_AddNullToObject(response, "id");
    cJSON_AddBoolToObject(response, "ok", 0);
    cJSON_AddStringToObject(error, "code", code);
    cJSON_AddStringToObject(error, "message", message);
    cJSON_AddItemToObject(response, "error", error);

    return response;
}

static int
fcs_get_drive(cJSON *params, int required, int *drive)
{
    cJSON *drive_json = cJSON_GetObjectItemCaseSensitive(params, "drive");

    if (!drive_json)
        return required ? -1 : 1;
    if (!cJSON_IsNumber(drive_json) || drive_json->valuedouble != drive_json->valueint)
        return -1;
    if (drive_json->valueint < 0 || drive_json->valueint >= FDD_NUM)
        return -1;

    *drive = drive_json->valueint;
    return 0;
}

static cJSON *
fcs_handle_status(const cJSON *id, cJSON *params)
{
    int drive        = -1;
    int drive_status = fcs_get_drive(params, 0, &drive);

    if (drive_status < 0)
        return fcs_error_response(id, "invalid_drive", "Drive must be an integer from 0 through 3.");

    if (drive_status == 0)
        return fcs_success_response(id, fcs_create_drive_status(drive));

    cJSON *result      = cJSON_CreateObject();
    cJSON *drives_json = cJSON_CreateArray();
    for (int i = 0; i < FDD_NUM; i++)
        cJSON_AddItemToArray(drives_json, fcs_create_drive_status(i));
    cJSON_AddItemToObject(result, "drives", drives_json);

    return fcs_success_response(id, result);
}

static cJSON *
fcs_process_request(cJSON *request)
{
    cJSON *id      = cJSON_GetObjectItemCaseSensitive(request, "id");
    cJSON *command = cJSON_GetObjectItemCaseSensitive(request, "command");
    cJSON *params  = cJSON_GetObjectItemCaseSensitive(request, "params");

    if (!cJSON_IsString(id) || !cJSON_IsString(command) || !cJSON_IsObject(params))
        return fcs_error_response(id, "internal_failure", "Request must include string id, string command, and object params.");

    if (!strcmp(command->valuestring, "floppy.status"))
        return fcs_handle_status(id, params);

    return fcs_error_response(id, "internal_failure", "Unknown command.");
}

static int
fcs_send_json_line(int fd, cJSON *response)
{
    char  *json = cJSON_PrintUnformatted(response);
    size_t len;

    if (!json)
        return -1;
    len = strlen(json);
    if (send(fd, json, len, 0) < 0 || send(fd, "\n", 1, 0) < 0) {
        cJSON_free(json);
        return -1;
    }
    cJSON_free(json);
    return 0;
}

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
    char   frame[FCS_MAX_FRAME + 1];
    size_t used = 0;

    while (!fcs.stopping) {
        char    ch;
        ssize_t got = recv(client_fd, &ch, 1, 0);
        if (got <= 0)
            return;

        if (used >= FCS_MAX_FRAME) {
            cJSON *err = fcs_error_response(NULL, "internal_failure", "Request frame exceeds 64 KiB.");
            fcs_send_json_line(client_fd, err);
            cJSON_Delete(err);
            return;
        }

        frame[used++] = ch;
        if (ch != '\n')
            continue;

        frame[used - 1] = '\0';
        cJSON *request = cJSON_Parse(frame);
        if (!request || !cJSON_IsObject(request)) {
            cJSON *err = fcs_error_response(NULL, "internal_failure", "Malformed JSON frame.");
            fcs_send_json_line(client_fd, err);
            cJSON_Delete(err);
            cJSON_Delete(request);
            return;
        }

        thread_wait_mutex(fcs.state_mutex);
        cJSON *response = fcs_process_request(request);
        thread_release_mutex(fcs.state_mutex);

        fcs_send_json_line(client_fd, response);
        cJSON_Delete(response);
        cJSON_Delete(request);
        used = 0;
    }
}
