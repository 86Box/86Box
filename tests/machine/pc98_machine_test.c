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
#include <86box/ini.h>
#include <86box/config.h>
#include <86box/machine.h>
#include <86box/pc98_rom.h>
#include <86box/rom.h>
#include <86box/vid_pc98.h>

const device_t fdc_pc98_device = {0};
const device_t pc9801_video_device = {0};
const device_t pc9801bx4_video_device = {0};
int machine_pc9801vx_init(const machine_t *m) { (void)m; return 0; }
int machine_pc9801bx4_init(const machine_t *m) { (void)m; return 0; }
int machine_at_nupro592_init(const machine_t *m) { (void)m; return 0; }
int machine_at_cobalt3k_init(const machine_t *m) { (void)m; return 0; }
int machine_at_ibm_pc700_init(const machine_t *m) { (void)m; return 0; }
int machine_at_vect486n_init(const machine_t *m) { (void)m; return 0; }
#include "pc98_config.inc"
#include "pc98_profiles.inc"
static ini_t test_ini;
int machine;
ini_t config_get_ini(void) { return test_ini; }
const device_t *machine_get_device(int m) { return machines[m].device; }

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
    {"roms/machines/pc9801/nec_d27c256d-15_cpu_extboard_yll04.bin", 0x8000, 10, 0},
    {"roms/machines/pc9801bx4/pc98BANK0.BIN", 0x8000, 11, 0},
    {"roms/machines/pc9801bx4/pc98BANK1.BIN", 0x8000, 12, 0},
    {"roms/machines/pc9801bx4/pc98BANK2.BIN", 0x8000, 13, 0},
    {"roms/machines/pc9801bx4/pc98BANK3.BIN", 0x8000, 14, 0},
    {"roms/machines/pc9801bx4/pc98BANK4.BIN", 0x8000, 15, 0},
    {"roms/machines/pc9801bx4/pc98BANK5.BIN", 0x8000, 16, 0},
    {"roms/machines/pc9801bx4/pc98BANK6.BIN", 0x8000, 17, 0},
    {"roms/machines/pc9801bx4/pc98BANK7.BIN", 0x8000, 18, 0}
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
    const cpu_family_t *am_sx = cpu_get_family("am386sx");
    const cpu_family_t *am_dx = cpu_get_family("am386dx");
    CHECK(i286 && sx && dx && am_sx && am_dx);
    if (!i286 || !sx || !dx || !am_sx || !am_dx) return;
    CHECK(machines[0].type == MACHINE_TYPE_286);
    CHECK(machines[0].flags & MACHINE_MOUSE);
    CHECK(machines[0].ram.min == 640 && machines[0].ram.max == 32768);
    CHECK(machines[0].cpu.max_bus == 33333333);
    for (cpu_override = 0; cpu_override <= 2; cpu_override++) {
        /* Check every catalogue entry, including CPUs with the same core type
           or package as a 386 but a different family (e.g. the M6117). */
        for (const cpu_family_t *f = cpu_families; f->package; f++) {
            int family_allowed = 0;
            for (int c = 0; f->cpus[c].cpu_type; c++) {
                const CPU *p = &f->cpus[c];
                int standard = f->package == CPU_PKG_286 && p->cpu_type == CPU_286 &&
                    (p->rspeed == 8000000 || p->rspeed == 10000000);
                int supported_family = f == sx || f == dx || f == am_sx || f == am_dx;
                int supported_speed = p->rspeed == 16000000 || p->rspeed == 20000000 ||
                    p->rspeed == 25000000 || p->rspeed == 33333333;
                int allowed = standard || (supported_family && supported_speed);
                CHECK(cpu_is_eligible(f, c, 0) == allowed);
                family_allowed |= allowed;
            }
            CHECK(cpu_family_is_eligible(f, 0) == family_allowed);
        }
        /* Generic PC override behavior is retained. */
        CHECK(cpu_is_eligible(dx, 0, 2) == (cpu_override == 2));
        CHECK(cpu_is_eligible(i286, 0, 2) == (cpu_override != 0));

        CPU probe = dx->cpus[0];
        cpu_family_t fake = *dx;
        fake.cpus = &probe;
        probe.rspeed = 33333333;
        CHECK(cpu_is_eligible(&fake, 0, 0));
        probe.rspeed++; /* No tolerance may extend the hard 33 MHz limit. */
        CHECK(!cpu_is_eligible(&fake, 0, 0));
        probe.rspeed = 10000000;
        CHECK(!cpu_is_eligible(&fake, 0, 0));
        probe.rspeed = 33333333;
        probe.multi = 2.0;
        CHECK(!cpu_is_eligible(&fake, 0, 0));
        probe.multi = 1.0;
        probe.cpu_type = CPU_486DLC;
        CHECK(!cpu_is_eligible(&fake, 0, 0));
        probe.cpu_type = CPU_386SX; /* Family/type mismatch. */
        CHECK(!cpu_is_eligible(&fake, 0, 0));
        probe.cpu_type = CPU_386DX;
        cpu_family_t wrong_package = {.package = CPU_PKG_386DX_DESKPRO386,
            .internal_name = dx->internal_name, .cpus = &probe};
        CHECK(!cpu_is_eligible(&wrong_package, 0, 0));
        fake.internal_name = "unlisted386";
        CHECK(!cpu_is_eligible(&fake, 0, 0));
        cpu_family_t fast286 = *i286;
        fast286.cpus = &probe;
        probe.cpu_type = CPU_286;
        probe.rspeed = 12000000;
        CHECK(!cpu_is_eligible(&fast286, 0, 0));
    }
    cpu_override = 0;
}

