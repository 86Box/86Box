/*
 * This file is part of the Aaru Data Preservation Suite.
 * Copyright (c) 2019-2026 Natalia Portillo.
 *
 * This library is free software; you can redistribute it and/or modify
 * it under the terms of the GNU Lesser General Public License as
 * published by the Free Software Foundation; either version 2.1 of the
 * License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, see <http://www.gnu.org/licenses/>.
 */

#ifndef LIBAARUFORMAT_DECLS_H
#define LIBAARUFORMAT_DECLS_H

#include "aaru.h"
#include "crc64.h"
#include "enums.h"
#include "md5.h"
#include "sha1.h"
#include "sha256.h"
#include "simd.h"
#include "spamsum.h"
#include "structs/optical.h"

#ifdef __cplusplus
#define EXTERNC extern "C"
#else
#define EXTERNC
#endif

#if defined(_WIN32)
#define AARU_CALL   __stdcall
#define AARU_EXPORT EXTERNC __declspec(dllexport)
#define AARU_LOCAL
#ifndef PATH_MAX
#define PATH_MAX _MAX_PATH
#endif
#else
#define AARU_CALL
#if defined(__APPLE__)
#define AARU_EXPORT EXTERNC __attribute__((visibility("default")))
#define AARU_LOCAL  __attribute__((visibility("hidden")))
#else
#if __GNUC__ >= 4
#define AARU_EXPORT EXTERNC __attribute__((visibility("default")))
#define AARU_LOCAL  __attribute__((visibility("hidden")))
#else
#define AARU_EXPORT EXTERNC
#define AARU_LOCAL
#endif
#endif
#endif

#ifdef _MSC_VER
#define FORCE_INLINE static inline
#else
#define FORCE_INLINE static inline __attribute__((always_inline))
#endif

AARU_EXPORT int AARU_CALL aaruf_identify(const char *filename);

AARU_EXPORT int AARU_CALL aaruf_identify_stream(FILE *image_stream);

AARU_EXPORT void *AARU_CALL aaruf_open(const char *filepath, bool resume_mode, const char *options);

AARU_EXPORT void *AARU_CALL aaruf_create(const char *filepath, uint32_t media_type, uint32_t sector_size,
                                         uint64_t user_sectors, uint64_t negative_sectors, uint64_t overflow_sectors,
                                         const char *options, const uint8_t *application_name,
                                         uint8_t application_name_length, uint8_t application_major_version,
                                         uint8_t application_minor_version, bool is_tape);

AARU_EXPORT int AARU_CALL aaruf_close(void *context);

AARU_EXPORT int32_t AARU_CALL aaruf_read_media_tag(void *context, uint8_t *data, int32_t tag, uint32_t *length);

AARU_EXPORT crc64_ctx *AARU_CALL aaruf_crc64_init();
AARU_EXPORT int AARU_CALL        aaruf_crc64_update(crc64_ctx *ctx, const uint8_t *data, uint32_t len);
AARU_EXPORT int AARU_CALL        aaruf_crc64_final(crc64_ctx *ctx, uint64_t *crc);
AARU_EXPORT void AARU_CALL       aaruf_crc64_free(crc64_ctx *ctx);
AARU_EXPORT void AARU_CALL       aaruf_crc64_slicing(uint64_t *previous_crc, const uint8_t *data, uint32_t len);
AARU_EXPORT uint64_t AARU_CALL   aaruf_crc64_data(const uint8_t *data, uint32_t len);

AARU_EXPORT int32_t AARU_CALL aaruf_get_tracks(const void *context, uint8_t *buffer, size_t *length);
AARU_EXPORT int32_t AARU_CALL aaruf_set_tracks(void *context, TrackEntry *tracks, const int count);
AARU_EXPORT int32_t AARU_CALL aaruf_get_flux_captures(void *context, uint8_t *buffer, size_t *length);
AARU_EXPORT int32_t AARU_CALL aaruf_read_flux_capture(void *context, uint32_t head, uint16_t track, uint8_t subtrack,
                                                      uint32_t capture_index, uint8_t *index_data,
                                                      uint32_t *index_length, uint8_t *data_data,
                                                      uint32_t *data_length);
AARU_EXPORT int32_t AARU_CALL aaruf_write_flux_capture(void *context, uint32_t head, uint16_t track, uint8_t subtrack,
                                                     uint32_t capture_index, uint64_t data_resolution,
                                                     uint64_t index_resolution, const uint8_t *data,
                                                     uint32_t data_length, const uint8_t *index,
                                                     uint32_t index_length);
