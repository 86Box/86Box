static int opCBW(uint32_t fetchdat)
{
        AH = (AL & 0x80) ? 0xff : 0;
        CLOCK_CYCLES(3);
        PREFETCH_RUN(3, 1, -1, 0,0,0,0, 0);
        return 0;
}
static int opCWDE(uint32_t fetchdat)
{
        EAX = (AX & 0x8000) ? (0xffff0000 | AX) : AX;
        CLOCK_CYCLES(3);
        PREFETCH_RUN(3, 1, -1, 0,0,0,0, 0);
        return 0;
}
static int opCWD(uint32_t fetchdat)
{
        DX = (AX & 0x8000) ? 0xFFFF : 0;
        CLOCK_CYCLES(2);
        PREFETCH_RUN(2, 1, -1, 0,0,0,0, 0);
        return 0;
}
static int opCDQ(uint32_t fetchdat)
{
        EDX = (EAX & 0x80000000) ? 0xffffffff : 0;
        CLOCK_CYCLES(2);
        PREFETCH_RUN(2, 1, -1, 0,0,0,0, 0);
        return 0;
}

static int opNOP(uint32_t fetchdat)
{
        CLOCK_CYCLES((is486) ? 1 : 3);
        PREFETCH_RUN(3, 1, -1, 0,0,0,0, 0);
        return 0;
}

static int opSETALC(uint32_t fetchdat)
{
        AL = (CF_SET()) ? 0xff : 0;
        CLOCK_CYCLES(timing_rr);
        PREFETCH_RUN(timing_rr, 1, -1, 0,0,0,0, 0);
        return 0;
}



