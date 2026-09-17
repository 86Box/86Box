#ifndef NET_IBM_TR_H
#define NET_IBM_TR_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define IBM_TR_MAC_ADDR_LEN 6
#define IBM_TR_LLC_HDR_LEN  3
#define IBM_TR_SNAP_HDR_LEN 8
#define IBM_TR_RIF_MAX_LEN  18

#define IBM_TR_AC_DEFAULT   0x10
#define IBM_TR_FC_LLC_FRAME 0x40

typedef enum ibm_tr_result_t {
    IBM_TR_OK = 0,
    IBM_TR_ERR_SHORT_FRAME,
    IBM_TR_ERR_INVALID_RIF,
    IBM_TR_ERR_UNSUPPORTED_FRAME,
    IBM_TR_ERR_BUFFER_TOO_SMALL,
    IBM_TR_ERR_FRAME_TOO_LARGE
} ibm_tr_result_t;

typedef struct ibm_tr_frame_info_t {
    uint8_t      access_control;
    uint8_t      frame_control;
    uint8_t      dst[IBM_TR_MAC_ADDR_LEN];
    uint8_t      src[IBM_TR_MAC_ADDR_LEN];
    bool         has_rif;
    uint8_t      rif[IBM_TR_RIF_MAX_LEN];
    uint8_t      rif_len;
    const uint8_t *llc;
    size_t       llc_len;
} ibm_tr_frame_info_t;

#ifdef __cplusplus
extern "C" {
#endif

uint8_t ibm_tr_bit_reverse(uint8_t value);
void ibm_tr_reverse_mac(uint8_t out[IBM_TR_MAC_ADDR_LEN], const uint8_t in[IBM_TR_MAC_ADDR_LEN]);
ibm_tr_result_t ibm_tr_parse_frame(const uint8_t *frame, size_t frame_len, ibm_tr_frame_info_t *info);
ibm_tr_result_t ibm_tr_translate_token_ring_to_ethernet(const uint8_t *tr_frame, size_t tr_frame_len, uint8_t *eth_frame, size_t eth_frame_cap, size_t *eth_frame_len);
ibm_tr_result_t ibm_tr_translate_ethernet_to_token_ring(const uint8_t *eth_frame, size_t eth_frame_len, uint8_t *tr_frame, size_t tr_frame_cap, size_t *tr_frame_len);
const char *ibm_tr_result_to_string(ibm_tr_result_t result);

#ifdef __cplusplus
}
#endif

#endif