AARU_EXPORT int32_t AARU_CALL aaruf_clear_flux_captures(void *context);

AARU_EXPORT int32_t AARU_CALL aaruf_read_sector(void *context, uint64_t sector_address, bool negative, uint8_t *data,
                                                uint32_t *length, uint8_t *sector_status);
AARU_EXPORT int32_t AARU_CALL aaruf_read_sector_long(void *context, uint64_t sector_address, bool negative,
                                                     uint8_t *data, uint32_t *length, uint8_t *sector_status);

AARU_EXPORT int32_t AARU_CALL aaruf_write_sector(void *context, uint64_t sector_address, bool negative,
                                                 const uint8_t *data, uint8_t sector_status, uint32_t length);
AARU_EXPORT int32_t AARU_CALL aaruf_write_sector_long(void *context, uint64_t sector_address, bool negative,
                                                      const uint8_t *data, uint8_t sector_status, uint32_t length);
AARU_EXPORT int32_t AARU_CALL aaruf_write_media_tag(void *context, const uint8_t *data, int32_t type, uint32_t length);
AARU_EXPORT int32_t AARU_CALL aaruf_write_sector_tag(void *context, uint64_t sector_address, bool negative,
                                                     const uint8_t *data, size_t length, int32_t tag);

AARU_EXPORT int32_t AARU_CALL aaruf_verify_image(void *context);

AARU_EXPORT int32_t AARU_CALL aaruf_cst_transform(const uint8_t *interleaved, uint8_t *sequential, size_t length);

AARU_EXPORT int32_t AARU_CALL aaruf_cst_untransform(const uint8_t *sequential, uint8_t *interleaved, size_t length);

AARU_EXPORT void *AARU_CALL aaruf_ecc_cd_init();

AARU_EXPORT void AARU_CALL aaruf_ecc_cd_free(void *context);

AARU_EXPORT bool AARU_CALL aaruf_ecc_cd_is_suffix_correct(void *context, const uint8_t *sector);

AARU_EXPORT bool AARU_CALL aaruf_ecc_cd_is_suffix_correct_mode2(void *context, const uint8_t *sector);

AARU_EXPORT bool AARU_CALL aaruf_ecc_cd_check(void *context, const uint8_t *address, const uint8_t *data,
                                              uint32_t major_count, uint32_t minor_count, uint32_t major_mult,
                                              uint32_t minor_inc, const uint8_t *ecc, int32_t address_offset,
                                              int32_t data_offset, int32_t ecc_offset);

AARU_EXPORT void AARU_CALL aaruf_ecc_cd_write(void *context, const uint8_t *address, const uint8_t *data,
                                              uint32_t major_count, uint32_t minor_count, uint32_t major_mult,
                                              uint32_t minor_inc, uint8_t *ecc, int32_t address_offset,
                                              int32_t data_offset, int32_t ecc_offset);

AARU_EXPORT void AARU_CALL aaruf_ecc_cd_write_sector(void *context, const uint8_t *address, const uint8_t *data,
                                                     uint8_t *ecc, int32_t address_offset, int32_t data_offset,
                                                     int32_t ecc_offset);

AARU_LOCAL void AARU_CALL aaruf_cd_lba_to_msf(int64_t pos, uint8_t *minute, uint8_t *second, uint8_t *frame);

AARU_EXPORT void AARU_CALL aaruf_ecc_cd_reconstruct_prefix(uint8_t *sector, uint8_t type, int64_t lba);

AARU_EXPORT void AARU_CALL aaruf_ecc_cd_reconstruct(void *context, uint8_t *sector, uint8_t type);

AARU_EXPORT uint32_t AARU_CALL aaruf_edc_cd_compute(void *context, uint32_t edc, const uint8_t *src, int size, int pos);

AARU_EXPORT int32_t AARU_CALL aaruf_read_track_sector(void *context, uint8_t *data, uint64_t sector_address,
                                                      uint32_t *length, uint8_t track, uint8_t *sector_status);
AARU_EXPORT int32_t AARU_CALL aaruf_read_sector_tag(const void *context, uint64_t sector_address, bool negative,
                                                    uint8_t *buffer, uint32_t *length, int32_t tag);

AARU_LOCAL int32_t AARU_CALL aaruf_get_media_tag_type_for_datatype(int32_t type);
AARU_LOCAL int32_t AARU_CALL aaruf_get_datatype_for_media_tag_type(int32_t tag_type);
AARU_LOCAL int32_t AARU_CALL aaruf_get_xml_mediatype(int32_t type);