static int opF6_a16(uint32_t fetchdat)
{
        int tempws, tempws2 = 0;
        uint16_t tempw, src16;
        uint8_t src, dst;
        int8_t temps;
        
        fetch_ea_16(fetchdat);
        if (cpu_mod != 3) {
		SEG_CHECK_READ(cpu_state.ea_seg);
                CHECK_READ(cpu_state.ea_seg, cpu_state.eaaddr, cpu_state.eaaddr);
        }
        dst = geteab();                 if (cpu_state.abrt) return 1;
        switch (rmdat & 0x38)
        {
                case 0x00: /*TEST b,#8*/
		case 0x08:
                src = readmemb(cs, cpu_state.pc); cpu_state.pc++;           if (cpu_state.abrt) return 1;
                setznp8(src & dst);
                if (is486) CLOCK_CYCLES((cpu_mod == 3) ? 1 : 2);
                else       CLOCK_CYCLES((cpu_mod == 3) ? 2 : 5);
                PREFETCH_RUN((cpu_mod == 3) ? 2 : 5, 3, rmdat, (cpu_mod == 3) ? 0:1,0,0,0, 0);
                break;
                case 0x10: /*NOT b*/
                if (cpu_mod != 3)
                        SEG_CHECK_WRITE(cpu_state.ea_seg);
                seteab(~dst);                           if (cpu_state.abrt) return 1;
                CLOCK_CYCLES((cpu_mod == 3) ? timing_rr : timing_mm);
                PREFETCH_RUN((cpu_mod == 3) ? timing_rr : timing_mm, 2, rmdat, (cpu_mod == 3) ? 0:1,0,(cpu_mod == 3) ? 0:1,0, 0);
                break;
                case 0x18: /*NEG b*/
                if (cpu_mod != 3)
                        SEG_CHECK_WRITE(cpu_state.ea_seg);
                seteab(0 - dst);                        if (cpu_state.abrt) return 1;
                setsub8(0, dst);
                CLOCK_CYCLES((cpu_mod == 3) ? timing_rr : timing_mm);
                PREFETCH_RUN((cpu_mod == 3) ? timing_rr : timing_mm, 2, rmdat, (cpu_mod == 3) ? 0:1,0,(cpu_mod == 3) ? 0:1,0, 0);
                break;
                case 0x20: /*MUL AL,b*/
                AX = AL * dst;
                flags_rebuild();
                if (AH) cpu_state.flags |=  (C_FLAG | V_FLAG);
                else    cpu_state.flags &= ~(C_FLAG | V_FLAG);
                CLOCK_CYCLES(13);
                PREFETCH_RUN(13, 2, rmdat, (cpu_mod == 3) ? 0:1,0,0,0, 0);
                break;
                case 0x28: /*IMUL AL,b*/
                tempws = (int)((int8_t)AL) * (int)((int8_t)dst);
                AX = tempws & 0xffff;
                flags_rebuild();
                if (((int16_t)AX >> 7) != 0 && ((int16_t)AX >> 7) != -1) cpu_state.flags |=  (C_FLAG | V_FLAG);
                else                                                     cpu_state.flags &= ~(C_FLAG | V_FLAG);
                CLOCK_CYCLES(14);
                PREFETCH_RUN(14, 2, rmdat, (cpu_mod == 3) ? 0:1,0,0,0, 0);
                break;
                case 0x30: /*DIV AL,b*/
                src16 = AX;
                if (dst) tempw = src16 / dst;
                if (dst && !(tempw & 0xff00))
                {
                        AH = src16 % dst;
                        AL = (src16 / dst) &0xff;
                        if (!cpu_iscyrix) 
                        {
                                flags_rebuild();
                                cpu_state.flags |= 0x8D5; /*Not a Cyrix*/
                                cpu_state.flags &= ~1;
                        }
                }
                else
                {
                        x86_int(0);
                        return 1;
                }
                CLOCK_CYCLES((is486 && !cpu_iscyrix) ? 16 : 14);
                PREFETCH_RUN((is486 && !cpu_iscyrix) ? 16 : 14, 2, rmdat, (cpu_mod == 3) ? 0:1,0,0,0, 0);
                break;
                case 0x38: /*IDIV AL,b*/
                tempws = (int)(int16_t)AX;
                if (dst != 0) tempws2 = tempws / (int)((int8_t)dst);
                temps = tempws2 & 0xff;
                if (dst && ((int)temps == tempws2))
                {
                        AH = (tempws % (int)((int8_t)dst)) & 0xff;
                        AL = tempws2 & 0xff;
                        if (!cpu_iscyrix) 
                        {
                                flags_rebuild();
                                cpu_state.flags|=0x8D5; /*Not a Cyrix*/
                                cpu_state.flags &= ~1;
                        }
                }
                else
                {
                        x86_int(0);
                        return 1;
                }
                CLOCK_CYCLES(19);
                PREFETCH_RUN(19, 2, rmdat, (cpu_mod == 3) ? 0:1,0,0,0, 0);
                break;

                default:
                x86illegal();
        }
        return 0;
}
static int opF6_a32(uint32_t fetchdat)
{
        int tempws, tempws2 = 0;
        uint16_t tempw, src16;
        uint8_t src, dst;
        int8_t temps;
        
        fetch_ea_32(fetchdat);
        if (cpu_mod != 3)
                SEG_CHECK_READ(cpu_state.ea_seg);
        dst = geteab();                 if (cpu_state.abrt) return 1;
        switch (rmdat & 0x38)
        {
                case 0x00: /*TEST b,#8*/
                case 0x08:
                src = readmemb(cs, cpu_state.pc); cpu_state.pc++;           if (cpu_state.abrt) return 1;
                setznp8(src & dst);
                if (is486) CLOCK_CYCLES((cpu_mod == 3) ? 1 : 2);
                else       CLOCK_CYCLES((cpu_mod == 3) ? 2 : 5);
                PREFETCH_RUN((cpu_mod == 3) ? 2 : 5, 3, rmdat, (cpu_mod == 3) ? 0:1,0,0,0, 1);
                break;
                case 0x10: /*NOT b*/
                if (cpu_mod != 3)
                        SEG_CHECK_WRITE(cpu_state.ea_seg);
                seteab(~dst);                           if (cpu_state.abrt) return 1;
                CLOCK_CYCLES((cpu_mod == 3) ? timing_rr : timing_mm);
                PREFETCH_RUN((cpu_mod == 3) ? timing_rr : timing_mm, 2, rmdat, (cpu_mod == 3) ? 0:1,0,(cpu_mod == 3) ? 0:1,0, 1);
                break;
                case 0x18: /*NEG b*/
                if (cpu_mod != 3)
                        SEG_CHECK_WRITE(cpu_state.ea_seg);
                seteab(0 - dst);                        if (cpu_state.abrt) return 1;
                setsub8(0, dst);
                CLOCK_CYCLES((cpu_mod == 3) ? timing_rr : timing_mm);
                PREFETCH_RUN((cpu_mod == 3) ? timing_rr : timing_mm, 2, rmdat, (cpu_mod == 3) ? 0:1,0,(cpu_mod == 3) ? 0:1,0, 1);
                break;
                case 0x20: /*MUL AL,b*/
                AX = AL * dst;
                flags_rebuild();
                if (AH) cpu_state.flags |=  (C_FLAG | V_FLAG);
                else    cpu_state.flags &= ~(C_FLAG | V_FLAG);
                CLOCK_CYCLES(13);
                PREFETCH_RUN(13, 2, rmdat, (cpu_mod == 3) ? 0:1,0,0,0, 1);
                break;
                case 0x28: /*IMUL AL,b*/
                tempws = (int)((int8_t)AL) * (int)((int8_t)dst);
                AX = tempws & 0xffff;
                flags_rebuild();
                if (((int16_t)AX >> 7) != 0 && ((int16_t)AX >> 7) != -1) cpu_state.flags |=  (C_FLAG | V_FLAG);
                else                                                     cpu_state.flags &= ~(C_FLAG | V_FLAG);
                CLOCK_CYCLES(14);
                PREFETCH_RUN(14, 2, rmdat, (cpu_mod == 3) ? 0:1,0,0,0, 1);
                break;
                case 0x30: /*DIV AL,b*/
                src16 = AX;
                if (dst) tempw = src16 / dst;
                if (dst && !(tempw & 0xff00))
                {
                        AH = src16 % dst;
                        AL = (src16 / dst) &0xff;
                        if (!cpu_iscyrix) 
                        {
                                flags_rebuild();
                                cpu_state.flags |= 0x8D5; /*Not a Cyrix*/
                                cpu_state.flags &= ~1;
                        }
                }
                else
                {
                        x86_int(0);
                        return 1;
                }
                CLOCK_CYCLES((is486 && !cpu_iscyrix) ? 16 : 14);
                PREFETCH_RUN((is486 && !cpu_iscyrix) ? 16 : 14, 2, rmdat, (cpu_mod == 3) ? 0:1,0,0,0, 1);
                break;
                case 0x38: /*IDIV AL,b*/
                tempws = (int)(int16_t)AX;
                if (dst != 0) tempws2 = tempws / (int)((int8_t)dst);
                temps = tempws2 & 0xff;
                if (dst && ((int)temps == tempws2))
                {
                        AH = (tempws % (int)((int8_t)dst)) & 0xff;
                        AL = tempws2 & 0xff;
                        if (!cpu_iscyrix) 
                        {
                                flags_rebuild();
                                cpu_state.flags |= 0x8D5; /*Not a Cyrix*/
                                cpu_state.flags &= ~1;
                        }
                }
                else
                {
                        x86_int(0);
                        return 1;
                }
                CLOCK_CYCLES(19);
                PREFETCH_RUN(19, 2, rmdat, (cpu_mod == 3) ? 0:1,0,0,0, 1);
                break;

                default:
                x86illegal();
        }
        return 0;
}