static void test_bx4_cpus(void)
{
    CHECK(machines[1].init == machine_pc9801bx4_init);
    CHECK(machines[1].vid_device == &pc9801bx4_video_device);
    CHECK(machines[1].flags & MACHINE_MOUSE);
    CHECK(machines[1].flags & MACHINE_LPT_PRI);
    CHECK(machines[1].ram.min == 2048 && machines[1].ram.max == 65536);
    for (cpu_override = 0; cpu_override <= 2; cpu_override++) {
        for (const cpu_family_t *f = cpu_families; f->package; f++) {
            int family_allowed = 0;
            for (unsigned c = 0; f->cpus[c].cpu_type; c++) {
                const CPU *p = &f->cpus[c];
                const int dx2 = (!strcmp(f->internal_name, "i486dx2") ||
                                  !strcmp(f->internal_name, "i486dx2_slenh")) &&
                    p->rspeed == 66666666 && p->multi == 2.0;
                const int dx4 = !strcmp(f->internal_name, "idx4") &&
                    p->rspeed == 100000000 && p->multi == 3.0;
                CHECK(cpu_is_eligible(f, c, 1) == (dx2 || dx4));
                family_allowed |= dx2 || dx4;
            }
            CHECK(cpu_family_is_eligible(f, 1) == family_allowed);
        }
    }
    cpu_override = 0;
}

static void test_cpu_upgrades(void)
{
    CHECK(machines[0].device == &pc9801vx_device);
    CHECK(pc9801vx_device.init == NULL);
    const device_config_t *cfg = pc9801vx_device.config;
    CHECK(cfg && cfg->type == CONFIG_BINARY && cfg->default_int == 0);
    CHECK(!strcmp(cfg->name, "skip_timer_checks"));
    CHECK(!strcmp(cfg->description, "Skip BIOS timer checks (CPU upgrades)"));
    CHECK(cfg[1].type == CONFIG_END);
    const char *families[] = {"i486sx", "i486dx", "i486dx2", "idx4",
                             "am486sx", "am486dx", "am486dx2", "am486dx4"};
    for (int enabled = 0; enabled <= 1; enabled++) {
        config_set_int(pc9801vx_device.name, "skip_timer_checks", enabled);
        /* The running machine can be BX4 while VX is selected in Settings. */
        machine = 1;
        for (cpu_override = 0; cpu_override <= 2; cpu_override++) {
            for (const cpu_family_t *f = cpu_families; f->package; f++) {
                int family_allowed = 0;
                for (int c = 0; f->cpus[c].cpu_type; c++) {
                    const CPU *p = &f->cpus[c];
                    const int standard = p->cpu_type == CPU_286 &&
                        (p->rspeed == 8000000 || p->rspeed == 10000000);
                    const int plain386 = !strcmp(f->internal_name, "i386sx") ||
                        !strcmp(f->internal_name, "i386dx") || !strcmp(f->internal_name, "am386sx") ||
                        !strcmp(f->internal_name, "am386dx");
                    int plain486 = 0;
                    for (unsigned n = 0; n < sizeof(families) / sizeof(families[0]); n++)
                        plain486 |= !strcmp(f->internal_name, families[n]);
                    const int allowed = standard ||
                        (plain386 && p->rspeed >= 16000000 && p->rspeed <= (enabled ? 40000000 : 33333333)) ||
                        (enabled && plain486 && p->rspeed <= 100000000);
                    CHECK(cpu_is_eligible(f, c, 0) == allowed);
                    family_allowed |= allowed;
                }
                CHECK(cpu_family_is_eligible(f, 0) == family_allowed);
            }
        }
    }
    const cpu_family_t *dx = cpu_get_family("i386dx");
    const cpu_family_t *dx4 = cpu_get_family("idx4");
    CPU probe = dx->cpus[0];
    cpu_family_t fake = *dx;
    fake.cpus = &probe;
    probe.rspeed = 40000000;
    CHECK(cpu_is_eligible(&fake, 0, 0));
    probe.rspeed++;
    CHECK(!cpu_is_eligible(&fake, 0, 0));
    cpu_family_t fake4 = *dx4;
    fake4.cpus = &probe;
    probe = dx4->cpus[0];
    probe.rspeed = 100000000;
    CHECK(cpu_is_eligible(&fake4, 0, 0));
    probe.rspeed++;
    CHECK(!cpu_is_eligible(&fake4, 0, 0));
    probe.rspeed = 100000000;
    probe.multi = 2;
    CHECK(!cpu_is_eligible(&fake4, 0, 0));
    probe.multi = 3;
    probe.cpu_type = CPU_PENTIUM;
    CHECK(!cpu_is_eligible(&fake4, 0, 0));
    /* Switching it off removes upgrades immediately without restarting. */
    config_set_int(pc9801vx_device.name, "skip_timer_checks", 0);
    CHECK(!cpu_family_is_eligible(cpu_get_family("idx4"), 0));
    machine = cpu_override = 0;
}