AARU_EXPORT int32_t AARU_CALL aaruf_get_geometry(const void *context, uint32_t *cylinders, uint32_t *heads,
                                                 uint32_t *sectors_per_track);
AARU_EXPORT int32_t AARU_CALL aaruf_set_geometry(void *context, uint32_t cylinders, uint32_t heads,
                                                 uint32_t sectors_per_track);
AARU_EXPORT int32_t AARU_CALL aaruf_set_media_sequence(void *context, int32_t sequence, int32_t last_sequence);
AARU_EXPORT int32_t AARU_CALL aaruf_set_creator(void *context, const uint8_t *data, int32_t length);
AARU_EXPORT int32_t AARU_CALL aaruf_set_comments(void *context, const uint8_t *data, int32_t length);
AARU_EXPORT int32_t AARU_CALL aaruf_set_media_title(void *context, const uint8_t *data, int32_t length);
AARU_EXPORT int32_t AARU_CALL aaruf_set_media_manufacturer(void *context, const uint8_t *data, int32_t length);
AARU_EXPORT int32_t AARU_CALL aaruf_set_media_model(void *context, const uint8_t *data, int32_t length);
AARU_EXPORT int32_t AARU_CALL aaruf_set_media_serial_number(void *context, const uint8_t *data, int32_t length);
AARU_EXPORT int32_t AARU_CALL aaruf_set_media_barcode(void *context, const uint8_t *data, int32_t length);
AARU_EXPORT int32_t AARU_CALL aaruf_set_media_part_number(void *context, const uint8_t *data, int32_t length);
AARU_EXPORT int32_t AARU_CALL aaruf_set_drive_manufacturer(void *context, const uint8_t *data, int32_t length);
AARU_EXPORT int32_t AARU_CALL aaruf_set_drive_model(void *context, const uint8_t *data, int32_t length);
AARU_EXPORT int32_t AARU_CALL aaruf_set_drive_serial_number(void *context, const uint8_t *data, int32_t length);
AARU_EXPORT int32_t AARU_CALL aaruf_set_drive_firmware_revision(void *context, const uint8_t *data, int32_t length);
AARU_EXPORT int32_t AARU_CALL aaruf_get_media_sequence(const void *context, int32_t *sequence, int32_t *last_sequence);
AARU_EXPORT int32_t AARU_CALL aaruf_get_creator(const void *context, uint8_t *buffer, int32_t *length);
AARU_EXPORT int32_t AARU_CALL aaruf_get_comments(const void *context, uint8_t *buffer, int32_t *length);
AARU_EXPORT int32_t AARU_CALL aaruf_get_media_title(const void *context, uint8_t *buffer, int32_t *length);
AARU_EXPORT int32_t AARU_CALL aaruf_get_media_manufacturer(const void *context, uint8_t *buffer, int32_t *length);
AARU_EXPORT int32_t AARU_CALL aaruf_get_media_model(const void *context, uint8_t *buffer, int32_t *length);
AARU_EXPORT int32_t AARU_CALL aaruf_get_media_serial_number(const void *context, uint8_t *buffer, int32_t *length);
AARU_EXPORT int32_t AARU_CALL aaruf_get_media_barcode(const void *context, uint8_t *buffer, int32_t *length);
AARU_EXPORT int32_t AARU_CALL aaruf_get_media_part_number(const void *context, uint8_t *buffer, int32_t *length);
AARU_EXPORT int32_t AARU_CALL aaruf_get_drive_manufacturer(const void *context, uint8_t *buffer, int32_t *length);
AARU_EXPORT int32_t AARU_CALL aaruf_get_drive_model(const void *context, uint8_t *buffer, int32_t *length);
AARU_EXPORT int32_t AARU_CALL aaruf_get_drive_serial_number(const void *context, uint8_t *buffer, int32_t *length);
AARU_EXPORT int32_t AARU_CALL aaruf_get_drive_firmware_revision(const void *context, uint8_t *buffer, int32_t *length);
AARU_EXPORT int32_t AARU_CALL aaruf_get_cicm_metadata(const void *context, uint8_t *buffer, size_t *length);
AARU_EXPORT int32_t AARU_CALL aaruf_get_aaru_json_metadata(const void *context, uint8_t *buffer, size_t *length);
AARU_EXPORT int32_t AARU_CALL aaruf_set_aaru_json_metadata(void *context, uint8_t *data, size_t length);
AARU_EXPORT int32_t AARU_CALL aaruf_get_user_sectors(const void *context, uint64_t *sectors);
AARU_EXPORT int32_t AARU_CALL aaruf_get_negative_sectors(const void *context, uint32_t *sectors);
AARU_EXPORT int32_t AARU_CALL aaruf_get_overflow_sectors(const void *context, uint32_t *sectors);
AARU_EXPORT int32_t AARU_CALL aaruf_get_image_info(const void *context, ImageInfo *image_info);
AARU_EXPORT int32_t AARU_CALL aaruf_clear_media_sequence(void *context);
AARU_EXPORT int32_t AARU_CALL aaruf_clear_creator(void *context);
AARU_EXPORT int32_t AARU_CALL aaruf_clear_comments(void *context);
AARU_EXPORT int32_t AARU_CALL aaruf_clear_media_title(void *context);
AARU_EXPORT int32_t AARU_CALL aaruf_clear_media_manufacturer(void *context);
AARU_EXPORT int32_t AARU_CALL aaruf_clear_media_model(void *context);
AARU_EXPORT int32_t AARU_CALL aaruf_clear_media_serial_number(void *context);
AARU_EXPORT int32_t AARU_CALL aaruf_clear_media_barcode(void *context);
AARU_EXPORT int32_t AARU_CALL aaruf_clear_media_part_number(void *context);
AARU_EXPORT int32_t AARU_CALL aaruf_clear_drive_manufacturer(void *context);
AARU_EXPORT int32_t AARU_CALL aaruf_clear_drive_model(void *context);
AARU_EXPORT int32_t AARU_CALL aaruf_clear_drive_serial_number(void *context);
AARU_EXPORT int32_t AARU_CALL aaruf_clear_drive_firmware_revision(void *context);
AARU_EXPORT int32_t AARU_CALL aaruf_get_readable_sector_tags(const void *context, uint8_t *buffer, size_t *length);
AARU_EXPORT int32_t AARU_CALL aaruf_get_readable_media_tags(const void *context, uint8_t *buffer, size_t *length);

