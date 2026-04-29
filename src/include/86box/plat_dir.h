/*
 * 86Box    A hypervisor and IBM PC system emulator that specializes in
 *          running old operating systems and software designed for IBM
 *          PC systems and compatibles from 1981 through fairly recent
 *          system designs based on the PCI bus.
 *
 *          This file is part of the 86Box distribution.
 *
 *          Definitions for the platform OpenDir module.
 *
 * Authors: Fred N. van Kempen, <decwiz@yahoo.com>
 *
 *          Copyright 2017 Fred N. van Kempen.
 */
#ifndef PLAT_DIR_H
#define PLAT_DIR_H

/* Windows and Termux needs the POSIX re-implementations */
#if defined(_WIN32) || defined(__TERMUX__)
#    ifdef _MAX_FNAME
#        define MAXNAMLEN _MAX_FNAME
#    else
#        define MAXNAMLEN 15
#    endif
#    define MAXDIRLEN 127

struct dirent {
    long           d_ino;
    unsigned short d_reclen;
    unsigned short d_off;
#    ifdef UNICODE
    wchar_t d_name[MAXNAMLEN + 1];
#    else
    char d_name[MAXNAMLEN + 1];
#    endif
};
#    define d_namlen d_reclen

typedef struct DIR_t {
    short flags;  /* internal flags */
    short offset; /* offset of entry into dir */
    long  handle; /* open handle to Win32 system */
    short sts;    /* last known status code */
    char *dta;    /* internal work data */
#    ifdef UNICODE
    wchar_t dir[MAXDIRLEN + 1]; /* open dir */
#    else
    char dir[MAXDIRLEN + 1]; /* open dir */
#    endif
    struct dirent dent; /* actual directory entry */
} DIR;

/* Directory routine flags. */
#    define DIR_F_LOWER  0x0001 /* force to lowercase */
#    define DIR_F_SANE   0x0002 /* force this to sane path */
#    define DIR_F_ISROOT 0x0010 /* this is the root directory */

/* Function prototypes. */
extern DIR           *opendir(const char *);
extern struct dirent *readdir(DIR *);
extern long           telldir(DIR *);
extern void           seekdir(DIR *, long);
extern int            closedir(DIR *);

#    define rewinddir(dirp) seekdir(dirp, 0L)
#else
/* On linux and macOS, use the standard functions and types */
#    include <dirent.h>
#endif

#define plat_dir_is_special_entry(fn) (((fn)[0] == '.') && (((fn)[1] == '\0') || (((fn)[1] == '.') && ((fn)[2] == '\0'))))

#ifdef _WIN32
#    ifndef FILE_ATTRIBUTE_DIRECTORY
#        define WIN32_LEAN_AND_MEAN
#        include <windows.h>
#    endif

typedef struct {
    char            *path;
    size_t           path_dir_len;
    size_t           path_len;
    HANDLE           find;
    WIN32_FIND_DATAA data;
} plat_dir_t;

static inline int
plat_dir_open(plat_dir_t *context, const char *path)
{
    context->path_dir_len = strlen(path);
    context->path_len     = context->path_dir_len + MAX_PATH + 2;
    context->path         = (char *) malloc(context->path_len);
    snprintf(context->path, context->path_len, "%s\\*", path);

    /* First entry is always . so we pre-load it for the default entry behavior. */
    context->find = FindFirstFileA(context->path, &context->data);
    if (context->find == INVALID_HANDLE_VALUE) {
        free(context->path);
        return 0;
    }
    return 1;
}

static inline void
plat_dir_close(plat_dir_t *context)
{
    FindClose(context->find);
    free(context->path);
}

static inline int
plat_dir_rewind(plat_dir_t *context)
{
    FindClose(context->find);

    context->path[context->path_dir_len]     = '\\';
    context->path[context->path_dir_len + 1] = '*';
    context->path[context->path_dir_len + 2] = '\0';

    context->find = FindFirstFileExA(context->path, FindExInfoBasic, &context->data, FindExSearchNameMatch, NULL, FIND_FIRST_EX_LARGE_FETCH);
    if (context->find == INVALID_HANDLE_VALUE) {
        free(context->path);
        return 0;
    }
    return 1;
}

