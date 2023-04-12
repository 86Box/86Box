/*
 * MiniVHD	Minimalist VHD implementation in C.
 *		MiniVHD is a minimalist implementation of read/write/creation
 *		of VHD files. It is designed to read and write to VHD files
 *		at a sector level. It does not enable file access, or provide
 *		mounting options. Those features are left to more advanced
 *		libraries and/or the operating system.
 *
 *		This file is part of the MiniVHD Project.
 *
 *		Definitions for the MiniVHD library.
 *
 * Version:	@(#)minivhd.h	1.0.3	2021/04/16
 *
 * Authors:	Sherman Perry, <shermperry@gmail.com>
 *		Fred N. van Kempen, <waltje@varcem.com>
 *
 *		Copyright 2019-2021 Sherman Perry.
 *		Copyright 2021 Fred N. van Kempen.
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
#ifndef MINIVHD_H
# define MINIVHD_H


typedef enum MVHDError {
    MVHD_ERR_MEM = -128,
    MVHD_ERR_FILE,
    MVHD_ERR_NOT_VHD,
    MVHD_ERR_TYPE,
    MVHD_ERR_FOOTER_CHECKSUM,
    MVHD_ERR_SPARSE_CHECKSUM,
    MVHD_ERR_UTF_TRANSCODING_FAILED,
    MVHD_ERR_UTF_SIZE,
    MVHD_ERR_PATH_REL,
    MVHD_ERR_PATH_LEN,
    MVHD_ERR_PAR_NOT_FOUND,
    MVHD_ERR_INVALID_PAR_UUID,
    MVHD_ERR_INVALID_GEOM,
    MVHD_ERR_INVALID_SIZE,
    MVHD_ERR_INVALID_BLOCK_SIZE,
    MVHD_ERR_INVALID_PARAMS,
    MVHD_ERR_CONV_SIZE,
    MVHD_ERR_TIMESTAMP
} MVHDError;

typedef enum MVHDType {
    MVHD_TYPE_FIXED = 2,
    MVHD_TYPE_DYNAMIC = 3,
    MVHD_TYPE_DIFF = 4
} MVHDType;

typedef enum MVHDBlockSize {
    MVHD_BLOCK_DEFAULT = 0,  /**< 2 MB blocks */
    MVHD_BLOCK_SMALL = 1024, /**< 512 KB blocks */
    MVHD_BLOCK_LARGE = 4096  /**< 2 MB blocks */
} MVHDBlockSize;

typedef struct MVHDGeom {
    uint16_t cyl;
    uint8_t heads;
    uint8_t spt;
} MVHDGeom;


