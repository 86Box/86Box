/*
 * 86Box    A hypervisor and IBM PC system emulator that specializes in
 *          running old operating systems and software designed for IBM
 *          PC systems and compatibles from 1981 through fairly recent
 *          system designs based on the PCI bus.
 *
 *          This file is part of the 86Box distribution.
 *
 *          Implementation of the pcjs v2 floppy image format (read-only)
 *
 * Authors: cold-brewed
 *
 *          Copyright 2024 cold-brewed
 *
 *          More info: https://www.pcjs.org/tools/diskimage/
 *          pcjs disk module v2: https://github.com/jeffpar/pcjs/blob/master/machines/pcx86/modules/v2/disk.js
 */
#ifndef EMU_FLOPPY_PCJS_H
#define EMU_FLOPPY_PCJS_H

/* Currently targeting v2 of the spec */
#define PCJS_DISK_SPEC_VERSION 2

#define PCJS_MAX_TRACKS  256
#define PCJS_MAX_SIDES   2
#define PCJS_MAX_SECTORS 256

/* The json keys as defined in each sector array item */
#define PCJS_OBJECT_KEY_CYLINDER "c"
#define PCJS_OBJECT_KEY_TRACK    PCJS_OBJECT_KEY_CYLINDER
#define PCJS_OBJECT_KEY_HEAD     "h"
#define PCJS_OBJECT_KEY_SECTOR   "s"
#define PCJS_OBJECT_KEY_LENGTH   "l"
#define PCJS_OBJECT_KEY_DATA     "d"
#define PCJS_OBJECT_KEY_FILE     "f"
#define PCJS_OBJECT_KEY_OFFSET   "d"

/* The json keys as defined in the fileTable object */
#define PCJS_OBJECT_KEY_FT_HASH "hash"
#define PCJS_OBJECT_KEY_FT_PATH "path"
#define PCJS_OBJECT_KEY_FT_ATTR "attr"
#define PCJS_OBJECT_KEY_FT_DATE "date"
#define PCJS_OBJECT_KEY_FT_SIZE "size"

/* String length defaults */
#define PCJS_IMAGE_INFO_STRING_LEN 128
#define PCJS_IMAGE_INFO_ARRAY_LEN  128
#define PCJS_FILE_TABLE_STRING_LEN 128

/* Defaults for optional json values */
#define JSON_OPTIONAL_NUMBER_DEFAULT 0
#define JSON_OPTIONAL_STRING_DEFAULT ""

/* Structure for each sector */
typedef struct pcjs_sector_t {
    /* Track number */
    uint8_t track;
    /* Side number */
    uint8_t side;
    /* Sector number */
    uint8_t sector;
    /* Size of the sector */
    uint16_t size;
    /* Encoded size of the sector */
    uint16_t encoded_size;
    /* Pointer the the allocated data for the sector */
    uint8_t *data;
    /* Number of times to repeat the pattern until end of sector */
    uint16_t pattern_repeat;
    /* Last pattern entry to repeat */
    int32_t last_entry;
    /* Maps back to a file entry. -1 if not set */
    int32_t file;
    /* The offset in the mapped file entry. -1 if not set */
    int32_t offset;
} pcjs_sector_t;

/* Cases are mixed here (some camelCase) to match the pcjs values */
typedef struct pcjs_image_info_t {
    char     type[PCJS_IMAGE_INFO_STRING_LEN];
    char     name[PCJS_IMAGE_INFO_STRING_LEN];
    char     format[PCJS_IMAGE_INFO_STRING_LEN];
    char     hash[PCJS_IMAGE_INFO_STRING_LEN];
    uint32_t checksum;
    uint8_t  cylinders;
    uint8_t  heads;
    uint8_t  trackDefault;
    uint16_t sectorDefault;
    uint32_t diskSize;
    uint8_t  boot_sector[PCJS_IMAGE_INFO_ARRAY_LEN];
    uint8_t  boot_sector_array_size;
    char     version[PCJS_IMAGE_INFO_STRING_LEN];
    char     repository[PCJS_IMAGE_INFO_STRING_LEN];
} pcjs_image_info_t;

typedef struct pcjs_file_table_entry_t {
    char     hash[PCJS_FILE_TABLE_STRING_LEN];
    char     path[PCJS_FILE_TABLE_STRING_LEN];
    char     attr[PCJS_FILE_TABLE_STRING_LEN];
    char     date[PCJS_FILE_TABLE_STRING_LEN];
    uint32_t size;
} pcjs_file_table_entry_t ;

typedef struct pcjs_file_table_t {
    pcjs_file_table_entry_t *entries;
    uint16_t num_entries;
} pcjs_file_table_t;