static inline size_t
plat_dir_count_children(plat_dir_t *context)
{
    context->path[context->path_dir_len]     = '\\';
    context->path[context->path_dir_len + 1] = '*';
    context->path[context->path_dir_len + 2] = '\0';

    WIN32_FIND_DATAA data;
    size_t ret = 0;
    HANDLE find = FindFirstFileExA(context->path, FindExInfoBasic, &data, FindExSearchNameMatch, NULL, FIND_FIRST_EX_LARGE_FETCH);
    if (find != INVALID_HANDLE_VALUE) {
        do {
            if (plat_dir_is_special_entry(data.cFileName))
                continue;
            ret++;
        } while (FindNextFileA(find, &data));
        FindClose(find);
    }
    context->path[context->path_dir_len] = '\0';
    return ret;
}

static inline int
plat_dir_read(plat_dir_t *context)
{
    context->path[context->path_dir_len] = '\0';
    while (1) {
        if (!FindNextFileA(context->find, &context->data))
            return 0;
        else if (plat_dir_is_special_entry(context->data.cFileName))
            continue;
        return 1;
    }
}

#    define plat_dir_get_name(context)      ((context)->data.cFileName)
#    define plat_dir_get_size(context)      (((uint64_t) (context)->data.nFileSizeHigh << 32) | (context)->data.nFileSizeLow)
#    define plat_dir_convert_time(ft)       (((((uint64_t) ft.dwHighDateTime << 32) | ft.dwLowDateTime) - 116444736000000000ULL) / 10000000ULL)
#    define plat_dir_get_birthtime(context) (plat_dir_convert_time((context)->data.ftCreationTime))
#    define plat_dir_get_mtime(context)     (plat_dir_convert_time((context)->data.ftLastWriteTime))
#    define plat_dir_get_atime(context)     (plat_dir_convert_time((context)->data.ftLastAccessTime))
#    define plat_dir_get_ctime(context)     (0)
#    define plat_dir_get_mode(context)      (0)
#    define plat_dir_get_nlink(context)     (0)
#    define plat_dir_get_uid(context)       (0)
#    define plat_dir_get_gid(context)       (0)
#    define plat_dir_get_dev(context)       (0)
#    define plat_dir_is_file(context)       (!((context)->data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY))
#    define plat_dir_is_dir(context)        (!!((context)->data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY))
#    define plat_dir_is_symlink(context)    (((context)->data.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) && ((context)->data.dwReserved0 == IO_REPARSE_TAG_SYMLINK))
#    define plat_dir_is_char(context)       (0)
#    define plat_dir_is_block(context)      (0)
#    define plat_dir_is_pipe(context)       (0)
#    define plat_dir_is_socket(context)     (0)
#    define plat_dir_is_hidden(context)     (!!((context)->data.dwFileAttributes & FILE_ATTRIBUTE_HIDDEN))
#    define plat_dir_is_system(context)     (!!((context)->data.dwFileAttributes & FILE_ATTRIBUTE_SYSTEM))
#elif defined(__APPLE__) && defined(__MAC_OS_X_VERSION_MIN_REQUIRED) && (__MAC_OS_X_VERSION_MIN_REQUIRED >= 101000)
#    include <sys/attr.h>
#    include <sys/vnode.h>
#    include <unistd.h>
#    include <fcntl.h>
#    include <limits.h>

typedef struct {
    char           *path;
    size_t          path_dir_len;
    size_t          path_len;
    int             find;
    struct attrlist attr_list;
    uint8_t        *attr_buf;
    uint8_t        *attr_ptr;
    size_t          attr_len;
    int             attr_remain;

    struct {
        attribute_set_t *returned;
        fsobj_type_t    *objtype;
        const char      *name;
        struct timespec *crtime;
        struct timespec *mtime;
        struct timespec *ctime;
        struct timespec *atime;
        uid_t           *uid;
        gid_t           *gid;
        uint32_t        *mode;
        uint32_t        *entrycount;
        uint32_t        *linkcount;
        uint32_t        *devtype;
        off_t           *datalength;
    } data;
    uint32_t dir_entrycount;
} plat_dir_t;

