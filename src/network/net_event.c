#ifdef _WIN32
#    define WIN32_LEAN_AND_MEAN
#    include <windows.h>
#else
#    include <unistd.h>
#    include <fcntl.h>
#endif

#include <86box/net_event.h>

#ifndef _WIN32
static void
setup_fd(int fd)
{
    fcntl(fd, F_SETFD, FD_CLOEXEC);
    fcntl(fd, F_SETFL, O_NONBLOCK);
}
#endif

void
net_event_init(net_evt_t *event)
{
#ifdef _WIN32
    event->handle = CreateEvent(NULL, FALSE, FALSE, NULL);
#else
    (void) !pipe(event->fds);
    setup_fd(event->fds[0]);
    setup_fd(event->fds[1]);
#endif
}

void
net_event_set(net_evt_t *event)
{
#ifdef _WIN32
    SetEvent(event->handle);
#else
    (void) !write(event->fds[1], "a", 1);
#endif
}

void
net_event_clear(net_evt_t *event)
{
#ifdef _WIN32
    /* Do nothing on WIN32 since we use an auto-reset event */
#else
    char dummy[1];
    (void) !read(event->fds[0], &dummy, sizeof(dummy));
#endif
}

void
net_event_close(net_evt_t *event)
{
#ifdef _WIN32
    CloseHandle(event->handle);
#else
    close(event->fds[0]);
    close(event->fds[1]);
#endif
}

#ifdef _WIN32
HANDLE
net_event_get_handle(net_evt_t *event)
{
    return event->handle;
}
#else
int
net_event_get_fd(net_evt_t *event)
{
    return event->fds[0];
}
#endif