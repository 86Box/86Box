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
 */


#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <wchar.h>
#ifndef _MSC_VER
#include <unistd.h>
#else
#include <io.h>
#endif
#define HAVE_STDARG_H
#include <86box/86box.h>
#include <86box/timer.h>
#include <86box/plat.h>
#include <86box/fdd.h>
#include <86box/fdd_86f.h>
#include <86box/fdd_common.h>
#include <86box/fdd_pcjs.h>
#include <cJSON.h>

static pcjs_t *images[FDD_NUM];
static pcjs_error_t pcjs_error = E_SUCCESS;

struct pcjs_error_description {
    int code;
    const char *message;
} pcjs_error_description[] = {
    { E_SUCCESS,          "No error"                               },
    { E_MISSING_KEY,      "The requested key was missing"          },
    { E_UNEXPECTED_VALUE, "The value was not of the expected type" },
    { E_INTEGRITY,        "Integrity check failed"                 },
    { E_INVALID_OBJECT,   "Object is missing or invalid"           },
    { E_ALLOC,            "Memory allocation failure"              },
    { E_PARSE,            "Parsing failure"                        },
    { -1,                 "Unknown error"                          },
};

#ifdef ENABLE_PCJS_LOG
int pcjs_do_log = ENABLE_PCJS_LOG;
static void
pcjs_log(const char *fmt, ...)
{
    if (pcjs_do_log) {
        va_list ap;
        va_start(ap, fmt);
        pclog_ex(fmt, ap);
        va_end(ap);
    }
}
#else
#    define pcjs_log(fmt, ...)
#endif

void
pcjs_init(void)
{
    memset(images, 0x00, sizeof(images));
}

const char* pcjs_errmsg(void)
{
    int i = 0;
    while (pcjs_error_description[i].code >= 0) {
        if (pcjs_error_description[i].code == pcjs_error) {
            return pcjs_error_description[i].message;
        }
        i++;
    }
    return "Unknown error";
}

int parse_image_info(pcjs_t *dev, const cJSON *parsed_json)
{
    const cJSON *imageInfo = NULL;

    if(dev == NULL || parsed_json == NULL) {
        pcjs_log("Null values passed\n");
        pcjs_error = E_INTEGRITY;
        return 1;
    }

    imageInfo = cJSON_GetObjectItemCaseSensitive(parsed_json, "imageInfo");

    if (imageInfo == NULL || !cJSON_IsObject(imageInfo)) {
        pcjs_log("imageInfo object does not exist or is invalid\n");
        pcjs_error = E_INVALID_OBJECT;
        return 1;
    }

    /* Macros are used here to avoid repetition */
    /* First get strings */
    IMAGE_INFO_GET_STRING(type)
    IMAGE_INFO_GET_STRING(name)
    IMAGE_INFO_GET_STRING(format)
    IMAGE_INFO_GET_STRING(hash)
    IMAGE_INFO_GET_STRING(version)
    IMAGE_INFO_GET_STRING(repository)
    /* Then the numbers */
    IMAGE_INFO_GET_NUMBER(cylinders)
    IMAGE_INFO_GET_NUMBER(heads)
    IMAGE_INFO_GET_NUMBER(trackDefault)
    IMAGE_INFO_GET_NUMBER(sectorDefault)
    IMAGE_INFO_GET_NUMBER(diskSize)

    /* Special cases */
    /* Convert bootSector to array if it exists */
    /* For some reason the array itself is stored as a string
     * which needs to be converted into an array before parsing. */
    dev->image_info.boot_sector_array_size = 0;
    const cJSON *array_string = cJSON_GetObjectItemCaseSensitive(imageInfo, "bootSector");
    cJSON *bootSector = NULL;
    if(cJSON_IsString(array_string) && array_string != NULL) {
        bootSector = cJSON_Parse(array_string->valuestring);
    }
    if(cJSON_IsArray(bootSector)) {
        const int array_size = cJSON_GetArraySize(bootSector);
        dev->image_info.boot_sector_array_size = array_size;
        const cJSON *array_item = NULL;
        int array_index = 0;
        cJSON_ArrayForEach(array_item, bootSector)
        {
            /* Make sure each item is a number */
            if(!cJSON_IsNumber(array_item)) {
                pcjs_log("Non-number item in bootSector array\n");
                dev->image_info.boot_sector_array_size = 0;
                /* Prevent the loop from continuing */
                array_item = NULL;
                break;
            }
            /* Make sure each number is in range */
            const int value = array_item->valueint;
            if (value < 0 || value > 255) {
                pcjs_log("bootSector value %i out of range (0-255)\n", value);
                dev->image_info.boot_sector_array_size = 0;
                /* Prevent the loop from continuing */
                array_item = NULL;
                break;
            }
            /* Make sure we don't exceed the array length */
            if (array_index + 1 > PCJS_IMAGE_INFO_ARRAY_LEN) {
                pcjs_log("bootSector array length exceeded (max %i)\n", PCJS_IMAGE_INFO_ARRAY_LEN);
                dev->image_info.boot_sector_array_size = 0;
                /* Prevent the loop from continuing */
                array_item = NULL;
                break;
            }
            dev->image_info.boot_sector[array_index] = value;
            array_index++;
        }
    }

    /* checksum: Can't use the number macro like the others because it uses valueInt
     * which is 32-bit signed and we need unsigned. Use the double value (valuedouble) instead. */
    const cJSON *checksum_json = cJSON_GetObjectItemCaseSensitive(imageInfo, "checksum");
    if (cJSON_IsNumber(checksum_json)) {
        dev->image_info.checksum = checksum_json->valuedouble;
    } else {
        pcjs_log("Required number value for \"%s\" missing from imageInfo\n", "checksum");
        pcjs_error = E_MISSING_KEY;
        cJSON_Delete(bootSector);
        return 1;
    }

    /* Use the metadata as the official source */
    dev->total_tracks  = dev->image_info.cylinders;
    dev->total_sides   = dev->image_info.heads;
    dev->total_sectors = dev->image_info.trackDefault;
    cJSON_Delete(bootSector);
    return 0;
}

