/*
 * 86Box       Host services for the CompactFlash / IDE integration test.
 *
 *             Released under the GNU General Public License version 2 or
 *             later. See COPYING for more information.
 */
#include <stdarg.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <wchar.h>
#define HAVE_STDARG_H
#include <86box/86box.h>
#include "cpu.h"
#include <86box/device.h>
#include <86box/io.h>
#include <86box/isapnp.h>
#include <86box/machine.h>
#include <86box/mca.h>
#include <86box/mem.h>
#include <86box/rom.h>
#include <86box/pic.h>
#include <86box/timer.h>
#include <86box/hdd.h>
#include <86box/hdc.h>
#include <86box/cdrom.h>
#include <86box/scsi_device.h>
#include <86box/rdisk.h>
#include <86box/lpt.h>
#include <86box/sound.h>
#include <86box/thread.h>
#include <86box/ui.h>
#include <86box/plat_unused.h>

int machine;
const machine_t machines[1];
cpu_state_t cpu_state;
double cpuclock;
hard_disk_t hdd[HDD_SLOTS];
int hdc_current[HDC_MAX];
int sound_card_current[SOUND_CARD_MAX];
cdrom_t cdrom[CDROM_NUM];
rdisk_drive_t rdisk_drives[RDISK_NUM];
lpt_port_t lpt_ports[PARALLEL_MAX];

static pc_timer_t *pending_timer;
static uint8_t fixed_sector[512];

void
ide_test_run_timer(void)
{
    pc_timer_t *timer = pending_timer;
    if (!timer)
        abort();
    pending_timer = NULL;
    timer->callback(timer->priv);
}

void
timer_add(pc_timer_t *timer, void (*callback)(void *), void *priv, UNUSED(int start))
{
    timer->callback = callback;
    timer->priv = priv;
}

void timer_on_auto(pc_timer_t *timer, UNUSED(double period)) { pending_timer = timer; }
void timer_stop(pc_timer_t *timer) { if (pending_timer == timer) pending_timer = NULL; }

int hdd_image_load(int id) { return id == 0; }
void hdd_image_close(uint8_t id) { if (id != 0) abort(); }
uint32_t hdd_image_get_last_sector(uint8_t id) { if (id != 0) abort(); return 15; }

int
hdd_image_read(uint8_t id, uint32_t sector, uint32_t count, uint8_t *buffer)
{
    if (id != 0 || sector != 0 || count != 1)
        abort();
    memcpy(buffer, fixed_sector, sizeof(fixed_sector));
    return 0;
}

int
hdd_image_write(uint8_t id, uint32_t sector, uint32_t count, uint8_t *buffer)
{
    if (id != 0 || sector != 0 || count != 1)
        abort();
    memcpy(fixed_sector, buffer, sizeof(fixed_sector));
    return 0;
}

int hdd_image_zero(UNUSED(uint8_t id), UNUSED(uint32_t sector), UNUSED(uint32_t count)) { abort(); }
void hdd_preset_apply(UNUSED(int id)) { }
double hdd_timing_read(UNUSED(hard_disk_t *disk), UNUSED(uint32_t addr), UNUSED(uint32_t len)) { return 0; }
double hdd_timing_write(UNUSED(hard_disk_t *disk), UNUSED(uint32_t addr), UNUSED(uint32_t len)) { return 0; }
double hdd_seek_get_time(UNUSED(hard_disk_t *disk), UNUSED(uint32_t addr), UNUSED(uint8_t op),
                         UNUSED(uint8_t continuous), UNUSED(double max_time)) { return 0; }

int rdisk_ata_media_present(UNUSED(int slot)) { abort(); }
int rdisk_ata_write_protected(UNUSED(int slot)) { abort(); }
int rdisk_ata_take_media_change(UNUSED(int slot)) { abort(); }
void rdisk_ata_lock(UNUSED(int slot), UNUSED(int lock)) { abort(); }
int rdisk_ata_eject(UNUSED(int slot)) { abort(); }
int rdisk_ata_soft_protect(UNUSED(int slot), UNUSED(uint8_t subcommand)) { abort(); }

