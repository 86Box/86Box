#ifndef EMU_NET_EVENT_H
#define EMU_NET_EVENT_H

typedef struct {
#ifdef _WIN32
    HANDLE handle;
#else
    int fds[2];
#endif
} net_evt_t;

extern void net_event_init(net_evt_t *event);
extern void net_event_set(net_evt_t *event);
extern void net_event_clear(net_evt_t *event);
extern void net_event_close(net_evt_t *event);
#ifdef _WIN32
extern HANDLE net_event_get_handle(net_evt_t *event);
#else
extern int net_event_get_fd(net_evt_t *event);
#endif

#endif