int parse_file_table(pcjs_t *dev, const cJSON *parsed_json)
{
    const cJSON *fileTable = NULL;

    if(dev == NULL || parsed_json == NULL) {
        pcjs_log("Null values passed\n");
        pcjs_error = E_INTEGRITY;
        return 1;
    }

    fileTable = cJSON_GetObjectItemCaseSensitive(parsed_json, "fileTable");

    if (fileTable == NULL || !cJSON_IsArray(fileTable)) {
        pcjs_log("fileTable object does not exist or is invalid\n");
        pcjs_error = E_INVALID_OBJECT;
        return 1;
    }

    const cJSON *each_file_table = NULL;
    dev->file_table.num_entries  = cJSON_GetArraySize(fileTable);
    uint16_t processed_entries   = 0;
    uint16_t current_entry       = 0;

    if (dev->file_table.num_entries == 0) {
        pcjs_log("No fileTable entries to process\n");
        return 0;
    }

    pcjs_log("Processing %i file table entries\n", dev->file_table.num_entries);

    /* Allocate the entries */
    dev->file_table.entries = (pcjs_file_table_entry_t *)calloc(dev->file_table.num_entries, sizeof(pcjs_file_table_entry_t));
    if (dev->file_table.entries == NULL ) {
        pcjs_log("Failed to allocate file table entries\n");
        pcjs_error = E_ALLOC;
        return 1;
    }

    cJSON_ArrayForEach(each_file_table, fileTable)
    {
        /* The -1 length of the temporary buffer brought to you by gcc's -Wstringop-truncation */
        char hash[PCJS_FILE_TABLE_STRING_LEN-1] = {0};
        char path[PCJS_FILE_TABLE_STRING_LEN-1] = {0};
        char attr[PCJS_FILE_TABLE_STRING_LEN-1] = {0};
        char date[PCJS_FILE_TABLE_STRING_LEN-1] = {0};
        uint16_t f_size = 0;

        JSON_GET_OBJECT_STRING_OPTIONAL(hash, each_file_table,   PCJS_OBJECT_KEY_FT_HASH)
        JSON_GET_OBJECT_STRING_OPTIONAL(path, each_file_table,   PCJS_OBJECT_KEY_FT_PATH)
        JSON_GET_OBJECT_STRING_REQUIRED(attr, each_file_table,   PCJS_OBJECT_KEY_FT_ATTR)
        JSON_GET_OBJECT_STRING_REQUIRED(date, each_file_table,   PCJS_OBJECT_KEY_FT_DATE)
        JSON_GET_OBJECT_NUMBER_OPTIONAL(f_size, each_file_table, PCJS_OBJECT_KEY_FT_SIZE)

        strncpy(dev->file_table.entries[current_entry].hash,  hash, sizeof(dev->file_table.entries[current_entry].hash) - 1);
        strncpy(dev->file_table.entries[current_entry].path,  path, sizeof(dev->file_table.entries[current_entry].path) - 1);
        strncpy(dev->file_table.entries[current_entry].attr,  attr, sizeof(dev->file_table.entries[current_entry].attr) - 1);
        strncpy(dev->file_table.entries[current_entry].date,  date, sizeof(dev->file_table.entries[current_entry].date) - 1);
        dev->file_table.entries[current_entry].size = f_size;

        processed_entries++;
        current_entry++;

    }

    if(processed_entries != dev->file_table.num_entries) {
        pcjs_log("fileTable entries processed (%i) inconsistent with number of entries in the table (%i)\n", processed_entries, dev->file_table.num_entries);
        pcjs_error = E_INTEGRITY;
        goto fail;
    }

    return 0;
fail:
    /* Deallocate the array */
    free(dev->file_table.entries);
    return 1;
}

