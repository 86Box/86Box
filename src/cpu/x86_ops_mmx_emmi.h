/* Notes: "src2" means "first operand" */

static int
opPMULHRWC_a16(UNUSED(uint32_t fetchdat))
{
    MMX_REG  src;
    MMX_REG *dst;

    if (!(ccr7 & 1)) {
        x86illegal();
        return 1;
    }

    MMX_ENTER();

    fetch_ea_16(fetchdat);

    dst = MMX_GETREGP(cpu_reg);

    MMX_GETSRC();

    dst->w[0] = ((int32_t) (dst->sw[0] * (int32_t) src.sw[0]) + 0x4000) >> 15;
    dst->w[1] = ((int32_t) (dst->sw[1] * (int32_t) src.sw[1]) + 0x4000) >> 15;
    dst->w[2] = ((int32_t) (dst->sw[2] * (int32_t) src.sw[2]) + 0x4000) >> 15;
    dst->w[3] = ((int32_t) (dst->sw[3] * (int32_t) src.sw[3]) + 0x4000) >> 15;

    MMX_SETEXP(cpu_reg);

    return 0;
}

static int
opPMULHRWC_a32(UNUSED(uint32_t fetchdat))
{
    MMX_REG  src;
    MMX_REG *dst;

    if (!(ccr7 & 1)) {
        x86illegal();
        return 1;
    }

    MMX_ENTER();

    fetch_ea_32(fetchdat);

    dst = MMX_GETREGP(cpu_reg);

    MMX_GETSRC();

    dst->w[0] = ((int32_t) (dst->sw[0] * (int32_t) src.sw[0]) + 0x4000) >> 15;
    dst->w[1] = ((int32_t) (dst->sw[1] * (int32_t) src.sw[1]) + 0x4000) >> 15;
    dst->w[2] = ((int32_t) (dst->sw[2] * (int32_t) src.sw[2]) + 0x4000) >> 15;
    dst->w[3] = ((int32_t) (dst->sw[3] * (int32_t) src.sw[3]) + 0x4000) >> 15;

    MMX_SETEXP(cpu_reg);

    return 0;
}

static int
opPMULHRIW_a16(uint32_t fetchdat)
{
    MMX_REG  src, src2;
    MMX_REG *dst;

    if (!(ccr7 & 1)) {
        x86illegal();
        return 1;
    }

    MMX_ENTER();

    fetch_ea_16(fetchdat);

    src2 = MMX_GETREG(cpu_reg);
    dst  = MMX_GETREGP(cpu_reg ^ 1);

    MMX_GETSRC();

    dst->w[0] = (((int32_t) src2.sw[0] * (int32_t) src.sw[0]) + 0x4000) >> 15;
    dst->w[1] = (((int32_t) src2.sw[1] * (int32_t) src.sw[1]) + 0x4000) >> 15;
    dst->w[2] = (((int32_t) src2.sw[2] * (int32_t) src.sw[2]) + 0x4000) >> 15;
    dst->w[3] = (((int32_t) src2.sw[3] * (int32_t) src.sw[3]) + 0x4000) >> 15;
    
    MMX_SETEXP(cpu_reg ^ 1);
    return 0;
}

static int
opPMULHRIW_a32(uint32_t fetchdat)
{
    MMX_REG  src, src2;
    MMX_REG *dst;

    if (!(ccr7 & 1)) {
        x86illegal();
        return 1;
    }

    MMX_ENTER();

    fetch_ea_32(fetchdat);

    src2 = MMX_GETREG(cpu_reg);
    dst  = MMX_GETREGP(cpu_reg ^ 1);

    MMX_GETSRC();

    dst->w[0] = (((int32_t) src2.sw[0] * (int32_t) src.sw[0]) + 0x4000) >> 15;
    dst->w[1] = (((int32_t) src2.sw[1] * (int32_t) src.sw[1]) + 0x4000) >> 15;
    dst->w[2] = (((int32_t) src2.sw[2] * (int32_t) src.sw[2]) + 0x4000) >> 15;
    dst->w[3] = (((int32_t) src2.sw[3] * (int32_t) src.sw[3]) + 0x4000) >> 15;
    
    MMX_SETEXP(cpu_reg ^ 1);
    return 0;
}

