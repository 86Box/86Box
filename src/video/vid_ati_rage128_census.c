/*
 * 86Box    A hypervisor and IBM PC system emulator that specializes in
 *          running old operating systems and software designed for IBM
 *          PC systems and compatibles from 1981 through fairly recent
 *          system designs based on the PCI bus.
 *
 *          This file is part of the 86Box distribution.
 *
 *          ATI Rage 128 Pro -- 3D draw-state census.
 *
 *          A log of the 3D states a guest uses. It records each distinct
 *          rage128_draw_state_t (the decoded per-draw raster state without
 *          the vertices, and the key of the span JIT's block cache), how
 *          many primitives used it, whether they were triangles, lines or
 *          points, and which renderer would take it: the span JIT's
 *          verdict (refused, scalar block or SoA block) and the GPU
 *          backend's kernel id. At device close it writes one line per
 *          state.
 *
 *          Each state field is printed as name=value, with the
 *          rage128_draw_state_t member name and the value in hex (a float
 *          as its 32-bit pattern). A tally of the same fields over the
 *          states the JIT host harness (tests/video/rage128/jit-harness)
 *          generates can then be compared field by field: a value a guest
 *          uses that the harness never generates is a state the harness
 *          does not test.
 *
 *          The census changes nothing a renderer does. The JIT query has
 *          no side effects, and the GPU query puts back the reject
 *          counters and log quota it touches. It is off unless the
 *          R128_STATE_CENSUS environment variable is set, and the triangle,
 *          line and point submit paths check dev->census before calling
 *          in.
 *
 * Authors: skiretic.
 *
 *          Copyright 2026 skiretic.
 */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>
#include <86box/86box.h>
#include <86box/device.h>
#include <86box/mem.h>
#include <86box/path.h>
#include <86box/plat.h>
#include <86box/timer.h>
#include <86box/thread.h>
#include <86box/video.h>
#include <86box/vid_svga.h>
#include <86box/vid_ati_rage128.h>
#include <86box/vid_ati_rage128_fnv.h>

/* clang-format off */
#define CENSUS_CAP 16384 /* distinct states kept; a draw with a new state
                            past this is only counted in overflow */
/* clang-format on */

typedef struct census_ent_t {
    rage128_draw_state_t d;
    uint64_t             hash;
    uint64_t             n;     /* primitives with this state */
    uint32_t             kinds; /* bit 0 tri, 1 line, 2 point */
    int                  jit;   /* 0 refused, 1 scalar, 2 SoA */
    int                  gpu;   /* kernel id, -1 CPU path, -2 no backend */
} census_ent_t;

typedef struct r128_census_t {
    census_ent_t *ent;
    uint32_t      n_ent;
    uint32_t      overflow; /* draws whose state found the table full;
                               a repeated unkept state counts each time */
    uint64_t draws;
    uint64_t last_hash; /* hash of the entry used last, which is
                           checked first; ~0 until one exists */
    uint32_t last_idx;
    char     path[1024 + 512];
} r128_census_t;

/* R128_STATE_CENSUS unset, empty or "0" leaves the census off. A value
   containing a '/' is the output path; any other value writes
   r128_census.txt. A relative path is taken from the machine's folder,
   where the GPU telemetry log goes, on every host; the folder path
   already ends in a separator. */
void
rage128_census_init(rage128_t *dev)
{
    const char    *env = getenv("R128_STATE_CENSUS");
    r128_census_t *c;
    char           rel[512];

    dev->census = NULL;
    if (!env || !env[0] || !strcmp(env, "0"))
        return;
    if (strchr(env, '/') && strlen(env) >= sizeof(rel)) {
        /* refused rather than cut short: a shorter path names a
           different file, which the census would then append to */
        pclog("RAGE128 CENSUS: path longer than %u bytes, census off\n",
              (unsigned) sizeof(rel) - 1);
        return;
    }
    c = (r128_census_t *) calloc(1, sizeof(*c));
    if (!c)
        return;
    c->ent = (census_ent_t *) calloc(CENSUS_CAP, sizeof(*c->ent));
    if (!c->ent) {
        free(c);
        return;
    }
    snprintf(rel, sizeof(rel), "%s", strchr(env, '/') ? env : "r128_census.txt");
    if (path_abs(rel))
        snprintf(c->path, sizeof(c->path), "%s", rel);
    else
        snprintf(c->path, sizeof(c->path), "%s%s", usr_path, rel);
    c->last_hash = ~0ull;
    rage128_log("RAGE128 CENSUS: on, file=%s\n", c->path);
    dev->census = c;
}