int json_parse(pcjs_t *dev)
{
    const cJSON *diskData = NULL;

    /* Determine the size of the file, reset back */
    fseek(dev->fp, 0L, SEEK_END);
    const long numbytes = ftell(dev->fp);
    fseek(dev->fp, 0L, SEEK_SET);

    /* Allocate memory for the contents */
    char *buffer = calloc(numbytes + 1, sizeof(char));
    if(buffer == NULL) {
        pcjs_error = E_ALLOC;
        return 1;
    }

    /* Read and null terminate */
    (void) !fread(buffer, sizeof(char), numbytes, dev->fp);
    buffer[numbytes] = '\0';

    cJSON *parsed_json = cJSON_Parse(buffer);

    if (parsed_json == NULL)
    {
        const char *error_ptr = cJSON_GetErrorPtr();
        if (error_ptr != NULL) {
            fprintf(stderr, "Error parsing json before: %s\n", error_ptr);
        }
        pcjs_error = E_PARSE;
        goto fail;
    }

    if(parse_image_info(dev, parsed_json)) {
        pcjs_log("Failed to parse imageInfo metadata\n");
        goto fail;
    }

    /* File table metadata is optional */
    if(parse_file_table(dev, parsed_json)) {
        pcjs_log("File table metadata is not present or invalid\n");
    }

    diskData = cJSON_GetObjectItemCaseSensitive(parsed_json, "diskData");

    const cJSON *each_track = NULL;
    int total_c = 0;

    /* The diskData array is essentially [c][h][s] */
    /* Start with the tracks in [c] */
    cJSON_ArrayForEach(each_track, diskData)
    {
        int total_heads = 0;
        const cJSON *each_head = NULL;

        /* For each track, loop on each head */
        /* Now in [c][h] */
        cJSON_ArrayForEach(each_head, each_track)
        {
            int total_sectors = 0;
            const cJSON *each_sector = NULL;

            /* Now loop on the sectors in [c][h][s] */
            /* Each sector item will have the information needed to fill in a pcjs_sector_t */
            cJSON_ArrayForEach(each_sector, each_head)
            {
                const cJSON *data            = NULL;
                const cJSON *each_data       = NULL;
                int          data_array_size = 0;
                int          total_d         = 0;
                int32_t      current_track   = 0;
                int32_t      current_head    = 0;
                int32_t      current_sector  = 0;
                int32_t      current_length  = 0;
                int32_t      file_mapping    = 0;
                int32_t      offset          = 0;
                pcjs_sector_t *sector = NULL;

                /* Macros to keep things tidy */
                JSON_GET_OBJECT_NUMBER_REQUIRED(current_track,          each_sector, PCJS_OBJECT_KEY_TRACK)
                JSON_GET_OBJECT_NUMBER_REQUIRED(current_head,           each_sector, PCJS_OBJECT_KEY_HEAD)
                JSON_GET_OBJECT_NUMBER_REQUIRED(current_sector,         each_sector, PCJS_OBJECT_KEY_SECTOR)
                JSON_GET_OBJECT_NUMBER_REQUIRED(current_length,         each_sector, PCJS_OBJECT_KEY_LENGTH)
                JSON_GET_OBJECT_NUMBER_OPTIONAL_DEFAULT(offset,         each_sector, PCJS_OBJECT_KEY_OFFSET, -1)
                JSON_GET_OBJECT_NUMBER_OPTIONAL_DEFAULT(file_mapping,   each_sector, PCJS_OBJECT_KEY_FILE,   -1)

                /* NOTE: The sectors array is zero indexed, but the metadata for each sector shows its conventional sector number */
                sector = &dev->sectors[current_track][current_head][current_sector-1];

                if (sector->data == NULL ) {
                    /* We could verify the sector size against the metadata here */
                    sector->data = (uint8_t *)calloc(1, current_length);
                    if (sector->data == NULL ) {
                        pcjs_log("Failed to allocate\n");
                        pcjs_error = E_ALLOC;
                        goto fail;
                    }
                }
                sector->track          = current_track;
                sector->side           = current_head;
                sector->sector         = current_sector;
                sector->size           = current_length;
                sector->encoded_size   = fdd_sector_size_code(current_length);
                sector->offset         = offset;
                sector->file           = file_mapping;
                sector->pattern_repeat = 0;
                sector->last_entry     = 0;

                data = cJSON_GetObjectItemCaseSensitive(each_sector, PCJS_OBJECT_KEY_DATA);
                if(data != NULL && cJSON_IsArray(data)) {
                    data_array_size = cJSON_GetArraySize(data);
                    cJSON_ArrayForEach(each_data, data)
                    {
                        /* total_d is our current position in the data array */
                        /* That number will be used to determine where to store the
                         * value in its destination (sector->data).
                         * Each value in the data array is a 32-bit integer, but the
                         * destination (sector->data) is a uint8_t array. Therefore
                         * we'll need to manually calculate offsets and place the bytes.
                         */
                        const int dest_offset = total_d * 4;
                        if(total_d == data_array_size - 1) {
                            /* This is the last value in the data array. Check to see if we'll need it
                             * for the repeating pattern.
                             * We use current_length / 4 because each data element is 32 bits in size.
                             * For example, if current_length = 512
                             * then 512 / 4 = 128
                             * so anything less than that value will have padding */
                            if (total_d + 1 < current_length / 4) {
                                sector->last_entry = each_data->valueint;
                                /* total_d + 1 because it is zero indexed at this point */
                                sector->pattern_repeat = current_length / 4 - (total_d + 1);
                            }
                        }
                        const int value = each_data->valueint;
                        /* Take each value and shift in as necessary */
                        for (int i = 0; i < 4; i++) {
                            sector->data[dest_offset + i] = (value >> i * 8) & 0xff;
                        }
                        total_d++;
                    }
                } else {
                    pcjs_log("Data array missing from [%d][%d][%d]", current_track, current_head, current_sector);
                    pcjs_error = E_MISSING_KEY;
                    goto fail;
                }
                /* Fill in the repeating pattern if needed */
                /* total_d was already advanced at the end of the previous loop */
                for (int i = 0; i < sector->pattern_repeat; i++ ) {
                    const int position = total_d + i;
                    const int dest_offset = position * 4;
                    if(position >= (sector->size / 4)) {
                        /* Something is wrong */
                        pcjs_log("Out of bounds write attempt to data array at %i", position);
                        pcjs_error = E_INTEGRITY;
                        goto fail;
                    }
                    for (int j = 0; j < 4; j++) {
                        sector->data[dest_offset + j] = (sector->last_entry >> j * 8) & 0xff;
                    }
                }

                total_sectors++;
                dev->calc_total_sectors = total_sectors;
                /* End sectors */
            }
            dev->spt[total_c][total_heads] = total_sectors;
            total_heads++;
            dev->calc_total_sides = total_heads;
            /* End heads */
        }
        total_c++;
        dev->calc_total_tracks = total_c;
        /* End tracks */
    }

    pcjs_log("calculated totals: c/h/s %i/%i/%i\n", dev->calc_total_tracks, dev->calc_total_sides, dev->calc_total_sectors);
    pcjs_log("metadata totals:   c/h/s %i/%i/%i\n", dev->image_info.cylinders, dev->image_info.heads, dev->image_info.trackDefault);

    free(buffer);
    cJSON_Delete(parsed_json);
    return 0;
fail:
    free(buffer);
    cJSON_Delete(parsed_json);
    return pcjs_error;
}

