/*
 * 86Box    A hypervisor and IBM PC system emulator that specializes in
 *          running old operating systems and software designed for IBM
 *          PC systems and compatibles from 1981 through fairly recent
 *          system designs based on the PCI bus.
 *
 *          This file is part of the 86Box distribution.
 *
 *          Replays batches the RIVA TNT recorded (NV4_3D_RECORD=<dir>
 *          in the emulator; the format is in nv4_pgraph_3d.h) through the
 *          CPU span shader and the GPU lane from the same VRAM and
 *          textures, and compares every byte of VRAM. The folder is
 *          NV4_3D_REPLAY_DIR; without it, or without a Vulkan device, the
 *          test is skipped. Files recorded by a build with other record
 *          layouts are skipped too.
 *
 *          The 3D code is included as C, as in nv4_3d_gpu_test.c, whose
 *          stand-ins this repeats; the DMA and system memory reads are
 *          answered from the recording.
 *
 * Authors: Marcos Alves, <xeon4d@gmail.com>
 *
 *          Copyright 2026 Marcos Alves
 */
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <dirent.h>

#include "../../src/video/nv/nv4/subsystems/nv4_pgraph_3d.c"

#define VRAM_SIZE (1 << 20)
#define COL_BASE  0x10000
#define Z_BASE    0x20000

#define CHECK(cond)                                                    \
    do {                                                               \
        if (!(cond)) {                                                 \
            fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #cond); \
            failures++;                                                \
        }                                                              \
    } while (0)

static int failures;

/* What the 3D code reaches outside itself: nothing here textures,
   threads or uses the GPU lane. */
uint64_t tsc;
double   cpuclock = 1.0;
uint32_t mem_size;
uint8_t *ram;

/* the lane's and the verifier's lines, for a failure's story */
void
pclog(const char *fmt, ...)
{
    va_list ap;

    if (!getenv("NV4_TEST_LOG") && !strstr(fmt, "verify: flush"))
        return;
    va_start(ap, fmt);
    vfprintf(stderr, fmt, ap);
    va_end(ap);
}

void
fatal(const char *fmt, ...)
{
    va_list ap;

    va_start(ap, fmt);
    vfprintf(stderr, fmt, ap);
    va_end(ap);
    exit(2);
}

int
agpgart_resolve(uint32_t addr, uint32_t *ram_addr)
{
    (void) addr;
    (void) ram_addr;
    return 0;
}

void
nv4_pgraph_2d_flush_run(nv4_t *nv4)
{
    (void) nv4;
}

void
nv4_pgraph_set_grobj(nv4_t *nv4, int word, int shift, int bits, uint32_t val)
{
    (void) nv4;
    (void) word;
    (void) shift;
    (void) bits;
    (void) val;
}

uint32_t
nv4_ramin_read32(nv4_t *nv4, uint32_t off)
{
    (void) nv4;
    (void) off;
    return 0;
}

/* nv_gpu_vk.c's telemetry file (NV_GPU_TELEMETRY, never set here) */
char usr_path[1024];

FILE *
plat_fopen(const char *path, const char *mode)
{
    return fopen(path, mode);
}

void
path_append_filename(char *dest, const char *s1, const char *s2)
{
    sprintf(dest, "%s/%s", s1, s2);
}

/* the lane's variant compiler runs at once: every variant is ready */
thread_t *
thread_create_named(void (*thread_func)(void *param), void *param, const char *name)
{
    (void) name;
    thread_func(param);
    return NULL;
}

int
thread_wait(thread_t *arg)
{
    (void) arg;
    return 0;
}

event_t *
thread_create_event(void)
{
    return NULL;
}

void
thread_set_event(event_t *arg)
{
    (void) arg;
}

void
thread_reset_event(event_t *arg)
{
    (void) arg;
}

int
thread_wait_event(event_t *arg, int timeout)
{
    (void) arg;
    (void) timeout;
    return 0;
}

void
thread_destroy_event(event_t *arg)
{
    (void) arg;
}

/* ------------------------------------------------------------------ */
/* ------------------------------------------------------------------ */
/* The recorded texture pieces: nv4_dma_translate and nv4_dma_page_left
   answer from them, and system memory is <ram>. */
typedef struct {
    uint32_t dma, off, left, len, addr;
    int      target;
} rpiece_t;

static rpiece_t *pieces;
static int       npieces, cpieces;

static const rpiece_t *
piece_at(uint32_t inst, uint32_t offset)
{
    for (int i = 0; i < npieces; i++)
        if ((pieces[i].dma == inst) && (offset >= pieces[i].off) && (offset < pieces[i].off + pieces[i].left))
            return &pieces[i];
    return NULL;
}

int
nv4_dma_translate(nv4_t *card, uint32_t inst, uint32_t offset, int write, uint32_t *addr, int *target)
{
    const rpiece_t *p = piece_at(inst, offset);

    (void) card;
    (void) write;
    if (!p)
        return !NV4_DMA_OK;
    *addr   = p->addr + (offset - p->off);
    *target = p->target;
    return NV4_DMA_OK;
}

