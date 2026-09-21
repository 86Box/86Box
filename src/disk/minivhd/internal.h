/*
 * MiniVHD	Minimalist VHD implementation in C.
 *
 *		This file is part of the MiniVHD Project.
 *
 *		Internal definitions.
 *
 * Version:	@(#)internal.h	1.0.1	2021/03/15
 *
 * Author:	Sherman Perry, <shermperry@gmail.com>
 *
 *		Copyright 2019-2021 Sherman Perry.
 *
 *		MIT License
 *
 *		Permission is hereby granted, free of  charge, to any person
 *		obtaining a copy of this software  and associated documenta-
 *		tion files (the "Software"), to deal in the Software without
 *		restriction, including without limitation the rights to use,
 *		copy, modify, merge, publish, distribute, sublicense, and/or
 *		sell copies of  the Software, and  to permit persons to whom
 *		the Software is furnished to do so, subject to the following
 *		conditions:
 *
 *		The above  copyright notice and this permission notice shall
 *		be included in  all copies or  substantial  portions of  the
 *		Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING  BUT NOT LIMITED TO THE  WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A  PARTICULAR PURPOSE AND NONINFRINGEMENT. IN  NO EVENT  SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER  IN AN ACTION OF  CONTRACT, TORT OR  OTHERWISE, ARISING
 * FROM, OUT OF  O R IN  CONNECTION WITH THE  SOFTWARE OR  THE USE  OR  OTHER
 * DEALINGS IN THE SOFTWARE.
 */
#ifndef MINIVHD_INTERNAL_H
# define MINIVHD_INTERNAL_H


#define MVHD_FOOTER_SIZE       512
#define MVHD_SPARSE_SIZE       1024

#define MVHD_SECTOR_SIZE       512
#define MVHD_BAT_ENT_PER_SECT  128

#define MVHD_MAX_SIZE_IN_BYTES 0x1fe00000000

#define MVHD_SPARSE_BLK        0xffffffff

/* For simplicity, we don't handle paths longer than this
 * Note, this is the max path in characters, as that is what
 * Windows uses
 */
#define MVHD_MAX_PATH_CHARS    260
#define MVHD_MAX_PATH_BYTES    1040

#define MVHD_DIF_LOC_W2RU      0x57327275
#define MVHD_DIF_LOC_W2KU      0x57326B75

#define MVHD_START_TS          946684800


typedef struct MVHDSectorBitmap {
    uint8_t* curr_bitmap;
    int      sector_count;
    int      curr_block;
} MVHDSectorBitmap;

typedef struct MVHDFooter {
    uint8_t  cookie[8];
    uint32_t features;
    uint32_t fi_fmt_vers;
    uint64_t data_offset;
    uint32_t timestamp;
    uint8_t  cr_app[4];
    uint32_t cr_vers;
    uint8_t  cr_host_os[4];
    uint64_t orig_sz;
    uint64_t curr_sz;
    struct {
        uint16_t cyl;
        uint8_t  heads;
        uint8_t  spt;
    } geom;
    uint32_t disk_type;
    uint32_t checksum;
    uint8_t  uuid[16];
    uint8_t  saved_st;
    uint8_t  reserved[427];
} MVHDFooter;

typedef struct MVHDSparseHeader {
    uint8_t  cookie[8];
    uint64_t data_offset;
    uint64_t bat_offset;
    uint32_t head_vers;
    uint32_t max_bat_ent;
    uint32_t block_sz;
    uint32_t checksum;
    uint8_t  par_uuid[16];
    uint32_t par_timestamp;
    uint32_t reserved_1;
    uint8_t  par_utf16_name[512];
    struct {
        uint32_t plat_code;
        uint32_t plat_data_space;
        uint32_t plat_data_len;
        uint32_t reserved;
        uint64_t plat_data_offset;
    } par_loc_entry[8];
    uint8_t  reserved_2[256];
} MVHDSparseHeader;

