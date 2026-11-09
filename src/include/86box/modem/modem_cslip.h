/*
 * 86Box    A hypervisor and IBM PC system emulator that specializes in
 *          running old operating systems and software designed for IBM
 *          PC systems and compatibles from 1981 through fairly recent
 *          system designs based on the PCI bus.
 *
 *          This file is part of the 86Box distribution.
 *
 *          CSLIP (Compressed SLIP) - Van Jacobson TCP/IP header
 *          compression for modem emulation. RFC 1144.
 *
 * Authors: Jasmine Iwanek, <jriwanek@gmail.com>
 *
 *          Copyright 2025-2026 Jasmine Iwanek.
 */
#ifndef EMU_MODEM_CSLIP_H
#define EMU_MODEM_CSLIP_H

#ifdef __cplusplus
extern "C" {
#endif

/* VJ compression packet types */
#define VJ_UNCOMPRESSED_MARKER   0x30
#define VJ_TYPE_IP               0x40
#define VJ_TYPE_UNCOMPRESSED_TCP 0x70
#define VJ_TYPE_COMPRESSED_TCP   0x80

/* Change mask flags for compressed TCP */
#define VJ_NEW_C 0x40 /* Connection number changed */
#define VJ_NEW_I 0x20 /* IP ID changed by a value other than one */
#define VJ_TCP_PUSH_BIT 0x10
#define VJ_NEW_S 0x08 /* Sequence changed */
#define VJ_NEW_A 0x04 /* Ack changed */
#define VJ_NEW_W 0x02 /* Window changed */
#define VJ_NEW_U 0x01 /* Urgent changed */

/* Special combined flags */
#define VJ_SPECIAL_I (VJ_NEW_S | VJ_NEW_W | VJ_NEW_U) /* Interactive echoed character */
#define VJ_SPECIAL_D (VJ_NEW_S | VJ_NEW_A | VJ_NEW_W | VJ_NEW_U) /* Unidirectional data */

#define VJ_MAX_SLOTS 16
#define VJ_MAX_HDR   128

/* Saved TCP/IP header state for one connection */
typedef struct {
    uint8_t  hdr[VJ_MAX_HDR]; /* Last full IP+TCP header */
    int      hdr_len;         /* Length of saved header */
    uint8_t  conn_id;         /* Connection slot number */
    uint8_t  ip_proto;        /* Saved IP protocol field */
} vj_slot_t;

/* VJ compressor/decompressor context */
typedef struct cslip_ctx_t {
    vj_slot_t slots[VJ_MAX_SLOTS];
    int       num_slots;
    uint8_t   last_conn_recv;  /* Last connection ID received (decompress) */
    uint8_t   last_conn_send;  /* Last connection ID sent (compress) */
    int       last_cs;         /* Last compress slot index */
    bool      compress_slot_id; /* Whether to compress slot ID */
    int       flags;           /* Error flags */
    void     *log;
} cslip_ctx_t;

#define VJ_FLAG_TOSS 1  /* Discard next incoming packet (error recovery) */

cslip_ctx_t *cslip_init(void *log);
void         cslip_close(cslip_ctx_t *ctx);

/* Compress an outgoing IP packet. Returns the compressed packet type
   in *type. out_buf must be at least as large as in_len + VJ_MAX_HDR.
   Returns length of compressed data, or 0 if not compressible. */
int cslip_compress(cslip_ctx_t *ctx, const uint8_t *in, int in_len,
                   uint8_t *out, int *type);
int cslip_compress_logged(cslip_ctx_t *ctx, const uint8_t *in, int in_len,
                          uint8_t *out, int *type);

/* Decompress an incoming packet. type is VJ_TYPE_*.
   Returns length of decompressed IP packet, or 0 on error.
   out_buf must be large enough (in_len + VJ_MAX_HDR). */
int cslip_decompress(cslip_ctx_t *ctx, const uint8_t *in, int in_len,
                     uint8_t *out, int type);
int cslip_decompress_packet(cslip_ctx_t *ctx, const uint8_t *in, int in_len,
                            uint8_t *out);

#ifdef __cplusplus
}
#endif

#endif /* EMU_MODEM_CSLIP_H */
