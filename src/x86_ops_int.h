/* Copyright holders: Sarah Walker
   see COPYING for more details
*/
static int opINT3(uint32_t fetchdat)
{
        if ((cr0 & 1) && (eflags & VM_FLAG) && (IOPL != 3))
        {
                x86gpf(NULL,0);
                return 1;
        }
        x86_int_sw(3);
        CLOCK_CYCLES((is486) ? 44 : 59);
        return 1;
}

static int opINT(uint32_t fetchdat)
{
        uint8_t temp;
        
        /*if (msw&1) pclog("INT %i %i %i\n",cr0&1,eflags&VM_FLAG,IOPL);*/
        if ((cr0 & 1) && (eflags & VM_FLAG) && (IOPL != 3))
        {
                x86gpf(NULL,0);
                return 1;
        }
        temp = getbytef();
//        /*if (temp == 0x10 && AH == 0xe) */pclog("INT %02X : %04X %04X %04X %04X   %c   %04X:%04X\n", temp, AX, BX, CX, DX, (AL < 32) ? ' ' : AL, CS, pc);
//        if (CS == 0x0028 && pc == 0xC03813C0)
//                output = 3;
/*      if (pc == 0x8028009A)
                output = 3;
        if (pc == 0x80282B6F)
        {
                __times++;
                if (__times == 2)
                        fatal("WRONG\n");
        }
        if (pc == 0x802809CE)
                fatal("RIGHT\n");*/
//        if (CS == 0x0028 && pc == 0x80037FE9)
//                output = 3;
//if (CS == 0x9087 && pc == 0x3763)
//        fatal("Here\n");
//if (CS==0x9087 && pc == 0x0850)
//        output = 1;

/*      if (output && pc == 0x80033008)
      {
                __times++;
                if (__times == 2)
                      fatal("WRONG\n");
        }*/
/*        if (output && pc == 0x80D8)
        {
                __times++;
                if (__times == 2)
                        fatal("RIGHT\n");
        }*/

        x86_int_sw(temp);
        return 1;
}

static int opINTO(uint32_t fetchdat)
{
        if ((cr0 & 1) && (eflags & VM_FLAG) && (IOPL != 3))
        {
                x86gpf(NULL,0);
                return 1;
        }
        if (VF_SET())
        {
                oldpc = cpu_state.pc;
                x86_int_sw(4);
                return 1;
        }
        CLOCK_CYCLES(3);
        return 0;
}

