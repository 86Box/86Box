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
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <wchar.h>
#include <stdbool.h>
#include <errno.h>
#include <fcntl.h>
#ifdef _WIN32
#    define WIN32_LEAN_AND_MEAN
#    include <windows.h>
#    include <winsock2.h>
#else
#    include <poll.h>
#    include <netdb.h>
#endif

#define HAVE_STDARG_H
#include <86box/86box.h>
#include <86box/device.h>
#include <86box/thread.h>
#include <86box/timer.h>
#include <86box/network.h>
#include <86box/net_event.h>
#include <86box/random.h>
#include <sys/time.h>
#include "netswitch.h"
#include "pb_encode.h"
#include "pb_decode.h"

#include "networkmessage.pb.h"

bool ns_socket_setup(NSCONN *conn) {

    if(conn == NULL) {
        errno=EINVAL;
        return false;
    }

#ifdef _WIN32
    // Initialize Windows Socket API with the given version.
    WSADATA wsaData;
    if (WSAStartup(MAKEWORD(2, 0), &wsaData)) {
        perror("WSAStartup");
        return false;
    }
#endif

    /* Create the "main" socket
        * Local mode: the listener socket for multicast packets
        * Remote mode: the "main" socket for send and receive */
    conn->fddata = socket(AF_INET, SOCK_DGRAM, 0);
    if (conn->fddata < 0) {
        perror("socket");
        return false;
    }

    /* Here things diverge depending on local or remote type */
    if(conn->switch_type == SWITCH_TYPE_LOCAL) {

        /* Set socket options - allow multiple sockets to use the same address */
        u_int on = 1;
        if (setsockopt(conn->fddata, SOL_SOCKET, SO_REUSEADDR, (char *) &on, sizeof(on)) < 0) {
            perror("Reusing ADDR failed");
            return false;
        }
#ifndef _WIN32
        /* ... and same port number
         * Not needed on windows because SO_REUSEPORT doesn't exist there. However, the same
         * functionality comes along with SO_REUSEADDR. */
        if (setsockopt(conn->fddata, SOL_SOCKET, SO_REUSEPORT, (char *) &on, sizeof(on)) < 0) {
            perror("Reusing PORT failed");
            return false;
        }
#endif

        memset(&conn->addr, 0, sizeof(conn->addr));
        conn->addr.sin_family      = AF_INET;
        conn->addr.sin_addr.s_addr = htonl(INADDR_ANY);
        conn->addr.sin_port        = htons(conn->local_multicast_port);

        /* Bind to receive address */
        if (bind(conn->fddata, (struct sockaddr *) &conn->addr, sizeof(conn->addr)) < 0) {
            perror("bind");
            return false;
        }

        /* Request to join multicast group */
        /*** NOTE: intermittent airplane (non-connected wifi) failures with 239.255.86.86 - needs more investigation */
        struct ip_mreq mreq;
        mreq.imr_multiaddr.s_addr = inet_addr(conn->mcast_group);
        mreq.imr_interface.s_addr = htonl(INADDR_ANY);
        if (setsockopt(conn->fddata, IPPROTO_IP, IP_ADD_MEMBERSHIP, (char *) &mreq, sizeof(mreq)) < 0) {
            perror("setsockopt");
            return false;
        }

        /* Now create the outgoing data socket */
        conn->fdout = socket(AF_INET, SOCK_DGRAM, 0);
        if (conn->fdout < 0) {
            perror("out socket");
            return false;
        }

        /* Set up destination address */
        memset(&conn->outaddr, 0, sizeof(conn->outaddr));
        conn->outaddr.sin_family      = AF_INET;
        conn->outaddr.sin_addr.s_addr = inet_addr(conn->mcast_group);
        conn->outaddr.sin_port        = htons(conn->local_multicast_port);
    } else if (conn->switch_type == SWITCH_TYPE_REMOTE) {
        /* Remote switch path */
        int status;
        struct addrinfo hints;
        struct addrinfo *servinfo;
        char connect_ip[128] = "\0";

        memset(&hints, 0, sizeof hints);
        hints.ai_family = AF_INET;
        hints.ai_socktype = SOCK_DGRAM;
        hints.ai_flags = AI_PASSIVE; // not sure?

        if((status = getaddrinfo(conn->nrs_hostname, NULL, &hints, &servinfo)) != 0) {
            net_switch_log("getaddrinfo error: %s\n", gai_strerror(status));
            errno=EFAULT;
            return false;
        }

        for(const struct addrinfo *p = servinfo; p != NULL; p = p->ai_next) { // NOLINT (only want the first result)
            /* Take the first result, ipv4 since AF_INET was set in the hints */
            const struct sockaddr_in *ipv4 = (struct sockaddr_in *) p->ai_addr;
            const void               *addr = &(ipv4->sin_addr);
            inet_ntop(p->ai_family, addr, connect_ip, sizeof connect_ip);
            break;
        }
        freeaddrinfo(servinfo);

        if(strlen(connect_ip) == 0) {
            /* Couldn't look up the hostname */
            net_switch_log("Hostname lookup failure?\n");
            errno=EFAULT;
            return false;
        }

        /* Set up local socket address and port */
        memset(&conn->addr, 0, sizeof(conn->addr));
        conn->addr.sin_family      = AF_INET;
        conn->addr.sin_addr.s_addr = htonl(INADDR_ANY);
        conn->addr.sin_port        = htons(conn->remote_source_port);

        /* Bind to receive address. Try the first 100 ports to allow the use of multiple systems simultaneously */
        for(int i=0; i<100; i++) {
            if(i==99) {
                net_switch_log("Unable to find an available port to bind\n");
                return false;
            }
            if (bind(conn->fddata, (struct sockaddr *) &conn->addr, sizeof(conn->addr)) < 0) {
                net_switch_log("local port %d unavailable, trying next..\n", conn->remote_source_port);
                conn->remote_source_port += 1;
                conn->addr.sin_port = htons(conn->remote_source_port);
                continue ;
            } else {
                net_switch_log("** Local port for net remote switch is %d\n", conn->remote_source_port);
                break;
            }

        }


        /* Set up remote address and port */
        memset(&conn->outaddr, 0, sizeof(conn->outaddr));
        conn->outaddr.sin_family      = AF_INET;
        conn->outaddr.sin_addr.s_addr = inet_addr(connect_ip);
        conn->outaddr.sin_port        = htons(conn->remote_network_port);

        /* In remote mode the file descriptor for send (fdout) is the same as receive */
        conn->fdout = conn->fddata;

    } else {
        errno=EINVAL;
        return false;
    }

    return true;
}

