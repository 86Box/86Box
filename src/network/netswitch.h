/*
 * 86Box    A hypervisor and IBM PC system emulator that specializes in
 *          running old operating systems and software designed for IBM
 *          PC systems and compatibles from 1981 through fairly recent
 *          system designs based on the PCI bus.
 *
 *          This file is part of the 86Box distribution.
 *
 *          Network Switch backend
 *
 * Authors: cold-brewed
 *
 *          Copyright 2024 cold-brewed
 */
#ifndef NET_SWITCH_H
#define NET_SWITCH_H

#ifdef _WIN32
#include <Winsock2.h> // before Windows.h, else Winsock 1 conflict
#include <Ws2tcpip.h> // needed for ip_mreq definition for multicast
#include <Windows.h>
#else
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <time.h>
#endif
#include <unistd.h>
#include "pb.h"
#include "networkmessage.pb.h"

/* Local switch multicast port */
#define NET_SWITCH_MULTICAST_PORT 8086
/* Remote switch connect port */
#define NET_SWITCH_REMOTE_PORT 8088
/* Remove switch. This offset is where the local source ports will begin. */
#define NET_SWITCH_RECV_PORT_OFFSET 198
/* Multicast group (IP Address) maximum length. String representation. */
#define MAX_MCAST_GROUP_LEN  32
/* The buffer length used for both receiving on sockets and protobuf serialize / deserialize */
#define NET_SWITCH_BUFFER_LENGTH 2048

/* Any frame above this size gets fragmented */
#define MAX_FRAME_SEND_SIZE 1200
/* Minimum fragment size we'll accept */
#define MIN_FRAG_RECV_SIZE  12
/*
   Size of the fragment buffer - how many can we hold?
   Note: FRAGMENT_BUFFER_LENGTH * MIN_FRAG_RECV_SIZE *must* be greater
   than NET_MAX_FRAME or bad things will happen with large packets!
*/
#define FRAGMENT_BUFFER_LENGTH 128
/* Maximum number of switch groups */
#define MAX_SWITCH_GROUP 31
/* Size of a mac address in bytes. Used for the protobuf serializing / deserializing  */
#define PB_MAC_ADDR_SIZE 6
/* This will define the version in use and the minimum required for communication */
#define NS_PROTOCOL_VERSION 1
/* Maximum string size for a printable (formatted) mac address */
#define MAX_PRINTABLE_MAC 32
/* Maximum hostname length for a remote switch host */
#define MAX_HOSTNAME 128

typedef enum {
    FLAGS_NONE    =      0,
    FLAGS_PROMISC = 1 << 0,
} ns_flags_t;

typedef enum {
    SWITCH_TYPE_LOCAL = 0,
    SWITCH_TYPE_REMOTE,
} ns_type_t;

typedef enum {
    DISCONNECTED,
    CONNECTING,
    CONNECTED,
    LOCAL,
} ns_client_state_t;

struct ns_open_args {
    uint8_t    group;
    ns_flags_t flags;
    ns_type_t  type;
    char      *client_id;
    uint8_t    mac_addr[6];
    char       nrs_hostname[MAX_HOSTNAME];
};

struct nsconn;

typedef struct nsconn NSCONN;

struct ns_stats {
    size_t  max_tx_frame;
    size_t  max_tx_packet;
    size_t  max_rx_frame;
    size_t  max_rx_packet;
    uint8_t last_tx_ethertype[2];
    uint8_t last_rx_ethertype[2];
    u_long  total_rx_packets;
    u_long  total_tx_packets;
    u_long  total_fragments;
    uint8_t max_vec;
};

typedef struct {
    /* The ID of the fragment. All fragments in a set should have the same ID. */
    uint32_t id;
    /* The fragment index in the sequence of fragments. NOTE: one indexed, not zero!
     * Example: the first fragment of three would be 1 in the sequence */
    uint32_t sequence;
    /* Total number of fragments for the collection */
    uint32_t total;
    /* The sequence number of the packet that delivered the fragment. Not the same as fragment sequence above! */
    uint32_t packet_sequence;
    /* Frame data */
    char     *data;
    /* Frame size. A size of zero indicates an unused fragment slot and unallocated data field. */
    uint32_t size;
    /* Epoch time (in ms) that the fragment is valid until */
    uint64_t ttl;
} ns_fragment_t;