static int
opPDISTIB_a16(uint32_t fetchdat)
{
    MMX_REG  src, src2;
    MMX_REG *dst;

    if (!(ccr7 & 1)) {
        x86illegal();
        return 1;
    }

    MMX_ENTER();

    fetch_ea_16(fetchdat);

    if (cpu_mod == 3) {
        x86illegal();
        return 1;
    }

    src2 = MMX_GETREG(cpu_reg);
    dst  = MMX_GETREGP(cpu_reg ^ 1);

    MMX_GETSRC();

    dst->b[0] = USATB(dst->b[0] + abs(src2.b[0] - src.b[0]));
    dst->b[1] = USATB(dst->b[1] + abs(src2.b[1] - src.b[1]));
    dst->b[2] = USATB(dst->b[2] + abs(src2.b[2] - src.b[2]));
    dst->b[3] = USATB(dst->b[3] + abs(src2.b[3] - src.b[3]));
    dst->b[4] = USATB(dst->b[4] + abs(src2.b[4] - src.b[4]));
    dst->b[5] = USATB(dst->b[5] + abs(src2.b[5] - src.b[5]));
    dst->b[6] = USATB(dst->b[6] + abs(src2.b[6] - src.b[6]));
    dst->b[7] = USATB(dst->b[7] + abs(src2.b[7] - src.b[7]));

    MMX_SETEXP(cpu_reg ^ 1);
    return 0;
}

static int
opPDISTIB_a32(uint32_t fetchdat)
{
    MMX_REG  src, src2;
    MMX_REG *dst;

    if (!(ccr7 & 1)) {
        x86illegal();
        return 1;
    }

    MMX_ENTER();

    fetch_ea_32(fetchdat);

    if (cpu_mod == 3) {
        x86illegal();
        return 1;
    }

    src2 = MMX_GETREG(cpu_reg);
    dst  = MMX_GETREGP(cpu_reg ^ 1);

    MMX_GETSRC();

    dst->b[0] = USATB(dst->b[0] + abs(src2.b[0] - src.b[0]));
    dst->b[1] = USATB(dst->b[1] + abs(src2.b[1] - src.b[1]));
    dst->b[2] = USATB(dst->b[2] + abs(src2.b[2] - src.b[2]));
    dst->b[3] = USATB(dst->b[3] + abs(src2.b[3] - src.b[3]));
    dst->b[4] = USATB(dst->b[4] + abs(src2.b[4] - src.b[4]));
    dst->b[5] = USATB(dst->b[5] + abs(src2.b[5] - src.b[5]));
    dst->b[6] = USATB(dst->b[6] + abs(src2.b[6] - src.b[6]));
    dst->b[7] = USATB(dst->b[7] + abs(src2.b[7] - src.b[7]));

    MMX_SETEXP(cpu_reg ^ 1);
    return 0;
}

static int
opPMACHRIW_a16(uint32_t fetchdat)
{
    MMX_REG  src, src2;
    MMX_REG *dst;

    if (!(ccr7 & 1)) {
        x86illegal();
        return 1;
    }

    MMX_ENTER();

    fetch_ea_16(fetchdat);

    if (cpu_mod == 3) {
        x86illegal();
        return 1;
    }

    src2 = MMX_GETREG(cpu_reg);
    dst  = MMX_GETREGP(cpu_reg ^ 1);

    MMX_GETSRC();

    dst->w[0] += (((int32_t) src2.sw[0] * (int32_t) src.sw[0]) + 0x4000) >> 15;
    dst->w[1] += (((int32_t) src2.sw[1] * (int32_t) src.sw[1]) + 0x4000) >> 15;
    dst->w[2] += (((int32_t) src2.sw[2] * (int32_t) src.sw[2]) + 0x4000) >> 15;
    dst->w[3] += (((int32_t) src2.sw[3] * (int32_t) src.sw[3]) + 0x4000) >> 15;
    
    MMX_SETEXP(cpu_reg ^ 1);
    return 0;
}