NSCONN *
ns_open(struct ns_open_args *open_args) {
    struct nsconn *conn=NULL;

    /* Each "group" is really just the base port + group number
     * A different group effectively gets you a different switch
     * Clamp the group at MAX_SWITCH_GROUP */
    if(open_args->group > MAX_SWITCH_GROUP) {
        open_args->group = MAX_SWITCH_GROUP;
    }
    // FIXME: hardcoded for testing
    char *mcast_group = "239.255.86.86"; // Admin scope
    // char *mcast_group = "224.0.0.86"; // Local scope

    if ( (conn=calloc(1,sizeof(struct nsconn)))==NULL) {
        errno=ENOMEM;
        return NULL;
    }

    /* Type */
    conn->switch_type = open_args->type;

    /* Allocate the fragment buffer */
    for (int i = 0; i < FRAGMENT_BUFFER_LENGTH; i++) {
        conn->fragment_buffer[i] = calloc(1, sizeof(ns_fragment_t));
        /* Set the default size to 0 and null data buffer to indicate it is unused.
         * The data buffer will be allocated as needed. */
        conn->fragment_buffer[i]->size = 0;
        conn->fragment_buffer[i]->data = NULL;
    }
//    net_switch_log("Fragment buffers: %d total, %d each\n", FRAGMENT_BUFFER_LENGTH, MAX_FRAME_SEND_SIZE);

    snprintf(conn->mcast_group, MAX_MCAST_GROUP_LEN, "%s", mcast_group);
    conn->flags = open_args->flags;

    /* Increment the multicast port by the switch group number. Each group is
     * just a different port. */
    conn->local_multicast_port = open_args->group + NET_SWITCH_MULTICAST_PORT;
    conn->remote_network_port = NET_SWITCH_REMOTE_PORT;
    /* Source ports for remote switch will start here and be incremented until an available port is found */
    conn->remote_source_port = NET_SWITCH_REMOTE_PORT + NET_SWITCH_RECV_PORT_OFFSET;

    /* Remote switch hostname */
    strncpy(conn->nrs_hostname, open_args->nrs_hostname, sizeof(conn->nrs_hostname) - 1);
    conn->nrs_hostname[127] = 0x00;

    /* Switch type */
    if(conn->switch_type == SWITCH_TYPE_REMOTE) {
        net_switch_log("Connecting to remote %s:%d, initial local port %d, group %d\n", conn->nrs_hostname, conn->remote_network_port, conn->remote_source_port, open_args->group);
    } else {
        net_switch_log("Opening IP %s, port %d, group %d\n", mcast_group, conn->local_multicast_port, open_args->group);
    }

    /* Client state, disconnected by default.
     * Primarily used in remote mode */
    conn->client_state = DISCONNECTED;

    /* Client ID. Generate the ID if set to zero. */
    if(open_args->client_id == 0) {
        conn->client_id = ns_gen_client_id();
    }

    /* MAC address is set from the emulated card */
    memcpy(conn->mac_addr, open_args->mac_addr, PB_MAC_ADDR_SIZE);

    /* Protocol version */
    conn->version = NS_PROTOCOL_VERSION;

    if(!ns_socket_setup(conn)) {
        goto fail;
    }

    if (conn->switch_type == SWITCH_TYPE_REMOTE) {
        /* Perhaps one day do the entire handshake process here */
        if(!ns_send_control(conn, MessageType_MESSAGE_TYPE_CONNECT_REQUEST)) {
            goto fail;
        }
        conn->client_state = CONNECTING;
        net_switch_log("Client state is now CONNECTING\n");
    } else {
        conn->client_state = LOCAL;
    }

    /* Initialize sequence numbers */
    conn->sequence = 1;
    conn->remote_sequence = 1;

    /* Initialize stats */
    conn->stats.max_tx_frame = 0;
    conn->stats.max_tx_packet = 0;
    conn->stats.max_rx_frame = 0;
    conn->stats.max_rx_packet = 0;
    conn->stats.total_rx_packets = 0;
    conn->stats.total_tx_packets = 0;
    conn->stats.total_fragments  = 0;
    conn->stats.max_vec = 0;
    memcpy(conn->stats.last_tx_ethertype, (uint8_t []) { 0, 0}, sizeof(conn->stats.last_tx_ethertype));
    memcpy(conn->stats.last_rx_ethertype, (uint8_t []) { 0, 0}, sizeof(conn->stats.last_rx_ethertype));

    /* Assuming all went well we have our sockets */
    return conn;

    /* Cleanup */
fail:
    for (int i = 0; i < FRAGMENT_BUFFER_LENGTH; i++) {
        free(conn->fragment_buffer[i]);
    }
    return NULL;
}

