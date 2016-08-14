extern uint16_t ea_rseg;

#undef readmemb
#undef writememb


#define readmemb(s,a) ((readlookup2[(uint32_t)((s)+(a))>>12]==-1 || (s)==0xFFFFFFFF)?readmemb386l(s,a): *(uint8_t *)(readlookup2[(uint32_t)((s)+(a))>>12] + (uint32_t)((s) + (a))) )
#define readmemq(s,a) ((readlookup2[(uint32_t)((s)+(a))>>12]==-1 || (s)==0xFFFFFFFF || (((s)+(a))&0xFFF)>0xFF8)?readmemql(s,a):*(uint64_t *)(readlookup2[(uint32_t)((s)+(a))>>12]+(uint32_t)((s)+(a))))

#define writememb(s,a,v) if (writelookup2[(uint32_t)((s)+(a))>>12]==-1 || (s)==0xFFFFFFFF) writememb386l(s,a,v); else *(uint8_t *)(writelookup2[(uint32_t)((s) + (a)) >> 12] + (uint32_t)((s) + (a))) = v

#define writememw(s,a,v) if (writelookup2[(uint32_t)((s)+(a))>>12]==-1 || (s)==0xFFFFFFFF || (((s)+(a))&0xFFF)>0xFFE) writememwl(s,a,v); else *(uint16_t *)(writelookup2[(uint32_t)((s) + (a)) >> 12] + (uint32_t)((s) + (a))) = v
#define writememl(s,a,v) if (writelookup2[(uint32_t)((s)+(a))>>12]==-1 || (s)==0xFFFFFFFF || (((s)+(a))&0xFFF)>0xFFC) writememll(s,a,v); else *(uint32_t *)(writelookup2[(uint32_t)((s) + (a)) >> 12] + (uint32_t)((s) + (a))) = v
#define writememq(s,a,v) if (writelookup2[(uint32_t)((s)+(a))>>12]==-1 || (s)==0xFFFFFFFF || (((s)+(a))&0xFFF)>0xFF8) writememql(s,a,v); else *(uint64_t *)(writelookup2[(uint32_t)((s) + (a)) >> 12] + (uint32_t)((s) + (a))) = v


#if 0
#define check_io_perm(port) if (!IOPLp || (eflags&VM_FLAG)) \
                        { \
                                int tempi = checkio(port); \
                                if (abrt) return 1; \
                                if (tempi) \
                                { \
                                        x86gpf("check_io_perm(): no permission",0); \
                                        return 1; \
                                } \
                        }

#define checkio_perm(port) if (!IOPLp || (eflags&VM_FLAG)) \
                        { \
                                tempi = checkio(port); \
                                if (abrt) break; \
                                if (tempi) \
                                { \
                                        x86gpf("checkio_perm(): no permission",0); \
                                        break; \
                                } \
                        }
#endif

#define check_io_perm(port) if (msw&1 && ((CPL > IOPL) || (eflags&VM_FLAG))) \
                        { \
                                int tempi = checkio(port); \
                                if (abrt) return 1; \
                                if (tempi) \
                                { \
                                        x86gpf("check_io_perm(): no permission",0); \
                                        return 1; \
                                } \
                        }

#define checkio_perm(port) if (msw&1 && ((CPL > IOPL) || (eflags&VM_FLAG))) \
                        { \
                                tempi = checkio(port); \
                                if (abrt) break; \
                                if (tempi) \
                                { \
                                        x86gpf("checkio_perm(): no permission",0); \
                                        break; \
                                } \
                        }
#if 0
#define CHECK_READ(chseg, low, high)  \
        if ((low < (chseg)->limit_low) || (high > (chseg)->limit_high))       \
        {                                       \
                x86gpf("Limit check (READ)", 0);       \
                return 1;                       \
	}					\
	if (msw&1 && !(eflags&VM_FLAG) && !((chseg)->access & 0x80))	\
	{					\
		if ((chseg) == &_ss)		\
			x86ss(NULL,(chseg)->seg & 0xfffc);	\
		else				\
			pclog("Read from seg not present", (chseg)->seg & 0xfffc);	\
		return 1;			\
        }