struct nsconn {
    uint16_t           flags;
    int                fdctl;
    int                fddata;
    int                fdout;
    char               mcast_group[MAX_MCAST_GROUP_LEN];
    struct sockaddr_in addr;
    struct sockaddr_in outaddr;
    size_t             outlen;
    struct sockaddr   *sock;
    struct sockaddr   *outsock;
    struct ns_stats    stats;
    uint32_t           client_id;
    uint8_t            mac_addr[6];
    uint16_t           sequence;
    uint16_t           remote_sequence;
    uint8_t            version;
    uint8_t            switch_type;
    ns_client_state_t  client_state;
    int64_t            last_packet_stamp;
    /* Remote switch hostname */
    char               nrs_hostname[MAX_HOSTNAME];
    /* Remote connect port for remote network switch */
    uint16_t           remote_network_port;
    /* Local multicast port for the local network switch */
    uint16_t           local_multicast_port;
    /*
     * The source port to receive packets. Only applies to remote mode.
     * This will also be the source port for sent packets in order to aid
     * NAT
     */
    uint16_t           remote_source_port;
    ns_fragment_t      *fragment_buffer[FRAGMENT_BUFFER_LENGTH];
};

typedef struct {
    uint32_t id;
    uint32_t history;
} ns_ack_t;

typedef struct {
    uint32_t id;
    uint32_t sequence;
    uint32_t total;
} ns_fragment_info_t;

typedef struct {
    netpkt_t           pkt;
    MessageType        type;
    uint32_t           client_id;
    uint8_t            mac[6];
    uint32_t           flags;
    int64_t            timestamp;
    ns_ack_t           ack;
    ns_fragment_info_t fragment;
    uint32_t           version;
} ns_rx_packet_t;

typedef struct {
    size_t size;
    char    src_mac_h[MAX_PRINTABLE_MAC];
    char    dest_mac_h[MAX_PRINTABLE_MAC];
    char    my_mac_h[MAX_PRINTABLE_MAC];
    uint8_t src_mac[6];
    uint8_t dest_mac[6];
    bool    is_packet_from_me;
    bool    is_broadcast;
    bool    is_packet_for_me;
    bool    is_data_packet;
    char    printable[128];
} data_packet_info_t;

typedef struct {
    uint8_t     src_mac[6];
    char        src_mac_h[MAX_PRINTABLE_MAC];
    bool        is_packet_from_me;
    MessageType type;
    char        printable[128];
    uint64_t    client_id;
    int64_t     timestamp;
} control_packet_info_t;

/* Initializes and opens the Net Multicast Switch */
NSCONN *ns_open(struct ns_open_args *open_args);

/* Returns the flags */
int ns_flags(const NSCONN *conn);

/* Returns the file descriptor for polling */
int ns_pollfd(const NSCONN *conn);

/* This should be used to receive serialized protobuf packets
 * and have the output placed in the packet struct */
bool ns_recv_pb(NSCONN *conn, ns_rx_packet_t *packet,size_t len,int flags);

/* Do not call directly! Used internally */
ssize_t ns_sock_recv(const NSCONN *conn,void *buf,size_t len,int flags);

/* This should be used to send serialized protobuf packets
* and have the output placed in the packet struct */
ssize_t ns_send_pb(NSCONN *conn, const netpkt_t *packet,int flags);

/* Send control messages */
bool ns_send_control(NSCONN *conn, MessageType type);

const char* ns_printable_message_type(MessageType type);

/* Do not call directly! Used internally */
ssize_t ns_sock_send(NSCONN *conn,const void *buf,size_t len,int flags);

uint32_t ns_gen_client_id(void);

/* Closes and cleans up */
int ns_close(NSCONN *conn);

/* Return current time in milliseconds */
int64_t ns_get_current_millis(void);

/* Is the packet a control packet?
 * Any type other than DATA is a control packet, including fragments */
bool is_control_packet(const ns_rx_packet_t *packet);

/* Logic for handling control packets */
bool process_control_packet(NSCONN *conn, const ns_rx_packet_t *packet);

/* Is the packet a fragment packet? */
bool is_fragment_packet(const ns_rx_packet_t *packet);

/* Store a fragment in the fragment buffer */
bool store_fragment(const NSCONN *conn, const NetworkMessage *network_message);

/* Reassemble a fragment from the fragment buffer */
bool reassemble_fragment(const NSCONN *conn, netpkt_t *pkt, uint32_t packet_count);

/* Set up the socket. Accounts for the differences between local and remote modes */
bool ns_socket_setup(NSCONN *conn);

/* Is the switch in a connected state? Always returns true in local mode */
bool ns_connected(const NSCONN *conn);

/* Return a string with a properly formatted mac address.
 * Note: Caller must free! */
char* formatted_mac(uint8_t mac_addr[6]);

/* Used for control packet info and logic */
control_packet_info_t get_control_packet_info(ns_rx_packet_t packet, const uint8_t *my_mac);

/* Used for data packet info and logic */
data_packet_info_t get_data_packet_info(const netpkt_t *packet, const uint8_t *my_mac);

/* Checks for a valid file descriptor */
bool fd_valid(int fd);

/* Wrapping increment for the sequence number */
bool seq_increment(NSCONN *conn);

#ifdef ENABLE_NET_SWITCH_LOG
static void
net_switch_log(const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    pclog_ex(fmt, ap);
    va_end(ap);
}
#else
#    define net_switch_log(fmt, ...)
#endif

#endif