int
ns_pollfd(const NSCONN *conn) {
    if (conn->fddata != 0)
        return conn->fddata;
    else {
        errno=EBADF;
        return -1;
    }
}

ssize_t
ns_sock_recv(const NSCONN *conn,void *buf, const size_t len, const int flags) {
    if (fd_valid(conn->fddata))
        return recv(conn->fddata,buf,len,0);
    else {
        errno=EBADF;
        return -1;
    }
}

ssize_t
ns_sock_send(NSCONN *conn,const void *buf, const size_t len, const int flags) {
    if (fd_valid(conn->fddata)) {
        /* Use the outgoing socket for sending, set elsewhere:
             * Remote mode: same as sending
             * Local mode: different from sending */
        return sendto(conn->fdout, buf, len, 0, (struct sockaddr *) &conn->outaddr, sizeof(conn->outaddr));
    } else {
        errno=EBADF;
        return -1;
    }
}

ssize_t
ns_send_pb(NSCONN *conn, const netpkt_t *packet,int flags) {

    NetworkMessage network_message = NetworkMessage_init_zero;
    uint8_t fragment_count;

    /* Do we need to fragment? First, determine how many packets we will be sending */
    if(packet->len <= MAX_FRAME_SEND_SIZE) {
        fragment_count = 1;
//        net_switch_log("No Fragmentation. Frame size %d is less than max size %d\n", packet->len, MAX_FRAME_SEND_SIZE);
    } else {
        /* Since we're using integer math and the remainder is
         * discarded we'll add one to the result *unless* the result can be evenly divided. */
        const uint8_t extra = (packet->len % MAX_FRAME_SEND_SIZE) == 0 ? 0 : 1;
             fragment_count = (packet->len / MAX_FRAME_SEND_SIZE) + extra;
//        net_switch_log("Fragmentation required, frame size %d exceeds max size %d\n", packet->len, MAX_FRAME_SEND_SIZE);
    }

    /* Loop here for each fragment. Send each fragment. In the even that the packet is *not* a fragment (regular data packet)
     * this will only execute once. */
    const uint32_t fragment_sequence = conn->sequence;
    const int64_t  packet_timestamp  = ns_get_current_millis();
    for (uint8_t fragment_index = 0; fragment_index < fragment_count; fragment_index++) {
        uint8_t buffer[NET_SWITCH_BUFFER_LENGTH];
        pb_ostream_t stream = pb_ostream_from_buffer(buffer, sizeof(buffer));
#ifdef ENABLE_NET_SWITCH_PB_FILE_DEBUG
        uint8_t file_buffer[NET_SWITCH_BUFFER_LENGTH];
        /* file_stream used for debugging and writing the message to a file */
        pb_ostream_t file_stream = pb_ostream_from_buffer(file_buffer, sizeof(file_buffer));
#endif
        /* Single frame is type DATA, fragments are FRAGMENT */
        network_message.message_type = fragment_count > 1 ? MessageType_MESSAGE_TYPE_FRAGMENT : MessageType_MESSAGE_TYPE_DATA;
        network_message.client_id = conn->client_id;
        network_message.timestamp = packet_timestamp;
        network_message.version = conn->version;

        /* Need some additional data if we're a fragment */
        if(fragment_count > 1) {
            network_message.fragment.total = fragment_count;
            network_message.fragment.id = fragment_sequence;
            network_message.fragment.sequence = fragment_index + 1;
            network_message.has_fragment = true;
        }

        /* TODO: Better / real ack logic. Needs its own function. Currently just putting in dummy data. */
        network_message.ack.id = 1;
        network_message.ack.history = 1;
        network_message.has_ack = true;
        network_message.sequence = conn->sequence;

        /* Frame data must be allocated */
        network_message.frame = calloc(1, PB_BYTES_ARRAY_T_ALLOCSIZE(packet->len));

        /* Calculate offsets based on our position in the fragment.
         * For anything other than the *last* packet, we'll have a max frame size */
        uint16_t       copy_length;
        const uint16_t copy_offset = fragment_index * MAX_FRAME_SEND_SIZE;
        if(fragment_index == (fragment_count - 1)) {
            copy_length = packet->len % MAX_FRAME_SEND_SIZE == 0 ? MAX_FRAME_SEND_SIZE : packet->len % MAX_FRAME_SEND_SIZE;
        } else {
            copy_length = MAX_FRAME_SEND_SIZE;
        }
        if(fragment_count > 1) {
//            net_switch_log("Fragment %d/%d, %d bytes\n", fragment_index + 1, fragment_count, copy_length);
        }
        network_message.frame->size = copy_length;
        memcpy(network_message.frame->bytes, packet->data + copy_offset, copy_length);

        /* mac address must be allocated */
        network_message.mac = calloc(1, PB_BYTES_ARRAY_T_ALLOCSIZE(PB_MAC_ADDR_SIZE));
        network_message.mac->size = PB_MAC_ADDR_SIZE;
        memcpy(network_message.mac->bytes, conn->mac_addr, PB_MAC_ADDR_SIZE);

        /* Encode the protobuf message */
        if (!pb_encode_ex(&stream, NetworkMessage_fields, &network_message,PB_ENCODE_DELIMITED)) {
            net_switch_log("Encoding failed: %s\n", PB_GET_ERROR(&stream));
            errno = EBADF;
            return -1;
        }

        /* Send on the socket */
        const ssize_t nc = ns_sock_send(conn, buffer, stream.bytes_written, 0);
        if(!nc) {
            net_switch_log("Error sending data on the socket\n");
            errno=EBADF;
            pb_release(NetworkMessage_fields, &network_message);
            return -1;
        }
#ifdef ENABLE_NET_SWITCH_PB_FILE_DEBUG
        /* File writing for troubleshooting when needed */
        FILE *f = fopen("/var/tmp/pbuf", "wb");
        if (f) {
            if (!pb_encode(&file_stream, NetworkMessage_fields, &network_message)) {
                net_switch_log("File encoding failed: %s\n", PB_GET_ERROR(&file_stream));
            }
            fwrite(file_buffer, file_stream.bytes_written, 1, f);
            fclose(f);
        } else {
            net_switch_log("file open failed\n");
        }
#endif

        /* Stats */
        if(network_message.frame->size > conn->stats.max_tx_frame) {
            conn->stats.max_tx_frame = network_message.frame->size;
        }
        if(nc > conn->stats.max_tx_packet) {
            conn->stats.max_tx_packet = nc;
        }
        if(nc > MAX_FRAME_SEND_SIZE) {
            conn->stats.total_fragments = fragment_count > 1 ? conn->stats.total_fragments + fragment_count : conn->stats.total_fragments;
        }
        conn->stats.total_tx_packets++;
        memcpy(conn->stats.last_tx_ethertype, &packet->data[12], 2);

        /* Increment the sequence number */
        seq_increment(conn);

        /* nanopb will free all the allocated entries for us */
        pb_release(NetworkMessage_fields, &network_message);

    }

    return packet->len;
}

