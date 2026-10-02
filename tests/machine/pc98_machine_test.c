/* Production CPU eligibility, actual profiles and firmware loader. Synthetic
   bytes distinguish firmware banks; no proprietary firmware is distributed. */
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <86box/86box.h>
#include "cpu.h"
#include <86box/mem.h>
#include <86box/device.h>
#include <86box/machine.h>
#include <86box/pc98_rom.h>
#include <86box/rom.h>
#include <86box/vid_pc98.h>

const device_t fdc_pc98_device = {0};
const device_t pc9801_video_device = {0};
int machine_pc9801vx_init(const machine_t *m) { (void)m; return 0; }
int machine_at_nupro592_init(const machine_t *m) { (void)m; return 0; }
int machine_at_cobalt3k_init(const machine_t *m) { (void)m; return 0; }
int machine_at_ibm_pc700_init(const machine_t *m) { (void)m; return 0; }
#include "pc98_profiles.inc"

static unsigned checks, failures;
#define CHECK(expr) do { checks++; if (!(expr)) { \
    fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #expr); failures++; } } while (0)

typedef struct fixture_t {
    const char *path;
    size_t size;
    unsigned seed;
    int present;
} fixture_t;
static fixture_t files[] = {
    {"roms/machines/pc9801/ipl.rom", 0x30000, 1, 0},
    {"roms/machines/pc9801/itf.rom", 0x8000, 2, 0},
    {"roms/machines/pc9801/bios.rom", 0x18000, 3, 0},
    {"roms/machines/pc9801/nec_d27c256d-15_cpu_extboard_yll01.bin", 0x8000, 7, 0},
    {"roms/machines/pc9801/nec_d27c256d-15_cpu_extboard_yll02.bin", 0x8000, 8, 0},
    {"roms/machines/pc9801/nec_d27c256d-15_cpu_extboard_yll03.bin", 0x8000, 9, 0},
    {"roms/machines/pc9801/nec_d27c256d-15_cpu_extboard_yll04.bin", 0x8000, 10, 0}
};
static uint8_t pattern(unsigned seed, unsigned pos)
{
    return (uint8_t)(seed * 19u + pos * 7u + (pos >> 8));
}
FILE *rom_fopen(const char *path, char *mode)
{
    (void)mode;
    for (unsigned n = 0; n < sizeof(files) / sizeof(files[0]); n++) {
        fixture_t *f = &files[n];
        if (!f->present || strcmp(path, f->path)) continue;
        FILE *fp = tmpfile();
        if (!fp) abort();
        for (unsigned pos = 0; pos < f->size; pos++)
            fputc(pattern(f->seed, pos), fp);
        rewind(fp);
        return fp;
    }
    return NULL;
}
int rom_present(const char *p)
{
    FILE *f = rom_fopen(p, "rb");
    if (f) fclose(f);
    return f != NULL;
}
void (pclog)(const char *fmt, ...) { (void)fmt; }

static void test_cpus(void)
{
    const cpu_family_t *i286 = cpu_get_family("286");
    const cpu_family_t *sx = cpu_get_family("i386sx");
    const cpu_family_t *dx = cpu_get_family("i386dx");
    CHECK(i286 && sx && dx);
    if (!i286 || !sx || !dx) return;
    CHECK(machines[0].type == MACHINE_TYPE_286);
    CHECK(machines[0].ram.min == 640 && machines[0].ram.max == 8832);
    for (cpu_override = 0; cpu_override <= 2; cpu_override++) {
        for (int c = 0; i286->cpus[c].cpu_type; c++) {
            int speed = i286->cpus[c].rspeed;
            CHECK(cpu_is_eligible(i286, c, 0) == (speed == 8000000 || speed == 10000000));
        }
        for (int c = 0; sx->cpus[c].cpu_type; c++)
            CHECK(!cpu_is_eligible(sx, c, 0));
        for (int c = 0; dx->cpus[c].cpu_type; c++)
            CHECK(!cpu_is_eligible(dx, c, 0));
        CHECK(!cpu_family_is_eligible(sx, 0));
        CHECK(!cpu_family_is_eligible(dx, 0));
        CHECK(cpu_family_is_eligible(i286, 0));
        /* Generic PC override behavior is retained. */
        CHECK(cpu_is_eligible(dx, 0, 1) == (cpu_override == 2));
        CHECK(cpu_is_eligible(i286, 0, 1) == (cpu_override != 0));
        /* A non-286 CPU cannot evade the type check through a matching package. */
        CPU upgrade = i286->cpus[0];
        upgrade.cpu_type = CPU_386SX;
        upgrade.rspeed = 8000000;
        cpu_family_t fake = *i286;
        fake.cpus = &upgrade;
        CHECK(!cpu_is_eligible(&fake, 0, 0));
    }
    cpu_override = 0;
}

