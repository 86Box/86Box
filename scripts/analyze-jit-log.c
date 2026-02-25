// Build: cc -O2 -o scripts/analyze-jit-log scripts/analyze-jit-log.c -lpthread
#undef _DARWIN_C_SOURCE
#define _DARWIN_C_SOURCE
#define _POSIX_C_SOURCE 200809L

#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <inttypes.h>
#include <pthread.h>
#include <stdarg.h>
#include <stdatomic.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

#define GREEN "\033[0;32m"
#define RED "\033[0;31m"
#define YELLOW "\033[1;33m"
#define CYAN "\033[0;36m"
#define BOLD "\033[1m"
#define NC "\033[0m"

#define PIXEL_BITSET_BYTES 8192u
#define MAX_STORED_ERROR_LINES 20u

typedef struct {
    uint32_t *data;
    size_t len;
    size_t cap;
} U32VecSet;

typedef struct {
    uint64_t *data;
    size_t len;
    size_t cap;
} U64VecSet;

typedef struct {
    int *data;
    size_t len;
    size_t cap;
} IntVecSet;

typedef struct {
    char **data;
    size_t len;
    size_t cap;
} StrVecSet;

typedef struct {
    uint32_t fbz_mode;
    uint32_t fbz_color_path;
    uint32_t alpha_mode;
    uint32_t texture_mode;
    uint32_t fog_mode;
    int xdir;
} PipelineConfig;

typedef struct {
    PipelineConfig *data;
    size_t len;
    size_t cap;
} ConfigVecSet;

typedef struct {
    char fbz_mode[32];
    char fbz_color_path[32];
    char alpha_mode[32];
    char texture_mode[32];
    char fog_mode[32];
    int xdir;
} RawPipelineConfig;

typedef struct {
    RawPipelineConfig *data;
    size_t len;
    size_t cap;
} RawConfigVecSet;

typedef struct {
    uint32_t *keys;
    uint8_t *used;
    size_t cap;
    size_t len;
} U32HashSet;

typedef struct {
    uint64_t line_no;
    char text[121];
} ErrorLine;

#define MAX_FOGMODE_COUNTERS 64
#define MAX_MISMATCH_CONFIGS 256

typedef struct {
    uint32_t fog_mode;
    uint64_t count;
    uint64_t pixels_differ;
} FogModeCounter;

typedef struct {
    uint32_t fbz_mode;
    uint32_t fbz_color_path;
    uint32_t alpha_mode;
    uint32_t texture_mode;
    uint32_t fog_mode;
    uint64_t count;
    uint64_t pixels_differ;
} MismatchConfigCounter;

typedef struct {
    uint64_t total_lines;
    uint64_t generate_count;
    uint64_t cache_hits;
    uint64_t execute_count;
    uint64_t post_count;
    uint64_t pixel_lines;
    uint64_t interleaved_lines;

    uint64_t interp_fallbacks;
    uint64_t reject_fallbacks;
    uint64_t reject_wx_write;
    uint64_t reject_wx_exec;
    uint64_t reject_emit_overflow;
    uint64_t warn_count;
    uint64_t verify_mismatch_count;
    uint64_t verify_pixels_differ;

    /* Per-fogMode mismatch breakdown */
    FogModeCounter vm_fog[MAX_FOGMODE_COUNTERS];
    size_t vm_fog_len;

    /* Per-pipeline-config mismatch breakdown */
    MismatchConfigCounter vm_configs[MAX_MISMATCH_CONFIGS];
    size_t vm_configs_len;

    /* Pixel diff magnitude stats (from "  pixel[" lines) */
    uint64_t diff_mag_0_1;
    uint64_t diff_mag_2_3;
    uint64_t diff_mag_4_6;
    uint64_t diff_mag_7_plus;
    int max_abs_dr;
    int max_abs_dg;
    int max_abs_db;
    uint64_t pixel_diffs_parsed;

    bool has_init;
    uint64_t init_line_no;
    int init_render_threads;
    int init_use_recompiler;
    int init_jit_debug;

    U64VecSet code_addrs;
    IntVecSet code_sizes;
    IntVecSet odd_even_values;
    uint64_t odd_even_zero_count;
    uint64_t odd_even_one_count;
    uint64_t xdir_pos_count;
    uint64_t xdir_neg_count;

    bool has_recomp_range;
    uint64_t recomp_min;
    uint64_t recomp_max;

    U32VecSet fbz_modes;
    U32VecSet fbz_color_paths;
    U32VecSet alpha_modes;
    U32VecSet texture_modes;
    U32VecSet fog_modes;
    ConfigVecSet configs;
    StrVecSet fbz_modes_raw;
    StrVecSet fbz_color_paths_raw;
    StrVecSet alpha_modes_raw;
    StrVecSet texture_modes_raw;
    StrVecSet fog_modes_raw;
    RawConfigVecSet configs_raw;

    uint64_t pixel_count_total;
    uint64_t pixel_count_max;
    uint64_t pixel_hist_1;
    uint64_t pixel_hist_2_10;
    uint64_t pixel_hist_11_100;
    uint64_t pixel_hist_101_320;
    uint64_t pixel_hist_321_plus;
    uint64_t negative_ir;
    uint64_t negative_ig;
    uint64_t negative_ib;
    uint64_t negative_ia;
    U32HashSet z_values;

    uint8_t unique_pixels[PIXEL_BITSET_BYTES];
    U32HashSet unique_scanlines;  /* real_y values from EXECUTE lines */

    uint64_t error_count;
    ErrorLine error_lines[MAX_STORED_ERROR_LINES];
    size_t error_lines_stored;
} Stats;

typedef struct {
    const char *data;
    size_t start;
    size_t end;
    atomic_uint_fast64_t *progress_lines;
    atomic_int *done_threads;
    Stats stats;
} WorkerCtx;

typedef struct {
    int odd_even;
    int code_size;
    uint64_t code;
    uint64_t recomp;
    uint32_t fbz_mode;
    uint32_t fbz_color_path;
    uint32_t alpha_mode;
    uint32_t texture_mode;
    uint32_t fog_mode;
    int xdir;
    char fbz_mode_raw[32];
    char fbz_color_path_raw[32];
    char alpha_mode_raw[32];
    char texture_mode_raw[32];
    char fog_mode_raw[32];
} GenerateFields;

typedef struct {
    int render_threads;
    int use_recompiler;
    int jit_debug;
} InitFields;

typedef struct {
    int ib;
    int ig;
    int ir;
    int ia;
    uint32_t z_value;
    bool z_is_zero_literal;
    uint64_t pixel_count;
} PostFields;

static const char *ERROR_PATTERNS[] = {
    "error", "fail", "crash", "overflow", "invalid", "abort", "sigill", "sigsegv",
    "sigbus", "rejected", "skip", "fault", "trap", "mprotect", "exceeded", "truncated",
    "mismatch",
};

static void die(const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    vfprintf(stderr, fmt, ap);
    va_end(ap);
    fputc('\n', stderr);
    exit(1);
}

static void *xmalloc(size_t n) {
    void *p = malloc(n);
    if (p == NULL) {
        die("Out of memory");
    }
    return p;
}

static void *xrealloc(void *p, size_t n) {
    void *q = realloc(p, n);
    if (q == NULL) {
        die("Out of memory");
    }
    return q;
}

static void *xcalloc(size_t n, size_t m) {
    void *p = calloc(n, m);
    if (p == NULL) {
        die("Out of memory");
    }
    return p;
}

static size_t next_pow2(size_t n) {
    size_t p = 1;
    while (p < n) {
        p <<= 1u;
    }
    return p;
}

static uint64_t hash_u32(uint32_t x) {
    uint64_t h = (uint64_t)x;
    h ^= h >> 16;
    h *= 0x7feb352dU;
    h ^= h >> 15;
    h *= 0x846ca68bU;
    h ^= h >> 16;
    return h;
}

static void u32_hash_init(U32HashSet *set, size_t hint) {
    size_t cap = hint < 16 ? 16 : next_pow2(hint);
    set->cap = cap;
    set->len = 0;
    set->keys = xmalloc(cap * sizeof(uint32_t));
    set->used = xcalloc(cap, sizeof(uint8_t));
}

static void u32_hash_free(U32HashSet *set) {
    free(set->keys);
    free(set->used);
    set->keys = NULL;
    set->used = NULL;
    set->cap = 0;
    set->len = 0;
}

static void u32_hash_rehash(U32HashSet *set, size_t new_cap) {
    U32HashSet fresh;
    u32_hash_init(&fresh, new_cap);

    size_t i;
    for (i = 0; i < set->cap; ++i) {
        if (!set->used[i]) {
            continue;
        }
        uint32_t key = set->keys[i];
        size_t mask = fresh.cap - 1;
        size_t idx = (size_t)(hash_u32(key) & (uint64_t)mask);
        while (fresh.used[idx]) {
            idx = (idx + 1) & mask;
        }
        fresh.used[idx] = 1;
        fresh.keys[idx] = key;
        fresh.len++;
    }

    free(set->keys);
    free(set->used);
    *set = fresh;
}

static bool u32_hash_insert(U32HashSet *set, uint32_t key) {
    if ((set->len + 1) * 10 >= set->cap * 7) {
        u32_hash_rehash(set, set->cap << 1u);
    }

    size_t mask = set->cap - 1;
    size_t idx = (size_t)(hash_u32(key) & (uint64_t)mask);
    while (set->used[idx]) {
        if (set->keys[idx] == key) {
            return false;
        }
        idx = (idx + 1) & mask;
    }
    set->used[idx] = 1;
    set->keys[idx] = key;
    set->len++;
    return true;
}

static bool u32_vec_add(U32VecSet *set, uint32_t value) {
    size_t i;
    for (i = 0; i < set->len; ++i) {
        if (set->data[i] == value) {
            return false;
        }
    }
    if (set->len == set->cap) {
        size_t new_cap = set->cap == 0 ? 8 : set->cap * 2;
        set->data = xrealloc(set->data, new_cap * sizeof(uint32_t));
        set->cap = new_cap;
    }
    set->data[set->len++] = value;
    return true;
}

static bool u64_vec_add(U64VecSet *set, uint64_t value) {
    size_t i;
    for (i = 0; i < set->len; ++i) {
        if (set->data[i] == value) {
            return false;
        }
    }
    if (set->len == set->cap) {
        size_t new_cap = set->cap == 0 ? 8 : set->cap * 2;
        set->data = xrealloc(set->data, new_cap * sizeof(uint64_t));
        set->cap = new_cap;
    }
    set->data[set->len++] = value;
    return true;
}

static bool int_vec_add(IntVecSet *set, int value) {
    size_t i;
    for (i = 0; i < set->len; ++i) {
        if (set->data[i] == value) {
            return false;
        }
    }
    if (set->len == set->cap) {
        size_t new_cap = set->cap == 0 ? 8 : set->cap * 2;
        set->data = xrealloc(set->data, new_cap * sizeof(int));
        set->cap = new_cap;
    }
    set->data[set->len++] = value;
    return true;
}

static bool str_vec_add(StrVecSet *set, const char *value) {
    size_t i;
    for (i = 0; i < set->len; ++i) {
        if (strcmp(set->data[i], value) == 0) {
            return false;
        }
    }
    if (set->len == set->cap) {
        size_t new_cap = set->cap == 0 ? 8 : set->cap * 2;
        set->data = xrealloc(set->data, new_cap * sizeof(char *));
        set->cap = new_cap;
    }
    size_t n = strlen(value);
    char *copy = xmalloc(n + 1);
    memcpy(copy, value, n + 1);
    set->data[set->len++] = copy;
    return true;
}

static void str_vec_free(StrVecSet *set) {
    size_t i;
    for (i = 0; i < set->len; ++i) {
        free(set->data[i]);
    }
    free(set->data);
    set->data = NULL;
    set->len = 0;
    set->cap = 0;
}

