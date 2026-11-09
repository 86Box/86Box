/*
 * 86Box    A hypervisor and IBM PC system emulator that specializes in
 *          running old operating systems and software designed for IBM
 *          PC systems and compatibles from 1981 through fairly recent
 *          system designs based on the PCI bus.
 *
 *          This file is part of the 86Box distribution.
 *
 *          A virtual IPv4 server for SLiRP and modem links. It answers
 *          NBNS itself and, optionally, the browser and SMB over TCP.
 *
 * Authors: Jasmine Iwanek, <jriwanek@gmail.com>
 *
 *          Copyright 2026 Jasmine Iwanek.
 */
#ifndef NETWORK_NET_VSERVER_H
#define NETWORK_NET_VSERVER_H

#ifdef __cplusplus
extern "C" {
#endif

typedef struct vserver_t vserver_t;

/* Called with each IPv4 packet the server wants to send to the guest. */
typedef void (*vserver_output_t)(void *priv, const uint8_t *ip_pkt, int len);

vserver_t *vserver_init(void *log, uint32_t server_ip, bool wins, bool smb, bool ftp, bool tftp,
                        vserver_output_t output, void *priv);
void       vserver_close(vserver_t *server);

/* Answers name queries broadcast on the segment, for guests that don't know about WINS. */
void vserver_answer_broadcasts(vserver_t *server);

/* Keeps TCP segments down to this, for a server at the far end of a tunnel. */
void vserver_limit_segment(vserver_t *server, int mss);

/* Call once per millisecond of emulated time. */
void vserver_tick(vserver_t *server);

/* A tunnel protocol: a TCP control connection on one port, and GRE. */
typedef struct vserver_tunnel_t {
    bool (*control_input)(void *priv, const uint8_t *data, int len); /* false closes the connection */
    void (*control_closed)(void *priv);
    void (*gre_input)(void *priv, const uint8_t *data, int len);
    void (*tick)(void *priv);
    void (*close)(void *priv);
} vserver_tunnel_t;

void vserver_set_tunnel(vserver_t *server, const vserver_tunnel_t *tunnel, uint16_t port, void *priv);
void vserver_tunnel_send_control(vserver_t *server, const uint8_t *data, int len);
void vserver_tunnel_send_gre(vserver_t *server, const uint8_t *data, int len);

/* Offers a packet from the guest. Returns true when the server consumed it. */
bool vserver_process_ipv4(vserver_t *server, const uint8_t *ip_pkt, int len);

#ifdef __cplusplus
}
#endif

#endif /* NETWORK_NET_VSERVER_H */