static int opF7_w_a16(uint32_t fetchdat)
{
        uint32_t templ, templ2;
        int tempws, tempws2 = 0;
        int16_t temps16;
        uint16_t src, dst;
        
        fetch_ea_16(fetchdat);
        if (cpu_mod != 3)
                SEG_CHECK_READ(cpu_state.ea_seg);
        dst = geteaw();        if (cpu_state.abrt) return 1;
        switch (rmdat & 0x38)
        {
                case 0x00: /*TEST w*/
                case 0x08:
                src = getword();        if (cpu_state.abrt) return 1;
                setznp16(src & dst);
                if (is486) CLOCK_CYCLES((cpu_mod == 3) ? 1 : 2);
                else       CLOCK_CYCLES((cpu_mod == 3) ? 2 : 5);
                PREFETCH_RUN((cpu_mod == 3) ? 2 : 5, 4, rmdat, (cpu_mod == 3) ? 0:1,0,0,0, 0);
                break;
                case 0x10: /*NOT w*/
                if (cpu_mod != 3)
                        SEG_CHECK_WRITE(cpu_state.ea_seg);
                seteaw(~dst);           if (cpu_state.abrt) return 1;
                CLOCK_CYCLES((cpu_mod == 3) ? timing_rr : timing_mm);
                PREFETCH_RUN((cpu_mod == 3) ? timing_rr : timing_mm, 2, rmdat, (cpu_mod == 3) ? 0:1,0,(cpu_mod == 3) ? 0:1,0, 0);
                break;
                case 0x18: /*NEG w*/
                if (cpu_mod != 3)
                        SEG_CHECK_WRITE(cpu_state.ea_seg);
                seteaw(0 - dst);        if (cpu_state.abrt) return 1;
                setsub16(0, dst);
                CLOCK_CYCLES((cpu_mod == 3) ? timing_rr : timing_mm);
                PREFETCH_RUN((cpu_mod == 3) ? timing_rr : timing_mm, 2, rmdat, (cpu_mod == 3) ? 0:1,0,(cpu_mod == 3) ? 0:1,0, 0);
                break;
                case 0x20: /*MUL AX,w*/
                templ = AX * dst;
                AX = templ & 0xFFFF;
                DX = templ >> 16;
                flags_rebuild();
                if (DX)    cpu_state.flags |=  (C_FLAG | V_FLAG);
                else       cpu_state.flags &= ~(C_FLAG | V_FLAG);
                CLOCK_CYCLES(21);
                PREFETCH_RUN(21, 2, rmdat, (cpu_mod == 3) ? 0:1,0,0,0, 0);
                break;
                case 0x28: /*IMUL AX,w*/
                templ = (int)((int16_t)AX) * (int)((int16_t)dst);
                AX = templ & 0xFFFF;
                DX = templ >> 16;
                flags_rebuild();
                if (((int32_t)templ >> 15) != 0 && ((int32_t)templ >> 15) != -1) cpu_state.flags |=  (C_FLAG | V_FLAG);
                else                                                             cpu_state.flags &= ~(C_FLAG | V_FLAG);
                CLOCK_CYCLES(22);
                PREFETCH_RUN(22, 2, rmdat, (cpu_mod == 3) ? 0:1,0,0,0, 0);
                break;
                case 0x30: /*DIV AX,w*/
                templ = (DX << 16) | AX;
                if (dst) templ2 = templ / dst;
                if (dst && !(templ2 & 0xffff0000))
                {
                        DX = templ % dst;
                        AX = (templ / dst) & 0xffff;
                        if (!cpu_iscyrix) setznp16(AX); /*Not a Cyrix*/                                                
                }
                else
                {
                        x86_int(0);
                        return 1;
                }
                CLOCK_CYCLES((is486 && !cpu_iscyrix) ? 24 : 22);
                PREFETCH_RUN((is486 && !cpu_iscyrix) ? 24 : 22, 2, rmdat, (cpu_mod == 3) ? 0:1,0,0,0, 0);
                break;
                case 0x38: /*IDIV AX,w*/
                tempws = (int)((DX << 16)|AX);
                if (dst) tempws2 = tempws / (int)((int16_t)dst);
                temps16 = tempws2 & 0xffff;
                if ((dst != 0) && ((int)temps16 == tempws2))
                {
                        DX = tempws % (int)((int16_t)dst);
                        AX = tempws2 & 0xffff;
                        if (!cpu_iscyrix) setznp16(AX); /*Not a Cyrix*/
                }
                else
                {
                        x86_int(0);
                        return 1;
                }
                CLOCK_CYCLES(27);
                PREFETCH_RUN(27, 2, rmdat, (cpu_mod == 3) ? 0:1,0,0,0, 0);
                break;

                default:
                x86illegal();
        }
        return 0;
}
static int opF7_w_a32(uint32_t fetchdat)
{
        uint32_t templ, templ2;
        int tempws, tempws2 = 1;
        int16_t temps16;
        uint16_t src, dst;

        fetch_ea_32(fetchdat);
        if (cpu_mod != 3)
                SEG_CHECK_READ(cpu_state.ea_seg);
        dst = geteaw();        if (cpu_state.abrt) return 1;
        switch (rmdat & 0x38)
        {
                case 0x00: /*TEST w*/
                case 0x08:
                src = getword();        if (cpu_state.abrt) return 1;
                setznp16(src & dst);
                if (is486) CLOCK_CYCLES((cpu_mod == 3) ? 1 : 2);
                else       CLOCK_CYCLES((cpu_mod == 3) ? 2 : 5);
                PREFETCH_RUN((cpu_mod == 3) ? 2 : 5, 4, rmdat, (cpu_mod == 3) ? 0:1,0,0,0, 1);
                break;
                case 0x10: /*NOT w*/
                if (cpu_mod != 3)
                        SEG_CHECK_WRITE(cpu_state.ea_seg);
                seteaw(~dst);           if (cpu_state.abrt) return 1;
                CLOCK_CYCLES((cpu_mod == 3) ? timing_rr : timing_mm);
                PREFETCH_RUN((cpu_mod == 3) ? timing_rr : timing_mm, 2, rmdat, (cpu_mod == 3) ? 0:1,0,(cpu_mod == 3) ? 0:1,0, 1);
                break;
                case 0x18: /*NEG w*/
                if (cpu_mod != 3)
                        SEG_CHECK_WRITE(cpu_state.ea_seg);
                seteaw(0 - dst);        if (cpu_state.abrt) return 1;
                setsub16(0, dst);
                CLOCK_CYCLES((cpu_mod == 3) ? timing_rr : timing_mm);
                PREFETCH_RUN((cpu_mod == 3) ? timing_rr : timing_mm, 2, rmdat, (cpu_mod == 3) ? 0:1,0,(cpu_mod == 3) ? 0:1,0, 1);
                break;
                case 0x20: /*MUL AX,w*/
                templ = AX * dst;
                AX = templ & 0xFFFF;
                DX = templ >> 16;
                flags_rebuild();
                if (DX)    cpu_state.flags |=  (C_FLAG | V_FLAG);
                else       cpu_state.flags &= ~(C_FLAG | V_FLAG);
                CLOCK_CYCLES(21);
                PREFETCH_RUN(21, 2, rmdat, (cpu_mod == 3) ? 0:1,0,0,0, 1);
                break;
                case 0x28: /*IMUL AX,w*/
                templ = (int)((int16_t)AX) * (int)((int16_t)dst);
                AX = templ & 0xFFFF;
                DX = templ >> 16;
                flags_rebuild();
                if (((int32_t)templ >> 15) != 0 && ((int32_t)templ >> 15) != -1) cpu_state.flags |=  (C_FLAG | V_FLAG);
                else                                                             cpu_state.flags &= ~(C_FLAG | V_FLAG);
                CLOCK_CYCLES(22);
                PREFETCH_RUN(22, 2, rmdat, (cpu_mod == 3) ? 0:1,0,0,0, 1);
                break;
                case 0x30: /*DIV AX,w*/
                templ = (DX << 16) | AX;
                if (dst) templ2 = templ / dst;
                if (dst && !(templ2 & 0xffff0000))
                {
                        DX = templ % dst;
                        AX = (templ / dst) & 0xffff;
                        if (!cpu_iscyrix) setznp16(AX); /*Not a Cyrix*/                                                
                }
                else
                {
//                        fatal("DIVw BY 0 %04X:%04X %i\n",cs>>4,pc,ins);
                        x86_int(0);
                        return 1;
                }
                CLOCK_CYCLES((is486 && !cpu_iscyrix) ? 24 : 22);
                PREFETCH_RUN((is486 && !cpu_iscyrix) ? 24 : 22, 2, rmdat, (cpu_mod == 3) ? 0:1,0,0,0, 1);
                break;
                case 0x38: /*IDIV AX,w*/
                tempws = (int)((DX << 16)|AX);
                if (dst) tempws2 = tempws / (int)((int16_t)dst);
                temps16 = tempws2 & 0xffff;
                if ((dst != 0) && ((int)temps16 == tempws2))
                {
                        DX = tempws % (int)((int16_t)dst);
                        AX = tempws2 & 0xffff;
                        if (!cpu_iscyrix) setznp16(AX); /*Not a Cyrix*/
                }
                else
                {
                        x86_int(0);
                        return 1;
                }
                CLOCK_CYCLES(27);
                PREFETCH_RUN(27, 2, rmdat, (cpu_mod == 3) ? 0:1,0,0,0, 1);
                break;

                default:
                x86illegal();
        }
        return 0;
}