static bool config_vec_add(ConfigVecSet *set, PipelineConfig cfg) {
    size_t i;
    for (i = 0; i < set->len; ++i) {
        PipelineConfig a = set->data[i];
        if (a.fbz_mode == cfg.fbz_mode &&
            a.fbz_color_path == cfg.fbz_color_path &&
            a.alpha_mode == cfg.alpha_mode &&
            a.texture_mode == cfg.texture_mode &&
            a.fog_mode == cfg.fog_mode &&
            a.xdir == cfg.xdir) {
            return false;
        }
    }
    if (set->len == set->cap) {
        size_t new_cap = set->cap == 0 ? 8 : set->cap * 2;
        set->data = xrealloc(set->data, new_cap * sizeof(PipelineConfig));
        set->cap = new_cap;
    }
    set->data[set->len++] = cfg;
    return true;
}

static bool raw_config_vec_add(RawConfigVecSet *set, const RawPipelineConfig *cfg) {
    size_t i;
    for (i = 0; i < set->len; ++i) {
        RawPipelineConfig *a = &set->data[i];
        if (a->xdir == cfg->xdir &&
            strcmp(a->fbz_mode, cfg->fbz_mode) == 0 &&
            strcmp(a->fbz_color_path, cfg->fbz_color_path) == 0 &&
            strcmp(a->alpha_mode, cfg->alpha_mode) == 0 &&
            strcmp(a->texture_mode, cfg->texture_mode) == 0 &&
            strcmp(a->fog_mode, cfg->fog_mode) == 0) {
            return false;
        }
    }
    if (set->len == set->cap) {
        size_t new_cap = set->cap == 0 ? 8 : set->cap * 2;
        set->data = xrealloc(set->data, new_cap * sizeof(RawPipelineConfig));
        set->cap = new_cap;
    }
    set->data[set->len++] = *cfg;
    return true;
}

static void stats_init(Stats *s, size_t z_hint) {
    memset(s, 0, sizeof(*s));
    u32_hash_init(&s->z_values, z_hint);
    u32_hash_init(&s->unique_scanlines, 1024);
}

static void stats_free(Stats *s) {
    free(s->code_addrs.data);
    free(s->code_sizes.data);
    free(s->odd_even_values.data);
    free(s->fbz_modes.data);
    free(s->fbz_color_paths.data);
    free(s->alpha_modes.data);
    free(s->texture_modes.data);
    free(s->fog_modes.data);
    free(s->configs.data);
    str_vec_free(&s->fbz_modes_raw);
    str_vec_free(&s->fbz_color_paths_raw);
    str_vec_free(&s->alpha_modes_raw);
    str_vec_free(&s->texture_modes_raw);
    str_vec_free(&s->fog_modes_raw);
    free(s->configs_raw.data);
    u32_hash_free(&s->z_values);
    u32_hash_free(&s->unique_scanlines);
    memset(s, 0, sizeof(*s));
}

static int is_hex_char(char c) {
    return (c >= '0' && c <= '9') ||
           (c >= 'a' && c <= 'f') ||
           (c >= 'A' && c <= 'F');
}

static uint32_t hex_value(char c) {
    if (c >= '0' && c <= '9') {
        return (uint32_t)(c - '0');
    }
    if (c >= 'a' && c <= 'f') {
        return (uint32_t)(10 + (c - 'a'));
    }
    return (uint32_t)(10 + (c - 'A'));
}

static const char *find_substr(const char *s, size_t len, const char *needle) {
    size_t nlen = strlen(needle);
    if (nlen == 0) {
        return s;
    }
    if (len < nlen) {
        return NULL;
    }

    char first = needle[0];
    const char *p = s;
    size_t remain = len;

    while (remain >= nlen) {
        const void *m = memchr(p, (unsigned char)first, remain - nlen + 1);
        if (m == NULL) {
            return NULL;
        }
        const char *cand = (const char *)m;
        if (memcmp(cand, needle, nlen) == 0) {
            return cand;
        }
        size_t consumed = (size_t)(cand - p) + 1;
        p += consumed;
        remain -= consumed;
    }

    return NULL;
}

static bool contains_substr(const char *s, size_t len, const char *needle) {
    return find_substr(s, len, needle) != NULL;
}


static int mem_equal_ci(const char *a, const char *b, size_t n) {
    size_t i;
    for (i = 0; i < n; ++i) {
        unsigned char ca = (unsigned char)a[i];
        unsigned char cb = (unsigned char)b[i];
        if ((unsigned char)tolower(ca) != cb) {
            return 0;
        }
    }
    return 1;
}

static int contains_ci(const char *s, size_t len, const char *needle_lower) {
    size_t nlen = strlen(needle_lower);
    if (nlen == 0 || len < nlen) {
        return 0;
    }
    char first = needle_lower[0];
    size_t i;
    for (i = 0; i + nlen <= len; ++i) {
        if ((char)tolower((unsigned char)s[i]) != first) {
            continue;
        }
        if (mem_equal_ci(s + i, needle_lower, nlen)) {
            return 1;
        }
    }
    return 0;
}

static void trim_bounds(const char *line, size_t len, size_t *out_start, size_t *out_len) {
    size_t a = 0;
    size_t b = len;
    while (a < b && isspace((unsigned char)line[a])) {
        a++;
    }
    while (b > a && isspace((unsigned char)line[b - 1])) {
        b--;
    }
    *out_start = a;
    *out_len = b - a;
}

static void skip_spaces(const char **p, const char *end) {
    while (*p < end && isspace((unsigned char)**p)) {
        (*p)++;
    }
}

static int consume_literal(const char **p, const char *end, const char *lit) {
    size_t n = strlen(lit);
    if ((size_t)(end - *p) < n) {
        return 0;
    }
    if (memcmp(*p, lit, n) != 0) {
        return 0;
    }
    *p += n;
    return 1;
}

static int parse_u64(const char **p, const char *end, uint64_t *out) {
    if (*p >= end || !isdigit((unsigned char)**p)) {
        return 0;
    }
    uint64_t v = 0;
    while (*p < end && isdigit((unsigned char)**p)) {
        v = v * 10u + (uint64_t)(**p - '0');
        (*p)++;
    }
    *out = v;
    return 1;
}

static int parse_i64(const char **p, const char *end, int64_t *out) {
    int sign = 1;
    if (*p < end && **p == '-') {
        sign = -1;
        (*p)++;
    }
    if (*p >= end || !isdigit((unsigned char)**p)) {
        return 0;
    }
    int64_t v = 0;
    while (*p < end && isdigit((unsigned char)**p)) {
        v = v * 10 + (int64_t)(**p - '0');
        (*p)++;
    }
    *out = sign < 0 ? -v : v;
    return 1;
}

static int parse_0x_hex_u64(const char **p, const char *end, uint64_t *out) {
    if (!consume_literal(p, end, "0x")) {
        return 0;
    }
    if (*p >= end || !is_hex_char(**p)) {
        return 0;
    }
    uint64_t v = 0;
    while (*p < end && is_hex_char(**p)) {
        v = (v << 4u) | (uint64_t)hex_value(**p);
        (*p)++;
    }
    *out = v;
    return 1;
}

/* Parse "key=0xHEX" from a line, returning the hex value. Returns 0 on failure. */
static uint32_t find_hex_field(const char *line, size_t len, const char *key) {
    const char *p = find_substr(line, len, key);
    if (!p) return 0;
    p += strlen(key);
    const char *end = line + len;
    uint64_t v = 0;
    if (!parse_0x_hex_u64(&p, end, &v)) return 0;
    return (uint32_t)v;
}

/* Parse "dR=+/-N" style signed int from a line. Returns 0 on failure. */
static int find_signed_field(const char *line, size_t len, const char *key, int *out) {
    const char *p = find_substr(line, len, key);
    if (!p) return 0;
    p += strlen(key);
    const char *end = line + len;
    int64_t v = 0;
    /* Handle explicit '+' sign */
    if (p < end && *p == '+') p++;
    if (!parse_i64(&p, end, &v)) return 0;
    *out = (int)v;
    return 1;
}

/* Increment per-fogMode mismatch counter */
static void vm_fog_increment(Stats *s, uint32_t fog_mode, uint64_t pixels_differ) {
    size_t i;
    for (i = 0; i < s->vm_fog_len; i++) {
        if (s->vm_fog[i].fog_mode == fog_mode) {
            s->vm_fog[i].count++;
            s->vm_fog[i].pixels_differ += pixels_differ;
            return;
        }
    }
    if (s->vm_fog_len < MAX_FOGMODE_COUNTERS) {
        s->vm_fog[s->vm_fog_len].fog_mode = fog_mode;
        s->vm_fog[s->vm_fog_len].count = 1;
        s->vm_fog[s->vm_fog_len].pixels_differ = pixels_differ;
        s->vm_fog_len++;
    }
}

/* Increment per-config mismatch counter */
static void vm_config_increment(Stats *s, uint32_t fbz, uint32_t fcp, uint32_t alpha,
                                uint32_t tex, uint32_t fog, uint64_t pixels_differ) {
    size_t i;
    for (i = 0; i < s->vm_configs_len; i++) {
        MismatchConfigCounter *c = &s->vm_configs[i];
        if (c->fbz_mode == fbz && c->fbz_color_path == fcp && c->alpha_mode == alpha &&
            c->texture_mode == tex && c->fog_mode == fog) {
            c->count++;
            c->pixels_differ += pixels_differ;
            return;
        }
    }
    if (s->vm_configs_len < MAX_MISMATCH_CONFIGS) {
        MismatchConfigCounter *c = &s->vm_configs[s->vm_configs_len];
        c->fbz_mode = fbz;
        c->fbz_color_path = fcp;
        c->alpha_mode = alpha;
        c->texture_mode = tex;
        c->fog_mode = fog;
        c->count = 1;
        c->pixels_differ = pixels_differ;
        s->vm_configs_len++;
    }
}

static void copy_span(char *dst, size_t dst_cap, const char *start, const char *end) {
    if (dst_cap == 0) {
        return;
    }
    size_t n = (size_t)(end - start);
    if (n >= dst_cap) {
        n = dst_cap - 1;
    }
    if (n > 0) {
        memcpy(dst, start, n);
    }
    dst[n] = '\0';
}

static int parse_init_line(const char *p, const char *end, InitFields *out) {
    skip_spaces(&p, end);
    if (!consume_literal(&p, end, "render_threads=")) {
        return 0;
    }
    uint64_t render_threads = 0;
    if (!parse_u64(&p, end, &render_threads)) {
        return 0;
    }

    skip_spaces(&p, end);
    if (!consume_literal(&p, end, "use_recompiler=")) {
        return 0;
    }
    uint64_t use_recompiler = 0;
    if (!parse_u64(&p, end, &use_recompiler)) {
        return 0;
    }

    skip_spaces(&p, end);
    if (!consume_literal(&p, end, "jit_debug=")) {
        return 0;
    }
    uint64_t jit_debug = 0;
    if (!parse_u64(&p, end, &jit_debug)) {
        return 0;
    }

    out->render_threads = (int)render_threads;
    out->use_recompiler = (int)use_recompiler;
    out->jit_debug = (int)jit_debug;
    return 1;
}