static void test_bx4_roms(void)
{
    /* A present VX firmware set must never make BX4 available. */
    CHECK(!pc98_ipl_available_for_model(PC98_MODEL_BX4));
    CHECK(pc98_ipl_load_for_model(PC98_MODEL_BX4) == NULL);
    for (unsigned n = 7; n < 14; n++) files[n].present = 1;
    CHECK(!pc98_ipl_available_for_model(PC98_MODEL_BX4));
    files[14].present = 1;
    CHECK(pc98_ipl_available_for_model(PC98_MODEL_BX4));
    uint8_t *ipl = pc98_ipl_load_for_model(PC98_MODEL_BX4);
    CHECK(ipl != NULL);
    if (ipl) {
        CHECK(ipl[0] == 0xff && ipl[0xffff] == 0xff);
        CHECK(ipl[0x10000] == pattern(15, 0));
        CHECK(ipl[0x17ff0] == pattern(15, 0x7ff0));
        CHECK(ipl[0x18000] == pattern(16, 0));
        CHECK(ipl[0x20000] == pattern(17, 0));
        CHECK(ipl[0x28000] == pattern(18, 0));
        CHECK(ipl[0x2ffff] == pattern(18, 0x7fff));
        free(ipl);
    }
    for (unsigned n = 7; n < 15; n++) {
        files[n].size--;
        CHECK(!pc98_ipl_available_for_model(PC98_MODEL_BX4));
        CHECK(pc98_ipl_load_for_model(PC98_MODEL_BX4) == NULL);
        files[n].size += 2;
        CHECK(!pc98_ipl_available_for_model(PC98_MODEL_BX4));
        CHECK(pc98_bx4_banks_load() == NULL);
        files[n].size--;
    }
    uint8_t *banks = pc98_bx4_banks_load();
    CHECK(banks != NULL);
    if (banks) {
        for (unsigned n = 0; n < 8; n++) {
            CHECK(banks[n * 0x8000] == pattern(11 + n, 0));
            CHECK(banks[n * 0x8000 + 0x7fff] == pattern(11 + n, 0x7fff));
        }
        free(banks);
    }
    CHECK(pc98_ipl_window_offset_for_model(0xfffffff0u, PC98_MODEL_BX4) == 0x17ff0u);
    CHECK(pc98_ipl_window_offset_for_model(0xfffe8000u, PC98_MODEL_BX4) == 0);
    CHECK(pc98_ipl_window_offset_for_model(0xfffe7fffu, PC98_MODEL_BX4) == UINT32_MAX);
    CHECK(pc98_ipl_window_offset_for_model(0xfffffff0u, PC98_MODEL_VX) == UINT32_MAX);
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
    test_ini = ini_new();
    test_cpus();
    test_cpu_upgrades();
    test_bx4_cpus();
    test_roms();
    test_bx4_roms();
    ini_close(test_ini);
    printf("PC-98 machine: %u checks, %u failures\n", checks, failures);
    return failures ? 1 : 0;
}