static int opF7_l_a16(uint32_t fetchdat)
{
        uint64_t temp64;
        uint32_t src, dst;

        fetch_ea_16(fetchdat);
        if (cpu_mod != 3)
                SEG_CHECK_READ(cpu_state.ea_seg);
        dst = geteal();                 if (cpu_state.abrt) return 1;

        switch (rmdat & 0x38)
        {
                case 0x00: /*TEST l*/
                case 0x08:
                src = getlong();        if (cpu_state.abrt) return 1;
                setznp32(src & dst);
                if (is486) CLOCK_CYCLES((cpu_mod == 3) ? 1 : 2);
                else       CLOCK_CYCLES((cpu_mod == 3) ? 2 : 5);
                PREFETCH_RUN((cpu_mod == 3) ? 2 : 5, 5, rmdat, 0,(cpu_mod == 3) ? 0:1,0,0, 0);
                break;
                case 0x10: /*NOT l*/
                if (cpu_mod != 3)
                        SEG_CHECK_WRITE(cpu_state.ea_seg);
                seteal(~dst);           if (cpu_state.abrt) return 1;
                CLOCK_CYCLES((cpu_mod == 3) ? timing_rr : timing_mml);
                PREFETCH_RUN((cpu_mod == 3) ? timing_rr : timing_mm, 2, rmdat, 0,(cpu_mod == 3) ? 0:1,0,(cpu_mod == 3) ? 0:1, 0);
                break;
                case 0x18: /*NEG l*/
                if (cpu_mod != 3)
                        SEG_CHECK_WRITE(cpu_state.ea_seg);
                seteal(0 - dst);        if (cpu_state.abrt) return 1;
                setsub32(0, dst);
                CLOCK_CYCLES((cpu_mod == 3) ? timing_rr : timing_mml);
                PREFETCH_RUN((cpu_mod == 3) ? timing_rr : timing_mm, 2, rmdat, 0,(cpu_mod == 3) ? 0:1,0,(cpu_mod == 3) ? 0:1, 0);
                break;
                case 0x20: /*MUL EAX,l*/
                temp64 = (uint64_t)EAX * (uint64_t)dst;
                EAX = temp64 & 0xffffffff;
                EDX = temp64 >> 32;
                flags_rebuild();
                if (EDX) cpu_state.flags |=  (C_FLAG|V_FLAG);
                else     cpu_state.flags &= ~(C_FLAG|V_FLAG);
                CLOCK_CYCLES(21);
                PREFETCH_RUN(21, 2, rmdat, 0,(cpu_mod == 3) ? 0:1,0,0, 0);
                break;
                case 0x28: /*IMUL EAX,l*/
                temp64 = (int64_t)(int32_t)EAX * (int64_t)(int32_t)dst;
                EAX = temp64 & 0xffffffff;
                EDX = temp64 >> 32;
                flags_rebuild();
                if (((int64_t)temp64 >> 31) != 0 && ((int64_t)temp64 >> 31) != -1) cpu_state.flags |=  (C_FLAG | V_FLAG);
                else                                                               cpu_state.flags &= ~(C_FLAG | V_FLAG);
                CLOCK_CYCLES(38);
                PREFETCH_RUN(38, 2, rmdat, 0,(cpu_mod == 3) ? 0:1,0,0, 0);
                break;
                case 0x30: /*DIV EAX,l*/
                if (divl(dst))
                        return 1;
                if (!cpu_iscyrix) setznp32(EAX); /*Not a Cyrix*/
                CLOCK_CYCLES((is486) ? 40 : 38);
                PREFETCH_RUN(is486 ? 40:38, 2, rmdat, 0,(cpu_mod == 3) ? 0:1,0,0, 0);
                break;
                case 0x38: /*IDIV EAX,l*/
                if (idivl((int32_t)dst))
                        return 1;
                if (!cpu_iscyrix) setznp32(EAX); /*Not a Cyrix*/
                CLOCK_CYCLES(43);
                PREFETCH_RUN(43, 2, rmdat, 0,(cpu_mod == 3) ? 0:1,0,0, 0);
                break;

                default:
                x86illegal();
        }
        return 0;
}
static int opF7_l_a32(uint32_t fetchdat)
{
        uint64_t temp64;
        uint32_t src, dst;

        fetch_ea_32(fetchdat);
        if (cpu_mod != 3)
                SEG_CHECK_READ(cpu_state.ea_seg);
        dst = geteal();                 if (cpu_state.abrt) return 1;

        switch (rmdat & 0x38)
        {
                case 0x00: /*TEST l*/
                case 0x08:
                src = getlong();        if (cpu_state.abrt) return 1;
                setznp32(src & dst);
                if (is486) CLOCK_CYCLES((cpu_mod == 3) ? 1 : 2);
                else       CLOCK_CYCLES((cpu_mod == 3) ? 2 : 5);
                PREFETCH_RUN((cpu_mod == 3) ? 2 : 5, 5, rmdat, 0,(cpu_mod == 3) ? 0:1,0,0, 1);
                break;
                case 0x10: /*NOT l*/
                if (cpu_mod != 3)
                        SEG_CHECK_WRITE(cpu_state.ea_seg);
                seteal(~dst);           if (cpu_state.abrt) return 1;
                CLOCK_CYCLES((cpu_mod == 3) ? timing_rr : timing_mml);
                PREFETCH_RUN((cpu_mod == 3) ? timing_rr : timing_mm, 2, rmdat, 0,(cpu_mod == 3) ? 0:1,0,(cpu_mod == 3) ? 0:1, 1);
                break;
                case 0x18: /*NEG l*/
                if (cpu_mod != 3)
                        SEG_CHECK_WRITE(cpu_state.ea_seg);
                seteal(0 - dst);        if (cpu_state.abrt) return 1;
                setsub32(0, dst);
                CLOCK_CYCLES((cpu_mod == 3) ? timing_rr : timing_mml);
                PREFETCH_RUN((cpu_mod == 3) ? timing_rr : timing_mm, 2, rmdat, 0,(cpu_mod == 3) ? 0:1,0,(cpu_mod == 3) ? 0:1, 1);
                break;
                case 0x20: /*MUL EAX,l*/
                temp64 = (uint64_t)EAX * (uint64_t)dst;
                EAX = temp64 & 0xffffffff;
                EDX = temp64 >> 32;
                flags_rebuild();
                if (EDX) cpu_state.flags |=  (C_FLAG|V_FLAG);
                else     cpu_state.flags &= ~(C_FLAG|V_FLAG);
                CLOCK_CYCLES(21);
                PREFETCH_RUN(21, 2, rmdat, 0,(cpu_mod == 3) ? 0:1,0,0, 1);
                break;
                case 0x28: /*IMUL EAX,l*/
                temp64 = (int64_t)(int32_t)EAX * (int64_t)(int32_t)dst;
                EAX = temp64 & 0xffffffff;
                EDX = temp64 >> 32;
                flags_rebuild();
                if (((int64_t)temp64 >> 31) != 0 && ((int64_t)temp64 >> 31) != -1) cpu_state.flags |=  (C_FLAG | V_FLAG);
                else                                                               cpu_state.flags &= ~(C_FLAG | V_FLAG);
                CLOCK_CYCLES(38);
                PREFETCH_RUN(38, 2, rmdat, 0,(cpu_mod == 3) ? 0:1,0,0, 1);
                break;
                case 0x30: /*DIV EAX,l*/
                if (divl(dst))
                        return 1;
                if (!cpu_iscyrix) setznp32(EAX); /*Not a Cyrix*/
                CLOCK_CYCLES((is486) ? 40 : 38);
                PREFETCH_RUN(is486 ? 40 : 38, 2, rmdat, 0,(cpu_mod == 3) ? 0:1,0,0, 1);
                break;
                case 0x38: /*IDIV EAX,l*/
                if (idivl((int32_t)dst))
                        return 1;
                if (!cpu_iscyrix) setznp32(EAX); /*Not a Cyrix*/
                CLOCK_CYCLES(43);
                PREFETCH_RUN(43, 2, rmdat, 0,(cpu_mod == 3) ? 0:1,0,0, 1);
                break;

                default:
                x86illegal();
        }
        return 0;
}


