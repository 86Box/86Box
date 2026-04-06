/* SPDX-License-Identifier: GPL-2.0-or-later */
static int
opUCOMISS_xmm_xmm_a16(uint32_t fetchdat)
{
    flags_rebuild();
    fetch_ea_16(fetchdat);
    cpu_state.flags &= ~(V_FLAG | A_FLAG | N_FLAG);
    if (cpu_mod == 3) {
        if (isunordered(XMM[cpu_reg].f[0], XMM[cpu_rm].f[0])) {
            cpu_state.flags |= Z_FLAG | P_FLAG | C_FLAG;
        } else if (XMM[cpu_reg].f[0] > XMM[cpu_rm].f[0]) {
            cpu_state.flags &= ~(Z_FLAG | P_FLAG | C_FLAG);
        } else if (XMM[cpu_reg].f[0] < XMM[cpu_rm].f[0]) {
            cpu_state.flags &= ~(Z_FLAG | P_FLAG);
            cpu_state.flags |= C_FLAG;
        } else if (XMM[cpu_reg].f[0] == XMM[cpu_rm].f[0]) {
            cpu_state.flags &= ~(P_FLAG | C_FLAG);
            cpu_state.flags |= Z_FLAG;
        }
        CLOCK_CYCLES(1);
    } else {
        uint32_t src;

        SEG_CHECK_READ(cpu_state.ea_seg);
        src = readmeml(easeg, cpu_state.eaaddr);
        if (cpu_state.abrt)
            return 1;
        float src_real;
        src_real = *(float *) &src;
        if (isunordered(XMM[cpu_reg].f[0], src_real)) {
            cpu_state.flags |= Z_FLAG | P_FLAG | C_FLAG;
        } else if (XMM[cpu_reg].f[0] > src_real) {
            cpu_state.flags &= ~(Z_FLAG | P_FLAG | C_FLAG);
        } else if (XMM[cpu_reg].f[0] < src_real) {
            cpu_state.flags &= ~(Z_FLAG | P_FLAG);
            cpu_state.flags |= C_FLAG;
        } else if (XMM[cpu_reg].f[0] == src_real) {
            cpu_state.flags &= ~(P_FLAG | C_FLAG);
            cpu_state.flags |= Z_FLAG;
        }
        CLOCK_CYCLES(2);
    }
    return 0;
}

static int
opUCOMISS_xmm_xmm_a32(uint32_t fetchdat)
{
    flags_rebuild();
    fetch_ea_32(fetchdat);
    cpu_state.flags &= ~(V_FLAG | A_FLAG | N_FLAG);
    if (cpu_mod == 3) {
        if (isunordered(XMM[cpu_reg].f[0], XMM[cpu_rm].f[0])) {
            cpu_state.flags |= Z_FLAG | P_FLAG | C_FLAG;
        } else if (XMM[cpu_reg].f[0] > XMM[cpu_rm].f[0]) {
            cpu_state.flags &= ~(Z_FLAG | P_FLAG | C_FLAG);
        } else if (XMM[cpu_reg].f[0] < XMM[cpu_rm].f[0]) {
            cpu_state.flags &= ~(Z_FLAG | P_FLAG);
            cpu_state.flags |= C_FLAG;
        } else if (XMM[cpu_reg].f[0] == XMM[cpu_rm].f[0]) {
            cpu_state.flags &= ~(P_FLAG | C_FLAG);
            cpu_state.flags |= Z_FLAG;
        }
        CLOCK_CYCLES(1);
    } else {
        uint32_t src;

        SEG_CHECK_READ(cpu_state.ea_seg);
        src = readmeml(easeg, cpu_state.eaaddr);
        if (cpu_state.abrt)
            return 1;
        float src_real;
        src_real = *(float *) &src;
        if (isunordered(XMM[cpu_reg].f[0], src_real)) {
            cpu_state.flags |= Z_FLAG | P_FLAG | C_FLAG;
        } else if (XMM[cpu_reg].f[0] > src_real) {
            cpu_state.flags &= ~(Z_FLAG | P_FLAG | C_FLAG);
        } else if (XMM[cpu_reg].f[0] < src_real) {
            cpu_state.flags &= ~(Z_FLAG | P_FLAG);
            cpu_state.flags |= C_FLAG;
        } else if (XMM[cpu_reg].f[0] == src_real) {
            cpu_state.flags &= ~(P_FLAG | C_FLAG);
            cpu_state.flags |= Z_FLAG;
        }
        CLOCK_CYCLES(2);
    }
    return 0;
}

