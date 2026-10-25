/*
 * 86Box    A hypervisor and IBM PC system emulator that specializes in
 *          running old operating systems and software designed for IBM
 *          PC systems and compatibles from 1981 through fairly recent
 *          system designs based on the PCI bus.
 *
 *          This file is part of the 86Box distribution.
 *
 *          NV3 3D -- the Direct3D triangle's batch: draw states,
 *          triangles and spans, as the CPU span shader
 *          (nv3_render_d3d.c) and the GPU lane (nv3_render_d3d_gpu.c)
 *          consume them.
 *
 * Authors: Marcos Alves, <xeon4d@gmail.com>
 *
 *          Copyright 2026 Marcos Alves
 */
#ifndef NV3_RENDER_D3D_H
#define NV3_RENDER_D3D_H

struct nv3_gpu_s;

#define D3_STATES 64
#define D3_TRIS   16384
#define D3_SPANS  262144

/* Attributes interpolated across a triangle, as plane equations */
enum {
    D3_AT_Z,
    D3_AT_Q,  /* rhw, or 1 without perspective */
    D3_AT_UQ, /* u * q */
    D3_AT_VQ,
    D3_AT_A,
    D3_AT_R,
    D3_AT_G,
    D3_AT_B,
    D3_AT_FOG,
    D3_AT_COUNT
};

#define D3_TEX_PAGES 64

/* A draw state: everything a pixel needs, taken from PGRAPH when a
   triangle is submitted; immutable once in the batch. */
typedef struct {
    uint32_t config, alpha_ctl; /* CONTROL0, CONTROL1 */
    uint32_t buf_on;            /* bit j: colour buffer j is written */
    int      z_on;              /* the zeta buffer (surface 3) is tested and written */
    uint32_t boff[4], bpitch[4];
    int32_t  cx0, cy0, cx1, cy1; /* the canvas and user clip, max exclusive */
    int      ncr, cr_inv;        /* WinNT-style clip rectangles, tested per pixel */
    int32_t  cr_x0[2], cr_y0[2], cr_x1[2], cr_y1[2];
    int      dither;
    uint32_t fog_rgb;
    /* the texture: tex_size bytes from tex_l0 within the first of its
       pages; system memory pages are physical addresses */
    int      textured;
    uint32_t tex_format;
    int      tex_vram;
    uint32_t tex_l0, tex_size;
    uint32_t tex_pages[D3_TEX_PAGES];
    int      rc[3], rz; /* their regions in the batch, -1: none */
} nv3_d3d_dstate_t;

typedef struct {
    int   state;
    float x0, y0; /* vertex a: the planes' origin */
    float c[D3_AT_COUNT], dx[D3_AT_COUNT], dy[D3_AT_COUNT];
} nv3_d3d_tri_t;

typedef struct {
    int16_t  y, x0, x1; /* x1 exclusive */
    uint16_t tri;
} nv3_d3d_span_t;

#define D3_MAX_WORKERS 8
#define D3_REGIONS     8

/* A row layout in the batch: row y at base + y * pitch. A batch can hold
   several when their bytes do not meet (colour buffers and the zeta
   buffer), since rows are then still each on their own bytes. */
typedef struct {
    uint32_t base, pitch;
    uint32_t lo, hi;   /* the bytes its states can reach */
    uint32_t wlo, whi; /* the bytes touched so far; wlo > whi: none */
} nv3_d3d_region_t;

typedef struct {
    int       id;
    thread_t *thread;
    event_t  *wake; /* a batch is ready, or the pool is closing */
    event_t  *done; /* this worker finished the batch */
} nv3_d3d_worker_t;

typedef struct nv3_d3d_batch_s {
    nv3_d3d_dstate_t states[D3_STATES];
    nv3_d3d_tri_t    tris[D3_TRIS];
    nv3_d3d_span_t   spans[D3_SPANS];
    int              nstates, ntris, nspans;

    /* The row layouts the batch draws into (threads and GPU only), with
       the bytes each has touched (zeta also when only read) */
    nv3_d3d_region_t regions[D3_REGIONS];
    int              nregions;
    uint32_t         tex_lo, tex_hi; /* VRAM its textures read; lo > hi: none */
    int              serial;         /* rows alias: shade on one thread */

    int              nthreads;
    int              run;
    nv3_d3d_worker_t workers[D3_MAX_WORKERS];

    /* Rows are shaded apart (threads or the GPU): the batch must keep
       every row on its own bytes */
    int               parallel;
    struct nv3_gpu_s *gpu;

    uint64_t st_flushes, st_spans, st_us; /* logged at close (nv3_d3d_stats) */
    uint64_t st_sync_us[NV3_SYNC_CAUSES]; /* time waiting for the GPU, by cause */
    uint32_t st_sync_n[NV3_SYNC_CAUSES];

    /* NV3_3D_VERIFY */
    int      verify;
    uint8_t *vsnap, *vres;
    uint32_t vflushes, vbad;
} nv3_d3d_batch_t;

/* The GPU lane (nv3_render_d3d_gpu.c). nv3_gpu_shade runs a whole batch
   and returns 1, or returns 0 and leaves VRAM untouched when it cannot
   take it; the caller then shades it on the CPU. */
extern struct nv3_gpu_s *nv3_gpu_init(void);
extern int               nv3_gpu_shade(struct nv3_gpu_s *g, const nv3_d3d_batch_t *b);
extern void              nv3_gpu_close(struct nv3_gpu_s *g);
/* The batches in flight: wait for them and copy their rows into VRAM;
   do they touch bytes lo..hi? */
extern void              nv3_gpu_wait(struct nv3_gpu_s *g);
extern int               nv3_gpu_pending_overlaps(struct nv3_gpu_s *g, uint32_t lo, uint32_t hi);
extern void              nv3_gpu_counts(struct nv3_gpu_s *g, uint64_t *batches, uint64_t *fallbacks);

/* A 16-bit texture word as the span shader reads it; NULL-safe for any
   byte offset (0 past the texture). The GPU gathers through it too. */
extern const uint8_t *nv3_d3d_tex_ptr(const nv3_d3d_dstate_t *s, uint32_t byte, uint32_t len, uint8_t *tmp);

#endif