static inline uint32_t
plat_dir_fill_attributes(plat_dir_t *context, uint8_t *buf)
{
    /* Clear existing data. */
    memset(&context->data, 0, sizeof(context->data));

    /* Return size for this attribute buffer. */
    uint32_t ret = AS_U32(buf[0]);
    buf += sizeof(uint32_t);

    /* Set mandatory attributes. */
    context->data.returned = (attribute_set_t *) buf;
    buf += sizeof(attribute_set_t);
    attrgroup_t attrs = context->data.returned->commonattr;
    uint32_t error = 0;
    if (attrs & ATTR_CMN_ERROR) {
        error = AS_U32(buf[0]);
        buf += sizeof(uint32_t);
    }
    if (LIKELY(attrs & ATTR_CMN_NAME)) {
        context->data.name = (char *) &buf[((attrreference_t *) buf)->attr_dataoffset];
        buf += sizeof(attrreference_t);
    }

    /* Stop if an error was returned in the mandatory attributes. */
    if (UNLIKELY(error))
        return ret;

    /* Set the rest of the attributes. */
    if (LIKELY(attrs & ATTR_CMN_OBJTYPE)) {
        context->data.objtype = (fsobj_type_t *) buf;
        buf += sizeof(fsobj_type_t);
    }
    if (LIKELY(attrs & ATTR_CMN_CRTIME)) {
        context->data.crtime = (struct timespec *) buf;
        buf += sizeof(struct timespec);
    }
    if (LIKELY(attrs & ATTR_CMN_MODTIME)) {
        context->data.mtime = (struct timespec *) buf;
        buf += sizeof(struct timespec);
    }
    if (LIKELY(attrs & ATTR_CMN_CHGTIME)) {
        context->data.ctime = (struct timespec *) buf;
        buf += sizeof(struct timespec);
    }
    if (LIKELY(attrs & ATTR_CMN_ACCTIME)) {
        context->data.atime = (struct timespec *) buf;
        buf += sizeof(struct timespec);
    }
    if (LIKELY(attrs & ATTR_CMN_OWNERID)) {
        context->data.uid = (uid_t *) buf;
        buf += sizeof(uid_t);
    }
    if (LIKELY(attrs & ATTR_CMN_GRPID)) {
        context->data.gid = (gid_t *) buf;
        buf += sizeof(gid_t);
    }
    if (LIKELY(attrs & ATTR_CMN_ACCESSMASK)) {
        context->data.mode = (uint32_t *) buf;
        buf += sizeof(uint32_t);
    }
    attrs = context->data.returned->dirattr;
    if (attrs & ATTR_DIR_ENTRYCOUNT) {
        context->data.entrycount = (uint32_t *) buf;
        buf += sizeof(uint32_t);
    }
    attrs = context->data.returned->fileattr;
    if (attrs & ATTR_FILE_LINKCOUNT) {
        context->data.linkcount = (uint32_t *) buf;
        buf += sizeof(uint32_t);
    }
    if (attrs & ATTR_FILE_DEVTYPE) {
        context->data.devtype = (uint32_t *) buf;
        buf += sizeof(uint32_t);
    }
    if (attrs & ATTR_FILE_DATALENGTH) {
        context->data.datalength = (off_t *) buf;
        buf += sizeof(off_t);
    }

    return ret;
}