static int parse_generate_line(const char *p, const char *end, GenerateFields *out) {
    /* p points after "GENERATE " — expect "#N ..." */
    if (p >= end || *p != '#') return 0;
    p++;  /* skip '#' */
    uint64_t ignored_generate_id = 0;
    if (!parse_u64(&p, end, &ignored_generate_id)) {
        (void)ignored_generate_id;
        return 0;
    }

    skip_spaces(&p, end);
    if (!consume_literal(&p, end, "odd_even=")) return 0;
    int64_t odd_even = 0;
    if (!parse_i64(&p, end, &odd_even)) return 0;

    skip_spaces(&p, end);
    if (!consume_literal(&p, end, "code=")) return 0;
    uint64_t code = 0;
    if (!parse_0x_hex_u64(&p, end, &code)) return 0;

    skip_spaces(&p, end);
    if (!consume_literal(&p, end, "code_size=")) return 0;
    uint64_t code_size = 0;
    if (!parse_u64(&p, end, &code_size)) return 0;

    skip_spaces(&p, end);
    if (!consume_literal(&p, end, "recomp=")) return 0;
    uint64_t recomp = 0;
    if (!parse_u64(&p, end, &recomp)) return 0;

    skip_spaces(&p, end);
    if (!consume_literal(&p, end, "fbzMode=")) return 0;
    const char *fbz_mode_start = p;
    uint64_t fbz_mode = 0;
    if (!parse_0x_hex_u64(&p, end, &fbz_mode)) return 0;
    copy_span(out->fbz_mode_raw, sizeof(out->fbz_mode_raw), fbz_mode_start, p);

    skip_spaces(&p, end);
    if (!consume_literal(&p, end, "fbzColorPath=")) return 0;
    const char *fbz_color_path_start = p;
    uint64_t fbz_color_path = 0;
    if (!parse_0x_hex_u64(&p, end, &fbz_color_path)) return 0;
    copy_span(out->fbz_color_path_raw, sizeof(out->fbz_color_path_raw), fbz_color_path_start, p);

    skip_spaces(&p, end);
    if (!consume_literal(&p, end, "alphaMode=")) return 0;
    const char *alpha_mode_start = p;
    uint64_t alpha_mode = 0;
    if (!parse_0x_hex_u64(&p, end, &alpha_mode)) return 0;
    copy_span(out->alpha_mode_raw, sizeof(out->alpha_mode_raw), alpha_mode_start, p);

    skip_spaces(&p, end);
    if (!consume_literal(&p, end, "textureMode[0]=")) return 0;
    const char *texture_mode_start = p;
    uint64_t texture_mode = 0;
    if (!parse_0x_hex_u64(&p, end, &texture_mode)) return 0;
    copy_span(out->texture_mode_raw, sizeof(out->texture_mode_raw), texture_mode_start, p);

    skip_spaces(&p, end);
    if (!consume_literal(&p, end, "fogMode=")) return 0;
    const char *fog_mode_start = p;
    uint64_t fog_mode = 0;
    if (!parse_0x_hex_u64(&p, end, &fog_mode)) return 0;
    copy_span(out->fog_mode_raw, sizeof(out->fog_mode_raw), fog_mode_start, p);

    skip_spaces(&p, end);
    if (!consume_literal(&p, end, "xdir=")) return 0;
    int64_t xdir = 0;
    if (!parse_i64(&p, end, &xdir)) return 0;

    out->odd_even = (int)odd_even;
    out->code_size = (int)code_size;
    out->code = code;
    out->recomp = recomp;
    out->fbz_mode = (uint32_t)fbz_mode;
    out->fbz_color_path = (uint32_t)fbz_color_path;
    out->alpha_mode = (uint32_t)alpha_mode;
    out->texture_mode = (uint32_t)texture_mode;
    out->fog_mode = (uint32_t)fog_mode;
    out->xdir = (int)xdir;
    return 1;
}

static int parse_execute_line(const char *p, const char *end, int32_t *out_real_y) {
    /* p points after "EXECUTE " — expect "#N ..." */
    *out_real_y = -1;
    if (p >= end || *p != '#') return 0;
    p++;  /* skip '#' */
    uint64_t execute_id = 0;
    if (!parse_u64(&p, end, &execute_id)) return 0;

    skip_spaces(&p, end);
    if (!consume_literal(&p, end, "code=")) return 0;
    uint64_t code = 0;
    if (!parse_0x_hex_u64(&p, end, &code)) return 0;

    skip_spaces(&p, end);
    if (!consume_literal(&p, end, "x=")) return 0;
    uint64_t x = 0;
    if (!parse_u64(&p, end, &x)) return 0;

    skip_spaces(&p, end);
    if (!consume_literal(&p, end, "x2=")) return 0;
    uint64_t x2 = 0;
    if (!parse_u64(&p, end, &x2)) return 0;

    skip_spaces(&p, end);
    if (consume_literal(&p, end, "real_y=")) {
        int64_t ry = 0;
        if (parse_i64(&p, end, &ry)) {
            *out_real_y = (int32_t)ry;
        }
    }

    (void)execute_id;
    (void)code;
    (void)x;
    (void)x2;
    return 1;
}

static int parse_post_line(const char *p, const char *end, PostFields *out) {
    skip_spaces(&p, end);
    if (!consume_literal(&p, end, "ib=")) return 0;
    int64_t ib = 0;
    if (!parse_i64(&p, end, &ib)) return 0;

    skip_spaces(&p, end);
    if (!consume_literal(&p, end, "ig=")) return 0;
    int64_t ig = 0;
    if (!parse_i64(&p, end, &ig)) return 0;

    skip_spaces(&p, end);
    if (!consume_literal(&p, end, "ir=")) return 0;
    int64_t ir = 0;
    if (!parse_i64(&p, end, &ir)) return 0;

    skip_spaces(&p, end);
    if (!consume_literal(&p, end, "ia=")) return 0;
    int64_t ia = 0;
    if (!parse_i64(&p, end, &ia)) return 0;

    skip_spaces(&p, end);
    if (!consume_literal(&p, end, "z=")) return 0;
    const char *z_start = p;
    if (p >= end || !is_hex_char(*p)) {
        return 0;
    }
    uint64_t z = 0;
    while (p < end && is_hex_char(*p)) {
        z = (z << 4u) | (uint64_t)hex_value(*p);
        p++;
    }
    size_t z_len = (size_t)(p - z_start);
    bool z_zero_literal = (z_len == 8 && memcmp(z_start, "00000000", 8) == 0);

    skip_spaces(&p, end);
    if (!consume_literal(&p, end, "pixel_count=")) return 0;
    uint64_t pixel_count = 0;
    if (!parse_u64(&p, end, &pixel_count)) return 0;

    out->ib = (int)ib;
    out->ig = (int)ig;
    out->ir = (int)ir;
    out->ia = (int)ia;
    out->z_value = (uint32_t)z;
    out->z_is_zero_literal = z_zero_literal;
    out->pixel_count = pixel_count;
    return 1;
}

static int parse_pixel_line(const char *p, const char *end, uint8_t unique_pixels[PIXEL_BITSET_BYTES]) {
    skip_spaces(&p, end);
    if (!consume_literal(&p, end, "y=")) return 0;
    uint64_t y = 0;
    if (!parse_u64(&p, end, &y)) return 0;

    skip_spaces(&p, end);
    if (!consume_literal(&p, end, "x=")) return 0;
    uint64_t x1 = 0;
    if (!parse_u64(&p, end, &x1)) return 0;

    if (!consume_literal(&p, end, "..")) return 0;
    uint64_t x2 = 0;
    if (!parse_u64(&p, end, &x2)) return 0;

    if (!consume_literal(&p, end, ":")) return 0;

    size_t vals_start = 0;
    size_t vals_len = 0;
    trim_bounds(p, (size_t)(end - p), &vals_start, &vals_len);
    const char *vals = p + vals_start;
    const char *vals_end = vals + vals_len;

    while (vals < vals_end) {
        while (vals < vals_end && isspace((unsigned char)*vals)) {
            vals++;
        }
        if (vals >= vals_end) {
            break;
        }
        const char *tok = vals;
        while (vals < vals_end && !isspace((unsigned char)*vals)) {
            vals++;
        }
        size_t tok_len = (size_t)(vals - tok);
        if (tok_len == 4 &&
            is_hex_char(tok[0]) && is_hex_char(tok[1]) &&
            is_hex_char(tok[2]) && is_hex_char(tok[3])) {
            uint32_t pix = (hex_value(tok[0]) << 12u) |
                           (hex_value(tok[1]) << 8u) |
                           (hex_value(tok[2]) << 4u) |
                           hex_value(tok[3]);
            unique_pixels[pix >> 3u] |= (uint8_t)(1u << (pix & 7u));
        }
    }

    (void)y;
    (void)x1;
    (void)x2;
    return 1;
}


static int line_has_error_pattern(const char *line, size_t len) {
    size_t i;
    for (i = 0; i < sizeof(ERROR_PATTERNS) / sizeof(ERROR_PATTERNS[0]); ++i) {
        if (contains_ci(line, len, ERROR_PATTERNS[i])) {
            return 1;
        }
    }
    return 0;
}

static void store_error_line(Stats *s, uint64_t line_no, const char *line, size_t len) {
    s->error_count++;
    if (s->error_lines_stored >= MAX_STORED_ERROR_LINES) {
        return;
    }

    size_t tstart = 0;
    size_t tlen = 0;
    trim_bounds(line, len, &tstart, &tlen);

    ErrorLine *dst = &s->error_lines[s->error_lines_stored++];
    dst->line_no = line_no;
    size_t copy_len = tlen > 120 ? 120 : tlen;
    if (copy_len > 0) {
        memcpy(dst->text, line + tstart, copy_len);
    }
    dst->text[copy_len] = '\0';
}