/* Handlers */

static uint16_t
disk_flags(int drive)
{
    const pcjs_t *dev = images[drive];

    return dev->disk_flags;
}

static uint16_t
track_flags(int drive)
{
    const pcjs_t *dev = images[drive];

    return dev->track_flags;
}

static void
set_sector(int drive, int side, uint8_t c, UNUSED(uint8_t h), uint8_t r, UNUSED(uint8_t n))
{
    pcjs_t *dev = images[drive];

    dev->current_sector[side] = 0;

    /* Make sure we are on the desired track. */
    if (c != dev->current_track)
        return;

    /* Set the desired side. */
    dev->current_side = side;

    /* Sectors are stored zero indexed, but sector zero should not be requested */
    if(r == 0) {
        pcjs_log("set_sector: Sector 0 requested?\n");
    } else {
        dev->current_sector[side] = r - 1;
    }
    /* The ifdef is necessary because if ENABLE_PCJS_LOG is not defined gcc throws an unused variable warning */
#ifdef ENABLE_PCJS_LOG
    const int file_index = dev->sectors[dev->current_track][side][dev->current_sector[side]].file;
    pcjs_log("set sector: %i/%i/%i %s%s%s\n", c, h, r,
        file_index == -1 ? "" : "(",
        file_index == -1 ? "" : dev->file_table.entries[file_index].path,
        file_index == -1 ? "" : ")");
#endif
}