static int
opCOMISS_xmm_xmm_a16(uint32_t fetchdat)
{
    flags_rebuild();
    fetch_ea_16(fetchdat);
    cpu_state.flags &= ~(V_FLAG | A_FLAG | N_FLAG);
    if (cpu_mod == 3) {
        if (isunordered(XMM[cpu_reg].f[0], XMM[cpu_rm].f[0])) {
            cpu_state.flags |= Z_FLAG | P_FLAG | C_FLAG;
        } else if (XMM[cpu_reg].f[0] > XMM[cpu_rm].f[0]) {
            cpu_state.flags &= ~(Z_FLAG | P_FLAG | C_FLAG);
        } else if (XMM[cpu_reg].f[0] < XMM[cpu_rm].f[0]) {
            cpu_state.flags &= ~(Z_FLAG | P_FLAG);
            cpu_state.flags |= C_FLAG;
        } else if (XMM[cpu_reg].f[0] == XMM[cpu_rm].f[0]) {
            cpu_state.flags &= ~(P_FLAG | C_FLAG);
            cpu_state.flags |= Z_FLAG;
        }
        CLOCK_CYCLES(1);
    } else {
        uint32_t src;

        SEG_CHECK_READ(cpu_state.ea_seg);
        src = readmeml(easeg, cpu_state.eaaddr);
        if (cpu_state.abrt)
            return 1;
        float src_real;
        src_real = *(float *) &src;
        if (isunordered(XMM[cpu_reg].f[0], src_real)) {
            cpu_state.flags |= Z_FLAG | P_FLAG | C_FLAG;
        } else if (XMM[cpu_reg].f[0] > src_real) {
            cpu_state.flags &= ~(Z_FLAG | P_FLAG | C_FLAG);
        } else if (XMM[cpu_reg].f[0] < src_real) {
            cpu_state.flags &= ~(Z_FLAG | P_FLAG);
            cpu_state.flags |= C_FLAG;
        } else if (XMM[cpu_reg].f[0] == src_real) {
            cpu_state.flags &= ~(P_FLAG | C_FLAG);
            cpu_state.flags |= Z_FLAG;
        }
        CLOCK_CYCLES(2);
    }
    return 0;
}

static int
opCOMISS_xmm_xmm_a32(uint32_t fetchdat)
{
    flags_rebuild();
    fetch_ea_32(fetchdat);
    cpu_state.flags &= ~(V_FLAG | A_FLAG | N_FLAG);
    if (cpu_mod == 3) {
        if (isunordered(XMM[cpu_reg].f[0], XMM[cpu_rm].f[0])) {
            cpu_state.flags |= Z_FLAG | P_FLAG | C_FLAG;
        } else if (XMM[cpu_reg].f[0] > XMM[cpu_rm].f[0]) {
            cpu_state.flags &= ~(Z_FLAG | P_FLAG | C_FLAG);
        } else if (XMM[cpu_reg].f[0] < XMM[cpu_rm].f[0]) {
            cpu_state.flags &= ~(Z_FLAG | P_FLAG);
            cpu_state.flags |= C_FLAG;
        } else if (XMM[cpu_reg].f[0] == XMM[cpu_rm].f[0]) {
            cpu_state.flags &= ~(P_FLAG | C_FLAG);
            cpu_state.flags |= Z_FLAG;
        }
        CLOCK_CYCLES(1);
    } else {
        uint32_t src;

        SEG_CHECK_READ(cpu_state.ea_seg);
        src = readmeml(easeg, cpu_state.eaaddr);
        if (cpu_state.abrt)
            return 1;
        float src_real;
        src_real = *(float *) &src;
        if (isunordered(XMM[cpu_reg].f[0], src_real)) {
            cpu_state.flags |= Z_FLAG | P_FLAG | C_FLAG;
        } else if (XMM[cpu_reg].f[0] > src_real) {
            cpu_state.flags &= ~(Z_FLAG | P_FLAG | C_FLAG);
        } else if (XMM[cpu_reg].f[0] < src_real) {
            cpu_state.flags &= ~(Z_FLAG | P_FLAG);
            cpu_state.flags |= C_FLAG;
        } else if (XMM[cpu_reg].f[0] == src_real) {
            cpu_state.flags &= ~(P_FLAG | C_FLAG);
            cpu_state.flags |= Z_FLAG;
        }
        CLOCK_CYCLES(2);
    }
    return 0;
}
