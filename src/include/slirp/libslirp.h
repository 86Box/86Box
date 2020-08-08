/* SPDX-License-Identifier: BSD-3-Clause */
#ifndef LIBSLIRP_H
#define LIBSLIRP_H

#include <stdint.h>
#include <stdbool.h>
#include <sys/types.h>

#ifdef _WIN32
#include <winsock2.h>
#include <in6addr.h>
#else
#include <netinet/in.h>
#include <arpa/inet.h>
#endif

#include "libslirp-version.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct Slirp Slirp;

enum {
    SLIRP_POLL_IN = 1 << 0,
    SLIRP_POLL_OUT = 1 << 1,
    SLIRP_POLL_PRI = 1 << 2,
    SLIRP_POLL_ERR = 1 << 3,
    SLIRP_POLL_HUP = 1 << 4,
};

typedef ssize_t (*SlirpReadCb)(void *buf, size_t len, void *opaque);
typedef ssize_t (*SlirpWriteCb)(const void *buf, size_t len, void *opaque);
typedef void (*SlirpTimerCb)(void *opaque);
typedef int (*SlirpAddPollCb)(int fd, int events, void *opaque);
typedef int (*SlirpGetREventsCb)(int idx, void *opaque);

/*
 * Callbacks from slirp
 */
typedef struct SlirpCb {
    /*
     * Send an ethernet frame to the guest network. The opaque
     * parameter is the one given to slirp_init(). The function
     * doesn't need to send all the data and may return <len (no
     * buffering is done on libslirp side, so the data will be dropped
     * in this case). <0 reports an IO error.
     */
    SlirpWriteCb send_packet;
    /* Print a message for an error due to guest misbehavior.  */
    void (*guest_error)(const char *msg, void *opaque);
    /* Return the virtual clock value in nanoseconds */
    int64_t (*clock_get_ns)(void *opaque);
    /* Create a new timer with the given callback and opaque data */
    void *(*timer_new)(SlirpTimerCb cb, void *cb_opaque, void *opaque);
    /* Remove and free a timer */
    void (*timer_free)(void *timer, void *opaque);
    /* Modify a timer to expire at @expire_time */
    void (*timer_mod)(void *timer, int64_t expire_time, void *opaque);
    /* Register a fd for future polling */
    void (*register_poll_fd)(int fd, void *opaque);
    /* Unregister a fd */
    void (*unregister_poll_fd)(int fd, void *opaque);
    /* Kick the io-thread, to signal that new events may be processed */
    void (*notify)(void *opaque);
} SlirpCb;

#define SLIRP_CONFIG_VERSION_MIN 1
#define SLIRP_CONFIG_VERSION_MAX 3

typedef struct SlirpConfig {
    /* Version must be provided */
    uint32_t version;
    /*
     * Fields introduced in SlirpConfig version 1 begin
     */
    int restricted;
    bool in_enabled;
    struct in_addr vnetwork;
    struct in_addr vnetmask;
    struct in_addr vhost;
    bool in6_enabled;
    struct in6_addr vprefix_addr6;
    uint8_t vprefix_len;
    struct in6_addr vhost6;
    const char *vhostname;
    const char *tftp_server_name;
    const char *tftp_path;
    const char *bootfile;
    struct in_addr vdhcp_start;
    struct in_addr vnameserver;
    struct in6_addr vnameserver6;
    const char **vdnssearch;
    const char *vdomainname;
    /* Default: IF_MTU_DEFAULT */
    size_t if_mtu;
    /* Default: IF_MRU_DEFAULT */
    size_t if_mru;
    /* Prohibit connecting to 127.0.0.1:* */
    bool disable_host_loopback;
    /*
     * Enable emulation code (*warning*: this code isn't safe, it is not
     * recommended to enable it)
     */
    bool enable_emu;
    /*
     * Fields introduced in SlirpConfig version 2 begin
     */
    struct sockaddr_in *outbound_addr;
    struct sockaddr_in6 *outbound_addr6;
    /*
     * Fields introduced in SlirpConfig version 3 begin
     */
    bool disable_dns;  /* slirp will not redirect/serve any DNS packet */
} SlirpConfig;

Slirp *slirp_new(const SlirpConfig *cfg, const SlirpCb *callbacks,
                 void *opaque);
/* slirp_init is deprecated in favor of slirp_new */
Slirp *slirp_init(int restricted, bool in_enabled, struct in_addr vnetwork,
                  struct in_addr vnetmask, struct in_addr vhost,
                  bool in6_enabled, struct in6_addr vprefix_addr6,
                  uint8_t vprefix_len, struct in6_addr vhost6,
                  const char *vhostname, const char *tftp_server_name,
                  const char *tftp_path, const char *bootfile,
                  struct in_addr vdhcp_start, struct in_addr vnameserver,
                  struct in6_addr vnameserver6, const char **vdnssearch,
                  const char *vdomainname, const SlirpCb *callbacks,
                  void *opaque);
void slirp_cleanup(Slirp *slirp);

void slirp_pollfds_fill(Slirp *slirp, uint32_t *timeout,
                        SlirpAddPollCb add_poll, void *opaque);

void slirp_pollfds_poll(Slirp *slirp, int select_error,
                        SlirpGetREventsCb get_revents, void *opaque);

void slirp_input(Slirp *slirp, const uint8_t *pkt, int pkt_len);

int slirp_add_hostfwd(Slirp *slirp, int is_udp, struct in_addr host_addr,
                      int host_port, struct in_addr guest_addr, int guest_port);
int slirp_remove_hostfwd(Slirp *slirp, int is_udp, struct in_addr host_addr,
                         int host_port);
int slirp_add_exec(Slirp *slirp, const char *cmdline,
                   struct in_addr *guest_addr, int guest_port);
int slirp_add_unix(Slirp *slirp, const char *unixsock,
                   struct in_addr *guest_addr, int guest_port);
int slirp_add_guestfwd(Slirp *slirp, SlirpWriteCb write_cb, void *opaque,
                       struct in_addr *guest_addr, int guest_port);
/* remove entries added by slirp_add_exec, slirp_add_unix or slirp_add_guestfwd */
int slirp_remove_guestfwd(Slirp *slirp, struct in_addr guest_addr,
                          int guest_port);

char *slirp_connection_info(Slirp *slirp);

void slirp_socket_recv(Slirp *slirp, struct in_addr guest_addr, int guest_port,
                       const uint8_t *buf, int size);
size_t slirp_socket_can_recv(Slirp *slirp, struct in_addr guest_addr,
                             int guest_port);

void slirp_state_save(Slirp *s, SlirpWriteCb write_cb, void *opaque);

int slirp_state_load(Slirp *s, int version_id, SlirpReadCb read_cb,
                     void *opaque);

int slirp_state_version(void);

const char *slirp_version_string(void);

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* LIBSLIRP_H */