/* Called once per primitive by the submit paths, after a draw dropped
   for dead AGP staging has returned, so dropped draws are not counted.
   b and c tell the primitive kind: both set for a triangle, c NULL for
   a line, both NULL for a point. The entry used last is tried first,
   then every entry with an equal hash is confirmed with memcmp. The JIT
   and GPU queries run once, on the first primitive with a new state. The
   GPU gate also reads the staging flags in rs, which are outside the
   key, so the kernel id is its answer for that first primitive. */
void
rage128_census_draw(rage128_t *dev, const rage128_raster_state_t *rs,
                    const r3d_vtx_t *b, const r3d_vtx_t *cv)
{
    r128_census_t *c    = (r128_census_t *) dev->census;
    uint32_t       kind = cv ? 1u : (b ? 2u : 4u);
    uint64_t       hash;
    census_ent_t  *e;

    c->draws++;
    if (c->last_hash != ~0ull && c->n_ent) {
        e = &c->ent[c->last_idx];
        if (!memcmp(&e->d, &rs->d, sizeof(rs->d))) {
            e->n++;
            e->kinds |= kind;
            return;
        }
    }
    hash = r128_fnv1a(&rs->d, sizeof(rs->d));
    for (uint32_t i = 0; i < c->n_ent; i++) {
        e = &c->ent[i];
        if (e->hash == hash && !memcmp(&e->d, &rs->d, sizeof(rs->d))) {
            e->n++;
            e->kinds |= kind;
            c->last_hash = hash;
            c->last_idx  = i;
            return;
        }
    }
    if (c->n_ent == CENSUS_CAP) {
        c->overflow++;
        return;
    }
    e            = &c->ent[c->n_ent];
    e->d         = rs->d;
    e->hash      = hash;
    e->n         = 1;
    e->kinds     = kind;
    e->jit       = rage128_jit_census_verdict(&rs->d);
    e->gpu       = rage128_gpu_census_kernel(dev, rs);
    c->last_hash = hash;
    c->last_idx  = c->n_ent++;
}

/* One line per state: the FNV-1a hash as the state's id, the primitive
   count, the kinds seen (T triangle, L line, P point), the JIT verdict
   and the GPU kernel id, then name=value for each state field. CI
   prints an integer field in hex, a negative one as its 64-bit two's
   complement; CF prints a float field's 32-bit pattern, which is exact
   where a decimal print could round. */
