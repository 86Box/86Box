/* Optional, version-checked PC-9801VX ITF timer POST bypass.
 * This changes a loaded buffer only; no firmware file is written. */
#include <stddef.h>
#include <stdint.h>
#include <string.h>
#include <86box/pc98_bios_patch.h>

#define VX_ITF_SIZE          0x8000u
#define VX_ITF_ORIGINAL_HASH UINT64_C(0x40ec270e008f53e9)
#define VX_ITF_PATCHED_HASH  UINT64_C(0xc9958255f5a6e707)

static uint64_t
itf_fingerprint(const uint8_t *itf)
{
    /* FNV-1a identifies the supported complete firmware revision. It is a
     * version check, not an authentication or security mechanism. */
    uint64_t hash = UINT64_C(0xcbf29ce484222325);
    for (size_t i = 0; i < VX_ITF_SIZE; i++)
        hash = (hash ^ itf[i]) * UINT64_C(0x100000001b3);
    return hash;
}

pc98_bios_patch_result_t
pc98_bios_patch_vx_timer(uint8_t *itf, size_t size)
{
    static const size_t  branches[]          = { 0x0900, 0x096c, 0x0980, 0x09ac };
    static const uint8_t original_branches[] = { 0x75, 0x74, 0x74, 0x74 };
    static const uint8_t timeout_original[]  = {
        0xbe, 0xcc, 0x17, 0xbc, 0xdd, 0x09, 0xe9, 0x86, 0x0e, 0xf4
    };
    static const uint8_t timeout_replacement[] = {
        /* MOV AL,FF; OUT 02,AL; JMP 09DE; four NOPs.
         * A successful IRQ0 handler masks the master PIC before returning.
         * Do the same when its bounded 32-iteration wait times out, then
         * retain the firmware's final channel-zero reload at 09DE. */
        0xb0, 0xff, 0xe6, 0x02, 0xeb, 0x04, 0x90, 0x90, 0x90, 0x90
    };

    if (!itf || size != VX_ITF_SIZE)
        return PC98_BIOS_PATCH_UNSUPPORTED;

    const uint64_t hash = itf_fingerprint(itf);
    if (hash == VX_ITF_PATCHED_HASH)
        return PC98_BIOS_PATCH_ALREADY_APPLIED;
    if (hash != VX_ITF_ORIGINAL_HASH)
        return PC98_BIOS_PATCH_UNSUPPORTED;

    /* Check every patch site before the first mutation. The full fingerprint
     * also validates the zero-filled padding used for checksum compensation. */
    for (size_t i = 0; i < sizeof(branches) / sizeof(branches[0]); i++) {
        if (itf[branches[i]] != original_branches[i] || itf[branches[i] + 1] != 0x0a)
            return PC98_BIOS_PATCH_UNSUPPORTED;
    }
    if (memcmp(itf + 0x09d4, timeout_original, sizeof(timeout_original)) || itf[0x7fec] || itf[0x7fed])
        return PC98_BIOS_PATCH_UNSUPPORTED;

    for (size_t i = 0; i < sizeof(branches) / sizeof(branches[0]); i++)
        itf[branches[i]] = 0xeb;
    memcpy(itf + 0x09d4, timeout_replacement, sizeof(timeout_replacement));

    /* ITF 04BC sums even and odd ROM lanes independently into DL and DH.
     * Both must remain zero or its checksum self-test hangs at 04D2. */
    uint8_t lane_sum[2] = { 0, 0 };
    for (size_t i = 0; i < VX_ITF_SIZE; i++)
        lane_sum[i & 1u] += itf[i];
    itf[0x7fec] = (uint8_t) -lane_sum[0];
    itf[0x7fed] = (uint8_t) -lane_sum[1];

    return PC98_BIOS_PATCH_APPLIED;
}