AARU_EXPORT int32_t AARU_CALL aaruf_get_tape_file(const void *context, uint8_t partition, uint32_t file,
                                                  uint64_t *starting_block, uint64_t *ending_block);
AARU_EXPORT int32_t AARU_CALL aaruf_set_tape_file(void *context, uint8_t partition, uint32_t file,
                                                  uint64_t starting_block, uint64_t ending_block);
AARU_EXPORT int32_t AARU_CALL aaruf_get_tape_partition(const void *context, uint8_t partition, uint64_t *starting_block,
                                                       uint64_t *ending_block);
AARU_EXPORT int32_t AARU_CALL aaruf_set_tape_partition(void *context, uint8_t partition, uint64_t starting_block,
                                                       uint64_t ending_block);
AARU_EXPORT int32_t AARU_CALL aaruf_get_all_tape_files(const void *context, uint8_t *buffer, size_t *length);
AARU_EXPORT int32_t AARU_CALL aaruf_get_all_tape_partitions(const void *context, uint8_t *buffer, size_t *length);

AARU_EXPORT int32_t AARU_CALL aaruf_get_dumphw(void *context, uint8_t *buffer, size_t *length);
AARU_EXPORT int32_t AARU_CALL aaruf_set_dumphw(void *context, uint8_t *data, size_t length);

AARU_EXPORT spamsum_ctx *AARU_CALL aaruf_spamsum_init(void);
AARU_EXPORT int AARU_CALL          aaruf_spamsum_update(spamsum_ctx *ctx, const uint8_t *data, uint32_t len);
AARU_EXPORT int AARU_CALL          aaruf_spamsum_final(spamsum_ctx *ctx, uint8_t *result);
AARU_EXPORT void AARU_CALL         aaruf_spamsum_free(spamsum_ctx *ctx);

AARU_LOCAL void fuzzy_engine_step(spamsum_ctx *ctx, uint8_t c);
AARU_LOCAL void roll_hash(spamsum_ctx *ctx, uint8_t c);
AARU_LOCAL void fuzzy_try_reduce_blockhash(spamsum_ctx *ctx);
AARU_LOCAL void fuzzy_try_fork_blockhash(spamsum_ctx *ctx);

AARU_EXPORT size_t AARU_CALL aaruf_flac_decode_redbook_buffer(uint8_t *dst_buffer, size_t dst_size,
                                                              const uint8_t *src_buffer, size_t src_size);