static int
opPMACHRIW_a32(uint32_t fetchdat)
{
    MMX_REG  src, src2;
    MMX_REG *dst;

    if (!(ccr7 & 1)) {
        x86illegal();
        return 1;
    }

    MMX_ENTER();

    fetch_ea_32(fetchdat);

    if (cpu_mod == 3) {
        x86illegal();
        return 1;
    }

    src2 = MMX_GETREG(cpu_reg);
    dst  = MMX_GETREGP(cpu_reg ^ 1);

    MMX_GETSRC();

    dst->w[0] += (((int32_t) src2.sw[0] * (int32_t) src.sw[0]) + 0x4000) >> 15;
    dst->w[1] += (((int32_t) src2.sw[1] * (int32_t) src.sw[1]) + 0x4000) >> 15;
    dst->w[2] += (((int32_t) src2.sw[2] * (int32_t) src.sw[2]) + 0x4000) >> 15;
    dst->w[3] += (((int32_t) src2.sw[3] * (int32_t) src.sw[3]) + 0x4000) >> 15;
    
    MMX_SETEXP(cpu_reg ^ 1);
    return 0;
}

static int
opPADDSIW_a16(uint32_t fetchdat)
{
    MMX_REG  src, src2;
    MMX_REG *dst;

    if (!(ccr7 & 1)) {
        x86illegal();
        return 1;
    }

    MMX_ENTER();

    fetch_ea_16(fetchdat);

    src2 = MMX_GETREG(cpu_reg);
    dst  = MMX_GETREGP(cpu_reg ^ 1);

    MMX_GETSRC();

    dst->sw[0] = SSATW(src2.sw[0] + src.sw[0]);
    dst->sw[1] = SSATW(src2.sw[1] + src.sw[1]);
    dst->sw[2] = SSATW(src2.sw[2] + src.sw[2]);
    dst->sw[3] = SSATW(src2.sw[3] + src.sw[3]);

    MMX_SETEXP(cpu_reg ^ 1);

    return 0;
}

static int
opPADDSIW_a32(uint32_t fetchdat)
{
    MMX_REG  src, src2;
    MMX_REG *dst;

    if (!(ccr7 & 1)) {
        x86illegal();
        return 1;
    }

    MMX_ENTER();

    fetch_ea_32(fetchdat);

    src2 = MMX_GETREG(cpu_reg);
    dst  = MMX_GETREGP(cpu_reg ^ 1);

    MMX_GETSRC();

    dst->sw[0] = SSATW(src2.sw[0] + src.sw[0]);
    dst->sw[1] = SSATW(src2.sw[1] + src.sw[1]);
    dst->sw[2] = SSATW(src2.sw[2] + src.sw[2]);
    dst->sw[3] = SSATW(src2.sw[3] + src.sw[3]);

    MMX_SETEXP(cpu_reg ^ 1);

    return 0;
}

static int
opPSUBSIW_a16(uint32_t fetchdat)
{
    MMX_REG  src, src2;
    MMX_REG *dst;

    if (!(ccr7 & 1)) {
        x86illegal();
        return 1;
    }

    MMX_ENTER();

    fetch_ea_16(fetchdat);

    src2 = MMX_GETREG(cpu_reg);
    dst  = MMX_GETREGP(cpu_reg ^ 1);

    MMX_GETSRC();

    dst->sw[0] = SSATW(src2.sw[0] - src.sw[0]);
    dst->sw[1] = SSATW(src2.sw[1] - src.sw[1]);
    dst->sw[2] = SSATW(src2.sw[2] - src.sw[2]);
    dst->sw[3] = SSATW(src2.sw[3] - src.sw[3]);

    MMX_SETEXP(cpu_reg);

    return 0;
}

static int
opPSUBSIW_a32(uint32_t fetchdat)
{
    MMX_REG  src, src2;
    MMX_REG *dst;

    if (!(ccr7 & 1)) {
        x86illegal();
        return 1;
    }

    MMX_ENTER();

    fetch_ea_32(fetchdat);

    src2 = MMX_GETREG(cpu_reg);
    dst  = MMX_GETREGP(cpu_reg ^ 1);

    MMX_GETSRC();

    dst->sw[0] = SSATW(src2.sw[0] - src.sw[0]);
    dst->sw[1] = SSATW(src2.sw[1] - src.sw[1]);
    dst->sw[2] = SSATW(src2.sw[2] - src.sw[2]);
    dst->sw[3] = SSATW(src2.sw[3] - src.sw[3]);

    MMX_SETEXP(cpu_reg);

    return 0;
}