static inline int
plat_dir_open(plat_dir_t *context, const char *path)
{
    /* Open directory for reading. */
    context->find = open(path, O_RDONLY, 0);
    if (context->find == -1)
        return 0;

    /* Initialize path buffer. */
    context->path_dir_len = strlen(path);
    context->path_len     = context->path_dir_len + PATH_MAX + 2;
    context->path         = (char *) malloc(context->path_len);
    strcpy(context->path, path);

    /* Initialize attribute buffer. */
    context->attr_len    = 131072;
    context->attr_buf    = (uint8_t *) malloc(context->attr_len);
    context->attr_ptr    = NULL;
    context->attr_remain = 0;

    /* Initialize attribute list with everything we should know about. */
    memset(&context->attr_list, 0, sizeof(context->attr_list));
    context->attr_list.bitmapcount = ATTR_BIT_MAP_COUNT;
    context->attr_list.commonattr  = ATTR_CMN_RETURNED_ATTRS | ATTR_CMN_NAME | ATTR_CMN_ERROR |
                                     ATTR_CMN_OBJTYPE |
                                     ATTR_CMN_CRTIME | ATTR_CMN_MODTIME | ATTR_CMN_CHGTIME | ATTR_CMN_ACCTIME |
                                     ATTR_CMN_OWNERID | ATTR_CMN_GRPID | ATTR_CMN_ACCESSMASK;
    context->attr_list.dirattr     = ATTR_DIR_ENTRYCOUNT;
    context->attr_list.fileattr    = ATTR_FILE_LINKCOUNT | ATTR_FILE_DEVTYPE | ATTR_FILE_DATALENGTH;

    /* Get attributes for the base directory separately, as . and .. are not included in getattrlistbulk. */
    if (fgetattrlist(context->find, &context->attr_list, context->attr_buf, context->attr_len, 0)) {
        free(context->path);
        free(context->attr_buf);
        return 0;
    }
    plat_dir_fill_attributes(context, context->attr_buf);
    context->data.name = ".";

    /* Save the base directory's child count. */
    context->dir_entrycount = *context->data.entrycount;

    /* We don't need the entry count for child directories. */
    context->attr_list.dirattr &= ~ATTR_DIR_ENTRYCOUNT;

    return 1;
}

static inline void
plat_dir_close(plat_dir_t *context)
{
    close(context->find);
    free(context->path);
    free(context->attr_buf);
}

static inline int
plat_dir_rewind(plat_dir_t *context)
{
    return lseek(context->find, 0, SEEK_SET) == 0;
}

static inline size_t
plat_dir_count_children(plat_dir_t *context)
{
    return context->dir_entrycount;
}

static inline int
plat_dir_read(plat_dir_t *context)
{
    /* Fetch next attribute batch if required. */
    if (context->attr_remain <= 0) {
        context->attr_remain = getattrlistbulk(context->find, &context->attr_list, context->attr_buf, context->attr_len, 0);
        if (context->attr_remain <= 0)
            return 0;
        context->attr_ptr = context->attr_buf;
    }

    context->path[context->path_dir_len] = '\0';

    /* Fill attributes for this entry and move on to the next one. */
    context->attr_ptr += plat_dir_fill_attributes(context, context->attr_ptr);
    context->attr_remain--;
    return 1;
}

#    define plat_dir_get_name(context)      ((context)->data.name)
#    define plat_dir_get_size(context)      (LIKELY((context)->data.datalength != NULL) ? *(context)->data.datalength : 0)
#    define plat_dir_get_birthtime(context) (LIKELY((context)->data.crtime != NULL) ? (context)->data.crtime->tv_sec : 0)
#    define plat_dir_get_mtime(context)     (LIKELY((context)->data.mtime != NULL) ? (context)->data.mtime->tv_sec : 0)
#    define plat_dir_get_atime(context)     (LIKELY((context)->data.atime != NULL) ? (context)->data.atime->tv_sec : 0)
#    define plat_dir_get_ctime(context)     (LIKELY((context)->data.ctime != NULL) ? (context)->data.ctime->tv_sec : 0)
#    define plat_dir_get_mode(context)      (LIKELY((context)->data.mode != NULL) ? *(context)->data.mode : 0)
#    define plat_dir_get_nlink(context)     (LIKELY((context)->data.linkcount != NULL) ? *(context)->data.linkcount : 0)
#    define plat_dir_get_uid(context)       (LIKELY((context)->data.uid != NULL) ? *(context)->data.uid : 0)
#    define plat_dir_get_gid(context)       (LIKELY((context)->data.gid != NULL) ? *(context)->data.gid : 0)
#    define plat_dir_get_dev(context)       (LIKELY((context)->data.devtype != NULL) ? *(context)->data.devtype : 0)
#    define plat_dir_is_file(context)       (LIKELY((context)->data.objtype != NULL) ? (*(context)->data.objtype == VREG) : 0)
#    define plat_dir_is_dir(context)        (LIKELY((context)->data.objtype != NULL) ? (*(context)->data.objtype == VDIR) : 0)
#    define plat_dir_is_symlink(context)    (LIKELY((context)->data.objtype != NULL) ? (*(context)->data.objtype == VLNK) : 0)
#    define plat_dir_is_char(context)       (LIKELY((context)->data.objtype != NULL) ? (*(context)->data.objtype == VCHR) : 0)
#    define plat_dir_is_block(context)      (LIKELY((context)->data.objtype != NULL) ? (*(context)->data.objtype == VBLK) : 0)
#    define plat_dir_is_pipe(context)       (LIKELY((context)->data.objtype != NULL) ? (*(context)->data.objtype == VFIFO) : 0)
#    define plat_dir_is_socket(context)     (LIKELY((context)->data.objtype != NULL) ? (*(context)->data.objtype == VSOCK) : 0)
#    define plat_dir_is_hidden(context)     (plat_dir_get_name((context))[0] == '.')
#    define plat_dir_is_system(context)     (plat_dir_is_char((context)) || plat_dir_is_block((context)) || plat_dir_is_socket((context)))
#else
#    ifndef _LARGEFILE_SOURCE
#        define _LARGEFILE_SOURCE
#    endif
#    ifndef _LARGEFILE64_SOURCE
#        define _LARGEFILE64_SOURCE
#    endif
#    include <86box/plat_dir.h>
#    include <sys/stat.h>
#    include <limits.h>