static void process_line(Stats *s, const char *line, size_t len, uint64_t line_no) {
    /*
     * Fast dispatch: find "VOODOO JIT" once, then branch on the keyword
     * that follows. Avoids 5+ cascading substring searches per line.
     *
     * Line formats:
     *   "VOODOO JIT: INIT ..."
     *   "VOODOO JIT: GENERATE #N ..."
     *   "VOODOO JIT: cache HIT ..."
     *   "VOODOO JIT: EXECUTE #N ..."
     *   "VOODOO JIT: REJECT ..."
     *   "VOODOO JIT POST: ..."
     *   "VOODOO JIT PIXELS y=..."
     *   "INTERPRETER FALLBACK ..." (no "VOODOO JIT" prefix)
     */
    #define VJ_PREFIX "VOODOO JIT"
    #define VJ_PREFIX_LEN 10

    const char *vj = NULL;
    /* Hot path: most lines start with "VOODOO JIT" */
    if (len >= VJ_PREFIX_LEN && memcmp(line, VJ_PREFIX, VJ_PREFIX_LEN) == 0) {
        vj = line;
    } else {
        /* Slow path: check if "VOODOO JIT" appears anywhere (rare) */
        vj = find_substr(line, len, VJ_PREFIX);
        if (vj == NULL) {
            /* Not a JIT line — check for INTERPRETER FALLBACK, VERIFY MISMATCH, and errors */
            if (len >= 22 && find_substr(line, len, "INTERPRETER FALLBACK")) {
                s->interp_fallbacks++;
                return;
            }
            if (len >= 15 && memcmp(line, "VERIFY MISMATCH", 15) == 0) {
                s->verify_mismatch_count++;
                /* Try to extract pixel differ count: "(%d/%d pixels differ)" */
                uint64_t this_diff = 0;
                const char *pd = find_substr(line, len, "pixels differ)");
                if (pd) {
                    /* Walk backwards to find the '/' then the '(' */
                    const char *slash = pd;
                    while (slash > line && *slash != '/') slash--;
                    if (slash > line) {
                        const char *paren = slash;
                        while (paren > line && *paren != '(') paren--;
                        if (*paren == '(') {
                            int64_t diff = 0;
                            const char *dp = paren + 1;
                            if (parse_i64(&dp, slash, &diff) && diff > 0) {
                                this_diff = (uint64_t)diff;
                                s->verify_pixels_differ += this_diff;
                            }
                        }
                    }
                }
                /* Extract register values for breakdown */
                uint32_t fm = find_hex_field(line, len, "fogMode=");
                uint32_t fbz = find_hex_field(line, len, "fbzMode=");
                uint32_t fcp = find_hex_field(line, len, "fbzColorPath=");
                uint32_t am = find_hex_field(line, len, "alphaMode=");
                uint32_t tm = find_hex_field(line, len, "textureMode=");
                vm_fog_increment(s, fm, this_diff);
                vm_config_increment(s, fbz, fcp, am, tm, fm, this_diff);
                store_error_line(s, line_no, line, len);
                return;
            }
            /* Parse pixel diff lines: "  pixel[N]: ... dR=+/-N dG=+/-N dB=+/-N" */
            if (len >= 10 && find_substr(line, len, "pixel[") && find_substr(line, len, "dR=")) {
                int dr = 0, dg = 0, db = 0;
                if (find_signed_field(line, len, "dR=", &dr) &&
                    find_signed_field(line, len, "dG=", &dg) &&
                    find_signed_field(line, len, "dB=", &db)) {
                    s->pixel_diffs_parsed++;
                    int adr = dr < 0 ? -dr : dr;
                    int adg = dg < 0 ? -dg : dg;
                    int adb = db < 0 ? -db : db;
                    if (adr > s->max_abs_dr) s->max_abs_dr = adr;
                    if (adg > s->max_abs_dg) s->max_abs_dg = adg;
                    if (adb > s->max_abs_db) s->max_abs_db = adb;
                    int maxc = adr;
                    if (adg > maxc) maxc = adg;
                    if (adb > maxc) maxc = adb;
                    if (maxc <= 1) s->diff_mag_0_1++;
                    else if (maxc <= 3) s->diff_mag_2_3++;
                    else if (maxc <= 6) s->diff_mag_4_6++;
                    else s->diff_mag_7_plus++;
                }
                return;
            }
            if (line_has_error_pattern(line, len)) {
                store_error_line(s, line_no, line, len);
            }
            return;
        }
        /* "VOODOO JIT" found but not at start — interleaved for sure if line also starts with JIT */
        s->interleaved_lines++;
    }

    /* Check for second "VOODOO JIT" occurrence (interleaved thread output) */
    if (vj == line) {
        size_t off = VJ_PREFIX_LEN;
        if (off < len) {
            const char *second = find_substr(line + off, len - off, VJ_PREFIX);
            if (second != NULL) {
                s->interleaved_lines++;
            }
        }
    }

    /* Dispatch based on what follows "VOODOO JIT" */
    const char *after_vj = vj + VJ_PREFIX_LEN;
    size_t remaining = len - (size_t)(after_vj - line);

    /* "VOODOO JIT: " (colon-space) — most common prefix */
    if (remaining >= 2 && after_vj[0] == ':' && after_vj[1] == ' ') {
        const char *keyword = after_vj + 2;
        size_t krem = remaining - 2;

        if (krem >= 9 && memcmp(keyword, "GENERATE", 8) == 0) {
            const char *gen_p = keyword + 9;  /* skip "GENERATE " */
            const char *gen_end = line + len;
            GenerateFields gen;
            if (parse_generate_line(gen_p, gen_end, &gen)) {
                s->generate_count++;
                int_vec_add(&s->odd_even_values, gen.odd_even);
                if (gen.odd_even == 0) s->odd_even_zero_count++;
                if (gen.odd_even == 1) s->odd_even_one_count++;
                int_vec_add(&s->code_sizes, gen.code_size);
                u64_vec_add(&s->code_addrs, gen.code);

                if (!s->has_recomp_range) {
                    s->has_recomp_range = true;
                    s->recomp_min = gen.recomp;
                    s->recomp_max = gen.recomp;
                } else {
                    if (gen.recomp < s->recomp_min) s->recomp_min = gen.recomp;
                    if (gen.recomp > s->recomp_max) s->recomp_max = gen.recomp;
                }

                u32_vec_add(&s->fbz_modes, gen.fbz_mode);
                u32_vec_add(&s->fbz_color_paths, gen.fbz_color_path);
                u32_vec_add(&s->alpha_modes, gen.alpha_mode);
                u32_vec_add(&s->texture_modes, gen.texture_mode);
                u32_vec_add(&s->fog_modes, gen.fog_mode);
                str_vec_add(&s->fbz_modes_raw, gen.fbz_mode_raw);
                str_vec_add(&s->fbz_color_paths_raw, gen.fbz_color_path_raw);
                str_vec_add(&s->alpha_modes_raw, gen.alpha_mode_raw);
                str_vec_add(&s->texture_modes_raw, gen.texture_mode_raw);
                str_vec_add(&s->fog_modes_raw, gen.fog_mode_raw);
                if (gen.xdir == 1) s->xdir_pos_count++;
                if (gen.xdir == -1) s->xdir_neg_count++;

                PipelineConfig cfg;
                cfg.fbz_mode = gen.fbz_mode;
                cfg.fbz_color_path = gen.fbz_color_path;
                cfg.alpha_mode = gen.alpha_mode;
                cfg.texture_mode = gen.texture_mode;
                cfg.fog_mode = gen.fog_mode;
                cfg.xdir = gen.xdir;
                config_vec_add(&s->configs, cfg);

                RawPipelineConfig raw_cfg;
                copy_span(raw_cfg.fbz_mode, sizeof(raw_cfg.fbz_mode), gen.fbz_mode_raw, gen.fbz_mode_raw + strlen(gen.fbz_mode_raw));
                copy_span(raw_cfg.fbz_color_path, sizeof(raw_cfg.fbz_color_path), gen.fbz_color_path_raw, gen.fbz_color_path_raw + strlen(gen.fbz_color_path_raw));
                copy_span(raw_cfg.alpha_mode, sizeof(raw_cfg.alpha_mode), gen.alpha_mode_raw, gen.alpha_mode_raw + strlen(gen.alpha_mode_raw));
                copy_span(raw_cfg.texture_mode, sizeof(raw_cfg.texture_mode), gen.texture_mode_raw, gen.texture_mode_raw + strlen(gen.texture_mode_raw));
                copy_span(raw_cfg.fog_mode, sizeof(raw_cfg.fog_mode), gen.fog_mode_raw, gen.fog_mode_raw + strlen(gen.fog_mode_raw));
                raw_cfg.xdir = gen.xdir;
                raw_config_vec_add(&s->configs_raw, &raw_cfg);
            }
            return;
        }

        if (krem >= 9 && memcmp(keyword, "cache HIT", 9) == 0) {
            s->cache_hits++;
            return;
        }

        if (krem >= 7 && memcmp(keyword, "EXECUTE", 7) == 0) {
            const char *exec_p = keyword + 8;  /* skip "EXECUTE " */
            const char *exec_end = line + len;
            int32_t real_y = -1;
            if (parse_execute_line(exec_p, exec_end, &real_y)) {
                s->execute_count++;
                if (real_y >= 0) {
                    u32_hash_insert(&s->unique_scanlines, (uint32_t)real_y);
                }
            }
            return;
        }

        if (krem >= 4 && memcmp(keyword, "INIT", 4) == 0) {
            if (!s->has_init) {
                const char *init_p = keyword + 4;  /* skip "INIT" */
                const char *init_end = line + len;
                InitFields init;
                if (parse_init_line(init_p, init_end, &init)) {
                    s->has_init = true;
                    s->init_line_no = line_no;
                    s->init_render_threads = init.render_threads;
                    s->init_use_recompiler = init.use_recompiler;
                    s->init_jit_debug = init.jit_debug;
                }
            }
            return;
        }

        if (krem >= 6 && memcmp(keyword, "REJECT", 6) == 0) {
            const char *rej_p = keyword + 6;  /* skip "REJECT" */
            const char *rej_end = line + len;
            size_t rej_rem = (size_t)(rej_end - rej_p);
            s->reject_fallbacks++;  /* count ALL rejects */
            if (find_substr(rej_p, rej_rem, "wx_write_enable_failed")) {
                s->reject_wx_write++;
            } else if (find_substr(rej_p, rej_rem, "emit_overflow")) {
                s->reject_emit_overflow++;
            } else if (find_substr(rej_p, rej_rem, "wx_exec_enable_failed")) {
                s->reject_wx_exec++;
            }
            return;
        }

        if (krem >= 4 && memcmp(keyword, "WARN", 4) == 0) {
            s->warn_count++;
            return;
        }

        /* Unknown "VOODOO JIT: ..." line — skip */
        return;
    }

    /* "VOODOO JIT POST:" */
    if (remaining >= 6 && memcmp(after_vj, " POST:", 6) == 0) {
        const char *post_payload = after_vj + 6;
        const char *line_end = line + len;
        PostFields post;
        if (parse_post_line(post_payload, line_end, &post)) {
            s->post_count++;
            s->pixel_count_total += post.pixel_count;
            if (post.pixel_count > s->pixel_count_max) {
                s->pixel_count_max = post.pixel_count;
            }

            if (post.pixel_count <= 1) {
                s->pixel_hist_1++;
            } else if (post.pixel_count <= 10) {
                s->pixel_hist_2_10++;
            } else if (post.pixel_count <= 100) {
                s->pixel_hist_11_100++;
            } else if (post.pixel_count <= 320) {
                s->pixel_hist_101_320++;
            } else {
                s->pixel_hist_321_plus++;
            }

            if (post.ir < 0) s->negative_ir++;
            if (post.ig < 0) s->negative_ig++;
            if (post.ib < 0) s->negative_ib++;
            if (post.ia < 0) s->negative_ia++;

            if (!post.z_is_zero_literal) {
                u32_hash_insert(&s->z_values, post.z_value);
            }
        }
        return;
    }

    /* "VOODOO JIT PIXELS y=..." */
    if (remaining >= 7 && memcmp(after_vj, " PIXELS", 7) == 0) {
        const char *pix_payload = after_vj + 7;
        const char *line_end_pix = line + len;
        if (parse_pixel_line(pix_payload, line_end_pix, s->unique_pixels)) {
            s->pixel_lines++;
        }
        return;
    }

    #undef VJ_PREFIX
    #undef VJ_PREFIX_LEN
}

static void *worker_main(void *argp) {
    WorkerCtx *ctx = (WorkerCtx *)argp;
    const char *p = ctx->data + ctx->start;
    const char *end = ctx->data + ctx->end;
    uint64_t local_line = 0;
    uint64_t batch = 0;

    while (p < end) {
        const char *nl = memchr(p, '\n', (size_t)(end - p));
        const char *line_end = nl != NULL ? nl : end;
        size_t len = (size_t)(line_end - p);

        local_line++;
        batch++;
        if (batch >= 4096) {
            atomic_fetch_add_explicit(ctx->progress_lines, batch, memory_order_relaxed);
            batch = 0;
        }
        process_line(&ctx->stats, p, len, local_line);

        p = (nl != NULL) ? (nl + 1) : end;
    }

    if (batch > 0) {
        atomic_fetch_add_explicit(ctx->progress_lines, batch, memory_order_relaxed);
    }
    ctx->stats.total_lines = local_line;
    atomic_fetch_add_explicit(ctx->done_threads, 1, memory_order_relaxed);
    return NULL;
}

static size_t find_nearest_boundary(const char *data, size_t size, size_t prev, size_t tentative) {
    if (tentative <= prev) {
        return prev;
    }
    if (tentative >= size) {
        return size;
    }
    if (data[tentative] == '\n') {
        return tentative + 1;
    }

    size_t left = tentative;
    size_t right = tentative;

    while (left > prev || right < size) {
        if (left > prev) {
            left--;
            if (data[left] == '\n') {
                return left + 1;
            }
        }
        if (right < size) {
            if (data[right] == '\n') {
                return right + 1;
            }
            right++;
        }
    }
    return prev;
}

static void format_u64_commas(uint64_t value, char *buf, size_t n) {
    char tmp[64];
    snprintf(tmp, sizeof(tmp), "%" PRIu64, value);
    size_t len = strlen(tmp);
    size_t commas = (len > 0) ? ((len - 1) / 3) : 0;
    size_t out_len = len + commas;
    if (out_len + 1 > n) {
        if (n > 0) {
            buf[0] = '\0';
        }
        return;
    }

    buf[out_len] = '\0';
    size_t i = len;
    size_t j = out_len;
    int group = 0;
    while (i > 0) {
        buf[--j] = tmp[--i];
        group++;
        if (group == 3 && i > 0) {
            buf[--j] = ',';
            group = 0;
        }
    }
}

