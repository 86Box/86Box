/*
 * 86Box    A hypervisor and IBM PC system emulator that specializes in
 *          running old operating systems and software designed for IBM
 *          PC systems and compatibles from 1981 through fairly recent
 *          system designs based on the PCI bus.
 *
 *          This file is part of the 86Box distribution.
 *
 *          FAT12/16/32 volume generator for media mounted from host
 *          folders and archives.
 *
 *          The tree is read first; the layout is then fixed (FAT type and
 *          cluster size from the volume size, as Microsoft's FORMAT does),
 *          directories are allocated first and files after them, each in
 *          one contiguous run, and the volume is written out through a
 *          callback onto an image that starts filled with zeros.
 *
 * Authors: RichardG, <richardg867@gmail.com>
 *
 *          Copyright 2026 RichardG.
 */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#define HAVE_STDARG_H
#include <86box/86box.h>
#include <86box/path.h>
#include <86box/plat.h>
#include <86box/plat_dir.h>
#include <86box/random.h>
#include <86box/media_archive.h>
#include <86box/fs_name.h>
#include <86box/civil_time.h>
#include <86box/fat_image.h>

#define MAX_DEPTH   64
#define COPY_BUFFER 65536

enum {
    ATTR_READ_ONLY = 0x01,
    ATTR_VOLUME_ID = 0x08,
    ATTR_DIRECTORY = 0x10,
    ATTR_ARCHIVE   = 0x20,
    ATTR_LFN       = 0x0f
};

typedef struct node_t {
    struct node_t         *parent;
    struct node_t         *children;
    struct node_t         *next;
    char                  *host_path; /* folder sources */
    media_archive_entry_t *entry;     /* archive sources */
    uint64_t               size;
    time_t                 mtime;
    uint8_t                is_dir;
    uint8_t                attr;
    uint8_t                nt_case;
    uint8_t                needs_tail;
    uint8_t                left_out;  /* doesn't fit on the volume */
    uint8_t                short_name[11];
    uint8_t                lfn_len;   /* in UTF-16 units */
    uint8_t                write_lfn; /* the long name differs from the short name */
    uint16_t              *lfn;
    uint32_t               entries;   /* directory entries used by a directory */
    uint32_t               first_cluster;
    uint32_t               clusters;
    char                   name[];    /* UTF-8 */
} node_t;

struct fat_image_t {
    node_t          *root;
    media_archive_t *archive;
    char             label[11];
    time_t           now;
    size_t           nodes;
    int              left_out;   /* FAT_IMAGE_LEFT_OUT_* of the last layout */
    int              unreadable; /* files or folders could not be read */
    int              cp;        /* code page of the short names and the label */
    int              tz_offset; /* host offset from UTC now, for times the C library can't place */
};

typedef struct layout_t {
    int      bits;
    uint32_t bps;
    uint32_t spc;
    uint32_t cluster_size;
    uint32_t reserved;
    uint32_t num_fats;
    uint32_t fat_sectors;
    uint32_t root_entries;
    uint32_t root_sectors;
    uint32_t clusters;
    uint64_t volume_sectors;
    uint64_t fat_start;   /* sectors from the start of the volume */
    uint64_t root_start;
    uint64_t data_start;
    uint8_t  media;
    uint32_t used;        /* clusters used by the tree */
} layout_t;

/* Boot code for the MBR and the boot sector. It runs from any segment:offset and prints the message after it. */
static const uint8_t boot_code[] = {
    0xfa,              /* cli */
    0x31, 0xc0,        /* xor ax, ax */
    0x8e, 0xd0,        /* mov ss, ax */
    0xbc, 0x00, 0x7c,  /* mov sp, 7C00h */
    0xfb,              /* sti */
    0x0e,              /* push cs */
    0x1f,              /* pop ds */
    0xe8, 0x00, 0x00,  /* call $+3 */
    0x5e,              /* pop si */
    0x83, 0xc6, 0x1a,  /* add si, 1Ah */
    0xac,              /* lodsb */
    0x84, 0xc0,        /* test al, al */
    0x74, 0x09,        /* jz $+11 */
    0xb4, 0x0e,        /* mov ah, 0Eh */
    0xbb, 0x07, 0x00,  /* mov bx, 7 */
    0xcd, 0x10,        /* int 10h */
    0xeb, 0xf2,        /* jmp $-12 */
    0x31, 0xc0,        /* xor ax, ax */
    0xcd, 0x16,        /* int 16h */
    0xcd, 0x19,        /* int 19h */
    0xeb, 0xfe         /* jmp $ */
};
static const char boot_message[] = "\r\nThis disk is not bootable.\r\n"
                                   "Insert a system disk and press any key to restart.\r\n";

static void
put_le16(uint8_t *p, uint32_t v)
{
    p[0] = v;
    p[1] = v >> 8;
}

static void
put_le32(uint8_t *p, uint32_t v)
{
    put_le16(p, v);
    put_le16(p + 2, v >> 16);
}

/* Converts a character to the code page in 1 or 2 bytes, or returns 0 if it has no equivalent. */
static int
to_oem(int cp, uint32_t c, uint8_t *out)
{
    if ((c >= 0x20) && (c < 0x7f)) {
        out[0] = c;
        return 1;
    }
    return (c >= 0x80) ? fs_codepage_encode(cp, c, out) : 0;
}

static int
valid_short_char(uint8_t c)
{
    return (c >= 0x80) || ((c > 0x20) && !strchr("\"*+,./:;<=>?[\\]|\x7f", c));
}

static int
is_lower(uint32_t c)
{
    return fs_upcase(c) != c;
}

static int
is_upper(uint32_t c)
{
    if (c < 0x80)
        return (c >= 'A') && (c <= 'Z');
    if ((c >= 0xc0) && (c <= 0xde) && (c != 0xd7))
        return 1;
    return (c >= 0x391) && (c <= 0x3a9) && (c != 0x3a2);
}

static uint32_t
hash_bytes(const uint8_t *p, size_t len)
{
    uint32_t h = 2166136261U;
    for (size_t i = 0; i < len; i++)
        h = (h ^ p[i]) * 16777619U;
    return h;
}

/* Long names as Windows makes them. */
static const fs_long_name_rules_t long_name_rules = {
    .forbidden     = "\"*/:<>?\\|",
    .max           = 255,
    .keep_ext      = 1,
    .trim_trailing = 1
};

/* Hash set of short names in one directory. */
typedef struct {
    node_t **slots;
    size_t   mask;
} name_set_t;

static int
name_set_init(name_set_t *set, size_t count)
{
    size_t size = 16;
    while (size < (count * 2))
        size <<= 1;
    set->slots = (node_t **) calloc(size, sizeof(node_t *));
    set->mask  = size - 1;
    return set->slots != NULL;
}