AARU_EXPORT size_t AARU_CALL aaruf_flac_encode_redbook_buffer(
    uint8_t *dst_buffer, size_t dst_size, const uint8_t *src_buffer, size_t src_size, uint32_t blocksize,
    int32_t do_mid_side_stereo, int32_t loose_mid_side_stereo, const char *apodization, uint32_t max_lpc_order,
    uint32_t qlp_coeff_precision, int32_t do_qlp_coeff_prec_search, int32_t do_exhaustive_model_search,
    uint32_t min_residual_partition_order, uint32_t max_residual_partition_order, const char *application_id,
    uint32_t application_id_len);

AARU_EXPORT int32_t AARU_CALL aaruf_lzma_decode_buffer(uint8_t *dst_buffer, size_t *dst_size, const uint8_t *src_buffer,
                                                       size_t *src_size, const uint8_t *props, size_t props_size);

AARU_EXPORT int32_t AARU_CALL aaruf_lzma_encode_buffer(uint8_t *dst_buffer, size_t *dst_size, const uint8_t *src_buffer,
                                                       size_t src_size, uint8_t *out_props, size_t *out_props_size,
                                                       int32_t level, uint32_t dict_size, int32_t lc, int32_t lp,
                                                       int32_t pb, int32_t fb, int32_t num_threads);

AARU_EXPORT size_t AARU_CALL aaruf_zstd_decode_buffer(uint8_t *dst_buffer, size_t dst_size, const uint8_t *src_buffer,
                                                       size_t src_size);
AARU_EXPORT size_t AARU_CALL aaruf_zstd_encode_buffer(uint8_t *dst_buffer, size_t dst_size, const uint8_t *src_buffer,
                                                       size_t src_size, int level, int num_threads);

AARU_EXPORT void AARU_CALL aaruf_md5_init(md5_ctx *ctx);
AARU_EXPORT void AARU_CALL aaruf_md5_update(md5_ctx *ctx, const void *data, unsigned long size);
AARU_EXPORT void AARU_CALL aaruf_md5_final(md5_ctx *ctx, unsigned char *result);
AARU_EXPORT void AARU_CALL aaruf_md5_buffer(const void *data, unsigned long size, unsigned char *result);

AARU_EXPORT void AARU_CALL aaruf_sha1_init(sha1_ctx *ctx);
AARU_EXPORT void AARU_CALL aaruf_sha1_update(sha1_ctx *ctx, const void *data, unsigned long size);
AARU_EXPORT void AARU_CALL aaruf_sha1_final(sha1_ctx *ctx, unsigned char *result);
AARU_EXPORT void AARU_CALL aaruf_sha1_buffer(const void *data, unsigned long size, unsigned char *result);

AARU_EXPORT void AARU_CALL aaruf_sha256_init(sha256_ctx *ctx);
AARU_EXPORT void AARU_CALL aaruf_sha256_update(sha256_ctx *ctx, const void *data, unsigned long size);
AARU_EXPORT void AARU_CALL aaruf_sha256_final(sha256_ctx *ctx, unsigned char *result);
AARU_EXPORT void AARU_CALL aaruf_sha256_buffer(const void *data, unsigned long size, unsigned char *result);

#if defined(__x86_64__) || defined(__amd64) || defined(_M_AMD64) || defined(_M_X64) || defined(__I386__) || \
    defined(__i386__) || defined(__THW_INTEL) || defined(_M_IX86)

AARU_EXPORT int have_clmul();
AARU_EXPORT int have_ssse3();
AARU_EXPORT int have_avx2();

AARU_EXPORT CLMUL uint64_t AARU_CALL aaruf_crc64_clmul(uint64_t crc, const uint8_t *data, long length);
#endif

#if defined(__aarch64__) || defined(_M_ARM64) || defined(__arm__) || defined(_M_ARM)
AARU_EXPORT int have_neon();
AARU_EXPORT int have_arm_crc32();
AARU_EXPORT int have_arm_crypto();

AARU_EXPORT TARGET_WITH_SIMD uint64_t AARU_CALL aaruf_crc64_vmull(uint64_t previous_crc, const uint8_t *data, long len);
#endif

/* Erasure coding */
AARU_EXPORT int32_t AARU_CALL aaruf_set_erasure_coding(void *context, uint8_t algorithm, uint16_t K, uint16_t M);
AARU_EXPORT int32_t AARU_CALL aaruf_set_erasure_coding_auto(void *context, uint8_t recovery_percent);

#endif  // LIBAARUFORMAT_DECLS_H