static int opHLT(uint32_t fetchdat)
{
        if ((CPL || (cpu_state.eflags&VM_FLAG)) && (cr0&1))
        {
                x86gpf(NULL,0);
                return 1;
        }
        if (!((cpu_state.flags & I_FLAG) && pic_intpending))
        {
                CLOCK_CYCLES_ALWAYS(100);
                cpu_state.pc--;
        }
        else
                CLOCK_CYCLES(5);

        CPU_BLOCK_END();
        PREFETCH_RUN(100, 1, -1, 0,0,0,0, 0);
                
        return 0;
}


static int opLOCK(uint32_t fetchdat)
{
        fetchdat = fastreadl(cs + cpu_state.pc);
        if (cpu_state.abrt) return 0;
        cpu_state.pc++;
        
        ILLEGAL_ON((fetchdat & 0xff) == 0x90);

        CLOCK_CYCLES(4);
        PREFETCH_PREFIX();
        return x86_opcodes[(fetchdat & 0xff) | cpu_state.op32](fetchdat >> 8);
}



static int opBOUND_w_a16(uint32_t fetchdat)
{       
        int16_t low, high;
        
        fetch_ea_16(fetchdat);
        ILLEGAL_ON(cpu_mod == 3);
        SEG_CHECK_READ(cpu_state.ea_seg);
        low = geteaw();
        high = readmemw(easeg, cpu_state.eaaddr + 2);     if (cpu_state.abrt) return 1;
        
        if (((int16_t)cpu_state.regs[cpu_reg].w < low) || ((int16_t)cpu_state.regs[cpu_reg].w > high))
        {
                x86_int(5);
                return 1;
        }
        
        CLOCK_CYCLES(is486 ? 7 : 10);
        PREFETCH_RUN(is486 ? 7 : 10, 2, rmdat, 2,0,0,0, 0);
        return 0;
}
static int opBOUND_w_a32(uint32_t fetchdat)
{       
        int16_t low, high;
        
        fetch_ea_32(fetchdat);
        ILLEGAL_ON(cpu_mod == 3);
        SEG_CHECK_READ(cpu_state.ea_seg);
        low = geteaw();
        high = readmemw(easeg, cpu_state.eaaddr + 2);     if (cpu_state.abrt) return 1;
        
        if (((int16_t)cpu_state.regs[cpu_reg].w < low) || ((int16_t)cpu_state.regs[cpu_reg].w > high))
        {
                x86_int(5);
                return 1;
        }
        
        CLOCK_CYCLES(is486 ? 7 : 10);
        PREFETCH_RUN(is486 ? 7 : 10, 2, rmdat, 2,0,0,0, 1);
        return 0;
}