struct MVHDMeta {
    FILE*            f;
    bool             readonly;
    bool             error;
    char             filename[MVHD_MAX_PATH_BYTES];
    struct MVHDMeta* parent;
    MVHDFooter       footer;
    MVHDSparseHeader sparse;
    uint32_t*        block_offset;
    int              sect_per_block;
    MVHDSectorBitmap bitmap;
    int (*read_sectors)(struct MVHDMeta*, uint32_t, int, void*);
    int (*write_sectors)(struct MVHDMeta*, uint32_t, int, void*);
    struct {
        uint8_t* zero_data;
        int      sector_count;
    } format_buffer;
};


#ifdef __cplusplus
extern "C" {
#endif

/**
 * Functions to deal with endian issues
 */
uint16_t mvhd_from_be16(uint16_t val);
uint32_t mvhd_from_be32(uint32_t val);
uint64_t mvhd_from_be64(uint64_t val);
uint16_t mvhd_to_be16(uint16_t val);
uint32_t mvhd_to_be32(uint32_t val);
uint64_t mvhd_to_be64(uint64_t val);

/**
 * \brief Check if provided buffer begins with the string "conectix"
 * 
 * \param [in] buffer The buffer to compare. Must be at least 8 bytes in length
 * 
 * \return true if the buffer begins with "conectix"
 * \return false if the buffer does not begin with "conectix"
 */
bool mvhd_is_conectix_str(const void* buffer);

/**
 * \brief Generate a raw 16 byte UUID
 * 
 * \param [out] uuid A 16 byte buffer in which the generated UUID will be stored to
 */
void mvhd_generate_uuid(uint8_t *uuid);

/**
 * \brief Calculate a VHD formatted timestamp from the current time
 */
uint32_t vhd_calc_timestamp(void);

/**
 * \brief Convert an epoch timestamp to a VHD timestamp
 * 
 * \param [in] ts epoch timestamp to convert.
 * 
 * \return The adjusted timestamp, or 0 if the input timestamp is
 * earlier that 1 Janurary 2000
 */
uint32_t mvhd_epoch_to_vhd_ts(time_t ts);

/**
 * \brief Return the created time from a VHD image
 * 
 * \param [in] vhdm Pointer to the MiniVHD metadata structure
 * 
 * \return The created time, as a Unix timestamp
 */
time_t vhd_get_created_time(struct MVHDMeta *vhdm);

/**
 * \brief Cross platform, unicode filepath opening
 * 
 * This function accounts for the fact that fopen() handles file paths differently compared to other 
 * operating systems. Windows version of fopen() will not handle multi byte encoded text like UTF-8. 
 * 
 * Unicode filepath support on Windows requires using the _wfopen() function, which expects UTF-16LE 
 * encoded path and modestring.
 * 
 * \param [in] path The filepath to open as a UTF-8 string
 * \param [in] mode The mode string to use (eg: "rb+"")
 * \param [out] err The error value, if an error occurrs
 * 
 * \return a FILE pointer if successful, NULL otherwise. If NULL, check the value of err
 */
FILE* mvhd_fopen(const char* path, const char* mode, int* err);

void mvhd_set_encoding_err(int encoding_retval, int* err);

/**
 * \brief Generate VHD footer checksum
 * 
 * \param [in] vhdm MiniVHD data structure
 */
uint32_t mvhd_gen_footer_checksum(MVHDFooter* footer);

/**
 * \brief Generate VHD sparse header checksum
 * 
 * \param [in] vhdm MiniVHD data structure
 */
uint32_t mvhd_gen_sparse_checksum(MVHDSparseHeader* header);

uint32_t mvhd_crc32_for_byte(uint32_t r);

/**
 * \brief Get current position in file stream
 * 
 * This is a portable version of the POSIX ftello64(). * 
 */
int64_t mvhd_ftello64(FILE* stream);

/**
 * \brief Reposition the file stream's position
 * 
 * This is a portable version of the POSIX fseeko64(). * 
 */
int mvhd_fseeko64(FILE* stream, int64_t offset, int origin);

/**
 * \brief Calculate the CRC32 of a data buffer.
 * 
 * This function can be used for verifying data integrity.
 * 
 * \param [in] data The data buffer
 * \param [in] n_bytes The size of the data buffer in bytes
 * 
 * \return The CRC32 of the data buffer
 */
uint32_t mvhd_crc32(const void* data, size_t n_bytes);

/**
 * \brief Calculate the file modification timestamp.
 * 
 * This function is primarily to help protect differencing VHD's
 * 
 * \param [in] path the UTF-8 file path
 * \param [out] err The error value, if an error occurrs
 * 
 * \return The file modified timestamp, in VHD compatible timestamp.
 * 'err' will be set to non-zero on error
 */
uint32_t mvhd_file_mod_timestamp(const char* path, int *err);

struct MVHDMeta* mvhd_create_fixed_raw(const char* path, FILE* raw_img, uint64_t size_in_bytes, MVHDGeom* geom, int* err, mvhd_progress_callback progress_callback);

/**
 * \brief Write zero filled sectors to file.
 * 
 * Note, the caller should set the file position before calling this 
 * function for correct operation.
 * 
 * \param [in] f File to write sectors to
 * \param [in] sector_count The number of sectors to write
 */
bool mvhd_write_empty_sectors(FILE* f, int sector_count);

/**
 * \brief Read a fixed VHD image
 * 
 * Fixed VHD images are essentially raw image files with a footer tacked on 
 * the end. They are therefore straightforward to write
 * 
 * \param [in] vhdm MiniVHD data structure
 * \param [in] offset Sector offset to read from
 * \param [in] num_sectors The desired number of sectors to read
 * \param [out] out_buff An output buffer to store read sectors. Must be 
 * large enough to hold num_sectors worth of sectors.
 * 
 * \retval 0 num_sectors were read from file
 * \retval >0 < num_sectors were read from file
 */
int mvhd_fixed_read(struct MVHDMeta* vhdm, uint32_t offset, int num_sectors, void* out_buff);

/**
 * \brief Read a sparse VHD image
 * 
 * Sparse, or dynamic images are VHD images that grow as data is written to them. 
 * 
 * This function implements the logic to read sectors from the file, taking into 
 * account the fact that blocks may be stored on disk in any order, and that the 
 * read could cross block boundaries.
 * 
 * \param [in] vhdm MiniVHD data structure
 * \param [in] offset Sector offset to read from
 * \param [in] num_sectors The desired number of sectors to read
 * \param [out] out_buff An output buffer to store read sectors. Must be 
 * large enough to hold num_sectors worth of sectors.
 * 
 * \retval 0 num_sectors were read from file
 * \retval >0 < num_sectors were read from file
 */
int mvhd_sparse_read(struct MVHDMeta* vhdm, uint32_t offset, int num_sectors, void* out_buff);

/**
 * \brief Read a differencing VHD image
 * 
 * Differencing images are a variant of a sparse image. They contain the grow-on-demand 
 * properties of sparse images, but also reference a parent image. Data is read from the 
 * child image only if it is newer than the data stored in the parent image.
 * 
 * This function implements the logic to read sectors from the child, or a parent image. 
 * Differencing images may have a differencing image as a parent, creating a chain of images. 
 * There is no theoretical chain length limit, although I do not consider long chains to be 
 * advisable. Verifying the parent-child relationship is not very robust.
 * 
 * \param [in] vhdm MiniVHD data structure
 * \param [in] offset Sector offset to read from
 * \param [in] num_sectors The desired number of sectors to read
 * \param [out] out_buff An output buffer to store read sectors. Must be 
 * large enough to hold num_sectors worth of sectors.
 * 
 * \retval 0 num_sectors were read from file
 * \retval >0 < num_sectors were read from file
 */
int mvhd_diff_read(struct MVHDMeta* vhdm, uint32_t offset, int num_sectors, void* out_buff);

/**
 * \brief Write to a fixed VHD image
 * 
 * Fixed VHD images are essentially raw image files with a footer tacked on 
 * the end. They are therefore straightforward to write
 * 
 * \param [in] vhdm MiniVHD data structure
 * \param [in] offset Sector offset to write to
 * \param [in] num_sectors The desired number of sectors to write
 * \param [in] in_buff A source buffer to write sectors from. Must be 
 * large enough to hold num_sectors worth of sectors.
 * 
 * \retval 0 num_sectors were written to file
 * \retval >0 < num_sectors were written to file
 */
int mvhd_fixed_write(struct MVHDMeta* vhdm, uint32_t offset, int num_sectors, void* in_buff);

/**
 * \brief Write to a sparse or differencing VHD image
 * 
 * Sparse, or dynamic images are VHD images that grow as data is written to them. 
 * 
 * Differencing images are a variant of a sparse image. They contain the grow-on-demand 
 * properties of sparse images, but also reference a parent image. Data is always written 
 * to the child image. This makes writing to differencing images essentially identical to 
 * writing to sparse images, hence they use the same function.
 * 
 * This function implements the logic to write sectors to the file, taking into 
 * account the fact that blocks may be stored on disk in any order, and that the 
 * write operation could cross block boundaries.
 * 
 * \param [in] vhdm MiniVHD data structure
 * \param [in] offset Sector offset to write to
 * \param [in] num_sectors The desired number of sectors to write
 * \param [in] in_buff A source buffer to write sectors from. Must be 
 * large enough to hold num_sectors worth of sectors.
 * 
 * \retval 0 num_sectors were written to file
 * \retval >0 < num_sectors were written to file
 */
int mvhd_sparse_diff_write(struct MVHDMeta* vhdm, uint32_t offset, int num_sectors, void* in_buff);

/**
 * \brief A no-op function to "write" to read-only VHD images
 * 
 * \param [in] vhdm MiniVHD data structure
 * \param [in] offset Sector offset to write to
 * \param [in] num_sectors The desired number of sectors to write
 * \param [in] in_buff A source buffer to write sectors from. Must be 
 * large enough to hold num_sectors worth of sectors.
 * 
 * \retval 0 num_sectors were written to file
 * \retval >0 < num_sectors were written to file
 */
int mvhd_noop_write(struct MVHDMeta* vhdm, uint32_t offset, int num_sectors, void* in_buff);

/**
 * \brief Save the contents of a VHD footer from a buffer to a struct
 * 
 * \param [out] footer save contents of buffer into footer
 * \param [in] buffer VHD footer in raw bytes
 */
void mvhd_buffer_to_footer(MVHDFooter* footer, uint8_t* buffer);

/**
 * \brief Save the contents of a VHD sparse header from a buffer to a struct
 * 
 * \param [out] header save contents of buffer into header
 * \param [in] buffer VHD header in raw bytes
 */
void mvhd_buffer_to_header(MVHDSparseHeader* header, uint8_t* buffer);

/**
 * \brief Save the contents of a VHD footer struct to a buffer
 * 
 * \param [in] footer save contents of struct into buffer
 * \param [out] buffer VHD footer in raw bytes
 */
void mvhd_footer_to_buffer(MVHDFooter* footer, uint8_t* buffer);

/**
 * \brief Save the contents of a VHD sparse header struct to a buffer
 * 
 * \param [in] header save contents of struct into buffer
 * \param [out] buffer VHD sparse header in raw bytes
 */
void mvhd_header_to_buffer(MVHDSparseHeader* header, uint8_t* buffer);

#ifdef __cplusplus
}
#endif


#endif /*MINIVHD_INTERNAL_H*/