/* Returns 1 if the short name was free and has been added. */
static int
short_set_add(name_set_t *set, node_t *node)
{
    size_t i = hash_bytes(node->short_name, 11) & set->mask;
    while (set->slots[i]) {
        if (!memcmp(set->slots[i]->short_name, node->short_name, 11))
            return 0;
        i = (i + 1) & set->mask;
    }
    set->slots[i] = node;
    return 1;
}

/* Makes the short name from the long name, following the Windows basis-name algorithm.
   Returns 1 if a numeric tail is needed, 0 if the name is already a valid 8.3 name. */
static int
make_basis_name(node_t *node, int cp)
{
    uint8_t base[512];
    uint8_t ext[512];
    size_t  base_len = 0;
    size_t  ext_len  = 0;
    int     lossy    = 0;
    int     last_dot = -1;

    memset(node->short_name, ' ', 11);

    /* Leading dots are dropped, so the extension starts at the last dot after them. */
    int start = 0;
    while ((start < node->lfn_len) && (node->lfn[start] == '.'))
        start++;
    for (int i = start; i < node->lfn_len; i++) {
        if (node->lfn[i] == '.')
            last_dot = i;
    }

    /* Case of each part: bit 0 = has lower case, bit 1 = has upper case. */
    int base_case = 0;
    int ext_case  = 0;
    int leading   = 1;
    for (int i = 0; i < node->lfn_len; i++) {
        uint32_t c = node->lfn[i];
        if ((c >= 0xd800) && (c <= 0xdfff)) {
            /* Not in the code page; one replacement per surrogate pair. */
            if (c >= 0xdc00)
                continue;
            c = 0xfffd;
        }
        if (c == ' ') {
            lossy = 1;
            continue;
        }
        if (c == '.') {
            if (leading || (i != last_dot))
                lossy = 1;
            continue;
        }
        leading = 0;

        int     *part_case = (i > last_dot && last_dot >= 0) ? &ext_case : &base_case;
        if (is_lower(c))
            *part_case |= 1;
        else if (is_upper(c))
            *part_case |= 2;

        uint8_t oem[2];
        int     n = to_oem(cp, fs_upcase(c), oem);
        if ((n == 1) && !valid_short_char(oem[0]))
            n = 0;
        if (!n) {
            oem[0] = '_';
            n      = 1;
            lossy  = 1;
        }
        if ((i > last_dot) && (last_dot >= 0)) {
            memcpy(ext + ext_len, oem, n);
            ext_len += n;
        } else {
            memcpy(base + base_len, oem, n);
            base_len += n;
        }
    }

    /* A name that is a valid 8.3 name apart from its case keeps it as the short name; only a
       mix of cases in one part needs a long name to keep the case. */
    int fits = !lossy && base_len && (base_len <= 8) && (ext_len <= 3) && ((last_dot < 0) || ext_len);
    if (!base_len) {
        base[0]  = '_';
        base_len = 1;
    }
    /* Cut without splitting a double-byte character. */
    memcpy(node->short_name, base, fs_codepage_fit(cp, base, base_len, 8));
    memcpy(node->short_name + 8, ext, fs_codepage_fit(cp, ext, ext_len, 3));
    if (node->short_name[0] == 0xe5)
        node->short_name[0] = 0x05;

    if (fits) {
        /* All-lower-case parts keep their case through the NT flags. */
        node->write_lfn = (base_case == 3) || (ext_case == 3);
        node->nt_case   = node->write_lfn ? 0 : (((base_case == 1) ? 0x08 : 0) | ((ext_case == 1) ? 0x10 : 0));
        return 0;
    }
    node->write_lfn = 1;
    return 1;
}

static void
apply_tail(node_t *node, const uint8_t *basis, uint32_t n, int cp)
{
    char tail[12];
    int  tail_len = snprintf(tail, sizeof(tail), "~%u", n);
    int  base_len = 8;

    memcpy(node->short_name, basis, 11);
    while (base_len && (basis[base_len - 1] == ' '))
        base_len--;
    /* Don't split a double-byte character. */
    base_len = (int) fs_codepage_fit(cp, basis, base_len, 8 - tail_len);
    memset(node->short_name + base_len, ' ', 8 - base_len);
    memcpy(node->short_name + base_len, tail, tail_len);
}

/* Names every child of a directory. */
static fat_image_result_t
name_children(node_t *dir, int cp)
{
    size_t count = 0;
    for (node_t *child = dir->children; child; child = child->next)
        count++;

    name_set_t    shorts;
    fs_name_set_t longs;
    if (!name_set_init(&shorts, count))
        return FAT_IMAGE_NO_MEMORY;
    if (!fs_name_set_init(&longs, count)) {
        free(shorts.slots);
        return FAT_IMAGE_NO_MEMORY;
    }

    fat_image_result_t result = FAT_IMAGE_OK;

    /* Long names, and the short names that are valid 8.3 names already, so that
       generated names never take them. */
    for (node_t *child = dir->children; child && (result == FAT_IMAGE_OK); child = child->next) {
        /* FAT is case-insensitive: names that only differ by case or by replaced characters get numbered. */
        int len    = 0;
        child->lfn = fs_long_name(child->name, &long_name_rules, &len);
        if (!child->lfn || !fs_name_set_add_unique(&longs, &child->lfn, &len, &long_name_rules)) {
            result = FAT_IMAGE_NO_MEMORY;
            break;
        }
        child->lfn_len = len;

        child->needs_tail = make_basis_name(child, cp);
        if (!child->needs_tail && !short_set_add(&shorts, child)) {
            /* The same 8.3 name as an earlier one; only possible through case folding. */
            child->needs_tail = 1;
            child->write_lfn  = 1;
        }
    }

    /* Generated names with numeric tails. */
    for (node_t *child = dir->children; child && (result == FAT_IMAGE_OK); child = child->next) {
        if (!child->needs_tail)
            continue;
        uint8_t basis[11];
        memcpy(basis, child->short_name, 11);
        uint32_t n = 1;
        do {
            if (n > 999999) {
                result = FAT_IMAGE_ROOT_FULL;
                break;
            }
            apply_tail(child, basis, n++, cp);
        } while (!short_set_add(&shorts, child));
        child->nt_case = 0;
    }

    free(shorts.slots);
    fs_name_set_free(&longs);
    return result;
}

static node_t *
node_new(fat_image_t *image, node_t *parent, const char *name)
{
    size_t  len  = strlen(name) + 1;
    node_t *node = (node_t *) calloc(1, sizeof(node_t) + len);
    if (!node)
        return NULL;
    memcpy(node->name, name, len);
    node->parent = parent;
    image->nodes++;
    return node;
}

static void
node_free(node_t *node)
{
    while (node) {
        node_t *next = node->next;
        node_free(node->children);
        free(node->host_path);
        free(node->lfn);
        free(node);
        node = next;
    }
}