static void print_status(const char *color, const char *label, int spaces, const char *msg) {
    printf("%s%s%s", color, label, NC);
    int i;
    for (i = 0; i < spaces; ++i) {
        putchar(' ');
    }
    printf("%s\n", msg);
}

static void ok_msg(const char *msg) {
    print_status(GREEN, "OK", 7, msg);
}

static void warn_msg(const char *msg) {
    print_status(YELLOW, "WARN", 5, msg);
}

static void fail_msg(const char *msg) {
    print_status(RED, "FAIL", 5, msg);
}

static void info_msg(const char *msg) {
    print_status(CYAN, "INFO", 5, msg);
}

static int cmp_int_asc(const void *a, const void *b) {
    int aa = *(const int *)a;
    int bb = *(const int *)b;
    if (aa < bb) return -1;
    if (aa > bb) return 1;
    return 0;
}

static size_t bitset_count(const uint8_t *bits, size_t n) {
    size_t count = 0;
    size_t i;
    for (i = 0; i < n; ++i) {
        count += (size_t)__builtin_popcount((unsigned)bits[i]);
    }
    return count;
}

static int bitset_get(const uint8_t *bits, uint32_t v) {
    return (bits[v >> 3u] >> (v & 7u)) & 1u;
}

static void bitset_or(uint8_t *dst, const uint8_t *src, size_t n) {
    size_t i;
    for (i = 0; i < n; ++i) {
        dst[i] |= src[i];
    }
}

static size_t count_non_zero_modes(const StrVecSet *set) {
    size_t i;
    size_t count = 0;
    for (i = 0; i < set->len; ++i) {
        if (strcmp(set->data[i], "0x00000000") != 0) {
            count++;
        }
    }
    return count;
}

static void merge_stats(Stats *agg, Stats *src, uint64_t line_offset) {
    agg->total_lines += src->total_lines;
    agg->generate_count += src->generate_count;
    agg->cache_hits += src->cache_hits;
    agg->execute_count += src->execute_count;
    agg->post_count += src->post_count;
    agg->pixel_lines += src->pixel_lines;
    agg->interleaved_lines += src->interleaved_lines;

    agg->interp_fallbacks += src->interp_fallbacks;
    agg->reject_fallbacks += src->reject_fallbacks;
    agg->reject_wx_write += src->reject_wx_write;
    agg->reject_wx_exec += src->reject_wx_exec;
    agg->reject_emit_overflow += src->reject_emit_overflow;
    agg->warn_count += src->warn_count;
    agg->verify_mismatch_count += src->verify_mismatch_count;
    agg->verify_pixels_differ += src->verify_pixels_differ;

    /* Merge per-fogMode counters */
    {
        size_t mi;
        for (mi = 0; mi < src->vm_fog_len; mi++) {
            size_t j;
            int found = 0;
            for (j = 0; j < agg->vm_fog_len; j++) {
                if (agg->vm_fog[j].fog_mode == src->vm_fog[mi].fog_mode) {
                    agg->vm_fog[j].count += src->vm_fog[mi].count;
                    agg->vm_fog[j].pixels_differ += src->vm_fog[mi].pixels_differ;
                    found = 1;
                    break;
                }
            }
            if (!found && agg->vm_fog_len < MAX_FOGMODE_COUNTERS) {
                agg->vm_fog[agg->vm_fog_len++] = src->vm_fog[mi];
            }
        }
    }

    /* Merge per-config counters */
    {
        size_t mi;
        for (mi = 0; mi < src->vm_configs_len; mi++) {
            size_t j;
            int found = 0;
            MismatchConfigCounter *sc = &src->vm_configs[mi];
            for (j = 0; j < agg->vm_configs_len; j++) {
                MismatchConfigCounter *ac = &agg->vm_configs[j];
                if (ac->fbz_mode == sc->fbz_mode && ac->fbz_color_path == sc->fbz_color_path &&
                    ac->alpha_mode == sc->alpha_mode && ac->texture_mode == sc->texture_mode &&
                    ac->fog_mode == sc->fog_mode) {
                    ac->count += sc->count;
                    ac->pixels_differ += sc->pixels_differ;
                    found = 1;
                    break;
                }
            }
            if (!found && agg->vm_configs_len < MAX_MISMATCH_CONFIGS) {
                agg->vm_configs[agg->vm_configs_len++] = *sc;
            }
        }
    }

    /* Merge pixel diff magnitude stats */
    agg->diff_mag_0_1 += src->diff_mag_0_1;
    agg->diff_mag_2_3 += src->diff_mag_2_3;
    agg->diff_mag_4_6 += src->diff_mag_4_6;
    agg->diff_mag_7_plus += src->diff_mag_7_plus;
    if (src->max_abs_dr > agg->max_abs_dr) agg->max_abs_dr = src->max_abs_dr;
    if (src->max_abs_dg > agg->max_abs_dg) agg->max_abs_dg = src->max_abs_dg;
    if (src->max_abs_db > agg->max_abs_db) agg->max_abs_db = src->max_abs_db;
    agg->pixel_diffs_parsed += src->pixel_diffs_parsed;

    if (src->has_init) {
        uint64_t global_line = src->init_line_no + line_offset;
        if (!agg->has_init || global_line < agg->init_line_no) {
            agg->has_init = true;
            agg->init_line_no = global_line;
            agg->init_render_threads = src->init_render_threads;
            agg->init_use_recompiler = src->init_use_recompiler;
            agg->init_jit_debug = src->init_jit_debug;
        }
    }

    size_t i;
    for (i = 0; i < src->code_addrs.len; ++i) u64_vec_add(&agg->code_addrs, src->code_addrs.data[i]);
    for (i = 0; i < src->code_sizes.len; ++i) int_vec_add(&agg->code_sizes, src->code_sizes.data[i]);
    for (i = 0; i < src->odd_even_values.len; ++i) int_vec_add(&agg->odd_even_values, src->odd_even_values.data[i]);

    agg->odd_even_zero_count += src->odd_even_zero_count;
    agg->odd_even_one_count += src->odd_even_one_count;
    agg->xdir_pos_count += src->xdir_pos_count;
    agg->xdir_neg_count += src->xdir_neg_count;

    if (src->has_recomp_range) {
        if (!agg->has_recomp_range) {
            agg->has_recomp_range = true;
            agg->recomp_min = src->recomp_min;
            agg->recomp_max = src->recomp_max;
        } else {
            if (src->recomp_min < agg->recomp_min) agg->recomp_min = src->recomp_min;
            if (src->recomp_max > agg->recomp_max) agg->recomp_max = src->recomp_max;
        }
    }

    for (i = 0; i < src->fbz_modes.len; ++i) u32_vec_add(&agg->fbz_modes, src->fbz_modes.data[i]);
    for (i = 0; i < src->fbz_color_paths.len; ++i) u32_vec_add(&agg->fbz_color_paths, src->fbz_color_paths.data[i]);
    for (i = 0; i < src->alpha_modes.len; ++i) u32_vec_add(&agg->alpha_modes, src->alpha_modes.data[i]);
    for (i = 0; i < src->texture_modes.len; ++i) u32_vec_add(&agg->texture_modes, src->texture_modes.data[i]);
    for (i = 0; i < src->fog_modes.len; ++i) u32_vec_add(&agg->fog_modes, src->fog_modes.data[i]);
    for (i = 0; i < src->configs.len; ++i) config_vec_add(&agg->configs, src->configs.data[i]);
    for (i = 0; i < src->fbz_modes_raw.len; ++i) str_vec_add(&agg->fbz_modes_raw, src->fbz_modes_raw.data[i]);
    for (i = 0; i < src->fbz_color_paths_raw.len; ++i) str_vec_add(&agg->fbz_color_paths_raw, src->fbz_color_paths_raw.data[i]);
    for (i = 0; i < src->alpha_modes_raw.len; ++i) str_vec_add(&agg->alpha_modes_raw, src->alpha_modes_raw.data[i]);
    for (i = 0; i < src->texture_modes_raw.len; ++i) str_vec_add(&agg->texture_modes_raw, src->texture_modes_raw.data[i]);
    for (i = 0; i < src->fog_modes_raw.len; ++i) str_vec_add(&agg->fog_modes_raw, src->fog_modes_raw.data[i]);
    for (i = 0; i < src->configs_raw.len; ++i) raw_config_vec_add(&agg->configs_raw, &src->configs_raw.data[i]);

    agg->pixel_count_total += src->pixel_count_total;
    if (src->pixel_count_max > agg->pixel_count_max) {
        agg->pixel_count_max = src->pixel_count_max;
    }
    agg->pixel_hist_1 += src->pixel_hist_1;
    agg->pixel_hist_2_10 += src->pixel_hist_2_10;
    agg->pixel_hist_11_100 += src->pixel_hist_11_100;
    agg->pixel_hist_101_320 += src->pixel_hist_101_320;
    agg->pixel_hist_321_plus += src->pixel_hist_321_plus;
    agg->negative_ir += src->negative_ir;
    agg->negative_ig += src->negative_ig;
    agg->negative_ib += src->negative_ib;
    agg->negative_ia += src->negative_ia;

    for (i = 0; i < src->z_values.cap; ++i) {
        if (src->z_values.used[i]) {
            u32_hash_insert(&agg->z_values, src->z_values.keys[i]);
        }
    }

    for (i = 0; i < src->unique_scanlines.cap; ++i) {
        if (src->unique_scanlines.used[i]) {
            u32_hash_insert(&agg->unique_scanlines, src->unique_scanlines.keys[i]);
        }
    }

    bitset_or(agg->unique_pixels, src->unique_pixels, PIXEL_BITSET_BYTES);

    agg->error_count += src->error_count;
    for (i = 0; i < src->error_lines_stored; ++i) {
        if (agg->error_lines_stored >= MAX_STORED_ERROR_LINES) {
            break;
        }
        ErrorLine *dst = &agg->error_lines[agg->error_lines_stored++];
        *dst = src->error_lines[i];
        dst->line_no += line_offset;
    }
}

static void get_last_clean_line(const char *data, size_t size, char *out, size_t out_cap) {
    if (out_cap == 0) {
        return;
    }
    out[0] = '\0';
    if (size == 0) {
        return;
    }

    size_t end = size;
    if (end > 0 && data[end - 1] == '\n') {
        end--;
    }

    size_t start = end;
    while (start > 0 && data[start - 1] != '\n') {
        start--;
    }

    size_t tstart = 0;
    size_t tlen = 0;
    trim_bounds(data + start, end - start, &tstart, &tlen);
    const char *clean = data + start + tstart;
    size_t copy_len = tlen >= out_cap ? out_cap - 1 : tlen;
    if (copy_len > 0) {
        memcpy(out, clean, copy_len);
    }
    out[copy_len] = '\0';
}

