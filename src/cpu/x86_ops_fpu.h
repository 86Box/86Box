/* Copyright holders: Sarah Walker
   see COPYING for more details
*/
static int
opESCAPE_d8_a16(uint32_t fetchdat)
{
    return x86_opcodes_d8_a16[(fetchdat >> 3) & 0x1f](fetchdat);
}
static int
opESCAPE_d8_a32(uint32_t fetchdat)
{
    return x86_opcodes_d8_a32[(fetchdat >> 3) & 0x1f](fetchdat);
}

static int
opESCAPE_d9_a16(uint32_t fetchdat)
{
    return x86_opcodes_d9_a16[fetchdat & 0xff](fetchdat);
}
static int
opESCAPE_d9_a32(uint32_t fetchdat)
{
    return x86_opcodes_d9_a32[fetchdat & 0xff](fetchdat);
}

static int
opESCAPE_da_a16(uint32_t fetchdat)
{
    return x86_opcodes_da_a16[fetchdat & 0xff](fetchdat);
}
static int
opESCAPE_da_a32(uint32_t fetchdat)
{
    return x86_opcodes_da_a32[fetchdat & 0xff](fetchdat);
}

static int
opESCAPE_db_a16(uint32_t fetchdat)
{
    return x86_opcodes_db_a16[fetchdat & 0xff](fetchdat);
}
static int
opESCAPE_db_a32(uint32_t fetchdat)
{
    return x86_opcodes_db_a32[fetchdat & 0xff](fetchdat);
}

static int
opESCAPE_dc_a16(uint32_t fetchdat)
{
    return x86_opcodes_dc_a16[(fetchdat >> 3) & 0x1f](fetchdat);
}
static int
opESCAPE_dc_a32(uint32_t fetchdat)
{
    return x86_opcodes_dc_a32[(fetchdat >> 3) & 0x1f](fetchdat);
}

static int
opESCAPE_dd_a16(uint32_t fetchdat)
{
    return x86_opcodes_dd_a16[fetchdat & 0xff](fetchdat);
}
static int
opESCAPE_dd_a32(uint32_t fetchdat)
{
    return x86_opcodes_dd_a32[fetchdat & 0xff](fetchdat);
}

static int
opESCAPE_de_a16(uint32_t fetchdat)
{
    return x86_opcodes_de_a16[fetchdat & 0xff](fetchdat);
}
static int
opESCAPE_de_a32(uint32_t fetchdat)
{
    return x86_opcodes_de_a32[fetchdat & 0xff](fetchdat);
}

static int
opESCAPE_df_a16(uint32_t fetchdat)
{
    return x86_opcodes_df_a16[fetchdat & 0xff](fetchdat);
}
static int
opESCAPE_df_a32(uint32_t fetchdat)
{
    return x86_opcodes_df_a32[fetchdat & 0xff](fetchdat);
}

static int
opWAIT(uint32_t fetchdat)
{
    if ((cr0 & 0xa) == 0xa) {
        x86_int(7);
        return 1;
    }

    // if (!cpu_use_dynarec && fpu_softfloat) {
    if (fpu_softfloat) {
        if (fpu_state.swd & FPU_SW_Summary) {
            if (cr0 & 0x20) {
                x86_int(16);
                return 1;
            }
        }
    }
    CLOCK_CYCLES(4);
    return 0;
}