bool store_fragment(const NSCONN *conn, const NetworkMessage *network_message) {

    if(conn == NULL || network_message == NULL) {
        return false;
    }

    /* The fragment sequence indicates which fragment this is in the overall fragment
     * collection. This is used to index the fragments while being stored for reassembly
     * (zero indexed locally) */
    const uint32_t fragment_index = network_message->fragment.sequence - 1;
    const uint32_t fragment_size  = network_message->frame->size;

    /* Make sure the fragments aren't too small
     * (see header notes about size requirements for MIN_FRAG_RECV_SIZE and FRAGMENT_BUFFER_LENGTH)
     * NOTE: The last packet is exempt from this rule because it can have a smaller amount.
     * This is primarily to ensure there's enough space to fit all the fragments. */
    if(network_message->fragment.sequence != network_message->fragment.total) {
        if (network_message->frame->size < MIN_FRAG_RECV_SIZE) {
            net_switch_log("size: %d < %d\n", network_message->frame->size, MIN_FRAG_RECV_SIZE);
            return false;
        }
    }

    /* Make sure we can handle the amount of incoming fragments */
    if (network_message->fragment.total > FRAGMENT_BUFFER_LENGTH) {
        net_switch_log("buflen: %d > %d\n", network_message->fragment.total, FRAGMENT_BUFFER_LENGTH);
        return false;
    }

    /* Allocate or reallocate as needed.
     * size > 0 indicates this buffer has already been allocated. */
    if(conn->fragment_buffer[fragment_index]->size > 0) {
        conn->fragment_buffer[fragment_index]->data = realloc(conn->fragment_buffer[fragment_index]->data, sizeof(char) * fragment_size);
    } else {
        conn->fragment_buffer[fragment_index]->data = calloc(1, sizeof(char) * fragment_size);
    }

    if (conn->fragment_buffer[fragment_index]->data == NULL) {
        net_switch_log("Failed to allocate / reallocate fragment buffer space\n");
        return false;
    }

    /* Each fragment will belong to a particular ID. All members will have the same ID,
     * which is generally set to the sequence number of the first fragment */
    conn->fragment_buffer[fragment_index]->id       = network_message->fragment.id;
    /* The sequence here is set to the index of the packet in the total fragment collection
     * (network_message->fragment.sequence) */
    conn->fragment_buffer[fragment_index]->sequence = fragment_index;
    /* Total number of fragments in this set */
    conn->fragment_buffer[fragment_index]->total    = network_message->fragment.total;
    /* The sequence number from the packet that contained the fragment */
    conn->fragment_buffer[fragment_index]->packet_sequence = network_message->sequence;
    /* Copy the fragment data and size */
    /* The size of fragment_buffer[fragment_index]->data is checked against MAX_FRAME_SEND_SIZE above */
    memcpy(conn->fragment_buffer[fragment_index]->data, network_message->frame->bytes, fragment_size);
    conn->fragment_buffer[fragment_index]->size     = fragment_size;
    /* 10 seconds for a TTL */
    conn->fragment_buffer[fragment_index]->ttl      = ns_get_current_millis() + 10000;

    return true;
}