static int
node_compare(const void *a, const void *b)
{
    const char *name_a = (*(const node_t *const *) a)->name;
    const char *name_b = (*(const node_t *const *) b)->name;
    int         ret    = strcasecmp(name_a, name_b);
    return ret ? ret : strcmp(name_a, name_b);
}

/* Sorts a directory by name, so the layout does not depend on the host's listing order. */
static int
sort_children(node_t *dir)
{
    size_t count = 0;
    for (node_t *child = dir->children; child; child = child->next)
        count++;
    if (count < 2)
        return 1;

    node_t **list = (node_t **) malloc(count * sizeof(node_t *));
    if (!list)
        return 0;
    count = 0;
    for (node_t *child = dir->children; child; child = child->next)
        list[count++] = child;
    qsort(list, count, sizeof(node_t *), node_compare);
    for (size_t i = 0; i < count; i++)
        list[i]->next = (i + 1 < count) ? list[i + 1] : NULL;
    dir->children = list[0];
    free(list);
    return 1;
}

static int
file_readable(const char *path)
{
    FILE *fp = plat_fopen64(path, "rb");
    if (!fp)
        return 0;
    fclose(fp);
    return 1;
}

/* Leaves out a folder that could not be read, which holds nothing yet. */
static void
drop_node(fat_image_t *image, node_t **link)
{
    node_t *node = *link;
    *link        = node->next;
    node->next   = NULL;
    node_free(node);
    image->unreadable = 1;
}

static fat_image_result_t
scan_folder(fat_image_t *image, node_t *dir, const char *path, int depth)
{
    plat_dir_t         context = PLAT_DIR_INIT;
    fat_image_result_t result  = FAT_IMAGE_OK;
    node_t           **tail    = &dir->children;

    if (depth >= MAX_DEPTH)
        return FAT_IMAGE_READ_ERROR;
    if (!plat_dir_open(&context, path)) {
        plat_dir_close(&context);
        return FAT_IMAGE_READ_ERROR;
    }

    while (plat_dir_read_no_links(&context)) {
        int       is_dir = plat_dir_is_dir(&context);
        if (!is_dir && !plat_dir_is_file(&context))
            continue;
        if (!is_dir && plat_dir_get_size(&context) && !file_readable(plat_dir_get_path(&context))) {
            image->unreadable = 1;
            continue;
        }

        /* Names stored decomposed (on macOS) are composed, as FAT names are. */
        char   *composed = fs_utf8_compose(plat_dir_get_name(&context));
        node_t *node     = node_new(image, dir, composed ? composed : plat_dir_get_name(&context));
        free(composed);
        if (!node || !(node->host_path = strdup(plat_dir_get_path(&context)))) {
            free(node);
            result = FAT_IMAGE_NO_MEMORY;
            break;
        }
        node->is_dir = is_dir;
        node->mtime  = plat_dir_get_mtime(&context);
        if (!is_dir)
            node->size = plat_dir_get_size(&context);
        *tail = node;
        tail  = &node->next;
    }
    plat_dir_close(&context);

    if ((result == FAT_IMAGE_OK) && !sort_children(dir))
        result = FAT_IMAGE_NO_MEMORY;
    for (node_t **link = &dir->children; *link && (result == FAT_IMAGE_OK);) {
        if ((*link)->is_dir) {
            result = scan_folder(image, *link, (*link)->host_path, depth + 1);
            if (result == FAT_IMAGE_READ_ERROR) {
                drop_node(image, link);
                result = FAT_IMAGE_OK;
                continue;
            }
        }
        link = &(*link)->next;
    }
    return result;
}

static fat_image_result_t
scan_archive(fat_image_t *image, node_t *dir, media_archive_entry_t *entry, int depth)
{
    node_t **tail = &dir->children;

    if (depth >= MAX_DEPTH)
        return FAT_IMAGE_READ_ERROR;

    /* Archive children come sorted by name. */
    for (media_archive_entry_t *child = entry->children; child; child = child->next) {
        node_t *node = node_new(image, dir, child->name);
        if (!node)
            return FAT_IMAGE_NO_MEMORY;
        node->entry  = child;
        node->is_dir = child->is_dir;
        node->size   = child->is_dir ? 0 : child->size;
        node->mtime  = child->has_header ? child->mtime : image->now;
        node->attr   = child->dos_attr & ATTR_READ_ONLY;
        child->priv  = node;
        *tail        = node;
        tail         = &node->next;
    }
    for (node_t **link = &dir->children; *link;) {
        if ((*link)->is_dir) {
            fat_image_result_t result = scan_archive(image, *link, (*link)->entry, depth + 1);
            if (result == FAT_IMAGE_READ_ERROR) {
                (*link)->entry->priv = NULL;
                drop_node(image, link);
                continue;
            }
            if (result != FAT_IMAGE_OK)
                return result;
        }
        link = &(*link)->next;
    }
    return FAT_IMAGE_OK;
}

static fat_image_result_t
name_tree(node_t *dir, int cp)
{
    fat_image_result_t result = name_children(dir, cp);

    /* . and .. in subdirectories, the volume label in the root. */
    dir->entries = 2;
    for (node_t *child = dir->children; child && (result == FAT_IMAGE_OK); child = child->next) {
        dir->entries += 1 + (child->write_lfn ? ((child->lfn_len + 12) / 13) : 0);
        if (child->is_dir)
            result = name_tree(child, cp);
    }
    return result;
}

static void
make_label(fat_image_t *image, const char *path)
{
    char  buf[1024];
    char *name;

    snprintf(buf, sizeof(buf), "%s", path);
    path_normalize(buf);
    size_t len = strlen(buf);
    while (len > 1 && ((buf[len - 1] == '/') || (buf[len - 1] == '\\')))
        buf[--len] = '\0';
    name = path_get_filename(buf);
    if (!plat_dir_check(buf) && name[0]) {
        /* Drop the archive extension, including the .tar of a .tar.gz. */
        char *dot = strrchr(name + 1, '.');
        if (dot) {
            if (((dot - name) > 4) && !strncasecmp(dot - 4, ".tar", 4))
                dot -= 4;
            *dot = '\0';
        }
    }

    memset(image->label, ' ', 11);
    int n = 0;
    for (const char *p = name; *p && (n < 11);) {
        uint8_t c[2];
        int     len = to_oem(image->cp, fs_upcase(fs_utf8_next(&p)), c);
        if ((len == 1) && (c[0] != ' ') && !valid_short_char(c[0]))
            len = 0;
        if ((n + len) > 11)
            break;
        memcpy(image->label + n, c, len);
        n += len;
    }
    /* A label can't start with a space. */
    while ((n > 0) && (image->label[0] == ' ')) {
        memmove(image->label, image->label + 1, 10);
        image->label[10] = ' ';
        n--;
    }
    if ((uint8_t) image->label[0] == 0xe5)
        image->label[0] = 0x05;
}

int
fat_image_is_source(const char *path)
{
    return plat_dir_check((char *) path) || media_archive_is_archive(path);
}

