/*
 * 86Box    A hypervisor and IBM PC system emulator that specializes in
 *          running old operating systems and software designed for IBM
 *          PC systems and compatibles from 1981 through fairly recent
 *          system designs based on the PCI bus.
 *
 *          This file is part of the 86Box distribution.
 *
 *          Definitions for the 86F floppy image format.
 *
 * Authors: Miran Grca, <mgrca8@gmail.com>
 *          Fred N. van Kempen, <decwiz@yahoo.com>
 *
 *          Copyright 2016-2025 Miran Grca.
 *          Copyright 2018-2025 Fred N. van Kempen.
 */
#ifndef EMU_FLOPPY_86F_H
#define EMU_FLOPPY_86F_H

#define D86FVER 0x020C

/* Thesere were borrowed from TeleDisk. */
#define SECTOR_DUPLICATED   0x01
#define SECTOR_CRC_ERROR    0x02
#define SECTOR_DELETED_DATA 0x04
#define SECTOR_DATA_SKIPPED 0x10
#define SECTOR_NO_DATA      0x20
#define SECTOR_NO_ID        0x40

#define length_gap0         80
#define length_gap1         50
#define length_sync         12
#define length_am           4
#define length_crc          2

#define IBM
#define MFM
#ifdef IBM
#    define pre_gap1 length_gap0 + length_sync + length_am
#else
#    define pre_gap1 0
#endif

#define pre_track pre_gap1 + length_gap1
#define pre_gap   length_sync + length_am + 4 + length_crc
#define pre_data  length_sync + length_am
#define post_gap  length_crc

extern void     d86f_init(void);
extern void     d86f_load(void *priv, char *fn);
extern void     d86f_close(void *priv);
extern void     d86f_seek(void *priv, int track);
extern int      d86f_hole(void *priv);
extern uint64_t d86f_byteperiod(void *priv);
extern void     d86f_stop(void *priv);
extern void     d86f_poll(void *priv);
extern int      d86f_realtrack(int track, void *priv);
extern void     d86f_reset(void *priv, int side);
extern void     d86f_readsector(void *priv, int sector, int track, int side, int rate,
                                int sector_size);
extern void     d86f_writesector(void *priv, int sector, int track, int side, int rate,
                                 int sector_size);
extern void     d86f_comparesector(void *priv, int sector, int track, int side, int rate,
                                   int sector_size);
extern void     d86f_readaddress(void *priv, int side, int density);
extern void     d86f_format(void *priv, int side, int density, uint8_t fill);

extern void     d86f_prepare_track_layout(void *priv, int side);
extern void     d86f_set_version(void *priv, uint16_t version);
extern uint16_t d86f_side_flags(void *priv);
extern uint16_t d86f_track_flags(void *priv);
extern void     d86f_initialize_last_sector_id(void *priv, int c, int h, int r, int n);
extern void     d86f_initialize_linked_lists(void *priv);
extern void     d86f_destroy_linked_lists(void *priv, int side);

extern uint16_t d86f_prepare_sector(void* priv, int side, int prev_pos, uint8_t *id_buf,
                                    uint8_t *data_buf, int data_len, int gap2, int gap3,
                                    int flags);
extern void     d86f_setup(void *priv);
extern void     d86f_destroy(void *priv);
extern int      d86f_export(void *priv, char *fn);
extern void     d86f_unregister(void *priv);
extern void     d86f_common_handlers(void *priv);
extern int      d86f_is_40_track(void *priv);
extern void     d86f_reset_index_hole_pos(void *priv, int side);
extern uint16_t d86f_prepare_pretrack(void *priv, int side, int iso);
extern void     d86f_set_track_pos(void *priv, uint32_t track_pos);
extern uint32_t d86f_get_track_pos(void *priv);
extern uint32_t d86f_get_raw_size(void *priv, int side);
extern void     d86f_set_cur_track(void *priv, int track);
extern void     d86f_zero_track(void *priv);

extern uint16_t *common_encoded_data(void *priv, int side);
extern void      common_read_revolution(void *priv);
extern uint32_t  common_get_raw_size(void *priv, int side);

extern void     null_writeback(void *priv);
extern void     null_write_data(void *priv, int side, uint16_t pos, uint8_t data);
extern int      null_format_conditions(void *priv);
extern int32_t  null_extra_bit_cells(void *priv, int side);
extern void     null_set_sector(void *priv, int side,
                                uint8_t c, uint8_t h, uint8_t r, uint8_t n);
extern uint32_t null_index_hole_pos(void *priv, int side);

#endif /*EMU_FLOPPY_86F_H*/
