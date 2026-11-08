/* Protocol vectors evaluated from installed CINMST32 2.0.36.6152's
   instructions, plus reset/failure and documented parser control checks. */
#include "../../src/video/vid_ati_mach64_idct.c"

static unsigned failures;
#define CHECK(c) do { if (!(c)) { fprintf(stderr, "%d: %s\n", __LINE__, #c); failures++; } } while (0)
void pclog(const char *fmt, ...) { (void) fmt; }

int main(void)
{
    static const uint32_t vectors[][2] = {
        { 0x00000000u, 0x6711e308u }, { 0xffffffffu, 0xff9e9f65u },
        { 0x12345678u, 0x9f4ece9du }, { 0x9e3779b9u, 0x54abc694u },
        { 0x80000000u, 0x67b87225u }, { 0x01020304u, 0xe5e36fbeu }
    };
    mach64_t *m = calloc(1, sizeof(*m));
    uint32_t value, challenge;
    CHECK(m != NULL);
    if (!m) return 1;
    m->idct = mach64_idct_init();
    CHECK(m->idct != NULL);
    if (!m->idct) return 1;
    for (unsigned i = 0; i < sizeof(vectors)/sizeof(vectors[0]); i++)
        CHECK(idct_auth_hash(vectors[i][0]) == vectors[i][1]);
    CHECK(mach64_idct_auth_read(m, IDCT_AUTH_STATUS, &value) && value == 8);
    mach64_idct_auth_write(m, IDCT_AUTH_STATUS, 4);
    mach64_idct_auth_write(m, IDCT_AUTH_STATUS, 0);
    mach64_idct_auth_read(m, IDCT_AUTH_DATA, &challenge);
    CHECK(challenge == 0x54abc694u);
    mach64_idct_auth_write(m, IDCT_AUTH_DATA, idct_auth_hash(challenge));
    mach64_idct_auth_read(m, IDCT_AUTH_STATUS, &value);
    CHECK(value == 1);
    mach64_idct_auth_write(m, IDCT_AUTH_DATA, 0x12345678u);
    mach64_idct_auth_read(m, IDCT_AUTH_STATUS, &value);
    CHECK(value == 9);
    mach64_idct_auth_read(m, IDCT_AUTH_DATA, &value);
    CHECK(value == 0x9f4ece9du);
    mach64_idct_auth_read(m, IDCT_AUTH_STATUS, &value);
    CHECK(value == 0x1a);
    CHECK(m->idct->key == idct_auth_hash(0x9f4ece9du ^ (idct_auth_hash(challenge) & 0x9009000cu)));
    mach64_idct_auth_write(m, IDCT_AUTH_STATUS, 4);
    mach64_idct_auth_write(m, IDCT_AUTH_STATUS, 0);
    CHECK(m->idct->key == 0);
    mach64_idct_auth_read(m, IDCT_AUTH_DATA, &challenge);
    mach64_idct_auth_write(m, IDCT_AUTH_DATA, 0);
    mach64_idct_auth_read(m, IDCT_AUTH_STATUS, &value);
    CHECK(value == 0);
    mach64_idct_reset(m);
    CHECK(m->idct->auth_state == AUTH_RESET && m->idct->count == 0);

    /* Bit 17 suppresses a PIO launch; the obsolete bit 13 does not. */
    m->rp.gui_cntl = 1u << 17;
    mach64_idct_write(m, IDCT_RUNS, 0xffffffffu);
    CHECK(m->idct->count == 0);
    m->rp.gui_cntl = 1u << 13;
    mach64_idct_write(m, IDCT_RUNS, 0xffffffffu);
    CHECK(m->idct->count == 1);
    mach64_idct_reset(m);
    /* Parser mode reverses the level dwords. DC=80 gives residual 10. */
    m->rp.gui_cntl = 1u << 16;
    mach64_idct_write(m, IDCT_RUNS, 0x00ffffffu);
    mach64_idct_write(m, IDCT_LEVELS, 0);
    mach64_idct_write(m, IDCT_LEVELS, 80u << 16);
    CHECK(m->idct->count == 1 && m->idct->queue[0][0] == 10);
    mach64_idct_reset(m);
    /* The IDCT DMA lane is independent of the PIO launch inhibit. */
    m->rp.gui_cntl = 1u << 17;
    mach64_idct_bm_write(m, IDCT_RUNS, 0xffffffffu);
    CHECK(m->idct->count == 1);
    mach64_idct_reset(m);
    mach64_idct_bm_write(m, IDCT_FIRST + 0x3c, 0x420);
    m->idct->auth_state = AUTH_ARMED;
    m->idct->key = 0x12345678u;
    m->rp.gui_cntl = 0;
    mach64_idct_bm_write(m, IDCT_RUNS, 0x00ffffffu ^ (0x12345678u & 0x7f7f7f7fu));
    CHECK(m->idct->runs == 0x00ffffffu && m->idct->key == idct_key_step(0x12345678u));
    mach64_idct_bm_write(m, IDCT_LEVELS, 80u << 16);
    mach64_idct_bm_write(m, IDCT_LEVELS, 0);
    CHECK(m->idct->count == 1 && m->idct->queue[0][0] == 10);
    /* Inter blocks carry clear runs, but advance the session key at EOB. */
    mach64_idct_bm_write(m, 0x3bc, 0);
    uint32_t saved_key = m->idct->key;
    mach64_idct_bm_write(m, IDCT_RUNS, 0x00003f3f);
    CHECK(m->idct->runs == 0x00003f3f && m->idct->key == saved_key);
    mach64_idct_end_block(m);
    CHECK(m->idct->key == idct_key_step(saved_key));
    mach64_idct_close(m->idct);
    free(m);
    printf("Mach64 XL IDCT/authentication: %u failures\n", failures);
    return failures != 0;
}