fat_image_result_t
fat_image_scan(const char *path, fat_image_t **out)
{
    fat_image_t *image = (fat_image_t *) calloc(1, sizeof(fat_image_t));
    if (!image)
        return FAT_IMAGE_NO_MEMORY;
    image->now       = time(NULL);
    image->cp        = media_codepage();
    image->tz_offset = civil_time_local_offset(image->now, 0);
    image->root = node_new(image, NULL, "");
    if (!image->root) {
        free(image);
        return FAT_IMAGE_NO_MEMORY;
    }
    image->root->is_dir = 1;

    fat_image_result_t result;
    if (plat_dir_check((char *) path)) {
        result = scan_folder(image, image->root, path, 0);
    } else {
        image->archive = media_archive_open(path);
        result         = image->archive ? scan_archive(image, image->root, media_archive_root(image->archive), 0) :
                                          FAT_IMAGE_READ_ERROR;
        if (image->archive && media_archive_unreadable(image->archive))
            image->unreadable = 1;
    }
    if (result == FAT_IMAGE_OK)
        result = name_tree(image->root, image->cp);
    if (result == FAT_IMAGE_OK)
        make_label(image, path);

    if (result != FAT_IMAGE_OK) {
        fat_image_free(image);
        return result;
    }
    *out = image;
    return FAT_IMAGE_OK;
}

void
fat_image_free(fat_image_t *image)
{
    if (!image)
        return;
    node_free(image->root);
    media_archive_close(image->archive);
    free(image);
}

static int
has_label(const fat_image_t *image)
{
    return image->label[0] != ' ';
}

/* Computes the FAT size and cluster count for a cluster size. */
static void
size_fat(layout_t *l)
{
    l->root_sectors = (l->root_entries * 32 + l->bps - 1) / l->bps;
    l->fat_sectors  = 1;
    for (;;) {
        uint64_t meta = (uint64_t) l->reserved + ((uint64_t) l->num_fats * l->fat_sectors) + l->root_sectors;
        uint64_t data = (l->volume_sectors > meta) ? (l->volume_sectors - meta) : 0;
        uint64_t clusters = data / l->spc;
        if (clusters > 0x0ffffff4)
            clusters = 0x0ffffff4;
        uint64_t need = (((clusters + 2) * l->bits + 7) / 8 + l->bps - 1) / l->bps;
        if (need <= l->fat_sectors) {
            l->clusters = (uint32_t) clusters;
            break;
        }
        l->fat_sectors = (uint32_t) need;
    }
}

static int
fat_type_of(uint32_t clusters)
{
    return (clusters < 4085) ? 12 : ((clusters < 65525) ? 16 : 32);
}

static void
set_type(layout_t *l, int bits)
{
    l->bits         = bits;
    l->reserved     = (bits == 32) ? 32 : 1;
    l->root_entries = (bits == 32) ? 0 : 512;
}

/* Picks the FAT type and cluster size from the volume size, like Microsoft's FORMAT. */
static int
choose_layout(const fat_image_params_t *p, layout_t *l)
{
    uint64_t       bytes = l->volume_sectors * l->bps;
    uint32_t       cluster_size;

    l->num_fats = 2;
    l->media    = p->partition_start ? 0xf8 : 0xf0;

    if (p->spc) {
        /* Fixed floppy layout. */
        l->bits         = 12;
        l->spc          = p->spc;
        l->reserved     = 1;
        l->num_fats     = p->num_fats;
        l->fat_sectors  = p->spfat;
        l->root_entries = p->root_entries;
        l->root_sectors = (l->root_entries * 32 + l->bps - 1) / l->bps;
        l->media        = p->media_desc;
        l->clusters     = (uint32_t) ((l->volume_sectors - l->reserved - (l->num_fats * l->fat_sectors) - l->root_sectors) / l->spc);
        return 1;
    }

    if (bytes < (16ULL << 20)) {
        set_type(l, 12);
        cluster_size = 512;
    } else if (bytes <= (2048ULL << 20)) {
        set_type(l, 16);
        if (bytes <= (16ULL << 20))
            cluster_size = 1024;
        else if (bytes <= (128ULL << 20))
            cluster_size = 2048;
        else if (bytes <= (256ULL << 20))
            cluster_size = 4096;
        else if (bytes <= (512ULL << 20))
            cluster_size = 8192;
        else if (bytes <= (1024ULL << 20))
            cluster_size = 16384;
        else
            cluster_size = 32768;
    } else {
        set_type(l, 32);
        if (bytes <= (8ULL << 30))
            cluster_size = 4096;
        else if (bytes <= (16ULL << 30))
            cluster_size = 8192;
        else if (bytes <= (32ULL << 30))
            cluster_size = 16384;
        else
            cluster_size = 32768;
    }

    for (int tries = 0; tries < 32; tries++) {
        if (cluster_size < l->bps)
            cluster_size = l->bps;
        l->spc = cluster_size / l->bps;
        size_fat(l);

        /* The cluster count is what decides the FAT type; keep the two in agreement. */
        int type = fat_type_of(l->clusters);
        if (type == l->bits)
            return 1;
        if (type > l->bits) {
            /* Too many clusters for this type: larger clusters. */
            if (cluster_size >= 32768) {
                if (l->bits == 32)
                    return 0;
                set_type(l, l->bits == 12 ? 16 : 32);
            } else
                cluster_size <<= 1;
        } else {
            /* Too few clusters: smaller clusters, or a smaller FAT. */
            if (cluster_size > l->bps)
                cluster_size >>= 1;
            else
                set_type(l, l->bits == 32 ? 16 : 12);
        }
    }
    return 0;
}

static uint64_t
clusters_for(const layout_t *l, uint64_t bytes)
{
    return (bytes + l->cluster_size - 1) / l->cluster_size;
}

/* Leaves a node out of the volume, with everything under it. */
static void
leave_out(node_t *node, int left_out)
{
    node->left_out = left_out;
    for (node_t *child = node->children; child; child = child->next)
        leave_out(child, left_out);
}

/* Leaves out the children that don't fit in their directory's entries, and counts the entries used. */
static void
fit_entries(fat_image_t *image, const layout_t *l, node_t *dir)
{
    /* The FAT12 and FAT16 root directory has a fixed size; other directories hold up to 65536 entries,
       less . and .. or the volume label. */
    int       fixed_root = !dir->parent && (l->bits != 32);
    uint32_t  avail      = fixed_root ? (l->root_entries - has_label(image)) : (65536 - 2);
    uint32_t  used       = 0;

    for (node_t *child = dir->children; child; child = child->next) {
        uint32_t need = 1 + (child->write_lfn ? ((child->lfn_len + 12) / 13) : 0);
        if ((used + need) > avail) {
            leave_out(child, 1);
            image->left_out |= fixed_root ? FAT_IMAGE_LEFT_OUT_ROOT : FAT_IMAGE_LEFT_OUT_SPACE;
            continue;
        }
        used += need;
        if (child->is_dir)
            fit_entries(image, l, child);
    }
    dir->entries = 2 + used;
}