static void print_report(const char *path, double file_mb, const Stats *s, const char *last_clean) {
    char nbuf1[64], nbuf2[64];

    if (s->has_init) {
        printf("%s═══ CONFIGURATION ═══%s\n", BOLD, NC);
        char msg[256];
        snprintf(msg, sizeof(msg), "Render threads: %d", s->init_render_threads);
        info_msg(msg);
        snprintf(msg, sizeof(msg), "JIT recompiler: %s", s->init_use_recompiler ? "enabled" : "disabled");
        info_msg(msg);
        snprintf(msg, sizeof(msg), "JIT debug level: %d", s->init_jit_debug);
        info_msg(msg);
        printf("\n");
    } else {
        info_msg("No INIT line found (older log format)");
        size_t inferred = s->odd_even_values.len;
        if (inferred > 0) {
            char msg[256];
            snprintf(msg, sizeof(msg), "Render threads (inferred from odd_even): %zu", inferred);
            info_msg(msg);
        }
        printf("\n");
    }

    printf("%s═══ COMPILATION ═══%s\n", BOLD, NC);
    if (s->generate_count > 0) {
        format_u64_commas(s->generate_count, nbuf1, sizeof(nbuf1));
        char msg[256];
        snprintf(msg, sizeof(msg), "Blocks compiled: %s", nbuf1);
        ok_msg(msg);
    } else {
        fail_msg("No GENERATE events found — JIT may not be active");
    }

    {
        char msg[256];
        snprintf(msg, sizeof(msg), "Cache hits: %" PRIu64, s->cache_hits);
        info_msg(msg);
        snprintf(msg, sizeof(msg), "Unique code addresses: %zu", s->code_addrs.len);
        info_msg(msg);
    }

    {
        char msg[512];
        if (s->code_sizes.len > 0) {
            int *sorted = xmalloc(s->code_sizes.len * sizeof(int));
            memcpy(sorted, s->code_sizes.data, s->code_sizes.len * sizeof(int));
            qsort(sorted, s->code_sizes.len, sizeof(int), cmp_int_asc);

            int cs_min = sorted[0];
            int cs_max = sorted[s->code_sizes.len - 1];
            uint64_t cs_sum = 0;
            size_t i;
            for (i = 0; i < s->code_sizes.len; ++i)
                cs_sum += (uint64_t)sorted[i];
            int cs_avg = (int)(cs_sum / s->code_sizes.len);

            snprintf(msg, sizeof(msg),
                     "Code size stats: %zu blocks, min=%d avg=%d max=%d bytes",
                     s->code_sizes.len, cs_min, cs_avg, cs_max);
            free(sorted);
        } else {
            snprintf(msg, sizeof(msg), "Code size stats: 0 blocks");
        }
        info_msg(msg);
    }

    {
        uint64_t even = s->odd_even_zero_count;
        uint64_t odd = s->odd_even_one_count;
        if (even > 0 && odd > 0) {
            char msg[256];
            snprintf(msg, sizeof(msg), "Even/odd distribution: %" PRIu64 " / %" PRIu64, even, odd);
            ok_msg(msg);
        } else if (s->generate_count > 0) {
            warn_msg(even > 0 ? "Only even blocks generated" : "Only odd blocks generated");
        }
    }

    {
        uint64_t xp = s->xdir_pos_count;
        uint64_t xm = s->xdir_neg_count;
        if (xp > 0 && xm > 0) {
            format_u64_commas(xp, nbuf1, sizeof(nbuf1));
            format_u64_commas(xm, nbuf2, sizeof(nbuf2));
            char msg[256];
            snprintf(msg, sizeof(msg), "xdir coverage: +1 (%s) / -1 (%s)", nbuf1, nbuf2);
            ok_msg(msg);
        } else if (xp > 0) {
            format_u64_commas(xp, nbuf1, sizeof(nbuf1));
            char msg[256];
            snprintf(msg, sizeof(msg), "xdir: only +1 (%s), no -1 (may be normal for test workload)", nbuf1);
            info_msg(msg);
        } else if (xm > 0) {
            format_u64_commas(xm, nbuf1, sizeof(nbuf1));
            char msg[256];
            snprintf(msg, sizeof(msg), "xdir: only -1 (%s), no +1", nbuf1);
            info_msg(msg);
        }
    }

    if (s->has_recomp_range) {
        char msg[256];
        snprintf(msg, sizeof(msg), "Recomp range: %" PRIu64 " .. %" PRIu64, s->recomp_min, s->recomp_max);
        info_msg(msg);
    }

    uint64_t total_fallbacks = s->interp_fallbacks + s->reject_fallbacks;
    if (total_fallbacks == 0) {
        ok_msg("No interpreter fallbacks or rejects");
    } else {
        format_u64_commas(total_fallbacks, nbuf1, sizeof(nbuf1));
        char msg[256];
        snprintf(msg, sizeof(msg), "Interpreter fallbacks + rejects: %s", nbuf1);
        fail_msg(msg);
        if (s->interp_fallbacks > 0) {
            format_u64_commas(s->interp_fallbacks, nbuf2, sizeof(nbuf2));
            printf("             INTERPRETER FALLBACK: %s\n", nbuf2);
        }
        if (s->reject_emit_overflow > 0) {
            format_u64_commas(s->reject_emit_overflow, nbuf2, sizeof(nbuf2));
            printf("             REJECT emit_overflow: %s\n", nbuf2);
        }
        if (s->reject_wx_write > 0) {
            format_u64_commas(s->reject_wx_write, nbuf2, sizeof(nbuf2));
            printf("             REJECT wx_write_enable_failed: %s\n", nbuf2);
        }
        if (s->reject_wx_exec > 0) {
            format_u64_commas(s->reject_wx_exec, nbuf2, sizeof(nbuf2));
            printf("             REJECT wx_exec_enable_failed: %s\n", nbuf2);
        }
    }

    if (s->warn_count > 0) {
        char msg[256];
        snprintf(msg, sizeof(msg), "JIT warnings: %" PRIu64, s->warn_count);
        warn_msg(msg);
    }

    printf("\n");
    printf("%s═══ ERRORS ═══%s\n", BOLD, NC);

    if (s->error_count == 0) {
        format_u64_commas(s->total_lines, nbuf1, sizeof(nbuf1));
        char msg[256];
        snprintf(msg, sizeof(msg), "Zero errors in %s lines", nbuf1);
        ok_msg(msg);
    } else {
        char msg[256];
        snprintf(msg, sizeof(msg), "%" PRIu64 " error(s) found:", s->error_count);
        fail_msg(msg);
        size_t to_show = s->error_lines_stored < 10 ? s->error_lines_stored : 10;
        size_t i;
        for (i = 0; i < to_show; ++i) {
            printf("           Line %" PRIu64 ": %s\n", s->error_lines[i].line_no, s->error_lines[i].text);
        }
        if (s->error_count > 10) {
            snprintf(msg, sizeof(msg), "           ... and %" PRIu64 " more", s->error_count - 10);
            printf("%s\n", msg);
        }
    }

    if (s->interleaved_lines > 0) {
        double pct = s->total_lines > 0 ? (100.0 * (double)s->interleaved_lines / (double)s->total_lines) : 0.0;
        format_u64_commas(s->interleaved_lines, nbuf1, sizeof(nbuf1));
        char msg[256];
        snprintf(msg, sizeof(msg), "Interleaved lines: %s (%.1f%%) — cosmetic threading race, not a bug", nbuf1, pct);
        warn_msg(msg);
    } else {
        ok_msg("No interleaved log output");
    }

    if (contains_substr(last_clean, strlen(last_clean), "VOODOO JIT")) {
        ok_msg("Log ends cleanly");
    } else {
        char msg[256];
        snprintf(msg, sizeof(msg), "Log ends with unexpected line: %.80s", last_clean);
        warn_msg(msg);
    }

    if (s->verify_mismatch_count > 0) {
        printf("\n");
        printf("%s═══ JIT VERIFY ═══%s\n", BOLD, NC);
        char msg[512];
        format_u64_commas(s->verify_mismatch_count, nbuf1, sizeof(nbuf1));
        snprintf(msg, sizeof(msg), "VERIFY MISMATCH events: %s", nbuf1);
        fail_msg(msg);
        if (s->verify_pixels_differ > 0) {
            format_u64_commas(s->verify_pixels_differ, nbuf1, sizeof(nbuf1));
            snprintf(msg, sizeof(msg), "Total differing pixels: %s", nbuf1);
            fail_msg(msg);
        }
        /* Match rate */
        if (s->pixel_count_total > 0 && s->verify_pixels_differ > 0) {
            double match_pct = 100.0 * (1.0 - (double)s->verify_pixels_differ / (double)s->pixel_count_total);
            format_u64_commas(s->pixel_count_total, nbuf1, sizeof(nbuf1));
            snprintf(msg, sizeof(msg), "Match rate: %.2f%% (%s total pixels)", match_pct, nbuf1);
            if (match_pct >= 99.0)
                ok_msg(msg);
            else
                fail_msg(msg);
        }

        /* Per-fogMode breakdown (copy to sort without modifying const) */
        if (s->vm_fog_len > 0) {
            printf("\n%s  Mismatches by fogMode:%s\n", BOLD, NC);
            FogModeCounter fog_sorted[MAX_FOGMODE_COUNTERS];
            size_t fog_n = s->vm_fog_len;
            memcpy(fog_sorted, s->vm_fog, fog_n * sizeof(FogModeCounter));
            /* Insertion sort descending by count */
            size_t fi, fj;
            for (fi = 1; fi < fog_n; fi++) {
                FogModeCounter tmp = fog_sorted[fi];
                fj = fi;
                while (fj > 0 && fog_sorted[fj - 1].count < tmp.count) {
                    fog_sorted[fj] = fog_sorted[fj - 1];
                    fj--;
                }
                fog_sorted[fj] = tmp;
            }
            for (fi = 0; fi < fog_n && fi < 10; fi++) {
                FogModeCounter *fc = &fog_sorted[fi];
                int fog_enabled = (fc->fog_mode & 0x01) ? 1 : 0;
                format_u64_commas(fc->count, nbuf1, sizeof(nbuf1));
                format_u64_commas(fc->pixels_differ, nbuf2, sizeof(nbuf2));
                printf("             0x%08x: %s events, %s pixels%s\n",
                       fc->fog_mode, nbuf1, nbuf2,
                       fog_enabled ? "" : " (fog disabled)");
            }
        }

        /* Top mismatch pipeline configs (copy to sort) */
        if (s->vm_configs_len > 0) {
            printf("\n%s  Top mismatch pipeline configs:%s\n", BOLD, NC);
            MismatchConfigCounter *cfg_sorted = xmalloc(s->vm_configs_len * sizeof(MismatchConfigCounter));
            size_t cfg_n = s->vm_configs_len;
            memcpy(cfg_sorted, s->vm_configs, cfg_n * sizeof(MismatchConfigCounter));
            size_t ci, cj;
            for (ci = 1; ci < cfg_n; ci++) {
                MismatchConfigCounter tmp = cfg_sorted[ci];
                cj = ci;
                while (cj > 0 && cfg_sorted[cj - 1].count < tmp.count) {
                    cfg_sorted[cj] = cfg_sorted[cj - 1];
                    cj--;
                }
                cfg_sorted[cj] = tmp;
            }
            size_t show = cfg_n < 10 ? cfg_n : 10;
            for (ci = 0; ci < show; ci++) {
                MismatchConfigCounter *cc = &cfg_sorted[ci];
                format_u64_commas(cc->count, nbuf1, sizeof(nbuf1));
                format_u64_commas(cc->pixels_differ, nbuf2, sizeof(nbuf2));
                printf("             #%zu: %s events (%s px) fbz=0x%08x fcp=0x%08x alpha=0x%08x tex=0x%08x fog=0x%08x\n",
                       ci + 1, nbuf1, nbuf2,
                       cc->fbz_mode, cc->fbz_color_path, cc->alpha_mode,
                       cc->texture_mode, cc->fog_mode);
            }
            free(cfg_sorted);
        }

        /* Pixel diff magnitude distribution */
        if (s->pixel_diffs_parsed > 0) {
            printf("\n%s  Pixel diff magnitude (max channel per pixel):%s\n", BOLD, NC);
            uint64_t total = s->pixel_diffs_parsed;
            format_u64_commas(s->diff_mag_0_1, nbuf1, sizeof(nbuf1));
            printf("             ±0-1: %s (%.1f%%)\n", nbuf1, 100.0 * (double)s->diff_mag_0_1 / (double)total);
            format_u64_commas(s->diff_mag_2_3, nbuf1, sizeof(nbuf1));
            printf("             ±2-3: %s (%.1f%%)\n", nbuf1, 100.0 * (double)s->diff_mag_2_3 / (double)total);
            format_u64_commas(s->diff_mag_4_6, nbuf1, sizeof(nbuf1));
            printf("             ±4-6: %s (%.1f%%)\n", nbuf1, 100.0 * (double)s->diff_mag_4_6 / (double)total);
            format_u64_commas(s->diff_mag_7_plus, nbuf1, sizeof(nbuf1));
            printf("             ±7+:  %s (%.1f%%)\n", nbuf1, 100.0 * (double)s->diff_mag_7_plus / (double)total);
            printf("             Max |dR|=%d |dG|=%d |dB|=%d (RGB565)\n",
                   s->max_abs_dr, s->max_abs_dg, s->max_abs_db);
        }
    }

    printf("\n");
    printf("%s═══ EXECUTION ═══%s\n", BOLD, NC);

    format_u64_commas(s->execute_count, nbuf1, sizeof(nbuf1));
    {
        char msg[256];
        snprintf(msg, sizeof(msg), "EXECUTE calls: %s", nbuf1);
        info_msg(msg);
    }

    if (s->unique_scanlines.len > 0) {
        char msg[256];
        snprintf(msg, sizeof(msg), "Unique scanlines (real_y): %zu", s->unique_scanlines.len);
        info_msg(msg);
    }

    format_u64_commas(s->post_count, nbuf1, sizeof(nbuf1));
    {
        char msg[256];
        snprintf(msg, sizeof(msg), "POST entries: %s", nbuf1);
        info_msg(msg);
    }

    format_u64_commas(s->pixel_count_total, nbuf1, sizeof(nbuf1));
    {
        char msg[256];
        snprintf(msg, sizeof(msg), "Total pixels rendered: %s", nbuf1);
        info_msg(msg);
    }

    {
        char msg[256];
        snprintf(msg, sizeof(msg), "Max pixels/scanline: %" PRIu64, s->pixel_count_max);
        info_msg(msg);
    }

    if (s->pixel_hist_1 || s->pixel_hist_2_10 || s->pixel_hist_11_100 || s->pixel_hist_101_320 || s->pixel_hist_321_plus) {
        info_msg("Pixel count distribution:");
        if (s->pixel_hist_1) {
            format_u64_commas(s->pixel_hist_1, nbuf1, sizeof(nbuf1));
            printf("             %7s: %s\n", "1", nbuf1);
        }
        if (s->pixel_hist_2_10) {
            format_u64_commas(s->pixel_hist_2_10, nbuf1, sizeof(nbuf1));
            printf("             %7s: %s\n", "2-10", nbuf1);
        }
        if (s->pixel_hist_11_100) {
            format_u64_commas(s->pixel_hist_11_100, nbuf1, sizeof(nbuf1));
            printf("             %7s: %s\n", "11-100", nbuf1);
        }
        if (s->pixel_hist_101_320) {
            format_u64_commas(s->pixel_hist_101_320, nbuf1, sizeof(nbuf1));
            printf("             %7s: %s\n", "101-320", nbuf1);
        }
        if (s->pixel_hist_321_plus) {
            format_u64_commas(s->pixel_hist_321_plus, nbuf1, sizeof(nbuf1));
            printf("             %7s: %s\n", "321+", nbuf1);
        }
    }

    printf("\n");
    printf("%s═══ PIPELINE COVERAGE ═══%s\n", BOLD, NC);
    {
        char msg[256];
        snprintf(msg, sizeof(msg), "Unique pipeline configs: %zu", s->configs_raw.len);
        info_msg(msg);
    }
    printf("\n");

    size_t non_zero_tex = count_non_zero_modes(&s->texture_modes_raw);
    if (non_zero_tex > 0) {
        char msg[256];
        snprintf(msg, sizeof(msg), "Texture fetch: %zu modes (%zu non-zero)", s->texture_modes_raw.len, non_zero_tex);
        ok_msg(msg);
    } else {
        warn_msg("Texture fetch: not exercised (all textureMode=0)");
    }

    if (s->fbz_color_paths_raw.len > 1) {
        char msg[256];
        snprintf(msg, sizeof(msg), "Color combine: %zu fbzColorPath configs", s->fbz_color_paths_raw.len);
        ok_msg(msg);
    } else if (s->fbz_color_paths_raw.len == 1) {
        char msg[256];
        snprintf(msg, sizeof(msg), "Color combine: 1 config (%s)", s->fbz_color_paths_raw.data[0]);
        info_msg(msg);
    } else {
        warn_msg("Color combine: no data");
    }

    size_t non_zero_alpha = count_non_zero_modes(&s->alpha_modes_raw);
    if (non_zero_alpha > 0) {
        char msg[256];
        snprintf(msg, sizeof(msg), "Alpha test/blend: %zu modes (%zu non-zero)", s->alpha_modes_raw.len, non_zero_alpha);
        ok_msg(msg);
    } else {
        warn_msg("Alpha test/blend: not exercised (all alphaMode=0)");
    }

    size_t non_zero_fog = count_non_zero_modes(&s->fog_modes_raw);
    if (non_zero_fog > 0) {
        char msg[256];
        snprintf(msg, sizeof(msg), "Fog: %zu modes (%zu non-zero)", s->fog_modes_raw.len, non_zero_fog);
        ok_msg(msg);
    } else {
        info_msg("Fog: not used by test workload (fogMode=0)");
    }

    if (s->z_values.len > 0) {
        format_u64_commas((uint64_t)s->z_values.len, nbuf1, sizeof(nbuf1));
        char msg[256];
        snprintf(msg, sizeof(msg), "Depth test: active (%s unique Z values)", nbuf1);
        ok_msg(msg);
    } else {
        warn_msg("Depth test: no non-zero Z values seen");
    }

    {
        char msg[256];
        snprintf(msg, sizeof(msg), "fbzMode configs: %zu", s->fbz_modes_raw.len);
        info_msg(msg);
    }

    bool dither_found = false;
    size_t i;
    for (i = 0; i < s->fbz_modes.len; ++i) {
        if (s->fbz_modes.data[i] & (1u << 8u)) {
            dither_found = true;
            break;
        }
    }
    if (!dither_found) {
        for (i = 0; i < s->alpha_modes.len; ++i) {
            if (s->alpha_modes.data[i] & 1u) {
                dither_found = true;
                break;
            }
        }
    }
    if (dither_found) {
        ok_msg("Dithering: exercised");
    } else {
        info_msg("Dithering: not enabled in test workload");
    }

    if (s->post_count > 0) {
        format_u64_commas(s->post_count, nbuf1, sizeof(nbuf1));
        char msg[256];
        snprintf(msg, sizeof(msg), "Framebuffer write: %s scanlines completed", nbuf1);
        ok_msg(msg);
    } else {
        fail_msg("Framebuffer write: no POST entries — blocks may not be executing");
    }

    printf("\n");
    printf("%s═══ PIXEL OUTPUT ═══%s\n", BOLD, NC);

    format_u64_commas(s->pixel_lines, nbuf1, sizeof(nbuf1));
    {
        char msg[256];
        snprintf(msg, sizeof(msg), "PIXEL log lines: %s", nbuf1);
        info_msg(msg);
    }

    size_t unique_pixels = bitset_count(s->unique_pixels, PIXEL_BITSET_BYTES);
    size_t non_zero_pixels = unique_pixels - (bitset_get(s->unique_pixels, 0) ? 1u : 0u);
    {
        char msg[256];
        snprintf(msg, sizeof(msg), "Unique RGB565 values: %zu (%zu non-zero)", unique_pixels, non_zero_pixels);
        info_msg(msg);
    }

    if (non_zero_pixels > 10) {
        ok_msg("Pixel diversity looks realistic");
        char sample[128];
        sample[0] = '\0';
        size_t off = 0;
        size_t shown = 0;
        uint32_t v;
        for (v = 1; v <= 0xFFFFu && shown < 16; ++v) {
            if (!bitset_get(s->unique_pixels, v)) {
                continue;
            }
            int n = snprintf(sample + off, sizeof(sample) - off, "%s%04x", shown == 0 ? "" : " ", v);
            if (n < 0) {
                n = 0;
            }
            off += (size_t)n;
            shown++;
        }
        printf("             Sample: %s\n", sample);
    } else if (non_zero_pixels > 0) {
        char msg[256];
        snprintf(msg, sizeof(msg), "Low pixel diversity (%zu non-zero values)", non_zero_pixels);
        warn_msg(msg);
    } else if (s->pixel_lines > 0) {
        warn_msg("All pixels are 0x0000 — may indicate rendering issue or early boot");
    }

    printf("\n");
    printf("%s═══ ITERATORS ═══%s\n", BOLD, NC);

    uint64_t neg_total = s->negative_ir + s->negative_ig + s->negative_ib + s->negative_ia;
    if (neg_total > 0) {
        info_msg("Negative iterators (normal for signed Gouraud):");
        if (s->negative_ir) {
            format_u64_commas(s->negative_ir, nbuf1, sizeof(nbuf1));
            printf("             ir: %s\n", nbuf1);
        }
        if (s->negative_ig) {
            format_u64_commas(s->negative_ig, nbuf1, sizeof(nbuf1));
            printf("             ig: %s\n", nbuf1);
        }
        if (s->negative_ib) {
            format_u64_commas(s->negative_ib, nbuf1, sizeof(nbuf1));
            printf("             ib: %s\n", nbuf1);
        }
        if (s->negative_ia) {
            format_u64_commas(s->negative_ia, nbuf1, sizeof(nbuf1));
            printf("             ia: %s\n", nbuf1);
        }
    } else {
        info_msg("No negative iterator values seen");
    }

    printf("\n");
    printf("%s═══ SUMMARY ═══%s\n", BOLD, NC);
    printf("\n");

    char row_block_comp[128];
    if (s->generate_count > 0) {
        format_u64_commas(s->generate_count, nbuf1, sizeof(nbuf1));
        format_u64_commas(s->generate_count, nbuf2, sizeof(nbuf2));
        snprintf(row_block_comp, sizeof(row_block_comp), "%s/%s successful (100%%)", nbuf1, nbuf2);
    } else {
        snprintf(row_block_comp, sizeof(row_block_comp), "NONE");
    }

    char row_fallbacks[64];
    if (total_fallbacks > 0) {
        format_u64_commas(total_fallbacks, row_fallbacks, sizeof(row_fallbacks));
    } else {
        snprintf(row_fallbacks, sizeof(row_fallbacks), "0");
    }

    char row_error_count[64];
    snprintf(row_error_count, sizeof(row_error_count), "%" PRIu64, s->error_count);

    char row_crash[64];
    if (s->error_count == 0) {
        snprintf(row_crash, sizeof(row_crash), "0");
    } else {
        snprintf(row_crash, sizeof(row_crash), "%" PRIu64, s->error_count);
    }

    char row_mode_div[128];
    snprintf(row_mode_div, sizeof(row_mode_div), "%zu unique configurations", s->configs_raw.len);

    char row_texture[128];
    if (non_zero_tex > 0) {
        snprintf(row_texture, sizeof(row_texture), "Exercised (%zu modes)", s->texture_modes_raw.len);
    } else {
        snprintf(row_texture, sizeof(row_texture), "Not used");
    }

    char row_color[128];
    if (s->fbz_color_paths_raw.len > 0) {
        snprintf(row_color, sizeof(row_color), "Exercised (%zu configs)", s->fbz_color_paths_raw.len);
    } else {
        snprintf(row_color, sizeof(row_color), "No data");
    }

    char row_alpha[128];
    if (non_zero_alpha > 0) {
        snprintf(row_alpha, sizeof(row_alpha), "Exercised (%zu modes)", s->alpha_modes_raw.len);
    } else {
        snprintf(row_alpha, sizeof(row_alpha), "Not used");
    }

    char row_fog[128];
    if (non_zero_fog > 0) {
        snprintf(row_fog, sizeof(row_fog), "Exercised (%zu modes)", s->fog_modes_raw.len);
    } else {
        snprintf(row_fog, sizeof(row_fog), "Not used by workload");
    }

    char row_dither[64];
    snprintf(row_dither, sizeof(row_dither), "%s", dither_found ? "Exercised" : "Not enabled");

    char row_fb_write[128];
    if (s->post_count > 0) {
        format_u64_commas(s->post_count, nbuf1, sizeof(nbuf1));
        snprintf(row_fb_write, sizeof(row_fb_write), "~%s scanlines", nbuf1);
    } else {
        snprintf(row_fb_write, sizeof(row_fb_write), "NONE");
    }

    char row_depth[128];
    if (s->z_values.len > 0) {
        format_u64_commas((uint64_t)s->z_values.len, nbuf1, sizeof(nbuf1));
        snprintf(row_depth, sizeof(row_depth), "Active (%s Z values)", nbuf1);
    } else {
        snprintf(row_depth, sizeof(row_depth), "Not active");
    }

    char row_pixel[128];
    if (non_zero_pixels > 0) {
        snprintf(row_pixel, sizeof(row_pixel), "%zu unique RGB565 colors", non_zero_pixels);
    } else {
        snprintf(row_pixel, sizeof(row_pixel), "All zero");
    }

    char row_cache[64];
    snprintf(row_cache, sizeof(row_cache), "%" PRIu64, s->cache_hits);

    char row_xdir[128];
    if (s->xdir_pos_count > 0 && s->xdir_neg_count > 0) {
        format_u64_commas(s->xdir_pos_count, nbuf1, sizeof(nbuf1));
        format_u64_commas(s->xdir_neg_count, nbuf2, sizeof(nbuf2));
        snprintf(row_xdir, sizeof(row_xdir), "+1 (%s) / -1 (%s)", nbuf1, nbuf2);
    } else {
        snprintf(row_xdir, sizeof(row_xdir), "%c1 only", s->xdir_pos_count ? '+' : '-');
    }

    const char *row_interleave = s->interleaved_lines > 0 ? "Cosmetic only" : "None";
    const char *row_termination = contains_substr(last_clean, strlen(last_clean), "VOODOO JIT") ? "Clean" : "Unexpected";

    char row_scanlines[64];
    if (s->unique_scanlines.len > 0) {
        snprintf(row_scanlines, sizeof(row_scanlines), "%zu unique y values", s->unique_scanlines.len);
    } else {
        snprintf(row_scanlines, sizeof(row_scanlines), "No data");
    }

    char row_verify[128];
    if (s->verify_mismatch_count > 0) {
        if (s->pixel_count_total > 0 && s->verify_pixels_differ > 0) {
            double match_pct = 100.0 * (1.0 - (double)s->verify_pixels_differ / (double)s->pixel_count_total);
            snprintf(row_verify, sizeof(row_verify), "%" PRIu64 " MISMATCHES (%.2f%% match)", s->verify_mismatch_count, match_pct);
        } else {
            snprintf(row_verify, sizeof(row_verify), "%" PRIu64 " MISMATCHES", s->verify_mismatch_count);
        }
    } else {
        snprintf(row_verify, sizeof(row_verify), "Clean (no mismatches)");
    }

    char row_warns[64];
    if (s->warn_count > 0) {
        snprintf(row_warns, sizeof(row_warns), "%" PRIu64, s->warn_count);
    } else {
        snprintf(row_warns, sizeof(row_warns), "0");
    }

    const char *labels[] = {
        "Block compilation", "Rejects/fallbacks", "JIT warnings", "Error count", "Crash indicators",
        "Verify mismatches", "Mode diversity",
        "Texture fetch", "Color combine", "Alpha test/blend", "Fog", "Dither", "Framebuffer write",
        "Depth test", "Scanline coverage", "Pixel output", "Cache hits", "xdir coverage",
        "Thread interleave", "Log termination"
    };
    const char *values[] = {
        row_block_comp, row_fallbacks, row_warns, row_error_count, row_crash,
        row_verify, row_mode_div,
        row_texture, row_color, row_alpha, row_fog, row_dither, row_fb_write,
        row_depth, row_scanlines, row_pixel, row_cache, row_xdir,
        row_interleave, row_termination
    };
    size_t row_count = sizeof(labels) / sizeof(labels[0]);

    size_t max_label = 0;
    size_t irow;
    for (irow = 0; irow < row_count; ++irow) {
        size_t ll = strlen(labels[irow]);
        if (ll > max_label) {
            max_label = ll;
        }
    }

    for (irow = 0; irow < row_count; ++irow) {
        printf("  %-*s  │  %s\n", (int)max_label, labels[irow], values[irow]);
    }

    printf("\n");
    bool has_errors = s->error_count > 0;
    bool has_blocks = s->generate_count > 0;
    bool has_output = s->post_count > 0;
    bool has_fallbacks = total_fallbacks > 0;
    bool has_mismatches = s->verify_mismatch_count > 0;

    if (has_mismatches) {
        printf("  %s%sVERDICT: JIT VERIFY MISMATCH — CORRECTNESS ISSUE%s\n", BOLD, RED, NC);
    } else if (has_blocks && has_output && !has_errors && !has_fallbacks) {
        printf("  %s%sVERDICT: HEALTHY%s\n", BOLD, GREEN, NC);
    } else if (has_blocks && has_output && has_fallbacks && !has_errors) {
        printf("  %s%sVERDICT: FUNCTIONAL WITH INTERPRETER FALLBACKS%s\n", BOLD, YELLOW, NC);
    } else if (has_blocks && has_output && has_errors) {
        printf("  %s%sVERDICT: FUNCTIONAL WITH WARNINGS%s\n", BOLD, YELLOW, NC);
    } else if (has_blocks && !has_output) {
        printf("  %s%sVERDICT: COMPILING BUT NOT EXECUTING%s\n", BOLD, RED, NC);
    } else {
        printf("  %s%sVERDICT: JIT NOT ACTIVE%s\n", BOLD, RED, NC);
    }

    (void)path;
    (void)file_mb;
    printf("\n");
}