uint32_t
nv4_dma_page_left(nv4_t *card, uint32_t inst, uint32_t offset)
{
    const rpiece_t *p = piece_at(inst, offset);

    (void) card;
    return p ? (p->left - (offset - p->off)) : (0x1000 - (offset & 0xfff));
}

uint8_t
mem_readb_phys(uint32_t addr)
{
    return (ram && (addr < mem_size * 1024)) ? ram[addr] : 0xff;
}

uint16_t
mem_readw_phys(uint32_t addr)
{
    return mem_readb_phys(addr) | (mem_readb_phys(addr + 1) << 8);
}

uint32_t
mem_readl_phys(uint32_t addr)
{
    return mem_readw_phys(addr) | ((uint32_t) mem_readw_phys(addr + 2) << 16);
}

void
dma_bm_read(uint32_t phys_addr, uint8_t *data, uint32_t total_size, int transfer_size)
{
    (void) transfer_size;
    for (uint32_t i = 0; i < total_size; i++)
        data[i] = mem_readb_phys(phys_addr + i);
}

/* ------------------------------------------------------------------ */
static nv4_t    *nv4;
static monitor_t mon;

static void
card_new(uint32_t vram_size)
{
    nv4                      = calloc(1, sizeof(nv4_t));
    nv4->vram_size           = vram_size;
    nv4->vram_mask           = vram_size - 1;
    nv4->svga.vram           = calloc(1, vram_size + 4096); /* as svga_init: the lane moves it */
    nv4->svga.changedvram    = calloc(1, (vram_size >> 12) + 1);
    nv4->svga.monitor        = &mon;
    nv4->render_threads      = 1;
    nv4->gpu_raster          = 1;
    mon.mon_changeframecount = 2;
    nv4_3d_init(nv4);
}

static void
card_free(void)
{
    nv4_3d_close(nv4);
    free(nv4->svga.vram);
    free(nv4->svga.changedvram);
    free(nv4);
    nv4 = NULL;
}

static uint32_t
get32(FILE *f, int *ok)
{
    uint32_t v = 0;

    if (fread(&v, 4, 1, f) != 1)
        *ok = 0;
    return v;
}

static int      nfiles, nother, ngpu, ncpu, nbad;
static uint64_t ntex, nsys; /* texture pieces replayed, in system memory */

/* One recorded batch: the CPU's bytes, then the lane's from the same
   start, compared over all of VRAM */