bool
reassemble_fragment(const NSCONN *conn, netpkt_t *pkt, const uint32_t packet_count)
{
    uint32_t total = 0;

    /* Make sure the reassembled packet doesn't exceed NET_MAX_FRAME */
//    if (packet_count * MAX_FRAME_SEND_SIZE > NET_MAX_FRAME) {
//        return false;
//    }

    /* Too many packets! */
    if (packet_count > FRAGMENT_BUFFER_LENGTH) {
        return false;
    }

    // TODO: Check fragment ID
    // TODO: Check TTL

    /* Get the fragment size from the first entry. All fragments in a particular
     * set must be of the same size except the last fragment, which may be smaller.
     * The fragment size will be used to determine the offset. */
    const uint16_t fragment_size = conn->fragment_buffer[0]->size;

//    net_switch_log("Reassembling %d fragments\n", packet_count);

    for(int i = 0; i < packet_count; i++) {
        /* Size of zero means we're trying to assemble from a bad fragment */
        if(conn->fragment_buffer[i]->size == 0) {
            net_switch_log("Fragment size 0 when trying to reassemble (id %i/index %i/seq %i/ total %i)\n",conn->fragment_buffer[i]->id, i, conn->fragment_buffer[i]->sequence, conn->fragment_buffer[i]->total);
            return false;
        }
        if(conn->fragment_buffer[i]->data == NULL) {
            net_switch_log("Missing fragment data when trying to reassemble\n");
            return false;
        }

        memcpy(pkt->data + (fragment_size * i), conn->fragment_buffer[i]->data, conn->fragment_buffer[i]->size);
        total += conn->fragment_buffer[i]->size;

        /* Zero out the size to indicate the slot is unused */
        conn->fragment_buffer[i]->size = 0;
        free(conn->fragment_buffer[i]->data);
        conn->fragment_buffer[i]->data = NULL;
    }

    /* Set the size, must cast due to netpkt_t (len is int)  */
    pkt->len = (int) total;
//    net_switch_log("%d bytes reassembled and converted to data packet.\n", pkt->len);

    return true;
}