typedef struct pcjs_t {
    /* FILE pointer for the json file */
    FILE *fp;

    /* These values are read in from the metadata */
    /* Total number of tracks */
    uint8_t  total_tracks;
    /* Total number of sides */
    uint8_t  total_sides;
    /* Total number of sectors per track */
    uint16_t total_sectors;

    /* These values are calculated for validation */
    /* Calculated number of tracks */
    uint8_t  calc_total_tracks;
    /* Calculated number of sides */
    uint8_t  calc_total_sides;
    /* Calculated number of sectors per track */
    uint16_t calc_total_sectors;

    /* Number of sectors per track */
    uint8_t spt[PCJS_MAX_TRACKS][PCJS_MAX_SIDES];

    /* Current track */
    uint8_t current_track;
    /* Current side */
    uint8_t current_side;
    /* Current sector */
    uint8_t current_sector[PCJS_MAX_SIDES];

    /* Disk is in dmf format? */
    uint8_t dmf;
    uint8_t interleave;
    uint8_t gap2_len;
    uint8_t gap3_len;
    int     track_width;

    /* Flags for the entire disk */
    uint16_t disk_flags;
    /* Flags for the current track */
    uint16_t track_flags;

    uint8_t interleave_ordered[PCJS_MAX_TRACKS][PCJS_MAX_SIDES];

    /* The main mapping of all the sectors back to each individual pcjs_sector_t item. */
    pcjs_sector_t sectors[PCJS_MAX_TRACKS][PCJS_MAX_SIDES][PCJS_MAX_SECTORS];

    /* Disk metadata information contained in each image */
    pcjs_image_info_t image_info;
    /* Optional file table mapping for each sector */
    pcjs_file_table_t file_table;
} pcjs_t;

/* Errors */
enum pcjs_img_error {
    E_SUCCESS = 0,
    E_MISSING_KEY = 1,
    E_UNEXPECTED_VALUE = 2,
    E_INTEGRITY,
    E_INVALID_OBJECT,
    E_ALLOC,
    E_PARSE,
};

typedef enum pcjs_img_error pcjs_error_t;

/* Macros */

/* Macro for getting image info metadata: strings */
#define IMAGE_INFO_GET_STRING(type) \
    const cJSON * type##_json = cJSON_GetObjectItemCaseSensitive(imageInfo, #type); \
    if (cJSON_IsString( type##_json) && type##_json->valuestring != NULL) { \
        strncpy(dev->image_info.type,  type##_json->valuestring, sizeof(dev->image_info. type) - 1); \
    } else { \
        pcjs_log("Required string value for \"%s\" missing from imageInfo\n", #type); \
        pcjs_error = E_INVALID_OBJECT; \
        return 1; \
    }
/* Macro for getting image info metadata: ints */
#define IMAGE_INFO_GET_NUMBER(type) \
    const cJSON * type##_json = cJSON_GetObjectItemCaseSensitive(imageInfo, #type); \
    if (cJSON_IsNumber( type##_json)) { \
        dev->image_info.type = type##_json->valueint; \
    } else { \
        pcjs_log("Required number value for \"%s\" missing from imageInfo\n", #type); \
        pcjs_error = E_INVALID_OBJECT; \
        return 1; \
    }

/* Macro for getting required object value: number */
#define JSON_GET_OBJECT_NUMBER_REQUIRED(var, json, key) \
const cJSON *var##_json  = cJSON_GetObjectItemCaseSensitive(json, key); \
if (!cJSON_IsNumber(var##_json)) { \
    pcjs_log("Required number value for \"%s\" missing or invalid\n", key); \
    pcjs_error = E_INVALID_OBJECT; \
    goto fail; \
} else { \
    var = var##_json->valueint; \
}

/* Macro for getting optional object value: number
 * Default value will be used if the number does not exist */
#define JSON_GET_OBJECT_NUMBER_OPTIONAL(var, json, key) \
const cJSON *var##_json  = cJSON_GetObjectItemCaseSensitive(json, key); \
if (!cJSON_IsNumber(var##_json)) { \
var = JSON_OPTIONAL_NUMBER_DEFAULT; \
} else { \
var = var##_json->valueint; \
}

/* Macro for getting optional object value: number
 * Provided default value will be used if the number does not exist */
#define JSON_GET_OBJECT_NUMBER_OPTIONAL_DEFAULT(var, json, key, default) \
const cJSON *var##_json  = cJSON_GetObjectItemCaseSensitive(json, key); \
if (!cJSON_IsNumber(var##_json)) { \
var = default; \
} else { \
var = var##_json->valueint; \
}

/* Macro for getting optional object value: string
 * Default value will be used if the string does not exist */
#define JSON_GET_OBJECT_STRING_OPTIONAL(var, json, key) \
    const cJSON * var##_json = cJSON_GetObjectItemCaseSensitive(json, key); \
    if (cJSON_IsString( var##_json) && var##_json->valuestring != NULL) { \
        strncpy(var,  var##_json->valuestring, sizeof(var) - 1); \
    } else { \
        strncpy(var,  JSON_OPTIONAL_STRING_DEFAULT, sizeof(var) - 1); \
    }

/* Macro for getting required object value: string */
#define JSON_GET_OBJECT_STRING_REQUIRED(var, json, key) \
    const cJSON * var##_json = cJSON_GetObjectItemCaseSensitive(json, key); \
    if (cJSON_IsString( var##_json) && var##_json->valuestring != NULL) { \
        strncpy(var,  var##_json->valuestring, sizeof(var) - 1); \
    } else { \
        pcjs_error = E_INVALID_OBJECT; \
        goto fail; \
    }

extern void pcjs_init(void);
extern void pcjs_load(int drive, char *fn);
extern void pcjs_close(int drive);
extern const char* pcjs_errmsg(void);

#endif