static int opBOUND_l_a16(uint32_t fetchdat)
{       
        int32_t low, high;
        
        fetch_ea_16(fetchdat);
        ILLEGAL_ON(cpu_mod == 3);
        SEG_CHECK_READ(cpu_state.ea_seg);
        low = geteal();
        high = readmeml(easeg, cpu_state.eaaddr + 4);     if (cpu_state.abrt) return 1;
        
        if (((int32_t)cpu_state.regs[cpu_reg].l < low) || ((int32_t)cpu_state.regs[cpu_reg].l > high))
        {
                x86_int(5);
                return 1;
        }
        
        CLOCK_CYCLES(is486 ? 7 : 10);
        PREFETCH_RUN(is486 ? 7 : 10, 2, rmdat, 1,1,0,0, 0);
        return 0;
}
static int opBOUND_l_a32(uint32_t fetchdat)
{       
        int32_t low, high;
        
        fetch_ea_32(fetchdat);
        ILLEGAL_ON(cpu_mod == 3);
        SEG_CHECK_READ(cpu_state.ea_seg);
        low = geteal();
        high = readmeml(easeg, cpu_state.eaaddr + 4);     if (cpu_state.abrt) return 1;
        
        if (((int32_t)cpu_state.regs[cpu_reg].l < low) || ((int32_t)cpu_state.regs[cpu_reg].l > high))
        {
                x86_int(5);
                return 1;
        }
        
        CLOCK_CYCLES(is486 ? 7 : 10);
        PREFETCH_RUN(is486 ? 7 : 10, 2, rmdat, 1,1,0,0, 1);
        return 0;
}