static int
opPAVEB_a16(uint32_t fetchdat)
{
    MMX_REG  src;
    MMX_REG *dst;

    if (!(ccr7 & 1)) {
        x86illegal();
        return 1;
    }

    MMX_ENTER();

    fetch_ea_16(fetchdat);

    dst = MMX_GETREGP(cpu_reg);

    MMX_GETSRC();

    dst->b[0] = (dst->b[0] + src.b[0]) >> 1;
    dst->b[1] = (dst->b[1] + src.b[1]) >> 1;
    dst->b[2] = (dst->b[2] + src.b[2]) >> 1;
    dst->b[3] = (dst->b[3] + src.b[3]) >> 1;
    dst->b[4] = (dst->b[4] + src.b[4]) >> 1;
    dst->b[5] = (dst->b[5] + src.b[5]) >> 1;
    dst->b[6] = (dst->b[6] + src.b[6]) >> 1;
    dst->b[7] = (dst->b[7] + src.b[7]) >> 1;

    MMX_SETEXP(cpu_reg);
    return 0;
}

static int
opPAVEB_a32(uint32_t fetchdat)
{
    MMX_REG  src;
    MMX_REG *dst;

    if (!(ccr7 & 1)) {
        x86illegal();
        return 1;
    }

    MMX_ENTER();

    fetch_ea_32(fetchdat);

    dst = MMX_GETREGP(cpu_reg);

    MMX_GETSRC();

    dst->b[0] = (dst->b[0] + src.b[0]) >> 1;
    dst->b[1] = (dst->b[1] + src.b[1]) >> 1;
    dst->b[2] = (dst->b[2] + src.b[2]) >> 1;
    dst->b[3] = (dst->b[3] + src.b[3]) >> 1;
    dst->b[4] = (dst->b[4] + src.b[4]) >> 1;
    dst->b[5] = (dst->b[5] + src.b[5]) >> 1;
    dst->b[6] = (dst->b[6] + src.b[6]) >> 1;
    dst->b[7] = (dst->b[7] + src.b[7]) >> 1;

    MMX_SETEXP(cpu_reg);
    return 0;
}

static int
opPMAGW_a16(uint32_t fetchdat)
{
    MMX_REG  src;
    MMX_REG *dst;

    if (!(ccr7 & 1)) {
        x86illegal();
        return 1;
    }

    MMX_ENTER();

    fetch_ea_16(fetchdat);

    dst = MMX_GETREGP(cpu_reg);

    MMX_GETSRC();

    if (abs(src.sw[0]) > abs(dst->sw[0])) dst->sw[0] = src.sw[0];
    if (abs(src.sw[1]) > abs(dst->sw[1])) dst->sw[1] = src.sw[1];
    if (abs(src.sw[2]) > abs(dst->sw[2])) dst->sw[2] = src.sw[2];
    if (abs(src.sw[3]) > abs(dst->sw[3])) dst->sw[3] = src.sw[3];

    MMX_SETEXP(cpu_reg);

    return 0;
}

static int
opPMAGW_a32(uint32_t fetchdat)
{
    MMX_REG  src;
    MMX_REG *dst;

    if (!(ccr7 & 1)) {
        x86illegal();
        return 1;
    }

    MMX_ENTER();

    fetch_ea_32(fetchdat);

    dst = MMX_GETREGP(cpu_reg);

    MMX_GETSRC();

    if (abs(src.sw[0]) > abs(dst->sw[0])) dst->sw[0] = src.sw[0];
    if (abs(src.sw[1]) > abs(dst->sw[1])) dst->sw[1] = src.sw[1];
    if (abs(src.sw[2]) > abs(dst->sw[2])) dst->sw[2] = src.sw[2];
    if (abs(src.sw[3]) > abs(dst->sw[3])) dst->sw[3] = src.sw[3];

    MMX_SETEXP(cpu_reg);

    return 0;
}

static int
opPMVZB_a16(uint32_t fetchdat)
{
    MMX_REG  src, src2;
    MMX_REG *dst;

    if (!(ccr7 & 1)) {
        x86illegal();
        return 1;
    }

    MMX_ENTER();

    fetch_ea_16(fetchdat);

    if (cpu_mod == 3) {
        x86illegal();
        return 1;
    }

    dst = MMX_GETREGP(cpu_reg);
    src2 = MMX_GETREG(cpu_reg ^ 1);

    MMX_GETSRC();

    if (src2.b[0] == 0) dst->b[0] = src.b[0];
    if (src2.b[1] == 0) dst->b[1] = src.b[1];
    if (src2.b[2] == 0) dst->b[2] = src.b[2];
    if (src2.b[3] == 0) dst->b[3] = src.b[3];
    if (src2.b[4] == 0) dst->b[4] = src.b[4];
    if (src2.b[5] == 0) dst->b[5] = src.b[5];
    if (src2.b[6] == 0) dst->b[6] = src.b[6];
    if (src2.b[7] == 0) dst->b[7] = src.b[7];

    MMX_SETEXP(cpu_reg);
    return 0;
}