typedef struct {
    char          *path;
    size_t         path_dir_len;
    size_t         path_len;
    DIR           *find;
    struct dirent *data;
    struct stat    stats;
    uint8_t        stats_valid;
} plat_dir_t;

static inline int
plat_dir_open(plat_dir_t *context, const char *path)
{
    context->path_dir_len = strlen(path);
    context->path_len     = context->path_dir_len + PATH_MAX + 2;
    context->path         = (char *) malloc(context->path_len);
    strcpy(context->path, path);

    context->find = opendir(path);
    if (!context->find) {
        free(context->path);
        return 0;
    }

    /* First entry is always . so we pre-load it for the default entry behavior. */
    context->stats_valid = 0xff;
    context->data        = readdir(context->find);
    if (!context->data) {
        closedir(context->find);
        free(context->path);
        return 0;
    }
    return 1;
}

static inline void
plat_dir_close(plat_dir_t *context)
{
    closedir(context->find);
    free(context->path);
}

static inline int
plat_dir_rewind(plat_dir_t *context)
{
    rewinddir(context->find);
    return 1;
}

static inline size_t
plat_dir_count_children(plat_dir_t *context)
{
    rewinddir(context->find);
    size_t ret = 0;
    struct dirent *data;
    while ((data = readdir(context->find))) {
        if (plat_dir_is_special_entry(data->d_name))
            continue;
        ret++;
    }
    rewinddir(context->find);
    return ret;
}

static inline int
plat_dir_read(plat_dir_t *context)
{
    context->path[context->path_dir_len] = '\0';
    context->stats_valid                 = 0xff;
    struct dirent *data;
    while ((data = readdir(context->find))) {
        if (plat_dir_is_special_entry(data->d_name))
            continue;
        break;
    }
    context->data = data;
    return !!data;
}

#    define plat_dir_get_name(context) ((context)->data->d_name)
#    define plat_dir_get_size(context) (plat_dir_stat((context))->st_size)
#    ifdef st_birthtime
#        define plat_dir_get_birthtime(context) (plat_dir_stat((context))->st_birthtime)
#    else
#        define plat_dir_get_birthtime(context) (0)
#    endif
#    define plat_dir_get_mtime(context) (plat_dir_stat((context))->st_mtime)
#    define plat_dir_get_atime(context) (plat_dir_stat((context))->st_atime)
#    define plat_dir_get_ctime(context) (plat_dir_stat((context))->st_ctime)
#    define plat_dir_get_mode(context)  (plat_dir_stat((context))->st_mode)
#    define plat_dir_get_nlink(context) (plat_dir_stat((context))->st_nlink)
#    define plat_dir_get_uid(context)   (plat_dir_stat((context))->st_uid)
#    define plat_dir_get_gid(context)   (plat_dir_stat((context))->st_gid)
#    define plat_dir_get_dev(context)   (plat_dir_stat((context))->st_rdev)

