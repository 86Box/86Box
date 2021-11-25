static int opUCOMISD_xmm_xmm_a16(uint32_t fetchdat)
{
    //TODO: Unordered result.
    fetch_ea_16(fetchdat);
    cpu_state.flags &= ~(V_FLAG | A_FLAG | N_FLAG);
    if (cpu_mod == 3)
    {
        if(XMM[cpu_reg].d[0] > XMM[cpu_rm].d[0])
        {
            cpu_state.flags &= ~(Z_FLAG | P_FLAG | C_FLAG);
        }
        else if(XMM[cpu_reg].d[0] == XMM[cpu_rm].d[0])
        {
            cpu_state.flags &= ~(P_FLAG | C_FLAG);
            cpu_state.flags |= Z_FLAG;
        }
        else if(XMM[cpu_reg].d[0] < XMM[cpu_rm].d[0])
        {
            cpu_state.flags &= ~(Z_FLAG | P_FLAG);
            cpu_state.flags |= C_FLAG;
        }
        CLOCK_CYCLES(1);
    }
    else
    {
        uint64_t src;
        
        SEG_CHECK_READ(cpu_state.ea_seg);
        src = readmemq(easeg, cpu_state.eaaddr); if (cpu_state.abrt) return 1;
        double src_real;
        src_real = *(double*)&src;
        if(XMM[cpu_reg].d[0] > src_real)
        {
            cpu_state.flags &= ~(Z_FLAG | P_FLAG | C_FLAG);
        }
        else if(XMM[cpu_reg].d[0] == src_real)
        {
            cpu_state.flags &= ~(P_FLAG | C_FLAG);
            cpu_state.flags |= Z_FLAG;
        }
        else if(XMM[cpu_reg].d[0] < src_real)
        {
            cpu_state.flags &= ~(Z_FLAG | P_FLAG);
            cpu_state.flags |= C_FLAG;
        }
        CLOCK_CYCLES(2);
    }
    return 0;
}

static int opUCOMISD_xmm_xmm_a32(uint32_t fetchdat)
{
    //TODO: Unordered result.
    fetch_ea_32(fetchdat);
    cpu_state.flags &= ~(V_FLAG | A_FLAG | N_FLAG);
    if (cpu_mod == 3)
    {
        if(XMM[cpu_reg].d[0] > XMM[cpu_rm].d[0])
        {
            cpu_state.flags &= ~(Z_FLAG | P_FLAG | C_FLAG);
        }
        else if(XMM[cpu_reg].d[0] == XMM[cpu_rm].d[0])
        {
            cpu_state.flags &= ~(P_FLAG | C_FLAG);
            cpu_state.flags |= Z_FLAG;
        }
        else if(XMM[cpu_reg].d[0] < XMM[cpu_rm].d[0])
        {
            cpu_state.flags &= ~(Z_FLAG | P_FLAG);
            cpu_state.flags |= C_FLAG;
        }
        CLOCK_CYCLES(1);
    }
    else
    {
        uint64_t src;
        
        SEG_CHECK_READ(cpu_state.ea_seg);
        src = readmemq(easeg, cpu_state.eaaddr); if (cpu_state.abrt) return 1;
        double src_real;
        src_real = *(double*)&src;
        if(XMM[cpu_reg].d[0] > src_real)
        {
            cpu_state.flags &= ~(Z_FLAG | P_FLAG | C_FLAG);
        }
        else if(XMM[cpu_reg].d[0] == src_real)
        {
            cpu_state.flags &= ~(P_FLAG | C_FLAG);
            cpu_state.flags |= Z_FLAG;
        }
        else if(XMM[cpu_reg].d[0] < src_real)
        {
            cpu_state.flags &= ~(Z_FLAG | P_FLAG);
            cpu_state.flags |= C_FLAG;
        }
        CLOCK_CYCLES(2);
    }
    return 0;
}

static int opCOMISD_xmm_xmm_a16(uint32_t fetchdat)
{
    //TODO: Unordered result.
    fetch_ea_16(fetchdat);
    cpu_state.flags &= ~(V_FLAG | A_FLAG | N_FLAG);
    if (cpu_mod == 3)
    {
        if(XMM[cpu_reg].d[0] > XMM[cpu_rm].d[0])
        {
            cpu_state.flags &= ~(Z_FLAG | P_FLAG | C_FLAG);
        }
        else if(XMM[cpu_reg].d[0] == XMM[cpu_rm].d[0])
        {
            cpu_state.flags &= ~(P_FLAG | C_FLAG);
            cpu_state.flags |= Z_FLAG;
        }
        else if(XMM[cpu_reg].d[0] < XMM[cpu_rm].d[0])
        {
            cpu_state.flags &= ~(Z_FLAG | P_FLAG);
            cpu_state.flags |= C_FLAG;
        }
        CLOCK_CYCLES(1);
    }
    else
    {
        uint64_t src;
        
        SEG_CHECK_READ(cpu_state.ea_seg);
        src = readmemq(easeg, cpu_state.eaaddr); if (cpu_state.abrt) return 1;
        double src_real;
        src_real = *(double*)&src;
        if(XMM[cpu_reg].d[0] > src_real)
        {
            cpu_state.flags &= ~(Z_FLAG | P_FLAG | C_FLAG);
        }
        else if(XMM[cpu_reg].d[0] == src_real)
        {
            cpu_state.flags &= ~(P_FLAG | C_FLAG);
            cpu_state.flags |= Z_FLAG;
        }
        else if(XMM[cpu_reg].d[0] < src_real)
        {
            cpu_state.flags &= ~(Z_FLAG | P_FLAG);
            cpu_state.flags |= C_FLAG;
        }
        CLOCK_CYCLES(2);
    }
    return 0;
}

static int opCOMISD_xmm_xmm_a32(uint32_t fetchdat)
{
    //TODO: Unordered result.
    fetch_ea_32(fetchdat);
    cpu_state.flags &= ~(V_FLAG | A_FLAG | N_FLAG);
    if (cpu_mod == 3)
    {
        if(XMM[cpu_reg].d[0] > XMM[cpu_rm].d[0])
        {
            cpu_state.flags &= ~(Z_FLAG | P_FLAG | C_FLAG);
        }
        else if(XMM[cpu_reg].d[0] == XMM[cpu_rm].d[0])
        {
            cpu_state.flags &= ~(P_FLAG | C_FLAG);
            cpu_state.flags |= Z_FLAG;
        }
        else if(XMM[cpu_reg].d[0] < XMM[cpu_rm].d[0])
        {
            cpu_state.flags &= ~(Z_FLAG | P_FLAG);
            cpu_state.flags |= C_FLAG;
        }
        CLOCK_CYCLES(1);
    }
    else
    {
        uint64_t src;
        
        SEG_CHECK_READ(cpu_state.ea_seg);
        src = readmemq(easeg, cpu_state.eaaddr); if (cpu_state.abrt) return 1;
        double src_real;
        src_real = *(double*)&src;
        if(XMM[cpu_reg].d[0] > src_real)
        {
            cpu_state.flags &= ~(Z_FLAG | P_FLAG | C_FLAG);
        }
        else if(XMM[cpu_reg].d[0] == src_real)
        {
            cpu_state.flags &= ~(P_FLAG | C_FLAG);
            cpu_state.flags |= Z_FLAG;
        }
        else if(XMM[cpu_reg].d[0] < src_real)
        {
            cpu_state.flags &= ~(Z_FLAG | P_FLAG);
            cpu_state.flags |= C_FLAG;
        }
        CLOCK_CYCLES(2);
    }
    return 0;
}