static void test_roms(void)
{
    CHECK(!pc98_ipl_available());
    CHECK(pc98_ipl_load() == NULL);
    files[1].present = 1;
    CHECK(!pc98_ipl_available()); /* A partial split set is insufficient. */
    CHECK(pc98_ipl_load() == NULL);
    files[2].present = 1;
    CHECK(pc98_ipl_available());
    uint8_t *ipl = pc98_ipl_load();
    CHECK(ipl != NULL);
    if (ipl) {
        CHECK(ipl[0] == 0xff && ipl[0xffff] == 0xff);
        CHECK(ipl[0x10000] == pattern(2, 0));
        CHECK(ipl[0x17ff0] == pattern(2, 0x7ff0));
        CHECK(ipl[0x18000] == pattern(3, 0));
        CHECK(ipl[0x2ffff] == pattern(3, 0x17fff));
        free(ipl);
    }
    files[1].size--; /* Reject truncated ITF. */
    CHECK(!pc98_ipl_available());
    CHECK(pc98_ipl_load() == NULL);
    files[1].size++;
    files[2].size++; /* Reject oversized BIOS too. */
    CHECK(!pc98_ipl_available());
    CHECK(pc98_ipl_load() == NULL);
    files[2].size--;
    files[0].present = 1;
    ipl = pc98_ipl_load();
    CHECK(ipl && ipl[0x17ff0] == pattern(1, 0x17ff0)); /* Normalized preferred. */
    free(ipl);
    files[0].size--;
    ipl = pc98_ipl_load();
    CHECK(ipl && ipl[0x17ff0] == pattern(2, 0x7ff0)); /* Valid split fallback. */
    free(ipl);
    files[0].present = files[1].present = files[2].present = 0;
    for (unsigned n = 3; n < 7; n++) files[n].present = 1;
    CHECK(pc98_ipl_available());
    ipl = pc98_ipl_load();
    CHECK(ipl != NULL);
    if (ipl) {
        CHECK(ipl[0x10000] == pattern(8, 0x4000));
        CHECK(ipl[0x10001] == pattern(10, 0x4000));
        CHECK(ipl[0x18000] == pattern(7, 0x4000));
        CHECK(ipl[0x20000] == pattern(7, 0));
        CHECK(ipl[0x28000] == pattern(8, 0));
        free(ipl);
    }
    files[4].size--;
    CHECK(!pc98_ipl_available());
    CHECK(pc98_ipl_load() == NULL);
    const uint32_t bases[] = {0xe8000u, 0xfe8000u};
    for (unsigned n = 0; n < 2; n++) {
        CHECK(pc98_ipl_window_offset(bases[n]) == 0);
        CHECK(pc98_ipl_window_offset(bases[n] + 0x17ff0u) == 0x17ff0u);
        CHECK(pc98_ipl_window_offset(bases[n] + 0x17fffu) == 0x17fffu);
        CHECK(pc98_ipl_window_offset(bases[n] - 1u) == UINT32_MAX);
    }
    CHECK(pc98_ipl_window_offset(0x100000u) == UINT32_MAX);
    CHECK(pc98_ipl_window_offset(0x1000000u) == UINT32_MAX);
    CHECK(pc98_ipl_window_offset(0xfffffff0u) == UINT32_MAX);
}

int main(void)
{
    test_cpus();
    test_roms();
    printf("PC-98 machine: %u checks, %u failures\n", checks, failures);
    return failures ? 1 : 0;
}
