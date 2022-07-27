#define opSET(condition)                                                \
        static int opSET ## condition ## _a16(uint32_t fetchdat)        \
        {                                                               \
                fetch_ea_16(fetchdat);                                  \
                if (cpu_mod != 3)                                       \
                        SEG_CHECK_READ(cpu_state.ea_seg);               \
                seteab((cond_ ## condition) ? 1 : 0);                   \
                CLOCK_CYCLES(4);                                        \
                return cpu_state.abrt;                                            \
        }                                                               \
                                                                        \
        static int opSET ## condition ## _a32(uint32_t fetchdat)        \
        {                                                               \
                fetch_ea_32(fetchdat);                                  \
                if (cpu_mod != 3)                                       \
                        SEG_CHECK_READ(cpu_state.ea_seg);               \
                seteab((cond_ ## condition) ? 1 : 0);                   \
                CLOCK_CYCLES(4);                                        \
                return cpu_state.abrt;                                            \
        }

opSET(O)
opSET(NO)
opSET(B)
opSET(NB)
opSET(E)
opSET(NE)
opSET(BE)
opSET(NBE)
opSET(S)
opSET(NS)
opSET(P)
opSET(NP)
opSET(L)
opSET(NL)
opSET(LE)
opSET(NLE)