static int opCLTS(uint32_t fetchdat)
{
        if ((CPL || (cpu_state.eflags&VM_FLAG)) && (cr0&1))
        {
                x86gpf(NULL,0);
                return 1;
        }
        cr0 &= ~8;
        CLOCK_CYCLES(5);
        PREFETCH_RUN(5, 1, -1, 0,0,0,0, 0);
        return 0;
}

static int opINVD(uint32_t fetchdat)
{
        if (!is486) 
        {
                x86illegal();
                return 1;
        }
        CLOCK_CYCLES(1000);
        CPU_BLOCK_END();
        return 0;
}
static int opWBINVD(uint32_t fetchdat)
{
        if (!is486) 
        {
                x86illegal();
                return 1;
        }
        CLOCK_CYCLES(10000);
        CPU_BLOCK_END();
        return 0;
}

static int opLOADALL(uint32_t fetchdat)
{
        if (CPL && (cr0&1))
        {
                x86gpf(NULL,0);
                return 1;
        }
        msw = (msw & 1) | readmemw(0, 0x806);
        cpu_state.flags = (readmemw(0, 0x818) & 0xffd5) | 2;
        flags_extract();
        tr.seg = readmemw(0, 0x816);
        cpu_state.pc = readmemw(0, 0x81A);
        ldt.seg = readmemw(0, 0x81C);
        DS = readmemw(0, 0x81E);
        SS = readmemw(0, 0x820);
        CS = readmemw(0, 0x822);
        ES = readmemw(0, 0x824);
        DI = readmemw(0, 0x826);
        SI = readmemw(0, 0x828);
        BP = readmemw(0, 0x82A);
        SP = readmemw(0, 0x82C);
        BX = readmemw(0, 0x82E);
        DX = readmemw(0, 0x830);
        CX = readmemw(0, 0x832);
        AX = readmemw(0, 0x834);
        es = readmemw(0, 0x836) | (readmemb(0, 0x838) << 16);
        cpu_state.seg_es.access = readmemb(0, 0x839);
        cpu_state.seg_es.limit = readmemw(0, 0x83A);
        cs = readmemw(0, 0x83C) | (readmemb(0, 0x83E) << 16);
        cpu_state.seg_cs.access = readmemb(0, 0x83F);
        cpu_state.seg_cs.limit = readmemw(0, 0x840);
        ss = readmemw(0, 0x842) | (readmemb(0, 0x844) << 16);
        cpu_state.seg_ss.access = readmemb(0, 0x845);
        cpu_state.seg_ss.limit = readmemw(0, 0x846);
        if (cpu_state.seg_ss.base == 0 && cpu_state.seg_ss.limit_low == 0 && cpu_state.seg_ss.limit_high == 0xffffffff)
                cpu_cur_status &= ~CPU_STATUS_NOTFLATSS;
        else
                cpu_cur_status |= CPU_STATUS_NOTFLATSS;
        ds = readmemw(0, 0x848) | (readmemb(0, 0x84A) << 16);
        cpu_state.seg_ds.access = readmemb(0, 0x84B);
        cpu_state.seg_ds.limit = readmemw(0, 0x84C);
        if (cpu_state.seg_ds.base == 0 && cpu_state.seg_ds.limit_low == 0 && cpu_state.seg_ds.limit_high == 0xffffffff)
                cpu_cur_status &= ~CPU_STATUS_NOTFLATDS;
        else
                cpu_cur_status |= CPU_STATUS_NOTFLATDS;
        gdt.base = readmemw(0, 0x84E) | (readmemb(0, 0x850) << 16);
        gdt.limit = readmemw(0, 0x852);
        ldt.base = readmemw(0, 0x854) | (readmemb(0, 0x856) << 16);
        ldt.access = readmemb(0, 0x857);
        ldt.limit = readmemw(0, 0x858);
        idt.base = readmemw(0, 0x85A) | (readmemb(0, 0x85C) << 16);
        idt.limit = readmemw(0, 0x85E);
        tr.base = readmemw(0, 0x860) | (readmemb(0, 0x862) << 16);
        tr.access = readmemb(0, 0x863);
        tr.limit = readmemw(0, 0x864);
        CLOCK_CYCLES(195);
        PREFETCH_RUN(195, 1, -1, 51,0,0,0, 0);
        return 0;
}      