#define CHECK_WRITE(chseg, low, high)  \
        if ((low < (chseg)->limit_low) || (high > (chseg)->limit_high) || !((chseg)->access & 2))       \
        {                                       \
                x86gpf("Limit check (WRITE)", 0);       \
                return 1;                       \
	}					\
	if (msw&1 && !(eflags&VM_FLAG) && !((chseg)->access & 0x80))	\
	{					\
		if ((chseg) == &_ss)		\
			x86ss(NULL,(chseg)->seg & 0xfffc);	\
		else				\
			x86np("Write to seg not present", (chseg)->seg & 0xfffc);	\
		return 1;			\
        }

#if 0
#define CHECK_WRITE_REP(chseg, low, high)  \
        if ((low < (chseg)->limit_low) || (high > (chseg)->limit_high))       \
        {                                       \
                x86gpf("Limit check (WRITE_REP)", 0);       \
                if (1 != 1)  break;                       \
	}					\
	if (msw&1 && !(eflags&VM_FLAG) && !((chseg)->access & 0x80))	\
	{					\
		if ((chseg) == &_ss)		\
			x86ss(NULL,(chseg)->seg & 0xfffc);	\
		else				\
			x86np("Write (REP) to seg not present", (chseg)->seg & 0xfffc);	\
		break;                       \
        }
#endif

#define CHECK_WRITE_REP(chseg, low, high)  \
	if (msw&1 && !(eflags&VM_FLAG) && !((chseg)->access & 0x80))	\
	{					\
		if ((chseg) == &_ss)		\
			x86ss(NULL,(chseg)->seg & 0xfffc);	\
		else				\
			x86np("Write (REP) to seg not present", (chseg)->seg & 0xfffc);	\
		break;                       \
        }
#endif

#define CHECK_READ(chseg, low, high)  \
        if ((low < (chseg)->limit_low) || (high > (chseg)->limit_high))       \
        {                                       \
                x86gpf("Limit check", 0);       \
                return 1;                       \
	}					\
	if (msw&1 && !(eflags&VM_FLAG) && !((chseg)->access & 0x80))	\
	{					\
		if ((chseg) == &_ss)		\
			x86ss(NULL,(chseg)->seg & 0xfffc);	\
		else				\
			x86np("Read from seg not present", (chseg)->seg & 0xfffc);	\
		return 1;			\
        }

#define CHECK_WRITE(chseg, low, high)  \
        if ((low < (chseg)->limit_low) || (high > (chseg)->limit_high) || !((chseg)->access & 2))       \
        {                                       \
                x86gpf("Limit check", 0);       \
                return 1;                       \
	}					\
	if (msw&1 && !(eflags&VM_FLAG) && !((chseg)->access & 0x80))	\
	{					\
		if ((chseg) == &_ss)		\
			x86ss(NULL,(chseg)->seg & 0xfffc);	\
		else				\
			x86np("Write to seg not present", (chseg)->seg & 0xfffc);	\
		return 1;			\
        }

#define CHECK_WRITE_REP(chseg, low, high)  \
        if ((low < (chseg)->limit_low) || (high > (chseg)->limit_high))       \
        {                                       \
                x86gpf("Limit check", 0);       \
                break;                       \
	}					\
	if (msw&1 && !(eflags&VM_FLAG) && !((chseg)->access & 0x80))	\
	{					\
		if ((chseg) == &_ss)		\
			x86ss(NULL,(chseg)->seg & 0xfffc);	\
		else				\
			x86np("Write (REP) to seg not present", (chseg)->seg & 0xfffc);	\
		break;                       \
        }


#define NOTRM   if (!(msw & 1) || (eflags & VM_FLAG))\
                { \
                        x86_int(6); \
                        return 1; \
                }




static inline uint8_t fastreadb(uint32_t a)
{
        uint8_t *t;
        
        if ((a >> 12) == pccache) 
                return *((uint8_t *)&pccache2[a]);
        t = getpccache(a);
        if (abrt)
                return;
        pccache = a >> 12;
        pccache2 = t;
        return *((uint8_t *)&pccache2[a]);
}

static inline uint16_t fastreadw(uint32_t a)
{
        uint8_t *t;
        uint16_t val;
        if ((a&0xFFF)>0xFFE)
        {
                val = readmemb(0, a);
                val |= (readmemb(0, a + 1) << 8);
                return val;
        }
        if ((a>>12)==pccache) return *((uint16_t *)&pccache2[a]);
        t = getpccache(a);
        if (abrt)
                return;

        pccache = a >> 12;
        pccache2 = t;
        return *((uint16_t *)&pccache2[a]);
}