static int
opPMVZB_a32(uint32_t fetchdat)
{
    MMX_REG  src, src2;
    MMX_REG *dst;

    if (!(ccr7 & 1)) {
        x86illegal();
        return 1;
    }

    MMX_ENTER();

    fetch_ea_32(fetchdat);

    if (cpu_mod == 3) {
        x86illegal();
        return 1;
    }

    dst = MMX_GETREGP(cpu_reg);
    src2 = MMX_GETREG(cpu_reg ^ 1);

    MMX_GETSRC();

    if (src2.b[0] == 0) dst->b[0] = src.b[0];
    if (src2.b[1] == 0) dst->b[1] = src.b[1];
    if (src2.b[2] == 0) dst->b[2] = src.b[2];
    if (src2.b[3] == 0) dst->b[3] = src.b[3];
    if (src2.b[4] == 0) dst->b[4] = src.b[4];
    if (src2.b[5] == 0) dst->b[5] = src.b[5];
    if (src2.b[6] == 0) dst->b[6] = src.b[6];
    if (src2.b[7] == 0) dst->b[7] = src.b[7];

    MMX_SETEXP(cpu_reg);
    return 0;
}

static int
opPMVNZB_a16(uint32_t fetchdat)
{
    MMX_REG  src, src2;
    MMX_REG *dst;

    if (!(ccr7 & 1)) {
        x86illegal();
        return 1;
    }

    MMX_ENTER();

    fetch_ea_16(fetchdat);

    if (cpu_mod == 3) {
        x86illegal();
        return 1;
    }

    dst = MMX_GETREGP(cpu_reg);
    src2 = MMX_GETREG(cpu_reg ^ 1);

    MMX_GETSRC();

    if (src2.b[0] != 0) dst->b[0] = src.b[0];
    if (src2.b[1] != 0) dst->b[1] = src.b[1];
    if (src2.b[2] != 0) dst->b[2] = src.b[2];
    if (src2.b[3] != 0) dst->b[3] = src.b[3];
    if (src2.b[4] != 0) dst->b[4] = src.b[4];
    if (src2.b[5] != 0) dst->b[5] = src.b[5];
    if (src2.b[6] != 0) dst->b[6] = src.b[6];
    if (src2.b[7] != 0) dst->b[7] = src.b[7];

    MMX_SETEXP(cpu_reg);
    return 0;
}

static int
opPMVNZB_a32(uint32_t fetchdat)
{
    MMX_REG  src, src2;
    MMX_REG *dst;

    if (!(ccr7 & 1)) {
        x86illegal();
        return 1;
    }

    MMX_ENTER();

    fetch_ea_32(fetchdat);

    if (cpu_mod == 3) {
        x86illegal();
        return 1;
    }

    dst = MMX_GETREGP(cpu_reg);
    src2 = MMX_GETREG(cpu_reg ^ 1);

    MMX_GETSRC();

    if (src2.b[0] != 0) dst->b[0] = src.b[0];
    if (src2.b[1] != 0) dst->b[1] = src.b[1];
    if (src2.b[2] != 0) dst->b[2] = src.b[2];
    if (src2.b[3] != 0) dst->b[3] = src.b[3];
    if (src2.b[4] != 0) dst->b[4] = src.b[4];
    if (src2.b[5] != 0) dst->b[5] = src.b[5];
    if (src2.b[6] != 0) dst->b[6] = src.b[6];
    if (src2.b[7] != 0) dst->b[7] = src.b[7];

    MMX_SETEXP(cpu_reg);
    return 0;
}