bool
ns_recv_pb(NSCONN *conn, ns_rx_packet_t *packet,size_t len,int flags) {
    NetworkMessage  network_message = NetworkMessage_init_zero;
    ns_rx_packet_t *ns_packet       = packet;

    uint8_t buffer[NET_SWITCH_BUFFER_LENGTH];

    /* TODO: Use the passed len? Most likely not needed */
    const ssize_t nc = ns_sock_recv(conn, buffer, NET_SWITCH_BUFFER_LENGTH, 0);
    if(!nc) {
        net_switch_log("Error receiving data on the socket\n");
        errno=EBADF;
        return false;
    }
    pb_istream_t stream = pb_istream_from_buffer(buffer, sizeof(buffer));

    if (!pb_decode_delimited(&stream, NetworkMessage_fields, &network_message)) {
        /* Decode failed */
        net_switch_log("PB decoding failed: %s\n", PB_GET_ERROR(&stream));
        /* Allocated fields are automatically released upon failure */
        return false;
    }

    /* Basic checks for validity */
    if(network_message.mac == NULL || network_message.message_type == MessageType_MESSAGE_TYPE_UNSPECIFIED ||
        network_message.client_id == 0) {
        net_switch_log("Invalid packet received! Skipping..\n");
        goto fail;
    }

    /* These fields should always be set. Start copying into our packet structure. */
    ns_packet->client_id    = network_message.client_id;
    ns_packet->type         = network_message.message_type;
    memcpy(ns_packet->mac, network_message.mac->bytes, PB_MAC_ADDR_SIZE);
    ns_packet->timestamp    = network_message.timestamp;
    ns_packet->version      = network_message.version;
    conn->remote_sequence   = network_message.sequence;
    conn->last_packet_stamp = network_message.timestamp;

    /* Control messages take a different path */
    if(network_message.message_type != MessageType_MESSAGE_TYPE_DATA &&
        network_message.message_type != MessageType_MESSAGE_TYPE_FRAGMENT) {
        process_control_packet(conn, ns_packet);
        pb_release(NetworkMessage_fields, &network_message);
        return true;
    }

    /* All packets should be DATA or FRAGMENT at this point and have a frame */
    if(network_message.frame == NULL) {
        net_switch_log("Invalid data packet received! Frame is null. Skipping..\n");
        goto fail;
    }

    /* Fragment path first */
    if(network_message.message_type == MessageType_MESSAGE_TYPE_FRAGMENT) {

        /* Store fragment */
        if(!store_fragment(conn, &network_message)) {
            net_switch_log("Failed to store fragment\n");
            goto fail;
        }

        /* Is this the last fragment? If not, return */
        if(network_message.fragment.sequence != network_message.fragment.total) {
            // FIXME: Really dumb, needs to be smarter
            pb_release(NetworkMessage_fields, &network_message);
            return true;
        }

        /* This is the last fragment. Attempt to reassemble */
        if(!reassemble_fragment(conn, &ns_packet->pkt, network_message.fragment.total)) {
            net_switch_log("Failed to reassemble fragment\n");
            goto fail;
        }
        /* Change the type to DATA */
        ns_packet->type = MessageType_MESSAGE_TYPE_DATA;

    } else {
        /* Standard DATA packet path. Copy frame from the message */
        memcpy(ns_packet->pkt.data, network_message.frame->bytes, network_message.frame->size);
        ns_packet->pkt.len = network_message.frame->size;
    }

    /* Stats */
    if(network_message.frame->size > conn->stats.max_rx_frame) {
        conn->stats.max_rx_frame = network_message.frame->size;
    }
    if(nc > conn->stats.max_rx_packet) {
        conn->stats.max_rx_packet = nc;
    }
    memcpy(conn->stats.last_rx_ethertype, &packet->pkt.data[12], 2);
    conn->stats.total_rx_packets++;
    /* End Stats */

    /* nanopb allocates the necessary fields while serializing.
       They need to be manually released once you are done with the message */
    pb_release(NetworkMessage_fields, &network_message);
    return true;
fail:
    pb_release(NetworkMessage_fields, &network_message);
    return false;
}

bool process_control_packet(NSCONN *conn, const ns_rx_packet_t *packet) {

    control_packet_info_t packet_info = get_control_packet_info(*packet, conn->mac_addr);
//    net_switch_log("Last timestamp: %lld\n", ns_get_current_millis());
//    net_switch_log("(%lld ms) [%03d] ", ns_get_current_millis() - packet_info.timestamp, conn->sequence);

    /* I probably want to eventually differentiate between local and remote here, kind of basic now */
    if(!packet_info.is_packet_from_me) { /* in case of local mode */
        switch (packet_info.type) {
            case MessageType_MESSAGE_TYPE_JOIN:
                net_switch_log("Client ID 0x%08llx (MAC %s) has joined the chat\n", packet_info.client_id, packet_info.src_mac_h);
                break;
            case MessageType_MESSAGE_TYPE_LEAVE:
                net_switch_log("Client ID 0x%08llx (MAC %s) has left us\n", packet_info.client_id, packet_info.src_mac_h);
                break;
            case MessageType_MESSAGE_TYPE_KEEPALIVE:
//                net_switch_log("Client ID 0x%08llx (MAC %s) is still alive\n", packet_info.client_id, packet_info.src_mac_h);
                break;
            case MessageType_MESSAGE_TYPE_ACK:
//                net_switch_log("Client ID 0x%08llx (MAC %s) has sent an ACK\n", packet_info.client_id, packet_info.src_mac_h);
                break;
            case MessageType_MESSAGE_TYPE_CONNECT_REPLY:
                conn->client_state = CONNECTED;
                net_switch_log("Client ID 0x%08llx (MAC %s) has sent a connection reply\n", packet_info.client_id, packet_info.src_mac_h);
                net_switch_log("Client state is now CONNECTED\n");
                break;
            case MessageType_MESSAGE_TYPE_FRAGMENT:
                net_switch_log("Client ID 0x%08llx (MAC %s) has sent a fragment\n", packet_info.client_id, packet_info.src_mac_h);
                break;
            default:
                net_switch_log("Client ID 0x%08llx (MAC %s) has sent a message that we don't understand (type %d)\n", packet_info.client_id, packet_info.src_mac_h, packet_info.type);
                break;
        }
    }
    return true;

}