static void set_segment_limit(x86seg *s, uint8_t segdat3)
{
        if ((s->access & 0x18) != 0x10 || !(s->access & (1 << 2))) /*expand-down*/
        {
                s->limit_high = s->limit;
                s->limit_low = 0;
        }
        else
        {
                s->limit_high = (segdat3 & 0x40) ? 0xffffffff : 0xffff;
                s->limit_low = s->limit + 1;
        }
}

static void loadall_load_segment(uint32_t addr, x86seg *s)
{
	uint32_t attrib = readmeml(0, addr);
	uint32_t segdat3 = (attrib >> 16) & 0xff;
	s->access = (attrib >> 8) & 0xff;
	s->base = readmeml(0, addr + 4);
	s->limit = readmeml(0, addr + 8);

	if (s == &cpu_state.seg_cs)
                use32 = (segdat3 & 0x40) ? 0x300 : 0;
	if (s == &cpu_state.seg_ss)
                stack32 = (segdat3 & 0x40) ? 1 : 0;
                
	cpu_cur_status &= ~(CPU_STATUS_USE32 | CPU_STATUS_STACK32);
	if (use32)
	       cpu_cur_status |= CPU_STATUS_USE32;
	if (stack32)
	       cpu_cur_status |= CPU_STATUS_STACK32;

	set_segment_limit(s, segdat3);

        if (s == &cpu_state.seg_ds)
        {
                if (s->base == 0 && s->limit_low == 0 && s->limit_high == 0xffffffff)
                        cpu_cur_status &= ~CPU_STATUS_NOTFLATDS;
                else
                        cpu_cur_status |= CPU_STATUS_NOTFLATDS;
        }
        if (s == &cpu_state.seg_ss)
        {
                if (s->base == 0 && s->limit_low == 0 && s->limit_high == 0xffffffff)
                        cpu_cur_status &= ~CPU_STATUS_NOTFLATSS;
                else
                        cpu_cur_status |= CPU_STATUS_NOTFLATSS;
        }
}

static int opLOADALL386(uint32_t fetchdat)
{
	uint32_t la_addr = es + EDI;

	cr0 = readmeml(0, la_addr);
        cpu_state.flags = readmemw(0, la_addr + 4);
        cpu_state.eflags = readmemw(0, la_addr + 6);
        flags_extract();
        cpu_state.pc = readmeml(0, la_addr + 8);
	EDI = readmeml(0, la_addr + 0xC);
	ESI = readmeml(0, la_addr + 0x10);
	EBP = readmeml(0, la_addr + 0x14);
	ESP = readmeml(0, la_addr + 0x18);
	EBX = readmeml(0, la_addr + 0x1C);
	EDX = readmeml(0, la_addr + 0x20);
	ECX = readmeml(0, la_addr + 0x24);
	EAX = readmeml(0, la_addr + 0x28);
	dr[6] = readmeml(0, la_addr + 0x2C);
	dr[7] = readmeml(0, la_addr + 0x30);
	tr.seg = readmemw(0, la_addr + 0x34);
	ldt.seg = readmemw(0, la_addr + 0x38);
        GS = readmemw(0, la_addr + 0x3C);
        FS = readmemw(0, la_addr + 0x40);
        DS = readmemw(0, la_addr + 0x44);
        SS = readmemw(0, la_addr + 0x48);
        CS = readmemw(0, la_addr + 0x4C);
        ES = readmemw(0, la_addr + 0x50);

	loadall_load_segment(la_addr + 0x54, &tr);
	loadall_load_segment(la_addr + 0x60, &idt);
	loadall_load_segment(la_addr + 0x6c, &gdt);
	loadall_load_segment(la_addr + 0x78, &ldt);
	loadall_load_segment(la_addr + 0x84, &cpu_state.seg_gs);
	loadall_load_segment(la_addr + 0x90, &cpu_state.seg_fs);
	loadall_load_segment(la_addr + 0x9c, &cpu_state.seg_ds);
	loadall_load_segment(la_addr + 0xa8, &cpu_state.seg_ss);
	loadall_load_segment(la_addr + 0xb4, &cpu_state.seg_cs);
	loadall_load_segment(la_addr + 0xc0, &cpu_state.seg_es);

	if (CPL==3 && oldcpl!=3) flushmmucache_cr3();
	oldcpl = CPL;

	CLOCK_CYCLES(350);
        return 0;
}                          

static int opCPUID(uint32_t fetchdat)
{
        if (CPUID)
        {
                cpu_CPUID();
                CLOCK_CYCLES(9);
                return 0;
        }
        cpu_state.pc = cpu_state.oldpc;
        x86illegal();
        return 1;
}

static int opRDMSR(uint32_t fetchdat)
{
        if (cpu_has_feature(CPU_FEATURE_MSR))
        {
                cpu_RDMSR();
                CLOCK_CYCLES(9);
                return 0;
        }
        cpu_state.pc = cpu_state.oldpc;
        x86illegal();
        return 1;
}

static int opWRMSR(uint32_t fetchdat)
{
        if (cpu_has_feature(CPU_FEATURE_MSR))
        {
                cpu_WRMSR();
                CLOCK_CYCLES(9);
                return 0;
        }
        cpu_state.pc = cpu_state.oldpc;
        x86illegal();
        return 1;
}

static int opRSM(uint32_t fetchdat)
{
        if(!in_smm)
        {
                leave_smm();
                if(smi_latched) enter_smm();
                return 0;
        }
        cpu_state.pc = cpu_state.oldpc;
        x86illegal();
        return 1;
}