/* Allocates every directory, then every file, leaving out what doesn't fit. */
static fat_image_result_t
allocate_dirs(fat_image_t *image, layout_t *l, node_t *dir, uint32_t *next)
{
    if (dir->parent || (l->bits == 32)) {
        uint64_t n = clusters_for(l, (uint64_t) dir->entries * 32);
        if (n > (l->clusters - (*next - 2))) {
            if (!dir->parent)
                return FAT_IMAGE_FULL;
            leave_out(dir, 1);
            image->left_out |= FAT_IMAGE_LEFT_OUT_SPACE;
            return FAT_IMAGE_OK;
        }
        dir->first_cluster = *next;
        dir->clusters      = (uint32_t) n;
        *next += (uint32_t) n;
    }
    for (node_t *child = dir->children; child; child = child->next) {
        if (child->is_dir && !child->left_out) {
            fat_image_result_t result = allocate_dirs(image, l, child, next);
            if (result != FAT_IMAGE_OK)
                return result;
        }
    }
    return FAT_IMAGE_OK;
}

/* Returns 1 if the file was given its clusters, 0 if it was left out. */
static int
allocate_file(fat_image_t *image, const layout_t *l, node_t *file, uint32_t *next)
{
    uint64_t n = clusters_for(l, file->size);
    if ((file->size > 0xffffffffULL) || (n > (l->clusters - (*next - 2)))) {
        leave_out(file, 1);
        image->left_out |= FAT_IMAGE_LEFT_OUT_SPACE;
        return 0;
    }
    file->first_cluster = n ? *next : 0;
    file->clusters      = (uint32_t) n;
    *next += (uint32_t) n;
    return 1;
}

static void
allocate_files(fat_image_t *image, layout_t *l, node_t *dir, uint32_t *next)
{
    for (node_t *child = dir->children; child; child = child->next) {
        if (child->left_out)
            continue;
        if (child->is_dir)
            allocate_files(image, l, child, next);
        else
            (void) allocate_file(image, l, child, next);
    }
}

/* Allocates a directory, its files, then its subdirectories, so that a directory goes in with its
   contents. A directory none of whose contents fit is left out too, giving its clusters back; nothing
   has been allocated after them then. */
static fat_image_result_t
allocate_tree(fat_image_t *image, layout_t *l, node_t *dir, uint32_t *next)
{
    if (dir->parent || (l->bits == 32)) {
        uint64_t n = clusters_for(l, (uint64_t) dir->entries * 32);
        if (n > (l->clusters - (*next - 2))) {
            if (!dir->parent)
                return FAT_IMAGE_FULL;
            leave_out(dir, 1);
            image->left_out |= FAT_IMAGE_LEFT_OUT_SPACE;
            return FAT_IMAGE_OK;
        }
        dir->first_cluster = *next;
        dir->clusters      = (uint32_t) n;
        *next += (uint32_t) n;
    }

    int tried  = 0;
    int placed = 0;
    for (node_t *child = dir->children; child; child = child->next) {
        if (!child->left_out && !child->is_dir) {
            tried = 1;
            placed |= allocate_file(image, l, child, next);
        }
    }
    for (node_t *child = dir->children; child; child = child->next) {
        if (!child->left_out && child->is_dir) {
            tried = 1;
            fat_image_result_t result = allocate_tree(image, l, child, next);
            if (result != FAT_IMAGE_OK)
                return result;
            placed |= !child->left_out;
        }
    }

    if (dir->parent && tried && !placed) {
        *next = dir->first_cluster;
        leave_out(dir, 1);
    }
    return FAT_IMAGE_OK;
}

static fat_image_result_t
layout_volume(fat_image_t *image, const fat_image_params_t *p, layout_t *l)
{
    memset(l, 0, sizeof(layout_t));
    leave_out(image->root, 0);
    image->left_out = 0;

    l->bps = p->sector_size;
    if ((l->bps < 512) || (l->bps > 4096) || (l->bps & (l->bps - 1)) || (p->partition_start >= p->sectors))
        return FAT_IMAGE_UNSUPPORTED;
    l->volume_sectors = p->sectors - p->partition_start;
    if ((l->volume_sectors > 0xffffffffULL) || !choose_layout(p, l) || (l->clusters < 1))
        return FAT_IMAGE_UNSUPPORTED;

    l->cluster_size = l->spc * l->bps;
    l->fat_start    = l->reserved;
    l->root_start   = l->fat_start + (uint64_t) l->num_fats * l->fat_sectors;
    l->data_start   = l->root_start + l->root_sectors;

    fit_entries(image, l, image->root);

    /* Directories first, then files, when everything fits. */
    uint32_t           next   = 2;
    fat_image_result_t result = allocate_dirs(image, l, image->root, &next);
    if (result == FAT_IMAGE_OK)
        allocate_files(image, l, image->root, &next);

    /* Otherwise, the directories that would take space ahead of all files could crowd them out and go
       in empty: allocate each directory with its contents instead. */
    if ((result == FAT_IMAGE_OK) && (image->left_out & FAT_IMAGE_LEFT_OUT_SPACE)) {
        leave_out(image->root, 0);
        image->left_out = 0;
        fit_entries(image, l, image->root);
        next   = 2;
        result = allocate_tree(image, l, image->root, &next);
    }

    l->used = next - 2;
    return result;
}

fat_image_result_t
fat_image_check(fat_image_t *image, const fat_image_params_t *params)
{
    layout_t l;
    return layout_volume(image, params, &l);
}

int
fat_image_left_out(const fat_image_t *image)
{
    return image->left_out | (image->unreadable ? FAT_IMAGE_LEFT_OUT_READ : 0);
}

typedef struct {
    fat_image_write_t write;
    void             *opaque;
    uint64_t          base; /* volume start, in bytes */
    const layout_t   *l;
} writer_t;

static int
emit(const writer_t *w, uint64_t offset, const void *data, size_t size)
{
    return w->write(w->opaque, w->base + offset, data, size);
}

static uint64_t
cluster_offset(const layout_t *l, uint32_t cluster)
{
    return (l->data_start + (uint64_t) (cluster - 2) * l->spc) * l->bps;
}

static void
fat_set(const layout_t *l, uint8_t *fat, uint32_t cluster, uint32_t value)
{
    switch (l->bits) {
        case 12: {
            uint8_t *p = fat + cluster + (cluster >> 1);
            if (cluster & 1) {
                p[0] = (p[0] & 0x0f) | (value << 4);
                p[1] = value >> 4;
            } else {
                p[0] = value;
                p[1] = (p[1] & 0xf0) | ((value >> 8) & 0x0f);
            }
            break;
        }
        case 16:
            put_le16(fat + cluster * 2, value);
            break;
        default:
            put_le32(fat + cluster * 4, value & 0x0fffffff);
            break;
    }
}