static void analyze(const char *path) {
    int fd = open(path, O_RDONLY);
    if (fd < 0) {
        die("Error opening %s: %s", path, strerror(errno));
    }

    struct stat stbuf;
    if (fstat(fd, &stbuf) != 0) {
        close(fd);
        die("Error stating %s: %s", path, strerror(errno));
    }

    size_t file_size = (size_t)stbuf.st_size;
    double file_mb = (double)file_size / (1024.0 * 1024.0);

    const char *data = "";
    void *map = NULL;
    if (file_size > 0) {
        map = mmap(NULL, file_size, PROT_READ, MAP_PRIVATE, fd, 0);
        if (map == MAP_FAILED) {
            close(fd);
            die("mmap failed: %s", strerror(errno));
        }
        madvise(map, file_size, MADV_SEQUENTIAL);
        data = (const char *)map;
    }
    close(fd);

    long cpu_count;
#ifdef _SC_NPROCESSORS_ONLN
    cpu_count = sysconf(_SC_NPROCESSORS_ONLN);
#elif defined(_SC_NPROCESSORS_CONF)
    cpu_count = sysconf(_SC_NPROCESSORS_CONF);
#else
    cpu_count = 1;
#endif
    if (cpu_count < 1) {
        cpu_count = 1;
    }
    size_t thread_count = (size_t)cpu_count;

    size_t *bounds = xmalloc((thread_count + 1) * sizeof(size_t));
    bounds[0] = 0;
    size_t prev = 0;
    size_t i;
    for (i = 1; i < thread_count; ++i) {
        size_t tentative = (size_t)(((unsigned __int128)file_size * (unsigned __int128)i) / (unsigned __int128)thread_count);
        size_t b = find_nearest_boundary(data, file_size, prev, tentative);
        if (b < prev) {
            b = prev;
        }
        bounds[i] = b;
        prev = b;
    }
    bounds[thread_count] = file_size;

    printf("\n%sVoodoo JIT Log Analyzer%s\n", BOLD, NC);
    for (i = 0; i < 60; ++i) {
        fputs("─", stdout);
    }
    printf("\n");
    printf("File: %s (%.1f MB)\n", path, file_mb);
    printf("Threads: %zu\n", thread_count);
    printf("Scanning...");
    fflush(stdout);

    struct timespec t_scan_start;
    clock_gettime(CLOCK_MONOTONIC, &t_scan_start);

    atomic_uint_fast64_t progress_lines;
    atomic_init(&progress_lines, 0);
    atomic_int done_threads;
    atomic_init(&done_threads, 0);

    WorkerCtx *workers = xcalloc(thread_count, sizeof(WorkerCtx));
    pthread_t *threads = xmalloc(thread_count * sizeof(pthread_t));

    for (i = 0; i < thread_count; ++i) {
        workers[i].data = data;
        workers[i].start = bounds[i];
        workers[i].end = bounds[i + 1];
        workers[i].progress_lines = &progress_lines;
        workers[i].done_threads = &done_threads;
        stats_init(&workers[i].stats, 1u << 14u);
        if (pthread_create(&threads[i], NULL, worker_main, &workers[i]) != 0) {
            die("pthread_create failed");
        }
    }

    uint64_t next_progress = 1000000;
    while (atomic_load_explicit(&done_threads, memory_order_relaxed) < (int)thread_count) {
        uint64_t scanned = atomic_load_explicit(&progress_lines, memory_order_relaxed);
        while (scanned >= next_progress) {
            printf("\r  Scanned %" PRIu64 "M lines...", next_progress / 1000000);
            fflush(stdout);
            next_progress += 1000000;
        }
        struct timespec ts;
        ts.tv_sec = 0;
        ts.tv_nsec = 50 * 1000 * 1000;
        nanosleep(&ts, NULL);
    }

    for (i = 0; i < thread_count; ++i) {
        pthread_join(threads[i], NULL);
    }

    struct timespec t_scan_end;
    clock_gettime(CLOCK_MONOTONIC, &t_scan_end);
    double scan_secs = (double)(t_scan_end.tv_sec - t_scan_start.tv_sec)
                     + (double)(t_scan_end.tv_nsec - t_scan_start.tv_nsec) / 1e9;

    uint64_t scanned_final = atomic_load_explicit(&progress_lines, memory_order_relaxed);
    while (scanned_final >= next_progress) {
        printf("\r  Scanned %" PRIu64 "M lines...", next_progress / 1000000);
        fflush(stdout);
        next_progress += 1000000;
    }

    printf("\r  Merging thread results...");
    fflush(stdout);

    Stats merged;
    stats_init(&merged, 1u << 16u);
    uint64_t line_offset = 0;
    for (i = 0; i < thread_count; ++i) {
        merge_stats(&merged, &workers[i].stats, line_offset);
        line_offset += workers[i].stats.total_lines;
        stats_free(&workers[i].stats);
    }

    struct timespec t_merge_end;
    clock_gettime(CLOCK_MONOTONIC, &t_merge_end);
    double merge_secs = (double)(t_merge_end.tv_sec - t_scan_end.tv_sec)
                      + (double)(t_merge_end.tv_nsec - t_scan_end.tv_nsec) / 1e9;

    char total_lines_buf[64];
    format_u64_commas(merged.total_lines, total_lines_buf, sizeof(total_lines_buf));
    printf("\r  Scanned %s lines in %.1fs (merge %.1fs)       \n", total_lines_buf, scan_secs, merge_secs);
    printf("\n");

    char last_clean[1024];
    get_last_clean_line(data, file_size, last_clean, sizeof(last_clean));
    print_report(path, file_mb, &merged, last_clean);

    stats_free(&merged);
    free(workers);
    free(threads);
    free(bounds);
    if (map != NULL) {
        munmap(map, file_size);
    }
}

int main(int argc, char **argv) {
    if (argc != 2) {
        printf("Usage: %s <logfile>\n", argv[0]);
        printf("  Analyzes a Voodoo JIT debug log and produces a health report.\n");
        return 1;
    }

    struct stat st;
    if (stat(argv[1], &st) != 0 || !S_ISREG(st.st_mode)) {
        printf("Error: %s not found\n", argv[1]);
        return 1;
    }

    analyze(argv[1]);
    return 0;
}
