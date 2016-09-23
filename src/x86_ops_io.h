
static int opIN_AL_imm(uint32_t fetchdat)
{       
        uint16_t port = (uint16_t)getbytef();
        check_io_perm(port);
        AL = inb(port);
        CLOCK_CYCLES(12);
        return 0;
}
static int opIN_AX_imm(uint32_t fetchdat)
{
        uint16_t port = (uint16_t)getbytef();
        check_io_perm(port);
        check_io_perm(port + 1);
        AX = inw(port);
        CLOCK_CYCLES(12);
        return 0;
}
static int opIN_EAX_imm(uint32_t fetchdat)
{
        uint16_t port = (uint16_t)getbytef();
        check_io_perm(port);
        check_io_perm(port + 1);
        check_io_perm(port + 2);
        check_io_perm(port + 3);
        EAX = inl(port);
        CLOCK_CYCLES(12);
        return 0;
}

static int opOUT_AL_imm(uint32_t fetchdat)
{
        uint16_t port = (uint16_t)getbytef();        
        check_io_perm(port);
        outb(port, AL);
        CLOCK_CYCLES(10);
        if (port == 0x64)
                return x86_was_reset;
        return 0;
}
static int opOUT_AX_imm(uint32_t fetchdat)
{
        uint16_t port = (uint16_t)getbytef();        
        check_io_perm(port);
        check_io_perm(port + 1);
        outw(port, AX);
        CLOCK_CYCLES(10);
        return 0;
}
static int opOUT_EAX_imm(uint32_t fetchdat)
{
        uint16_t port = (uint16_t)getbytef();        
        check_io_perm(port);
        check_io_perm(port + 1);
        check_io_perm(port + 2);
        check_io_perm(port + 3);
        outl(port, EAX);
        CLOCK_CYCLES(10);
        return 0;
}

static int opIN_AL_DX(uint32_t fetchdat)
{       
        check_io_perm(DX);
        AL = inb(DX);
        CLOCK_CYCLES(12);
        return 0;
}
static int opIN_AX_DX(uint32_t fetchdat)
{
        check_io_perm(DX);
        check_io_perm(DX + 1);
        AX = inw(DX);
        CLOCK_CYCLES(12);
        return 0;
}
static int opIN_EAX_DX(uint32_t fetchdat)
{
        check_io_perm(DX);
        check_io_perm(DX + 1);
        check_io_perm(DX + 2);
        check_io_perm(DX + 3);
        EAX = inl(DX);
        CLOCK_CYCLES(12);
        return 0;
}

static int opOUT_AL_DX(uint32_t fetchdat)
{
        check_io_perm(DX);
        outb(DX, AL);
        CLOCK_CYCLES(11);
        return x86_was_reset;
}
static int opOUT_AX_DX(uint32_t fetchdat)
{
        //pclog("OUT_AX_DX %04X %04X\n", DX, AX);
        check_io_perm(DX);
        check_io_perm(DX + 1);
        outw(DX, AX);
        CLOCK_CYCLES(11);
        return 0;
}
static int opOUT_EAX_DX(uint32_t fetchdat)
{
        check_io_perm(DX);
        check_io_perm(DX + 1);
        check_io_perm(DX + 2);
        check_io_perm(DX + 3);
        outl(DX, EAX);
        CLOCK_CYCLES(11);
        return 0;
}
