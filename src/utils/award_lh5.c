/*
 * 86Box    A hypervisor and IBM PC system emulator that specializes in
 *          running old operating systems and software designed based on the
 *          IBM PC architecture.
 *
 *          This file is part of the 86Box distribution.
 */
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <86box/award_lh5.h>
#include "lhasa/lha_decoder.h"
extern const LHADecoderType lha_lh5_decoder;
typedef struct award_lh5_input_t {
    const uint8_t *data;
    size_t length;
    size_t pos;
} award_lh5_input_t;
static size_t
award_lh5_read_input(void *buf, size_t buf_len, void *priv)
{
    award_lh5_input_t *input = (award_lh5_input_t *) priv;
    size_t left = input->length - input->pos;
    if (buf_len > left)
        buf_len = left;
    if (buf_len) {
        memcpy(buf, input->data + input->pos, buf_len);
        input->pos += buf_len;
    }
    return buf_len;
}
int
award_lh5_decode(const uint8_t *src, size_t src_len, uint8_t *dest, size_t dest_len)
{
    award_lh5_input_t input = { src, src_len, 0 };
    uint8_t *block;
    void *state;
    size_t pos = 0;
    state = calloc(1, lha_lh5_decoder.extra_size);
    block = malloc(lha_lh5_decoder.max_read);
    if (!state || !block)
        goto fail;
    if (!lha_lh5_decoder.init(state, award_lh5_read_input, &input))
        goto fail;
    while (pos < dest_len) {
        size_t count = lha_lh5_decoder.read(state, block);
        if (!count || count > dest_len - pos)
            goto fail_decoder;
        memcpy(dest + pos, block, count);
        pos += count;
    }
    if (lha_lh5_decoder.free)
        lha_lh5_decoder.free(state);
    free(block);
    free(state);
    return 1;
fail_decoder:
    if (lha_lh5_decoder.free)
        lha_lh5_decoder.free(state);
fail:
    free(block);
    free(state);
    return 0;
}
