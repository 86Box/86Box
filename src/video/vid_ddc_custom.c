#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define EDID_BLOCK_SIZE 128
#define EDID_HEADER 0x00FFFFFFFFFFFF00
#define EDID_DECODE_HEADER "edid-decode (hex):"

static size_t
read_block(FILE *fp, uint8_t *buf)
{
    char    temp[64];
    size_t  read = 0;

    for (int i = 0; i < 8; i++) {
        if (!fgets(temp, sizeof(temp), fp)) {
            return 0;
        }

        char *tok = strtok(temp, " \t\r\n");

        for (int j = 0; j < 16; j++) {
            if (!tok) {
                return 0;
            }

            buf[read++] = strtoul(tok, NULL, 16);
            tok         = strtok(NULL, " \t\r\n");
        }
    }

    return read;
}

size_t
ddc_load_edid(char *path, uint8_t *buf, size_t size)
{
    FILE   *fp   = fopen(path, "rb");
    size_t  offset = 0;
    char    temp[64];
    long    pos;

    if (!fp) {
        return 0;
    }

    // Check the beginning of the file for the EDID header.
    uint64_t header;
    fread(&header, sizeof(header), 1, fp);

    if (header == EDID_HEADER) {
        // Binary format. Read as is
        fseek(fp, 0, SEEK_SET);
        offset = fread(buf, 1, size, fp);

        fclose(fp);
        return offset;
    }

    // Reopen in text mode.
    fclose(fp);
    fp = fopen(path, "rt");

    if (!fp) {
        return 0;
    }

#ifdef _WIN32
    // Disable buffering on Windows because of a UCRT bug.
    // https://developercommunity.visualstudio.com/t/fseek-ftell-fail-in-text-mode-for-unix-style-text/425878
    setvbuf(fp, NULL, _IONBF, 0);
#endif

    // Skip the UTF-8 BOM, if any.
    if (fread(temp, 1, 3, fp) != 3) {
        fclose(fp);
        return 0;
    };

    if (temp[0] != 0xEF || temp[1] != 0xBB || temp[2] != 0xBF) {
        rewind(fp);
    }

    // Find the `edid-decode (hex):` header.
    do {
        if (!fgets(temp, sizeof(temp), fp)) {
            fclose(fp);
            return 0;
        }
    } while (strncmp(temp, EDID_DECODE_HEADER, sizeof(EDID_DECODE_HEADER) - 1));

    while (offset + EDID_BLOCK_SIZE <= size) {
        // Skip any whitespace before the next block.
        do {
            pos = ftell(fp);
            if (!fgets(temp, sizeof(temp), fp)) {
                fclose(fp);
                return offset;
            }
        } while (strspn(temp, " \t\r\n") == strlen(temp));

        fseek(fp, pos, SEEK_SET);

        // Read the block.
        size_t block = read_block(fp, buf + offset);

        if (block != EDID_BLOCK_SIZE) {
            break;
        }

        offset += block;
    }

    fclose(fp);
    return offset;
}