static int
opPMVLZB_a16(uint32_t fetchdat)
{
    MMX_REG  src, src2;
    MMX_REG *dst;

    if (!(ccr7 & 1)) {
        x86illegal();
        return 1;
    }

    MMX_ENTER();

    fetch_ea_16(fetchdat);

    if (cpu_mod == 3) {
        x86illegal();
        return 1;
    }

    dst = MMX_GETREGP(cpu_reg);
    src2 = MMX_GETREG(cpu_reg ^ 1);

    MMX_GETSRC();

    if (src2.sb[0] < 0) dst->b[0] = src.b[0];
    if (src2.sb[1] < 0) dst->b[1] = src.b[1];
    if (src2.sb[2] < 0) dst->b[2] = src.b[2];
    if (src2.sb[3] < 0) dst->b[3] = src.b[3];
    if (src2.sb[4] < 0) dst->b[4] = src.b[4];
    if (src2.sb[5] < 0) dst->b[5] = src.b[5];
    if (src2.sb[6] < 0) dst->b[6] = src.b[6];
    if (src2.sb[7] < 0) dst->b[7] = src.b[7];

    MMX_SETEXP(cpu_reg);
    return 0;
}

static int
opPMVLZB_a32(uint32_t fetchdat)
{
    MMX_REG  src, src2;
    MMX_REG *dst;

    if (!(ccr7 & 1)) {
        x86illegal();
        return 1;
    }

    MMX_ENTER();

    fetch_ea_32(fetchdat);

    if (cpu_mod == 3) {
        x86illegal();
        return 1;
    }

    dst = MMX_GETREGP(cpu_reg);
    src2 = MMX_GETREG(cpu_reg ^ 1);

    MMX_GETSRC();

    if (src2.sb[0] < 0) dst->b[0] = src.b[0];
    if (src2.sb[1] < 0) dst->b[1] = src.b[1];
    if (src2.sb[2] < 0) dst->b[2] = src.b[2];
    if (src2.sb[3] < 0) dst->b[3] = src.b[3];
    if (src2.sb[4] < 0) dst->b[4] = src.b[4];
    if (src2.sb[5] < 0) dst->b[5] = src.b[5];
    if (src2.sb[6] < 0) dst->b[6] = src.b[6];
    if (src2.sb[7] < 0) dst->b[7] = src.b[7];

    MMX_SETEXP(cpu_reg);
    return 0;
}

static int
opPMVGEZB_a16(uint32_t fetchdat)
{
    MMX_REG  src, src2;
    MMX_REG *dst;

    if (!(ccr7 & 1)) {
        x86illegal();
        return 1;
    }

    MMX_ENTER();

    fetch_ea_16(fetchdat);

    if (cpu_mod == 3) {
        x86illegal();
        return 1;
    }

    dst = MMX_GETREGP(cpu_reg);
    src2 = MMX_GETREG(cpu_reg ^ 1);

    MMX_GETSRC();

    if (src2.sb[0] >= 0) dst->b[0] = src.b[0];
    if (src2.sb[1] >= 0) dst->b[1] = src.b[1];
    if (src2.sb[2] >= 0) dst->b[2] = src.b[2];
    if (src2.sb[3] >= 0) dst->b[3] = src.b[3];
    if (src2.sb[4] >= 0) dst->b[4] = src.b[4];
    if (src2.sb[5] >= 0) dst->b[5] = src.b[5];
    if (src2.sb[6] >= 0) dst->b[6] = src.b[6];
    if (src2.sb[7] >= 0) dst->b[7] = src.b[7];

    MMX_SETEXP(cpu_reg);
    return 0;
}

static int
opPMVGEZB_a32(uint32_t fetchdat)
{
    MMX_REG  src, src2;
    MMX_REG *dst;

    if (!(ccr7 & 1)) {
        x86illegal();
        return 1;
    }

    MMX_ENTER();

    fetch_ea_32(fetchdat);

    if (cpu_mod == 3) {
        x86illegal();
        return 1;
    }

    dst = MMX_GETREGP(cpu_reg);
    src2 = MMX_GETREG(cpu_reg ^ 1);

    MMX_GETSRC();

    if (src2.sb[0] >= 0) dst->b[0] = src.b[0];
    if (src2.sb[1] >= 0) dst->b[1] = src.b[1];
    if (src2.sb[2] >= 0) dst->b[2] = src.b[2];
    if (src2.sb[3] >= 0) dst->b[3] = src.b[3];
    if (src2.sb[4] >= 0) dst->b[4] = src.b[4];
    if (src2.sb[5] >= 0) dst->b[5] = src.b[5];
    if (src2.sb[6] >= 0) dst->b[6] = src.b[6];
    if (src2.sb[7] >= 0) dst->b[7] = src.b[7];

    MMX_SETEXP(cpu_reg);
    return 0;
}