static int
opIN_AL_imm(uint32_t fetchdat)
{
    uint16_t port = (uint16_t) getbytef();
    check_io_perm(port, 1);
    AL = inb(port);
    CLOCK_CYCLES(12);
    PREFETCH_RUN(12, 2, -1, 1, 0, 0, 0, 0);
    if (nmi && nmi_enable && nmi_mask)
        return 1;
    return 0;
}
static int
opIN_AX_imm(uint32_t fetchdat)
{
    uint16_t port = (uint16_t) getbytef();
    check_io_perm(port, 2);
    AX = inw(port);
    CLOCK_CYCLES(12);
    PREFETCH_RUN(12, 2, -1, 1, 0, 0, 0, 0);
    if (nmi && nmi_enable && nmi_mask)
        return 1;
    return 0;
}
static int
opIN_EAX_imm(uint32_t fetchdat)
{
    uint16_t port = (uint16_t) getbytef();
    check_io_perm(port, 4);
    EAX = inl(port);
    CLOCK_CYCLES(12);
    PREFETCH_RUN(12, 2, -1, 0, 1, 0, 0, 0);
    if (nmi && nmi_enable && nmi_mask)
        return 1;
    return 0;
}

static int
opOUT_AL_imm(uint32_t fetchdat)
{
    uint16_t port = (uint16_t) getbytef();
    check_io_perm(port, 1);
    outb(port, AL);
    CLOCK_CYCLES(10);
    PREFETCH_RUN(10, 2, -1, 0, 0, 1, 0, 0);
    if (port == 0x64)
        return x86_was_reset;
    if (nmi && nmi_enable && nmi_mask)
        return 1;
    return 0;
}
static int
opOUT_AX_imm(uint32_t fetchdat)
{
    uint16_t port = (uint16_t) getbytef();
    check_io_perm(port, 2);
    outw(port, AX);
    CLOCK_CYCLES(10);
    PREFETCH_RUN(10, 2, -1, 0, 0, 1, 0, 0);
    if (nmi && nmi_enable && nmi_mask)
        return 1;
    return 0;
}
static int
opOUT_EAX_imm(uint32_t fetchdat)
{
    uint16_t port = (uint16_t) getbytef();
    check_io_perm(port, 4);
    outl(port, EAX);
    CLOCK_CYCLES(10);
    PREFETCH_RUN(10, 2, -1, 0, 0, 0, 1, 0);
    if (nmi && nmi_enable && nmi_mask)
        return 1;
    return 0;
}

static int
opIN_AL_DX(UNUSED(uint32_t fetchdat))
{
    check_io_perm(DX, 1);
    AL = inb(DX);
    CLOCK_CYCLES(12);
    PREFETCH_RUN(12, 1, -1, 1, 0, 0, 0, 0);
    if (nmi && nmi_enable && nmi_mask)
        return 1;
    return 0;
}
static int
opIN_AX_DX(UNUSED(uint32_t fetchdat))
{
    check_io_perm(DX, 2);
    AX = inw(DX);
    CLOCK_CYCLES(12);
    PREFETCH_RUN(12, 1, -1, 1, 0, 0, 0, 0);
    if (nmi && nmi_enable && nmi_mask)
        return 1;
    return 0;
}
static int
opIN_EAX_DX(UNUSED(uint32_t fetchdat))
{
    check_io_perm(DX, 4);
    EAX = inl(DX);
    CLOCK_CYCLES(12);
    PREFETCH_RUN(12, 1, -1, 0, 1, 0, 0, 0);
    if (nmi && nmi_enable && nmi_mask)
        return 1;
    return 0;
}

static int
opOUT_AL_DX(UNUSED(uint32_t fetchdat))
{
    check_io_perm(DX, 1);
    outb(DX, AL);
    CLOCK_CYCLES(11);
    PREFETCH_RUN(11, 1, -1, 0, 0, 1, 0, 0);
    if (nmi && nmi_enable && nmi_mask)
        return 1;
    return x86_was_reset;
}
static int
opOUT_AX_DX(UNUSED(uint32_t fetchdat))
{
    check_io_perm(DX, 2);
    outw(DX, AX);
    CLOCK_CYCLES(11);
    PREFETCH_RUN(11, 1, -1, 0, 0, 1, 0, 0);
    if (nmi && nmi_enable && nmi_mask)
        return 1;
    return 0;
}
static int
opOUT_EAX_DX(UNUSED(uint32_t fetchdat))
{
    check_io_perm(DX, 4);
    outl(DX, EAX);
    PREFETCH_RUN(11, 1, -1, 0, 0, 0, 1, 0);
    if (nmi && nmi_enable && nmi_mask)
        return 1;
    return 0;
}
