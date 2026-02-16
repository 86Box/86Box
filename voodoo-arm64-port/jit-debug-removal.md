# JIT Debug Logging — Removal Guide

When the ARM64 JIT is stable and debug logging is no longer needed, remove it with these steps:

## Files to modify

### 1. `src/include/86box/vid_voodoo_common.h`
Remove from `voodoo_t`:
```c
int   jit_debug;
FILE *jit_debug_log;
```

### 2. `src/include/86box/vid_voodoo_codegen_arm64.h`
- Remove `static int voodoo_jit_hit_count` and `voodoo_jit_gen_count`
- Remove the two `if (voodoo->jit_debug ...)` blocks in `voodoo_get_block()`

### 3. `src/video/vid_voodoo_render.c`
- Remove `static int voodoo_jit_exec_count`
- Remove all three `if (voodoo->jit_debug ...)` blocks (pre-execute, post-execute, interpreter fallback)

### 4. `src/video/vid_voodoo.c`
- Remove the `jit_debug` config entry from `voodoo_config[]`
- Remove the `jit_debug` + `fopen` blocks from both init paths
- Remove the `fclose(voodoo->jit_debug_log)` block from `voodoo_card_close()`

### 5. `src/video/vid_voodoo_banshee.c`
- Remove the `jit_debug` config entry from `banshee_sgram_config[]`, `banshee_sgram_16mbonly_config[]`, and `banshee_sdram_config[]`

## Search pattern
`grep -rn 'jit_debug' src/` will find all remaining references.