static uint8_t
poll_read_data(int drive, int side, uint16_t pos)
{
    const pcjs_t *dev = images[drive];
    const uint8_t sec = dev->current_sector[side];
    return (dev->sectors[dev->current_track][side][sec].data[pos]);
}

static void
pcjs_seek(int drive, int track)
{
    uint8_t id[4] = { 0, 0, 0, 0 };
    pcjs_t *dev   = images[drive];
    int     rate;
    int     gap2;
    int     gap3;
    int     pos;
    int     ssize;
    int     rsec;
    int     asec;

    if (dev->fp == NULL) {
        pcjs_log("pcjs_seek: no file loaded\n");
        return;
    }

    /* Allow for doublestepping tracks. */
    if (!dev->track_width && fdd_doublestep_40(drive))
        track /= 2;

    /* Set the new track. */
    dev->current_track = track;
    d86f_set_cur_track(drive, track);

    /* Reset the 86F state machine. */
    d86f_reset_index_hole_pos(drive, 0);
    d86f_destroy_linked_lists(drive, 0);
    d86f_reset_index_hole_pos(drive, 1);
    d86f_destroy_linked_lists(drive, 1);

    if (track > dev->total_tracks) {
        d86f_zero_track(drive);
        return;
    }

    pcjs_log("seeking to track %i\n", track);

    for (uint8_t side = 0; side < dev->total_sides; side++) {
        /* Get transfer rate for this side. */
        rate = dev->track_flags & 0x07;
        if (!rate && (dev->track_flags & 0x20))
            rate = 4;

        /* Get correct GAP3 value for this side. */
        gap3 = fdd_get_gap3_size(rate,
                                 // dev->sectors[track][side][0].size,
                                 dev->sectors[track][side][0].encoded_size,
                                 dev->spt[track][side]);

        /* Get correct GAP2 value for this side. */
        gap2 = ((dev->track_flags & 0x07) >= 3) ? 41 : 22;

        pos = d86f_prepare_pretrack(drive, side, 0);

        for (uint8_t sector = 0; sector < dev->spt[track][side]; sector++) {
            rsec = dev->sectors[track][side][sector].sector;
            asec = sector;

            id[0] = track;
            id[1] = side;
            id[2] = rsec;
            if (dev->sectors[track][side][asec].encoded_size > 255)
                perror("PCJS: pcjs_seek: sector size too big.");
            id[3] = dev->sectors[track][side][asec].encoded_size & 0xff;
            ssize = fdd_sector_code_size(dev->sectors[track][side][asec].encoded_size & 0xff);

            pos = d86f_prepare_sector(
                drive, side, pos, id,
                dev->sectors[track][side][asec].data,
                ssize, gap2, gap3,
                0
            );

            if (sector == 0)
                d86f_initialize_last_sector_id(drive, id[0], id[1], id[2], id[3]);
        }
    }
}