static void
fat_chain(const layout_t *l, uint8_t *fat, const node_t *dir)
{
    uint32_t       eoc = (l->bits == 12) ? 0xfff : ((l->bits == 16) ? 0xffff : 0x0fffffff);

    for (const node_t *node = dir; node; node = node->next) {
        if (node->left_out)
            continue;
        for (uint32_t i = 0; i < node->clusters; i++)
            fat_set(l, fat, node->first_cluster + i, (i + 1 < node->clusters) ? (node->first_cluster + i + 1) : eoc);
        if (node->is_dir)
            fat_chain(l, fat, node->children);
    }
}

static void
fat_time(int64_t t, int tz_offset, uint16_t *date, uint16_t *time_out, uint8_t *tenth)
{
    /* FAT holds local times, as the clock showed them then. */
    civil_time_t civil;
    civil_time_from_seconds(t + civil_time_local_offset(t, tz_offset), &civil);
    if (civil.year < 1980) {
        civil.year  = 1980;
        civil.month = civil.day = 1;
        civil.hour = civil.minute = civil.second = 0;
    } else if (civil.year > 2107) {
        civil.year   = 2107;
        civil.month  = 12;
        civil.day    = 31;
        civil.hour   = 23;
        civil.minute = 59;
        civil.second = 58;
    }
    *date     = ((civil.year - 1980) << 9) | (civil.month << 5) | civil.day;
    *time_out = (civil.hour << 11) | (civil.minute << 5) | (civil.second >> 1);
    if (tenth)
        *tenth = (civil.second & 1) ? 100 : 0;
}

static void
dir_entry(uint8_t *p, const uint8_t *name, uint8_t attr, uint8_t nt_case, time_t mtime, int tz_offset, uint32_t cluster,
          uint32_t size)
{
    uint16_t date;
    uint16_t time_v;
    uint8_t  tenth;

    memcpy(p, name, 11);
    p[11] = attr;
    p[12] = nt_case;
    fat_time(mtime, tz_offset, &date, &time_v, &tenth);
    p[13] = tenth;
    put_le16(p + 14, time_v); /* creation */
    put_le16(p + 16, date);
    put_le16(p + 18, date);   /* last access */
    put_le16(p + 20, cluster >> 16);
    put_le16(p + 22, time_v); /* last write */
    put_le16(p + 24, date);
    put_le16(p + 26, cluster);
    put_le32(p + 28, size);
}

static uint8_t
lfn_checksum(const uint8_t *name)
{
    uint8_t sum = 0;
    for (int i = 0; i < 11; i++)
        sum = ((sum & 1) ? 0x80 : 0) + (sum >> 1) + name[i];
    return sum;
}

/* Fills a directory's entries; returns the number of bytes used. */
static size_t
fill_dir(const fat_image_t *image, const node_t *dir, uint8_t *buf)
{
    static const uint8_t dot[11]    = { '.', ' ', ' ', ' ', ' ', ' ', ' ', ' ', ' ', ' ', ' ' };
    static const uint8_t dotdot[11] = { '.', '.', ' ', ' ', ' ', ' ', ' ', ' ', ' ', ' ', ' ' };
    uint8_t             *p          = buf;

    if (dir->parent) {
        const node_t  *parent         = dir->parent;
        uint32_t       parent_cluster = parent->parent ? parent->first_cluster : 0;
        dir_entry(p, dot, ATTR_DIRECTORY, 0, dir->mtime, image->tz_offset, dir->first_cluster, 0);
        dir_entry(p + 32, dotdot, ATTR_DIRECTORY, 0, parent->parent ? parent->mtime : dir->mtime, image->tz_offset, parent_cluster, 0);
        p += 64;
    } else if (has_label(image)) {
        dir_entry(p, (const uint8_t *) image->label, ATTR_VOLUME_ID, 0, image->now, image->tz_offset, 0, 0);
        p += 32;
    }

    for (const node_t *child = dir->children; child; child = child->next) {
        if (child->left_out)
            continue;
        if (child->write_lfn) {
            int           count = (child->lfn_len + 12) / 13;
            uint8_t       sum   = lfn_checksum(child->short_name);
            for (int seq = count; seq >= 1; seq--) {
                static const uint8_t offsets[13] = { 1, 3, 5, 7, 9, 14, 16, 18, 20, 22, 24, 28, 30 };
                memset(p, 0, 32);
                p[0]  = seq | ((seq == count) ? 0x40 : 0);
                p[11] = ATTR_LFN;
                p[13] = sum;
                for (int i = 0; i < 13; i++) {
                    int      pos = (seq - 1) * 13 + i;
                    uint16_t c   = (pos < child->lfn_len) ? child->lfn[pos] : ((pos == child->lfn_len) ? 0x0000 : 0xffff);
                    put_le16(p + offsets[i], c);
                }
                p += 32;
            }
        }
        dir_entry(p, child->short_name, child->is_dir ? ATTR_DIRECTORY : (ATTR_ARCHIVE | child->attr), child->nt_case,
                  child->mtime, image->tz_offset, child->first_cluster, child->is_dir ? 0 : (uint32_t) child->size);
        p += 32;
    }
    return p - buf;
}

static fat_image_result_t
write_dirs(const fat_image_t *image, const writer_t *w, const node_t *dir, uint8_t **buf, size_t *buf_size)
{
    const layout_t *l      = w->l;
    size_t          needed = (size_t) dir->entries * 32 + 32;

    if (needed > *buf_size) {
        uint8_t *new_buf = (uint8_t *) realloc(*buf, needed);
        if (!new_buf)
            return FAT_IMAGE_NO_MEMORY;
        *buf      = new_buf;
        *buf_size = needed;
    }

    size_t used = fill_dir(image, dir, *buf);
    if (used) {
        uint64_t offset = dir->first_cluster ? cluster_offset(l, dir->first_cluster) : (l->root_start * l->bps);
        if (!emit(w, offset, *buf, used))
            return FAT_IMAGE_WRITE_ERROR;
    }

    for (const node_t *child = dir->children; child; child = child->next) {
        if (child->is_dir && !child->left_out) {
            fat_image_result_t result = write_dirs(image, w, child, buf, buf_size);
            if (result != FAT_IMAGE_OK)
                return result;
        }
    }
    return FAT_IMAGE_OK;
}