bool
ns_send_control(NSCONN *conn, const MessageType type) {

    NetworkMessage network_message = NetworkMessage_init_zero;
    uint8_t buffer[NET_SWITCH_BUFFER_LENGTH];

    pb_ostream_t stream = pb_ostream_from_buffer(buffer, sizeof(buffer));
    network_message.message_type = type;
    network_message.client_id = conn->client_id;

    /* No frame data so we only need to allocate mac address */
    network_message.mac = calloc(1, PB_BYTES_ARRAY_T_ALLOCSIZE(PB_MAC_ADDR_SIZE));
    network_message.mac->size = PB_MAC_ADDR_SIZE;
    memcpy(network_message.mac->bytes, conn->mac_addr, PB_MAC_ADDR_SIZE);

    network_message.timestamp = ns_get_current_millis();
    network_message.version   = conn->version;
    network_message.sequence  = conn->sequence;

    if (!pb_encode_ex(&stream, NetworkMessage_fields, &network_message, PB_ENCODE_DELIMITED)) {
        net_switch_log("Encoding failed: %s\n", PB_GET_ERROR(&stream));
        errno = EBADF;
        return false;
    }

    const ssize_t nc = ns_sock_send(conn, buffer, stream.bytes_written, 0);
    if(!nc) {
        net_switch_log("Error sending control message on the socket\n");
        errno=EBADF;
        pb_release(NetworkMessage_fields, &network_message);
        return -1;
    }
    /* Increment the sequence number */
    seq_increment(conn);

    /* Stats */
    conn->stats.total_tx_packets++;

    /* Must release allocated data */
    pb_release(NetworkMessage_fields, &network_message);

    return true;
}

uint32_t
ns_gen_client_id(void) {
    uint32_t msb;
    do {
        msb = random_generate();
    } while (msb < 0x10);
    return ( random_generate() | (random_generate() << 8) | (random_generate() << 16) | (msb << 24));
}

const char *
ns_printable_message_type(const MessageType type)
{
    switch (type) {
        case MessageType_MESSAGE_TYPE_DATA:
                return "Data";
        case MessageType_MESSAGE_TYPE_JOIN:
                return "Join";
        case MessageType_MESSAGE_TYPE_LEAVE:
                return "Leave";
        case MessageType_MESSAGE_TYPE_KEEPALIVE:
                return "Keepalive";
        case MessageType_MESSAGE_TYPE_FRAGMENT:
                return "Fragment";
        case MessageType_MESSAGE_TYPE_ACK:
                return "Ack";
        case MessageType_MESSAGE_TYPE_UNSPECIFIED:
                return "Unspecified (shouldn't get this)";
        default:
                return "Unknown message type - probably hasn't been added yet!";
    }
}

int64_t
ns_get_current_millis(void) {
    struct timeval time;
    gettimeofday(&time, NULL);
    /* Windows won't properly promote integers so this is necessary */
    const int64_t seconds = (int64_t) time.tv_sec * 1000;

    return seconds + (time.tv_usec / 1000);
}

int
ns_flags(const NSCONN *conn) {
    return conn->flags;
}

int
ns_close(NSCONN *conn) {
    if(conn->switch_type == SWITCH_TYPE_REMOTE) {
        /* TBD */
    }
    /* No need to check the return here as we're closing out */
    ns_send_control(conn, MessageType_MESSAGE_TYPE_LEAVE);
    for (int i = 0; i < FRAGMENT_BUFFER_LENGTH; i++) {
        if (conn->fragment_buffer[i]->size > 0) {
            free(conn->fragment_buffer[i]->data);
            conn->fragment_buffer[i]->data = NULL;
        }
        free(conn->fragment_buffer[i]);
    }
    close(conn->fddata);
    close(conn->fdout);
    return 0;
}

bool is_control_packet(const ns_rx_packet_t *packet) {
    return packet->type != MessageType_MESSAGE_TYPE_DATA;
}

bool is_fragment_packet(const ns_rx_packet_t *packet) {
    return packet->type == MessageType_MESSAGE_TYPE_FRAGMENT;
}

bool ns_connected(const NSCONN *conn) {
    if(conn->switch_type == SWITCH_TYPE_LOCAL) {
        return true;
    }

    if(conn->switch_type == SWITCH_TYPE_REMOTE) {
        if(conn->client_state == CONNECTED) {
            return true;
        }
    }

    return false;
}