static int
pcjs_load_image(pcjs_t *dev)
{
    if (dev->fp == NULL) {
        pcjs_log("No file loaded!\n");
        return 1;
    }

    /* Initialize. */
    for (uint16_t i = 0; i < PCJS_MAX_TRACKS; i++) {
        for (uint8_t j = 0; j < PCJS_MAX_SIDES; j++)
            memset(dev->sectors[i][j], 0x00, sizeof(pcjs_sector_t));
    }
    dev->current_track = 0;
    dev->current_side = 0;

    return json_parse(dev);
}

void
pcjs_load(int drive, char *fn)
{
    double          bit_rate = 0;
    int             temp_rate;
    const pcjs_sector_t *sector;
    pcjs_t         *dev;

    d86f_unregister(drive);

    /* Allocate a drive block */
    dev = (pcjs_t *) calloc(1, sizeof(pcjs_t));

    /* Open the image file, read-only */
    dev->fp = plat_fopen(fn, "rb");
    if (dev->fp == NULL) {
        free(dev);
        memset(fn, 0x00, sizeof(char));
        return;
    }

    pcjs_log("Opening filename: %s\n", fn);

    /* Always set the drive to write-protected mode */
    writeprot[drive] = 1;

    /* Place in the correct slot */
    images[drive] = dev;

    /* Parse and load the information from the json file */
    if (pcjs_load_image(dev)) {
        pcjs_log("Failed to initialize: %s\n", pcjs_errmsg());
        (void) fclose(dev->fp);
        free(dev);
        images[drive] = NULL;
        memset(fn, 0x00, sizeof(char));
        return;
    }

    pcjs_log("Drive %d: %s (%i tracks, %i sides, %i sectors, sector size %i)\n",
             drive, fn, dev->image_info.cylinders, dev->image_info.heads, dev->image_info.trackDefault, dev->image_info.sectorDefault);


    /*
     * If the image has more than 43 tracks, then
     * the tracks are thin (96 tpi).
     */
    dev->track_width = (dev->total_tracks > 43) ? 1 : 0;

    /* If the image has 2 sides, mark it as such. */
    dev->disk_flags = 0x00;
    if (dev->total_sides == 2)
        dev->disk_flags |= 0x08;

    /* PCJS files are always assumed to be MFM-encoded. */
    dev->track_flags = 0x08;

    dev->interleave = 0;

    temp_rate = 0xff;
    sector       = &dev->sectors[0][0][0];
    for (uint8_t i = 0; i < 6; i++) {
        if (dev->spt[0][0] > fdd_max_sectors[sector->encoded_size][i])
            continue;

        bit_rate  = fdd_bit_rates_300[i];
        temp_rate = fdd_rates[i];
        dev->disk_flags |= (fdd_holes[i] << 1);

        if ((bit_rate == 500.0) && (dev->spt[0][0] == 21) && (sector->encoded_size == 2) && (dev->total_tracks >= 80) && (dev->total_tracks <= 82) && (dev->total_sides == 2)) {
            /*
             * This is a DMF floppy, set the flag so
             * we know to interleave the sectors.
             */
            dev->dmf = 1;
        } else {
            if ((bit_rate == 500.0) && (dev->spt[0][0] == 22) && (sector->encoded_size == 2) && (dev->total_tracks >= 80) && (dev->total_tracks <= 82) && (dev->total_sides == 2)) {
                /*
                 * This is marked specially because of the
                 * track flag (a RPM slow down is needed).
                 */
                dev->interleave = 2;
            }
            dev->dmf = 0;
        }
        break;
    }

    if (temp_rate == 0xff) {
        pcjs_log("Invalid image (temp_rate=0xff)\n");
        (void) fclose(dev->fp);
        dev->fp = NULL;
        free(dev);
        images[drive] = NULL;
        memset(fn, 0x00, sizeof(char));
        return;
    }

    if (dev->interleave == 2) {
        dev->interleave = 1;
        dev->disk_flags |= 0x60;
    }

    dev->gap2_len = (temp_rate == 3) ? 41 : 22;
    if (dev->dmf)
        dev->gap3_len = 8;
    else
        dev->gap3_len = fdd_get_gap3_size(temp_rate, sector->encoded_size, dev->spt[0][0]);

    if (!dev->gap3_len) {
        pcjs_log("Image of unknown format was inserted into drive %c:\n",
                 'C' + drive);
        (void) fclose(dev->fp);
        dev->fp = NULL;
        free(dev);
        images[drive] = NULL;
        memset(fn, 0x00, sizeof(char));
        return;
    }

    dev->track_flags |= (temp_rate & 0x03); /* data rate */
    if (temp_rate & 0x04)
        dev->track_flags |= 0x20; /* RPM */

    pcjs_log("      disk_flags: 0x%02x, track_flags: 0x%02x, GAP3 length: %i\n",
             dev->disk_flags, dev->track_flags, dev->gap3_len);
    pcjs_log("      bit rate 300: %.2f, temporary rate: %i, hole: %i, DMF: %i\n",
             bit_rate, temp_rate, (dev->disk_flags >> 1), dev->dmf);
    pcjs_log("      encoded_size: %i spt: %i\n", sector->encoded_size, dev->spt[0][0]);

    /* Set up 86F handlers */

    d86f_handler[drive].disk_flags        = disk_flags;
    d86f_handler[drive].side_flags        = track_flags;
    d86f_handler[drive].writeback         = null_writeback;
    d86f_handler[drive].set_sector        = set_sector;
    d86f_handler[drive].read_data         = poll_read_data;
    d86f_handler[drive].write_data        = null_write_data;
    d86f_handler[drive].format_conditions = null_format_conditions;
    d86f_handler[drive].extra_bit_cells   = null_extra_bit_cells;
    d86f_handler[drive].encoded_data      = common_encoded_data;
    d86f_handler[drive].read_revolution   = common_read_revolution;
    d86f_handler[drive].index_hole_pos    = null_index_hole_pos;
    d86f_handler[drive].get_raw_size      = common_get_raw_size;
    d86f_handler[drive].check_crc         = 1;
    d86f_set_version(drive, 0x0063);

    d86f_common_handlers(drive);

    drives[drive].seek = pcjs_seek;

}

void
pcjs_close(int drive)
{
    pcjs_t *dev = images[drive];

    if (dev == NULL)
        return;

    /* Unlink image from the system. */
    d86f_unregister(drive);

    /* Release all the sector buffers. */
    for (int c = 0; c < PCJS_MAX_TRACKS; c++) {
        for (int h = 0; h < PCJS_MAX_SIDES; h++) {
            for (uint16_t s = 0; s < PCJS_MAX_SECTORS; s++) {
                if (dev->sectors[c][h][s].data != NULL)
                    free(dev->sectors[c][h][s].data);
                dev->sectors[c][h][s].data = NULL;

            }
        }
    }
    /* Release file table entries */
    if(dev->file_table.entries != NULL)
        free(dev->file_table.entries);
    dev->file_table.entries = NULL;
    dev->file_table.num_entries = 0;

    if (dev->fp != NULL)
        (void) fclose(dev->fp);

    /* Release the memory. */
    free(dev);
    images[drive] = NULL;
}