event_t *thread_create_event(void) { return NULL; }
thread_t *thread_create_named(UNUSED(void (*func)(void *)), UNUSED(void *priv), UNUSED(const char *name)) { return NULL; }
int thread_wait(UNUSED(thread_t *thread)) { abort(); }
int thread_wait_event(UNUSED(event_t *event), UNUSED(int timeout)) { abort(); }
void thread_reset_event(UNUSED(event_t *event)) { abort(); }
void thread_set_event(UNUSED(event_t *event)) { abort(); }
void thread_destroy_event(UNUSED(event_t *event)) { abort(); }

void ui_sb_update_icon(UNUSED(int tag), UNUSED(int active)) { }
void ui_sb_update_icon_write(UNUSED(int tag), UNUSED(int write)) { }
void picint_common(UNUSED(uint16_t mask), UNUSED(int level), UNUSED(int set), UNUSED(uint8_t *irq_state)) { abort(); }

void
io_handler(UNUSED(uint8_t set), UNUSED(uint16_t base), UNUSED(uint16_t size),
           UNUSED(uint8_t (*readb)(uint16_t, void *)), UNUSED(uint16_t (*readw)(uint16_t, void *)),
           UNUSED(uint32_t (*readl)(uint16_t, void *)), UNUSED(void (*writeb)(uint16_t, uint8_t, void *)),
           UNUSED(void (*writew)(uint16_t, uint16_t, void *)), UNUSED(void (*writel)(uint16_t, uint32_t, void *)),
           UNUSED(void *priv))
{
    abort();
}

void io_set_turbo(UNUSED(uint16_t port), UNUSED(int turbo)) { abort(); }
void *device_add_params(UNUSED(const device_t *dev), UNUSED(void *params)) { abort(); }
int device_get_config_int(UNUSED(const char *name)) { abort(); }
void device_context_inst(UNUSED(const device_t *dev), UNUSED(int inst)) { abort(); }
void device_context_restore(void) { abort(); }
const device_t *sound_card_getdevice(UNUSED(int card)) { abort(); }
const device_t *hdc_get_device(UNUSED(int card)) { abort(); }
int hdd_is_valid(UNUSED(int id)) { abort(); }
const device_t *hdd_lpt_bridge_device(UNUSED(int bridge)) { abort(); }
int machine_at_lgibmx61_init(UNUSED(const machine_t *model)) { abort(); }
void fatal(UNUSED(const char *fmt), ...) { abort(); }
void warning(UNUSED(const char *fmt), ...) { abort(); }

void *
isapnp_add_card(UNUSED(uint8_t *rom), UNUSED(uint16_t size),
                UNUSED(void (*config)(uint8_t, isapnp_device_config_t *, void *)),
                UNUSED(void (*csn)(uint8_t, void *)), UNUSED(uint8_t (*read)(uint8_t, uint8_t, void *)),
                UNUSED(void (*write)(uint8_t, uint8_t, uint8_t, void *)), UNUSED(void *priv))
{
    abort();
}

uint8_t
mca_add(UNUSED(uint8_t (*read)(uint16_t, void *)), UNUSED(void (*write)(uint16_t, uint8_t, void *)),
        UNUSED(uint8_t (*feedb)(void *)), UNUSED(void (*reset)(void *)), UNUSED(void *priv))
{
    abort();
}

int rom_present(UNUSED(const char *fn)) { abort(); }

int
rom_init(UNUSED(rom_t *rom), UNUSED(const char *fn), UNUSED(uint32_t addr), UNUSED(int size),
         UNUSED(int mask), UNUSED(int offset), UNUSED(uint32_t flags))
{
    abort();
}

void mem_mapping_disable(UNUSED(mem_mapping_t *map)) { abort(); }
void mem_mapping_set_addr(UNUSED(mem_mapping_t *map), UNUSED(uint32_t base), UNUSED(uint32_t size)) { abort(); }