#ifdef __cplusplus
extern "C" {
#endif

typedef void (*mvhd_progress_callback)(uint32_t current_sector, uint32_t total_sectors);

typedef struct MVHDCreationOptions {
    int type; /** MVHD_TYPE_FIXED, MVHD_TYPE_DYNAMIC, or MVHD_TYPE_DIFF */
    char* path; /** Absolute path of the new VHD file */
    char* parent_path; /** For MVHD_TYPE_DIFF, this is the absolute path of the VHD's parent. For non-diff VHDs, this should be NULL. */
    uint64_t size_in_bytes; /** Total size of the VHD's virtual disk in bytes. Must be a multiple of 512. If 0, the size is auto-calculated from the geometry field. Ignored for MVHD_TYPE_DIFF. */
    MVHDGeom geometry; /** The geometry of the VHD. If set to 0, the geometry is auto-calculated from the size_in_bytes field. */
    uint32_t block_size_in_sectors; /** MVHD_BLOCK_LARGE or MVHD_BLOCK_SMALL, or 0 for the default value. The number of sectors per block. */
    mvhd_progress_callback progress_callback; /** Optional; if not NULL, gets called to indicate progress on the creation operation. Only applies to MVHD_TYPE_FIXED. */
} MVHDCreationOptions;

typedef struct MVHDMeta MVHDMeta;


extern int mvhd_errno;


/* Shared-library madness. */
//#if defined(_WIN32)
//# ifdef STATIC
#  define MVHDAPI  /*nothing*/
//# else
//#  ifdef BUILDING_LIBRARY
//#   define MVHDAPI __declspec(dllexport)
//#  else
//#   define MVHDAPI __declspec(dllimport)
//#  endif
//# endif
//#elif defined(__GNUC__)
//# ifdef BUILDING_LIBRARY
//#  define MVHDAPI  __attribute__((visibility("default")))
//# else
//#  define MVHDAPI  /*nothing*/
//# endif
//#else
//# define MVHDAPI   /*nothing*/
//#endif


/**
 * \brief Return the library version as a string
 */
MVHDAPI const char *mvhd_version(void);

/**
 * \brief Return the library version as a number
 */
MVHDAPI uint32_t mvhd_version_id(void);

/**
 * \brief Output a string from a MiniVHD error number
 *
 * \param [in] err is the error number to return string from
 *
 * \return Error string
 */
MVHDAPI const char* mvhd_strerr(MVHDError err);

/**
 * \brief A simple test to see if a given file is a VHD
 *
 * \param [in] f file to test
 *
 * \retval 1 if f is a VHD
 * \retval 0 if f is not a VHD
 */
MVHDAPI int mvhd_file_is_vhd(FILE* f);

/**
 * \brief Return the file type of the given file
 * 
 * \param [in] vhdm VHD to check.
 * 
 * \retval one of the defined MVHDType values
 */
MVHDAPI MVHDType mvhd_get_type(MVHDMeta* vhdm);

/**
 * \brief Open a VHD image for reading and/or writing
 *
 * The returned pointer contains all required values and structures (and files) to
 * read and write to a VHD file.
 *
 * Remember to call mvhd_close() when you are finished.
 *
 * \param [in] Absolute path to VHD file. Relative path will cause issues when opening
 * a differencing VHD file
 * \param [in] readonly set this to 1 to open the VHD in a read only manner
 * \param [out] err will be set if the VHD fails to open. Value could be one of
 * MVHD_ERR_MEM, MVHD_ERR_FILE, MVHD_ERR_NOT_VHD, MVHD_ERR_FOOTER_CHECKSUM, MVHD_ERR_SPARSE_CHECKSUM,
 * MVHD_ERR_TYPE, MVHD_ERR_TIMESTAMP
 * If MVHD_ERR_FILE is set, mvhd_errno will be set to the appropriate system errno value
 *
 * \return MVHDMeta pointer. If NULL, check err. err may also be set to MVHD_ERR_TIMESTAMP if
 *         opening a differencing VHD.
 */
MVHDAPI MVHDMeta* mvhd_open(const char* path, int readonly, int* err);

/**
 * \brief Update the parent modified timestamp in the VHD file
 *
 * Differencing VHD's use a parent last modified timestamp to try and detect if the
 * parent has been modified after the child has been created. However, this is rather
 * fragile and can be broken by moving/copying the parent. Also, MS DiskPart does not
 * set this timestamp in the child :(
 *
 * Be careful when using this function that you don't update the timestamp after the
 * parent actually has been modified.
 *
 * \param [in] vhdm Differencing VHD to update.
 * \param [out] err will be set if the timestamp could not be updated
 *
 * \return non-zero on error, 0 on success
 */
MVHDAPI int mvhd_diff_update_par_timestamp(MVHDMeta* vhdm, int* err);

/**
 * \brief Create a fixed VHD image
 *
 * \param [in] path is the absolute path to the image to create
 * \param [in] geom is the HDD geometry of the image to create. Determines final image size
 * \param [out] err indicates what error occurred, if any
 * \param [out] progress_callback optional; if not NULL, gets called to indicate progress on the creation operation
 *
 * \retval NULL if an error occurrs. Check value of *err for actual error. Otherwise returns pointer to a MVHDMeta struct
 */
MVHDAPI MVHDMeta* mvhd_create_fixed(const char* path, MVHDGeom geom, int* err, mvhd_progress_callback progress_callback);

/**
 * \brief Create sparse (dynamic) VHD image.
 *
 * \param [in] path is the absolute path to the VHD file to create
 * \param [in] geom is the HDD geometry of the image to create. Determines final image size
 * \param [out] err indicates what error occurred, if any
 *
 * \return NULL if an error occurrs. Check value of *err for actual error. Otherwise returns pointer to a MVHDMeta struct
 */
MVHDAPI MVHDMeta* mvhd_create_sparse(const char* path, MVHDGeom geom, int* err);

/**
 * \brief Create differencing VHD imagee.
 *
 * \param [in] path is the absolute path to the VHD file to create
 * \param [in] par_path is the absolute path to a parent image. If NULL, a sparse image is created, otherwise create a differencing image
 * \param [out] err indicates what error occurred, if any
 *
 * \return NULL if an error occurrs. Check value of *err for actual error. Otherwise returns pointer to a MVHDMeta struct
 */
MVHDAPI MVHDMeta* mvhd_create_diff(const char* path, const char* par_path, int* err);

/**
 * \brief Create a VHD using the provided options
 *
 * Use mvhd_create_ex if you want more control over the VHD's options. For quick creation, you can use mvhd_create_fixed, mvhd_create_sparse, or mvhd_create_diff.
 *
 * \param [in] options the VHD creation options.
 * \param [out] err indicates what error occurred, if any
 *
 * \retval NULL if an error occurrs. Check value of *err for actual error. Otherwise returns pointer to a MVHDMeta struct
 */
MVHDAPI MVHDMeta* mvhd_create_ex(MVHDCreationOptions options, int* err);

/**
 * \brief Safely close a VHD image
 *
 * \param [in] vhdm MiniVHD data structure to close
 */
MVHDAPI void mvhd_close(MVHDMeta* vhdm);

/**
 * \brief Calculate hard disk geometry from a provided size
 *
 * The VHD format uses Cylinder, Heads, Sectors per Track (CHS) when accessing the disk.
 * The size of the disk can be determined from C * H * S * sector_size.
 *
 * Note, maximum geometry size (in bytes) is 65535 * 16 * 255 * 512, which is 127GB.
 * However, the maximum VHD size is 2040GB. For VHDs larger than 127GB, the geometry size will be
 * smaller than the actual VHD size.
 *
 * This function determines the appropriate CHS geometry from a provided size in bytes.
 * The calculations used are those provided in "Appendix: CHS Calculation" from the document
 * "Virtual Hard Disk Image Format Specification" provided by Microsoft.
 *
 * \param [in] size the desired VHD image size, in bytes
 *
 * \return MVHDGeom the calculated geometry. This can be used in the appropriate create functions.
 */
MVHDAPI MVHDGeom mvhd_calculate_geometry(uint64_t size);

/**
 * \brief Get the CHS geometry from the image
 * 
 * \param [in] vhdm MiniVHD data structure
 * 
 * \return The CHS geometry as stored in the image
 */
MVHDAPI MVHDGeom mvhd_get_geometry(MVHDMeta* vhdm);

/**
 * \brief Get the 'current_size' value from the image
 * 
 * Note that the size returned may not match the size calculated from the
 * CHS geometry. It is up to the caller to decide how best to handle this.
 * 
 * \param [in] vhdm MiniVHD data structure
 * 
 * \return The 'current_size' value in bytes, as stored in the image.
 *         Note, this may not match the CHS geometry.
 */
MVHDAPI uint64_t mvhd_get_current_size(MVHDMeta* vhdm);

/**
 * \brief Calculate CHS geometry size in bytes
 * 
 * \param [in] geom the CHS geometry to calculate
 * 
 * \return the size in bytes
 */
MVHDAPI uint64_t mvhd_calc_size_bytes(MVHDGeom *geom);

/**
 * \brief Calculate CHS geometry size in sectors
 * 
 * \param [in] geom the CHS geometry to calculate
 * 
 * \return the size in sectors
 */
MVHDAPI uint32_t mvhd_calc_size_sectors(MVHDGeom *geom);

/**
 * \brief Convert a raw disk image to a fixed VHD image
 *
 * \param [in] utf8_raw_path is the path of the raw image to convert
 * \param [in] utf8_vhd_path is the path of the VHD to create
 * \param [out] err indicates what error occurred, if any
 *
 * \return NULL if an error occurrs. Check value of *err for actual error. Otherwise returns pointer to a MVHDMeta struct
 */
MVHDAPI MVHDMeta* mvhd_convert_to_vhd_fixed(const char* utf8_raw_path, const char* utf8_vhd_path, int* err);

/**
 * \brief Convert a raw disk image to a sparse VHD image
 *
 * \param [in] utf8_raw_path is the path of the raw image to convert
 * \param [in] utf8_vhd_path is the path of the VHD to create
 * \param [out] err indicates what error occurred, if any
 *
 * \return NULL if an error occurrs. Check value of *err for actual error. Otherwise returns pointer to a MVHDMeta struct
 */
MVHDAPI MVHDMeta* mvhd_convert_to_vhd_sparse(const char* utf8_raw_path, const char* utf8_vhd_path, int* err);

/**
 * \brief Convert a VHD image to a raw disk image
 *
 * \param [in] utf8_vhd_path is the path of the VHD to convert
 * \param [in] utf8_raw_path is the path of the raw image to create
 * \param [out] err indicates what error occurred, if any
 *
 * \return NULL if an error occurrs. Check value of *err for actual error. Otherwise returns the raw disk image FILE pointer
 */
MVHDAPI FILE* mvhd_convert_to_raw(const char* utf8_vhd_path, const char* utf8_raw_path, int *err);

/**
 * \brief Read sectors from VHD file
 *
 * Read num_sectors, beginning at offset from the VHD file into a buffer
 *
 * \param [in] vhdm MiniVHD data structure
 * \param [in] offset the sector offset from which to start reading from
 * \param [in] num_sectors the number of sectors to read
 * \param [out] out_buff the buffer to write sector data to
 *
 * \return the number of sectors that were not read, or zero
 */
MVHDAPI int mvhd_read_sectors(MVHDMeta* vhdm, uint32_t offset, int num_sectors, void* out_buff);

/**
 * \brief Write sectors to VHD file
 *
 * Write num_sectors, beginning at offset from a buffer VHD file into the VHD file
 *
 * \param [in] vhdm MiniVHD data structure
 * \param [in] offset the sector offset from which to start writing to
 * \param [in] num_sectors the number of sectors to write
 * \param [in] in_buffer the buffer to write sector data to
 *
 * \return the number of sectors that were not written, or zero
 */
MVHDAPI int mvhd_write_sectors(MVHDMeta* vhdm, uint32_t offset, int num_sectors, void* in_buff);

/**
 * \brief Write zeroed sectors to VHD file
 *
 * Write num_sectors, beginning at offset, of zero data into the VHD file.
 * We reuse the existing write functions, with a preallocated zero buffer as
 * our source buffer.
 *
 * \param [in] vhdm MiniVHD data structure
 * \param [in] offset the sector offset from which to start writing to
 * \param [in] num_sectors the number of sectors to write
 *
 * \return the number of sectors that were not written, or zero
 */
MVHDAPI int mvhd_format_sectors(MVHDMeta* vhdm, uint32_t offset, int num_sectors);

#ifdef __cplusplus
}
#endif


#endif	/*MINIVHD_H*/