#    define plat_dir_is_file_stat(context)    (S_ISREG(plat_dir_stat((context))->st_mode))
#    define plat_dir_is_dir_stat(context)     (S_ISDIR(plat_dir_stat((context))->st_mode))
#    define plat_dir_is_symlink_stat(context) (S_ISLNK(plat_dir_stat((context))->st_mode))
#    define plat_dir_is_char_stat(context)    (S_ISCHR(plat_dir_stat((context))->st_mode))
#    define plat_dir_is_block_stat(context)   (S_ISBLK(plat_dir_stat((context))->st_mode))
#    define plat_dir_is_pipe_stat(context)    (S_ISFIFO(plat_dir_stat((context))->st_mode))
#    define plat_dir_is_socket_stat(context)  (S_ISSOCK(plat_dir_stat((context))->st_mode))

#    if defined(DT_UNKNOWN) && defined(DT_REG) && defined(DT_DIR)
#        define plat_dir_is_file(context)    (((context)->data->d_type == DT_UNKNOWN) ? plat_dir_is_file_stat((context)) : ((context)->data->d_type == DT_REG))
#        define plat_dir_is_dir(context)     (((context)->data->d_type == DT_UNKNOWN) ? plat_dir_is_dir_stat((context)) : ((context)->data->d_type == DT_DIR))
#        define plat_dir_is_symlink(context) (((context)->data->d_type == DT_UNKNOWN) ? plat_dir_is_symlink_stat((context)) : ((context)->data->d_type == DT_LNK))
#        define plat_dir_is_char(context)    (((context)->data->d_type == DT_UNKNOWN) ? plat_dir_is_char_stat((context)) : ((context)->data->d_type == DT_CHR))
#        define plat_dir_is_block(context)   (((context)->data->d_type == DT_UNKNOWN) ? plat_dir_is_block_stat((context)) : ((context)->data->d_type == DT_BLK))
#        define plat_dir_is_pipe(context)    (((context)->data->d_type == DT_UNKNOWN) ? plat_dir_is_pipe_stat((context)) : ((context)->data->d_type == DT_FIFO))
#        define plat_dir_is_socket(context)  (((context)->data->d_type == DT_UNKNOWN) ? plat_dir_is_socket_stat((context)) : ((context)->data->d_type == DT_SOCK))
#    else
#        define plat_dir_is_file    plat_dir_is_file_stat
#        define plat_dir_is_dir     plat_dir_is_dir_stat
#        define plat_dir_is_symlink plat_dir_is_symlink_stat
#        define plat_dir_is_char    plat_dir_is_char_stat
#        define plat_dir_is_block   plat_dir_is_block_stat
#        define plat_dir_is_pipe    plat_dir_is_pipe_stat
#        define plat_dir_is_socket  plat_dir_is_socket_stat
#    endif

#    define plat_dir_is_hidden(context) (plat_dir_get_name((context))[0] == '.')
#    define plat_dir_is_system(context) (plat_dir_is_char((context)) || plat_dir_is_block((context)) || plat_dir_is_socket((context)))
#endif

static inline const char *
plat_dir_get_path(plat_dir_t *context)
{
    if (context->path[context->path_dir_len])
        return context->path;
    size_t len = context->path_dir_len + strlen(plat_dir_get_name(context)) + 2;
    if (len > context->path_len) {
        free(context->path);
        context->path     = (char *) malloc(len);
        context->path_len = len;
    }
    snprintf(&context->path[context->path_dir_len], context->path_len - context->path_dir_len,
#ifdef _WIN32
        "\\"
#else
        "/"
#endif
        "%s", plat_dir_get_name(context));
    return context->path;
}

#ifdef plat_dir_is_file_stat
static inline struct stat *
plat_dir_stat(plat_dir_t *context)
{
    if (context->stats_valid > 1) {
        context->stats_valid = !stat(plat_dir_get_path(context), &context->stats);
        if (!context->stats_valid)
            memset(&context->stats, 0, sizeof(context->stats));
    }
    return &context->stats;
}
#endif

#endif /*PLAT_DIR_H*/