static inline uint32_t fastreadl(uint32_t a)
{
        uint8_t *t;
        uint32_t val;
        if ((a&0xFFF)<0xFFD)
        {
                if ((a>>12)!=pccache)
                {
                        t = getpccache(a);
                        if (abrt)
                                return 0;
                        pccache2 = t;
                        pccache=a>>12;
                        //return *((uint32_t *)&pccache2[a]);
                }
                return *((uint32_t *)&pccache2[a]);
        }
        val  =readmemb(0,a);
        val |=(readmemb(0,a+1)<<8);
        val |=(readmemb(0,a+2)<<16);
        val |=(readmemb(0,a+3)<<24);
        return val;
}

static inline uint8_t getbyte()
{
        cpu_state.pc++;
        return fastreadb(cs + (cpu_state.pc - 1));
}

static inline uint16_t getword()
{
        cpu_state.pc+=2;
        return fastreadw(cs+(cpu_state.pc-2));
}

static inline uint32_t getlong()
{
        cpu_state.pc+=4;
        return fastreadl(cs+(cpu_state.pc-4));
}

static inline uint64_t getquad()
{
        cpu_state.pc+=8;
        return fastreadl(cs+(cpu_state.pc-8)) | ((uint64_t)fastreadl(cs+(cpu_state.pc-4)) << 32);
}



static inline uint8_t geteab()
{
        if (cpu_mod == 3)
                return (cpu_rm & 4) ? cpu_state.regs[cpu_rm & 3].b.h : cpu_state.regs[cpu_rm&3].b.l;
        if (eal_r)
                return *(uint8_t *)eal_r;
        return readmemb(easeg, eaaddr);
}

static inline uint16_t geteaw()
{
        if (cpu_mod == 3)
                return cpu_state.regs[cpu_rm].w;
//        cycles-=3;
        if (eal_r)
                return *(uint16_t *)eal_r;
        return readmemw(easeg, eaaddr);
}

static inline uint32_t geteal()
{
        if (cpu_mod == 3)
                return cpu_state.regs[cpu_rm].l;
//        cycles-=3;
        if (eal_r)
                return *eal_r;
        return readmeml(easeg, eaaddr);
}

static inline uint64_t geteaq()
{
        return readmemq(easeg, eaaddr);
}

static inline uint8_t geteab_mem()
{
        if (eal_r) return *(uint8_t *)eal_r;
        return readmemb(easeg,eaaddr);
}
static inline uint16_t geteaw_mem()
{
        if (eal_r) return *(uint16_t *)eal_r;
        return readmemw(easeg,eaaddr);
}
static inline uint32_t geteal_mem()
{
        if (eal_r) return *eal_r;
        return readmeml(easeg,eaaddr);
}

static inline void seteaq(uint64_t v)
{
        writememql(easeg, eaaddr, v);
}

#define seteab(v) if (cpu_mod!=3) { if (eal_w) *(uint8_t *)eal_w=v;  else writememb386l(easeg,eaaddr,v); } else if (cpu_rm&4) cpu_state.regs[cpu_rm&3].b.h=v; else cpu_state.regs[cpu_rm].b.l=v
#define seteaw(v) if (cpu_mod!=3) { if (eal_w) *(uint16_t *)eal_w=v; else writememwl(easeg,eaaddr,v);    } else cpu_state.regs[cpu_rm].w=v
#define seteal(v) if (cpu_mod!=3) { if (eal_w) *eal_w=v;             else writememll(easeg,eaaddr,v);    } else cpu_state.regs[cpu_rm].l=v

#define seteab_mem(v) if (eal_w) *(uint8_t *)eal_w=v;  else writememb386l(easeg,eaaddr,v);
#define seteaw_mem(v) if (eal_w) *(uint16_t *)eal_w=v; else writememwl(easeg,eaaddr,v);
#define seteal_mem(v) if (eal_w) *eal_w=v;             else writememll(easeg,eaaddr,v);

#define getbytef() ((uint8_t)(fetchdat)); cpu_state.pc++
#define getwordf() ((uint16_t)(fetchdat)); cpu_state.pc+=2
#define getbyte2f() ((uint8_t)(fetchdat>>8)); cpu_state.pc++
#define getword2f() ((uint16_t)(fetchdat>>8)); cpu_state.pc+=2


#define rmdat rmdat32
#define fetchdat rmdat32

void x86_int(int num);