/* Files that can't be read keep zeros where their data could not be read. */
static fat_image_result_t
write_folder_files(const writer_t *w, const node_t *dir, uint8_t *buf, int *unreadable)
{
    for (const node_t *child = dir->children; child; child = child->next) {
        if (child->left_out)
            continue;
        if (child->is_dir) {
            fat_image_result_t result = write_folder_files(w, child, buf, unreadable);
            if (result != FAT_IMAGE_OK)
                return result;
            continue;
        }
        if (!child->size)
            continue;

        FILE *fp = plat_fopen64(child->host_path, "rb");
        if (!fp) {
            *unreadable = 1;
            continue;
        }

        /* A file that has shrunk since the scan reads as zeros past its end. */
        uint64_t offset = cluster_offset(w->l, child->first_cluster);
        uint64_t remain = child->size;
        while (remain) {
            size_t chunk = (remain > COPY_BUFFER) ? COPY_BUFFER : (size_t) remain;
            size_t read  = fread(buf, 1, chunk, fp);
            if (!read)
                break;
            if (!emit(w, offset, buf, read)) {
                fclose(fp);
                return FAT_IMAGE_WRITE_ERROR;
            }
            offset += read;
            remain -= read;
        }
        if (ferror(fp))
            *unreadable = 1;
        fclose(fp);
    }
    return FAT_IMAGE_OK;
}

typedef struct {
    const writer_t *w;
    int             write_error;
} archive_writer_t;

static int
write_archive_data(void *opaque, media_archive_entry_t *entry, uint64_t offset, const void *data, size_t size)
{
    archive_writer_t *aw   = (archive_writer_t *) opaque;
    const node_t     *node = (const node_t *) entry->priv;

    if (!node || node->left_out || !size)
        return 1;
    if (!emit(aw->w, cluster_offset(aw->w->l, node->first_cluster) + offset, data, size)) {
        aw->write_error = 1;
        return 0;
    }
    return 1;
}

static void
write_boot_code(uint8_t *sector, size_t code_offset, int pc98)
{
    if (pc98) {
        /* PC-98 loads boot sectors elsewhere and has no PC BIOS; just stop. */
        sector[code_offset]     = 0xeb;
        sector[code_offset + 1] = 0xfe;
    } else {
        memcpy(sector + code_offset, boot_code, sizeof(boot_code));
        memcpy(sector + code_offset + sizeof(boot_code), boot_message, sizeof(boot_message));
    }
    sector[510] = 0x55;
    sector[511] = 0xaa;
}

static void
lba_to_chs(const fat_image_params_t *p, uint64_t lba, uint8_t *chs)
{
    uint32_t spt   = p->sectors_per_track ? p->sectors_per_track : 63;
    uint32_t heads = p->heads ? p->heads : 255;
    uint64_t cyl   = lba / (spt * heads);

    if (cyl > 1023) {
        chs[0] = 0xfe;
        chs[1] = 0xff;
        chs[2] = 0xff;
        return;
    }
    uint32_t head   = (lba / spt) % heads;
    uint32_t sector = (lba % spt) + 1;
    chs[0]          = head;
    chs[1]          = sector | ((cyl >> 2) & 0xc0);
    chs[2]          = cyl;
}

fat_image_result_t
fat_image_write(fat_image_t *image, const fat_image_params_t *p, fat_image_write_t write, void *opaque)
{
    layout_t           l;
    fat_image_result_t result = layout_volume(image, p, &l);
    if (result != FAT_IMAGE_OK)
        return result;

    writer_t w = { .write = write, .opaque = opaque, .base = (uint64_t) p->partition_start * l.bps, .l = &l };
    uint8_t *sector = (uint8_t *) calloc(1, l.bps);
    if (!sector)
        return FAT_IMAGE_NO_MEMORY;

    /* Partition table. */
    if (p->partition_start) {
        uint8_t *entry = sector + 0x1be + (p->partition_slot & 3) * 16;
        write_boot_code(sector, 0, 0);
        entry[0] = p->partition_active ? 0x80 : 0x00;
        lba_to_chs(p, p->partition_start, entry + 1);
        lba_to_chs(p, p->sectors - 1, entry + 5);
        if (l.bits == 12)
            entry[4] = 0x01;
        else if (l.bits == 16)
            entry[4] = (l.volume_sectors < 65536) ? 0x04 : 0x06;
        else /* LBA type if the end is past what CHS can address */
            entry[4] = ((entry[5] == 0xfe) && (entry[6] == 0xff) && (entry[7] == 0xff)) ? 0x0c : 0x0b;
        put_le32(entry + 8, p->partition_start);
        put_le32(entry + 12, (uint32_t) l.volume_sectors);
        if (!write(opaque, 0, sector, 512)) {
            free(sector);
            return FAT_IMAGE_WRITE_ERROR;
        }
        memset(sector, 0, l.bps);
    }

    /* Boot sector. */
    uint32_t serial = random_generate() | (random_generate() << 8) | (random_generate() << 16) | ((uint32_t) random_generate() << 24);
    uint8_t *ebpb   = sector + ((l.bits == 32) ? 0x40 : 0x24);
    sector[0]       = 0xeb;
    sector[1]       = (l.bits == 32) ? 0x58 : 0x3c;
    sector[2]       = 0x90;
    memcpy(sector + 3, "MSWIN4.1", 8);
    put_le16(sector + 0x0b, l.bps);
    sector[0x0d] = l.spc;
    put_le16(sector + 0x0e, l.reserved);
    sector[0x10] = l.num_fats;
    put_le16(sector + 0x11, l.root_entries);
    if ((l.bits != 32) && (l.volume_sectors < 65536))
        put_le16(sector + 0x13, (uint32_t) l.volume_sectors);
    else
        put_le32(sector + 0x20, (uint32_t) l.volume_sectors);
    sector[0x15] = l.media;
    if (l.bits != 32)
        put_le16(sector + 0x16, l.fat_sectors);
    put_le16(sector + 0x18, p->sectors_per_track);
    put_le16(sector + 0x1a, p->heads);
    put_le32(sector + 0x1c, p->partition_start);
    if (l.bits == 32) {
        put_le32(sector + 0x24, l.fat_sectors);
        put_le32(sector + 0x2c, 2); /* root directory cluster */
        put_le16(sector + 0x30, 1); /* FSInfo sector */
        put_le16(sector + 0x32, 6); /* backup boot sector */
    }
    ebpb[0] = p->drive_number;
    ebpb[2] = 0x29;
    put_le32(ebpb + 3, serial);
    memcpy(ebpb + 7, has_label(image) ? image->label : "NO NAME    ", 11);
    memcpy(ebpb + 18, (l.bits == 12) ? "FAT12   " : ((l.bits == 16) ? "FAT16   " : "FAT32   "), 8);
    write_boot_code(sector, (l.bits == 32) ? 0x5a : 0x3e, p->pc98);
    if (!emit(&w, 0, sector, l.bps) || ((l.bits == 32) && !emit(&w, 6ULL * l.bps, sector, l.bps))) {
        free(sector);
        return FAT_IMAGE_WRITE_ERROR;
    }

    if (l.bits == 32) {
        /* FSInfo, and its backup after the backup boot sector. */
        memset(sector, 0, l.bps);
        put_le32(sector, 0x41615252);
        put_le32(sector + 484, 0x61417272);
        put_le32(sector + 488, l.clusters - l.used);
        put_le32(sector + 492, l.used + 2);
        put_le32(sector + 508, 0xaa550000);
        if (!emit(&w, 1ULL * l.bps, sector, l.bps) || !emit(&w, 7ULL * l.bps, sector, l.bps)) {
            free(sector);
            return FAT_IMAGE_WRITE_ERROR;
        }
        /* The Windows boot code's third sector carries the signature too. */
        memset(sector, 0, l.bps);
        sector[510] = 0x55;
        sector[511] = 0xaa;
        if (!emit(&w, 2ULL * l.bps, sector, l.bps) || !emit(&w, 8ULL * l.bps, sector, l.bps)) {
            free(sector);
            return FAT_IMAGE_WRITE_ERROR;
        }
    }
    free(sector);

    /* FATs, written only as far as they have entries. */
    uint64_t fat_used = ((uint64_t) (l.used + 2) * l.bits + 7) / 8;
    uint8_t *fat      = (uint8_t *) calloc(1, (size_t) fat_used + 4);
    if (!fat)
        return FAT_IMAGE_NO_MEMORY;
    fat_set(&l, fat, 0, (l.bits == 12) ? (0xf00 | l.media) : ((l.bits == 16) ? (0xff00 | l.media) : (0x0fffff00 | l.media)));
    fat_set(&l, fat, 1, (l.bits == 12) ? 0xfff : ((l.bits == 16) ? 0xffff : 0x0fffffff));
    fat_chain(&l, fat, image->root);
    for (uint32_t i = 0; i < l.num_fats; i++) {
        if (!emit(&w, (l.fat_start + (uint64_t) i * l.fat_sectors) * l.bps, fat, (size_t) fat_used)) {
            free(fat);
            return FAT_IMAGE_WRITE_ERROR;
        }
    }
    free(fat);

    /* Directories. */
    uint8_t *buf      = NULL;
    size_t   buf_size = 0;
    result            = write_dirs(image, &w, image->root, &buf, &buf_size);
    free(buf);
    if (result != FAT_IMAGE_OK)
        return result;

    /* File data. */
    if (image->archive) {
        archive_writer_t aw = { .w = &w };
        if (!media_archive_extract(image->archive, write_archive_data, &aw))
            return aw.write_error ? FAT_IMAGE_WRITE_ERROR : FAT_IMAGE_READ_ERROR;
        if (media_archive_unreadable(image->archive))
            image->unreadable = 1;
    } else {
        buf = (uint8_t *) malloc(COPY_BUFFER);
        if (!buf)
            return FAT_IMAGE_NO_MEMORY;
        result = write_folder_files(&w, image->root, buf, &image->unreadable);
        free(buf);
    }
    return result;
}

