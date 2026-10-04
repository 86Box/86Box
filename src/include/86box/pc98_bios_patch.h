#ifndef EMU_PC98_BIOS_PATCH_H
#define EMU_PC98_BIOS_PATCH_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum pc98_bios_patch_result_t {
    PC98_BIOS_PATCH_UNSUPPORTED = 0,
    PC98_BIOS_PATCH_APPLIED,
    PC98_BIOS_PATCH_ALREADY_APPLIED
} pc98_bios_patch_result_t;

/* Patch only the supported VX's 32 KiB ITF buffer in memory. An unsupported
 * image, wrong length, or invalid pointer is left unchanged. The timer tests
 * still program the PIT; their fatal exits are bypassed. */
pc98_bios_patch_result_t pc98_bios_patch_vx_timer(uint8_t *itf, size_t size);

#ifdef __cplusplus
}
#endif

#endif