static void
census_print(FILE *f, const census_ent_t *e)
{
    const rage128_draw_state_t *d    = &e->d;
    static const char *const    jn[] = { "refused", "scalar", "soa" };

    fprintf(f, "state %016llx n=%llu kinds=%s%s%s jit=%s gpu=%d |",
            (unsigned long long) e->hash, (unsigned long long) e->n,
            (e->kinds & 1) ? "T" : "", (e->kinds & 2) ? "L" : "",
            (e->kinds & 4) ? "P" : "", jn[e->jit], e->gpu);
#define CI(name, v) fprintf(f, " %s=%llx", name, (unsigned long long) (int64_t) (v))
#define CF(name, v)                                             \
    do {                                                        \
        float    fv_ = (v);                                     \
        uint32_t fu_;                                           \
        memcpy(&fu_, &fv_, 4);                                  \
        fprintf(f, " %s=%llx", name, (unsigned long long) fu_); \
    } while (0)
    CI("draw_ok", d->draw_ok);
    CI("dst_dt", d->dst_dt);
    CI("bpp", d->bpp);
    CI("wmask", d->wmask);
    CI("dither", d->dither);
    CI("stip_en", d->stip_en);
    CI("c_tiled", d->c_tiled);
    CI("aux_on", d->aux_on);
    CI("aux_cntl", d->aux_cntl);
    CI("aux_x0[0]", d->aux_x0[0]);
    CI("aux_x1[0]", d->aux_x1[0]);
    CI("aux_y0[0]", d->aux_y0[0]);
    CI("aux_y1[0]", d->aux_y1[0]);
    CI("sx0", d->sx0);
    CI("sy0", d->sy0);
    CI("sx1", d->sx1);
    CI("sy1", d->sy1);
    CI("sub", d->sub);
    CF("subf", d->subf);
    CI("rnd", d->rnd);
    CI("slim", d->slim);
    CI("woxi", d->woxi);
    CI("woyi", d->woyi);
    CI("z_en", d->z_en);
    CI("z_wr", d->z_wr);
    CI("zfn", d->zfn);
    CI("zbpp", d->zbpp);
    CI("zmax", d->zmax);
    CI("zshift", d->zshift);
    CI("zrowpx", d->zrowpx);
    CI("z_tiled", d->z_tiled);
    CI("sten_on", d->sten_on);
    CI("sfn", d->sfn);
    CI("sfail_op", d->sfail_op);
    CI("zpass_op", d->zpass_op);
    CI("zfail_op", d->zfail_op);
    CI("sref", d->sref);
    CI("svmask", d->svmask);
    CI("swmask", d->swmask);
    CI("sshift", d->sshift);
    CI("flat_on", d->flat_on);
    CI("flat_src", d->flat_src);
    CI("tex_en", d->tex_en);
    CI("sec_en", d->sec_en);
    CI("tex_tiled", d->tex_tiled);
    CI("premult", d->premult);
    CI("do_persp", d->do_persp);
    CI("sec_persp_diff", d->sec_persp_diff);
    CI("sel_w", d->sel_w);
    CI("need_lod", d->need_lod);
    CI("need_lod2", d->need_lod2);
    CF("lod_bias", d->lod_bias);
    CF("texw0", d->texw0);
    CF("texh0", d->texh0);
    CF("texw1", d->texw1);
    CF("texh1", d->texh1);
    CI("sec_sel", d->sec_sel);
    for (int s = 0; s < 2; s++) {
        char nm[24];
#define CS(fld)                                         \
    do {                                                \
        snprintf(nm, sizeof(nm), "sh[%d].%s", s, #fld); \
        CI(nm, d->sh[s].fld);                           \
    } while (0)
        CS(tsp);
        CS(clamp_s);
        CS(clamp_t);
        CS(dt);
        CS(s3tc);
        CS(aone);
        CS(border);
        CS(minb);
        CS(mag);
        CS(mipdis);
        CS(top);
#undef CS
    }
    for (int s = 0; s < 2; s++) {
        char nm[24];
#define CC(fld)                                           \
    do {                                                  \
        snprintf(nm, sizeof(nm), "comb[%d].%s", s, #fld); \
        CI(nm, d->comb[s].fld);                           \
    } while (0)
        CC(comb);
        CC(fmsb);
        CC(cfac);
        CC(ifac);
        CC(comba);
        CC(afac);
        CC(ifaca);
#undef CC
    }
    CI("need_ck", d->need_ck);
    CI("ck3d_on", d->ck3d_on);
    CI("ckc_on", d->ckc_on);
    CI("ckfn", d->ckfn);
    CI("ck3d_clr", d->ck3d_clr);
    CI("ck3d_msk", d->ck3d_msk);
    CI("ckc_clr", d->ckc_clr);
    CI("ckc_msk", d->ckc_msk);
    CF("cc[0]", d->cc[0]);
    CF("cc[1]", d->cc[1]);
    CF("cc[2]", d->cc[2]);
    CF("cc[3]", d->cc[3]);
    CI("spec_en", d->spec_en);
    CI("fog_en", d->fog_en);
    CI("fog_table_en", d->fog_table_en);
    CF("fogr", d->fogr);
    CF("fogg", d->fogg);
    CF("fogb", d->fogb);
    CI("atest_en", d->atest_en);
    CI("atest_fn", d->atest_fn);
    CI("atest_ref", d->atest_ref);
    CI("alpha_en", d->alpha_en);
    CI("bsrc", d->bsrc);
    CI("bdst", d->bdst);
    CI("bfcn", d->bfcn);
    CI("soa_selftex", d->soa_selftex);
#undef CI
#undef CF
    fputc('\n', f);
}

/* Device close calls this after the command executor and the raster
   workers have stopped, so no draw can reach the census while it is
   written and freed. The file is opened for append: several runs can
   share one file, each starting with its own "# census" line. */
void
rage128_census_close(rage128_t *dev)
{
    r128_census_t *c = (r128_census_t *) dev->census;
    FILE          *f;

    if (!c)
        return;
    dev->census = NULL;

    f = plat_fopen(c->path, "a");
    if (f) {
        fprintf(f, "# census states=%u overflow=%u draws=%llu\n", c->n_ent,
                c->overflow, (unsigned long long) c->draws);
        for (uint32_t i = 0; i < c->n_ent; i++)
            census_print(f, &c->ent[i]);
        fclose(f);
    }
    if (f)
        rage128_log("RAGE128 CENSUS: states=%u overflow=%u draws=%llu file=%s%s\n",
                    c->n_ent, c->overflow, (unsigned long long) c->draws, c->path,
                    f ? "" : " (OPEN FAILED)");
    else
        pclog("RAGE128 CENSUS: states=%u overflow=%u draws=%llu file=%s%s\n",
              c->n_ent, c->overflow, (unsigned long long) c->draws, c->path,
              f ? "" : " (OPEN FAILED)");
    free(c->ent);
    free(c);
}