int
fat_image_pick(fat_image_t *image, int count, int def, fat_image_medium_t medium, void *opaque,
               fat_image_params_t *params, fat_image_result_t *result)
{
    fat_image_params_t p;
    int                picked        = -1;
    int                largest       = -1; /* the medium to fill if none holds everything */
    uint64_t           largest_bytes = 0;
    uint8_t            tried[256];

    *result = FAT_IMAGE_UNSUPPORTED;
    if ((count <= 0) || (count > (int) sizeof(tried)))
        return -1;
    memset(tried, 0, count);
    if ((def >= 0) && (def < count) && !medium(opaque, def, &p))
        tried[def] = 1;

    for (;;) {
        /* The default medium first, then the smallest one not tried yet. */
        int      next       = -1;
        uint64_t next_bytes = 0;
        if ((def >= 0) && (def < count) && !tried[def]) {
            next = def;
        } else {
            for (int i = 0; i < count; i++) {
                if (tried[i] || !medium(opaque, i, &p))
                    continue;
                if ((next < 0) || ((p.sectors * p.sector_size) < next_bytes)) {
                    next       = i;
                    next_bytes = p.sectors * p.sector_size;
                }
            }
        }
        if (next < 0)
            break;
        tried[next] = 1;
        (void) medium(opaque, next, &p);
        next_bytes = p.sectors * p.sector_size;

        fat_image_result_t check = fat_image_check(image, &p);
        if (check == FAT_IMAGE_OK) {
            if (!image->left_out) {
                picked  = next;
                *params = p;
                break;
            }
            if ((largest < 0) || (next_bytes > largest_bytes)) {
                largest       = next;
                largest_bytes = next_bytes;
            }
        }
        if ((*result == FAT_IMAGE_UNSUPPORTED) || (check != FAT_IMAGE_UNSUPPORTED))
            *result = check;
    }

    /* Nothing holds everything: fill the largest medium with what fits. */
    if ((picked < 0) && (largest >= 0)) {
        (void) medium(opaque, largest, params);
        (void) fat_image_check(image, params);
        picked = largest;
    }
    if (picked >= 0)
        *result = FAT_IMAGE_OK;
    return picked;
}

fat_image_result_t
fat_image_write_temp(fat_image_t *image, const fat_image_params_t *params, const char *prefix, FILE **out)
{
    fat_image_result_t result;
    FILE              *fp = media_temp_file(prefix);

    if (!fp || !media_temp_file_set_size(fp, params->sectors * params->sector_size))
        result = FAT_IMAGE_WRITE_ERROR;
    else
        result = fat_image_write(image, params, fat_image_write_file, fp);
    if ((result == FAT_IMAGE_OK) && fflush(fp))
        result = FAT_IMAGE_WRITE_ERROR;

    if (result != FAT_IMAGE_OK) {
        if (fp)
            fclose(fp);
        fp = NULL;
    }
    *out = fp;
    return result;
}

int
fat_image_write_mem(void *opaque, uint64_t offset, const void *data, size_t size)
{
    memcpy((uint8_t *) opaque + offset, data, size);
    return 1;
}

int
fat_image_write_file(void *opaque, uint64_t offset, const void *data, size_t size)
{
    FILE *fp = (FILE *) opaque;
    return !fseeko64(fp, offset, SEEK_SET) && (fwrite(data, 1, size, fp) == size);
}

fat_image_result_t
fat_image_create_mem(const char *path, const fat_image_params_t *params, uint8_t **data, int *left_out)
{
    fat_image_t       *image;
    fat_image_result_t result = fat_image_scan(path, &image);
    if (result != FAT_IMAGE_OK)
        return result;

    result = fat_image_check(image, params);
    if (result == FAT_IMAGE_OK) {
        uint8_t *mem = (uint8_t *) calloc(params->sectors, params->sector_size);
        if (!mem) {
            result = FAT_IMAGE_NO_MEMORY;
        } else {
            result = fat_image_write(image, params, fat_image_write_mem, mem);
            if (result == FAT_IMAGE_OK) {
                *data = mem;
                if (left_out)
                    *left_out = fat_image_left_out(image);
            } else {
                free(mem);
            }
        }
    }
    fat_image_free(image);
    return result;
}