static void
replay(const char *path)
{
    FILE           *f = fopen(path, "rb");
    nv4_3d_batch_t *b;
    uint8_t        *init = NULL, *cpu = NULL;
    uint32_t        vsize, sz[4];
    int             ok = 1, n[4], serial, parallel;
    uint32_t        tex_lo, tex_hi;

    if (!f)
        return;
    nfiles++;
    if ((get32(f, &ok) != NV4_REC_MAGIC) || (get32(f, &ok) != NV4_REC_VERSION))
        ok = 0;
    for (int i = 0; ok && (i < 4); i++)
        sz[i] = get32(f, &ok);
    if (!ok || (sz[0] != sizeof(nv4_3d_state_t)) || (sz[1] != sizeof(nv4_3d_tri_t)) || (sz[2] != sizeof(nv4_3d_span_t))
        || (sz[3] != sizeof(nv4_3d_region_t))) {
        nother++; /* another build's layout */
        fclose(f);
        return;
    }
    vsize = get32(f, &ok);
    (void) get32(f, &ok); /* vram_mask */
    for (int i = 0; i < 4; i++)
        n[i] = (int) get32(f, &ok);
    serial   = (int) get32(f, &ok);
    parallel = (int) get32(f, &ok);
    tex_lo   = get32(f, &ok);
    tex_hi   = get32(f, &ok);
    if (!ok || (vsize & (vsize - 1)) || (n[0] > B3D_STATES) || (n[1] > B3D_TRIS) || (n[2] > B3D_SPANS) || (n[3] > B3D_REGIONS)) {
        fprintf(stderr, "  %s: bad header\n", path);
        failures++;
        fclose(f);
        return;
    }
    if (nv4 && (nv4->vram_size != vsize))
        card_free();
    if (!nv4) {
        card_new(vsize);
        if (!nv4->batch3d->gpu) {
            printf("no Vulkan device: replay skipped\n");
            fclose(f);
            card_free();
            exit(0);
        }
    }
    b = nv4->batch3d;
    nv4_3d_flush(nv4);
    nv4_gpu_wait(b->gpu, nv4);
    ok = (fread(b->states, sizeof(nv4_3d_state_t), n[0], f) == (size_t) n[0])
        && (fread(b->tris, sizeof(nv4_3d_tri_t), n[1], f) == (size_t) n[1])
        && (fread(b->spans, sizeof(nv4_3d_span_t), n[2], f) == (size_t) n[2])
        && (fread(b->regions, sizeof(nv4_3d_region_t), n[3], f) == (size_t) n[3]);
    init    = calloc(1, vsize);
    cpu     = malloc(vsize);
    npieces = 0;
    while (ok) {
        int      eof = 1;
        uint32_t kind = get32(f, &eof);

        if (!eof)
            break;
        if (kind == NV4_REC_VRAM) {
            uint32_t addr = get32(f, &ok), len = get32(f, &ok);

            if (!ok || (addr + len > vsize) || (fread(init + addr, 1, len, f) != len))
                ok = 0;
        } else if (kind == NV4_REC_TEX) {
            rpiece_t p;
            uint8_t  bytes[0x1000];

            p.dma    = get32(f, &ok);
            p.off    = get32(f, &ok);
            p.left   = get32(f, &ok);
            p.len    = get32(f, &ok);
            p.addr   = get32(f, &ok);
            p.target = (int) get32(f, &ok);
            if (!ok || (p.len > 0x1000) || (fread(bytes, 1, p.len, f) != p.len)) {
                ok = 0;
                break;
            }
            if (!p.target) {
                for (uint32_t k = 0; k < p.len; k++)
                    init[(p.addr + k) & (vsize - 1)] = bytes[k];
            } else {
                if ((uint64_t) p.addr + p.len > (uint64_t) mem_size * 1024) {
                    uint32_t want = ((p.addr + p.len) | 0xfffff) + 1;

                    ram = realloc(ram, want);
                    memset(ram + mem_size * 1024, 0, want - mem_size * 1024);
                    mem_size = want / 1024;
                }
                memcpy(ram + p.addr, bytes, p.len);
            }
            if (npieces == cpieces)
                pieces = realloc(pieces, (cpieces = cpieces * 2 + 64) * sizeof(*pieces));
            pieces[npieces++] = p;
            ntex++;
            nsys += (p.target != 0);
        } else
            ok = 0;
    }
    fclose(f);
    if (!ok) {
        fprintf(stderr, "  %s: truncated\n", path);
        failures++;
        goto done;
    }
    (void) parallel;

    /* the CPU */
    memcpy(nv4->svga.vram, init, vsize);
    b->nstates  = n[0];
    b->ntris    = n[1];
    b->nspans   = n[2];
    b->nregions = n[3];
    b->serial   = serial;
    b->tex_lo   = tex_lo;
    b->tex_hi   = tex_hi;
    nv4_3d_shade_serial(nv4, b);
    memcpy(cpu, nv4->svga.vram, vsize);

    /* the lane, from the same bytes */
    memcpy(nv4->svga.vram, init, vsize);
    if (!nv4_gpu_shade(b->gpu, nv4, b)) {
        ncpu++;
        goto done;
    }
    nv4_gpu_wait(b->gpu, nv4);
    ngpu++;
    if (memcmp(cpu, nv4->svga.vram, vsize)) {
        uint32_t bad = 0, first = 0;

        for (uint32_t a = 0; a < vsize; a++)
            if (cpu[a] != nv4->svga.vram[a]) {
                if (!bad)
                    first = a;
                bad++;
            }
        fprintf(stderr, "  %s: %u bytes differ, first at %06x (GPU %02x, CPU %02x), %d spans%s\n", path, bad, first,
                nv4->svga.vram[first], cpu[first], n[2], serial ? ", rows alias" : "");
        nbad++;
        failures++;
    }

done:
    /* nothing left for the next batch's flush to draw */
    b->nspans = b->ntris = b->nstates = b->nregions = 0;
    b->serial = 0;
    free(init);
    free(cpu);
}

int
main(void)
{
    const char    *dir = getenv("NV4_3D_REPLAY_DIR");
    DIR           *d;
    struct dirent *e;
    char         **names = NULL;
    int            nn = 0;

    if (!dir || !*dir) {
        printf("NV4_3D_REPLAY_DIR not set (a folder of NV4_3D_RECORD files): replay skipped\n");
        return 0;
    }
    if (!(d = opendir(dir))) {
        fprintf(stderr, "cannot open %s\n", dir);
        return 1;
    }
    while ((e = readdir(d)))
        if (!strncmp(e->d_name, "nv4-batch-", 10)) {
            names       = realloc(names, (nn + 1) * sizeof(*names));
            names[nn]   = malloc(strlen(dir) + strlen(e->d_name) + 2);
            sprintf(names[nn++], "%s/%s", dir, e->d_name);
        }
    closedir(d);
    /* in recorded order: the texture cache sees what it saw */
    for (int i = 1; i < nn; i++)
        for (int j = i; (j > 0) && (strcmp(names[j], names[j - 1]) < 0); j--) {
            char *t      = names[j];
            names[j]     = names[j - 1];
            names[j - 1] = t;
        }
    for (int i = 0; i < nn; i++) {
        replay(names[i]);
        free(names[i]);
    }
    free(names);
    if (nv4)
        card_free();
    printf("nv4 3D replay: %d files, %d of another build skipped; %d on the GPU, %d left to the CPU, %d differed; "
           "%llu texture pieces (%llu in system memory)\n",
           nfiles, nother, ngpu, ncpu, nbad, (unsigned long long) ntex, (unsigned long long) nsys);
    if (failures)
        fprintf(stderr, "%d failure(s)\n", failures);
    return failures != 0;
}