char* formatted_mac(uint8_t mac_addr[6])
{
    char *mac_h = calloc(1, sizeof(char)* 32);
    for(int i=0; i < 6; i++) {
        char octet[4];
        snprintf(octet, sizeof(octet), "%02X%s", mac_addr[i], i < 5 ? ":" : "");
        strncat(mac_h, octet, sizeof(mac_h) - 1);
    }
    return mac_h;
}

control_packet_info_t get_control_packet_info(const ns_rx_packet_t packet, const uint8_t *my_mac)
{
    control_packet_info_t packet_info;

    packet_info.src_mac_h[0]  = '\0';
    packet_info.printable[0]  = '\0';
    packet_info.client_id = packet.client_id;
    memcpy(packet_info.src_mac, &packet.mac, 6);
    packet_info.type = packet.type;
    packet_info.is_packet_from_me = (memcmp(my_mac, packet_info.src_mac, sizeof(uint8_t) * 6) == 0);
    char *formatted_mac_h = formatted_mac(packet_info.src_mac);
    strncpy(packet_info.src_mac_h, formatted_mac_h, MAX_PRINTABLE_MAC);
    free(formatted_mac_h);
    snprintf(packet_info.printable, sizeof(packet_info.printable), "%s", ns_printable_message_type(packet_info.type));
    packet_info.timestamp = packet.timestamp;

    return packet_info;
}

data_packet_info_t
get_data_packet_info(const netpkt_t *packet, const uint8_t *my_mac) {
    data_packet_info_t packet_info;

    packet_info.src_mac_h[0]  = '\0';
    packet_info.dest_mac_h[0] = '\0';
    packet_info.my_mac_h[0]   = '\0';
    packet_info.printable[0]  = '\0';

    memcpy(packet_info.dest_mac,&packet->data[0], 6);
    memcpy(packet_info.src_mac, &packet->data[6], 6);

    /* Broadcast and multicast are treated the same at L2 and both will have the
     * least significant bit of 1 in the first transmitted byte.
     * The below test matches 0xFF for standard broadcast along with 0x01 and 0x33 for multicast */
    packet_info.is_broadcast      = ((packet->data[0] & 1) == 1);
    packet_info.is_packet_for_me  = (memcmp(my_mac, packet_info.dest_mac, sizeof(uint8_t) * 6) == 0);
    packet_info.is_packet_from_me = (memcmp(my_mac, packet_info.src_mac, sizeof(uint8_t) * 6) == 0);
    packet_info.is_data_packet    = packet->len > 0;
    packet_info.size = packet->len;

    /* Since this function is applied to every packet, only enable the pretty formatting below
     * if logging is specifically enabled. */
#ifdef ENABLE_NET_SWITCH_LOG
    /* Pretty formatting for hardware addresses */
    for(int i=0; i < 6; i++) {
        char octet[4];
        snprintf(octet, sizeof(octet), "%02X%s", packet_info.src_mac[i], i < 5 ? ":" : "");
        strncat(packet_info.src_mac_h, octet, sizeof (packet_info.src_mac_h) - 1);

        snprintf(octet, sizeof(octet), "%02X%s", packet_info.dest_mac[i], i < 5 ? ":" : "");
        strncat(packet_info.dest_mac_h, octet, sizeof (packet_info.dest_mac_h) - 1);

        snprintf(octet, sizeof(octet), "%02X%s", my_mac[i], i < 5 ? ":" : "");
        strncat(packet_info.my_mac_h, octet, sizeof (packet_info.my_mac_h) - 1);
    }

    /* Printable output formatting */
    if(packet_info.is_broadcast) {
        if(packet_info.is_packet_from_me) {
            snprintf(packet_info.printable, sizeof(packet_info.printable), "(broadcast)");
        } else {
            snprintf(packet_info.printable, sizeof(packet_info.printable), "%s (broadcast)", packet_info.src_mac_h);
        }
    } else {
        snprintf(packet_info.printable, sizeof(packet_info.printable), "%s%s -> %s%s", packet_info.src_mac_h, packet_info.is_packet_from_me ? " (me)" : "     ",
                 packet_info.dest_mac_h, packet_info.is_packet_for_me ? " (me)" : "");
    }
#endif
    return packet_info;
}

bool
fd_valid(const int fd)
{
#ifdef _WIN32
    int error_code;
    int error_code_size = sizeof(error_code);
    getsockopt(fd, SOL_SOCKET, SO_ERROR, (char *) &error_code, &error_code_size);
    if (error_code == WSAENOTSOCK) {
        return false;
    }
    return true;
#else
    if (fcntl(fd, F_GETFD) == -1) {
        return false;
    }
    /* All other values will be a valid fd */
    return true;
#endif
}

bool
seq_increment(NSCONN *conn)
{
    if(conn == NULL) {
        return false;
    }
    conn->sequence++;
    if (conn->sequence == 0) {
        conn->sequence = 1;
    }
    return true;
}
