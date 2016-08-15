/* Copyright holders: Sarah Walker
   see COPYING for more details
*/
/*Registers :
        
  alphaMode
  fbzMode & 0x1f3fff
  fbzColorPath
*/

#ifdef __linux__
#include <sys/mman.h>
#include <unistd.h>
#endif
#if defined WIN32 || defined _WIN32 || defined _WIN32
#define BITMAP windows_BITMAP
#include <windows.h>
#undef BITMAP
#endif

#include <xmmintrin.h>

#define BLOCK_NUM 8
#define BLOCK_MASK (BLOCK_NUM-1)
#define BLOCK_SIZE 8192

typedef struct voodoo_x86_data_t
{
        uint8_t code_block[BLOCK_SIZE];
        int xdir;
        uint32_t alphaMode;
        uint32_t fbzMode;
        uint32_t fogMode;
        uint32_t fbzColorPath;
        uint32_t textureMode;
        uint32_t trexInit1;        
} voodoo_x86_data_t;

static int last_block[2] = {0, 0};
static int next_block_to_write[2] = {0, 0};

#define addbyte(val)                                    \
        code_block[block_pos++] = val;                  \
        if (block_pos >= BLOCK_SIZE)                    \
                fatal("Over!\n")

#define addword(val)                                    \
        *(uint16_t *)&code_block[block_pos] = val;      \
        block_pos += 2;                                 \
        if (block_pos >= BLOCK_SIZE)                    \
                fatal("Over!\n")

#define addlong(val)                                    \
        *(uint32_t *)&code_block[block_pos] = val;      \
        block_pos += 4;                                 \
        if (block_pos >= BLOCK_SIZE)                    \
                fatal("Over!\n")

#define addquad(val)                                    \
        *(uint64_t *)&code_block[block_pos] = val;      \
        block_pos += 8;                                 \
        if (block_pos >= BLOCK_SIZE)                    \
                fatal("Over!\n")


static __m128i xmm_01_w;// = 0x0001000100010001ull;
static __m128i xmm_ff_w;// = 0x00ff00ff00ff00ffull;
static __m128i xmm_ff_b;// = 0x00000000ffffffffull;

static uint32_t zero = 0;
static double const_1_48 = (double)(1ull << 4);

static __m128i alookup[257], aminuslookup[256];
static __m128i minus_254;// = 0xff02ff02ff02ff02ull;
static __m128i bilinear_lookup[256*4];

static inline void voodoo_generate(uint8_t *code_block, voodoo_t *voodoo, voodoo_params_t *params, voodoo_state_t *state, int depthop)
{        
        int block_pos = 0;
        int z_skip_pos = 0;
        int a_skip_pos = 0;
        int chroma_skip_pos = 0;
        int depth_jump_pos = 0;
        int depth_jump_pos2 = 0;
        int loop_jump_pos = 0;
//        xmm_01_w = (__m128i)0x0001000100010001ull;
//        xmm_ff_w = (__m128i)0x00ff00ff00ff00ffull;
//        xmm_ff_b = (__m128i)0x00000000ffffffffull;
        xmm_01_w = _mm_set_epi32(0, 0, 0x00010001, 0x00010001);
        xmm_ff_w = _mm_set_epi32(0, 0, 0x00ff00ff, 0x00ff00ff);
        xmm_ff_b = _mm_set_epi32(0, 0, 0, 0x00ffffff);
        minus_254 = _mm_set_epi32(0, 0, 0xff02ff02, 0xff02ff02);
//        *(uint64_t *)&const_1_48 = 0x45b0000000000000ull;
//        block_pos = 0;
//        voodoo_get_depth = &code_block[block_pos];
        /*W at (%esp+4)
          Z at (%esp+12)
          new_depth at (%esp+16)*/
//        if ((params->fbzMode & FBZ_DEPTH_ENABLE) && (depth_op == DEPTHOP_NEVER))
//        {
//                addbyte(0xC3); /*RET*/
//                return;
//        }
        addbyte(0x55); /*PUSH EBP*/
        addbyte(0x57); /*PUSH EDI*/
        addbyte(0x56); /*PUSH ESI*/
        addbyte(0x53); /*PUSH EBX*/
        
        addbyte(0x8b); /*MOV EDI, [ESP+4]*/
        addbyte(0x7c);
        addbyte(0x24);
        addbyte(4+16);
        loop_jump_pos = block_pos;
        addbyte(0x8b); /*MOV ESI, [ESP+8]*/
        addbyte(0x74);
        addbyte(0x24);
        addbyte(8+16);
        addbyte(0x66); /*PXOR XMM2, XMM2*/
        addbyte(0x0f);
        addbyte(0xef);
        addbyte(0xd2);

        if ((params->fbzMode & FBZ_W_BUFFER) || (params->fogMode & (FOG_ENABLE|FOG_CONSTANT|FOG_Z|FOG_ALPHA)) == FOG_ENABLE)
        {
                addbyte(0xb8); /*MOV new_depth, 0*/
                addlong(0);
                addbyte(0x66); /*TEST w+4, 0xffff*/
                addbyte(0xf7);
                addbyte(0x87);
                addlong(offsetof(voodoo_state_t, w)+4);
                addword(0xffff);
                addbyte(0x75); /*JNZ got_depth*/
                depth_jump_pos = block_pos;
                addbyte(0);
//                addbyte(4+5+2+3+2+5+5+3+2+2+2+/*3+*/3+2+6+4+5+2+3);
                addbyte(0x8b); /*MOV EDX, w*/
                addbyte(0x97);
                addlong(offsetof(voodoo_state_t, w));
                addbyte(0xb8); /*MOV new_depth, 0xf001*/
                addlong(0xf001);
                addbyte(0x89); /*MOV EBX, EDX*/
                addbyte(0xd3);
                addbyte(0xc1); /*SHR EDX, 16*/
                addbyte(0xea);
                addbyte(16);
                addbyte(0x74); /*JZ got_depth*/
                depth_jump_pos2 = block_pos;
                addbyte(0);
//                addbyte(5+5+3+2+2+2+/*3+*/3+2+6+4+5+2+3);
                addbyte(0xb9); /*MOV ECX, 19*/
                addlong(19);
                addbyte(0x0f); /*BSR EAX, EDX*/
                addbyte(0xbd);
                addbyte(0xc2);
                addbyte(0xba); /*MOV EDX, 15*/
                addlong(15);
                addbyte(0xf7); /*NOT EBX*/
                addbyte(0xd3);
                addbyte(0x29); /*SUB EDX, EAX - EDX = exp*/
                addbyte(0xc2);
                addbyte(0x29); /*SUB ECX, EDX*/
                addbyte(0xd1);
                addbyte(0xc1); /*SHL EDX, 12*/
                addbyte(0xe2);
                addbyte(12);
                addbyte(0xd3); /*SHR EBX, CL*/
                addbyte(0xeb);
                addbyte(0x81); /*AND EBX, 0xfff - EBX = mant*/
                addbyte(0xe3);
                addlong(0xfff);
                addbyte(0x8d); /*LEA EAX, 1[EDX, EBX]*/
                addbyte(0x44);
                addbyte(0x13);
                addbyte(1);
                addbyte(0xbb); /*MOV EBX, 0xffff*/
                addlong(0xffff);
                addbyte(0x39); /*CMP EAX, EBX*/
                addbyte(0xd8);
                addbyte(0x0f); /*CMOVA EAX, EBX*/
                addbyte(0x47);
                addbyte(0xc3);

        if (depth_jump_pos)
                *(uint8_t *)&code_block[depth_jump_pos] = (block_pos - depth_jump_pos) - 1;
        if (depth_jump_pos)
                *(uint8_t *)&code_block[depth_jump_pos2] = (block_pos - depth_jump_pos2) - 1;
                
                if ((params->fogMode & (FOG_ENABLE|FOG_CONSTANT|FOG_Z|FOG_ALPHA)) == FOG_ENABLE)
                {
                        addbyte(0x89); /*MOV state->w_depth[EDI], EAX*/
                        addbyte(0x87);
                        addlong(offsetof(voodoo_state_t, w_depth));
                }
        }
        if (!(params->fbzMode & FBZ_W_BUFFER))
        {
                addbyte(0x8b); /*MOV EAX, z*/
                addbyte(0x87);
                addlong(offsetof(voodoo_state_t, z));
                addbyte(0xbb); /*MOV EBX, 0xffff*/
                addlong(0xffff);
                addbyte(0x31); /*XOR ECX, ECX*/
                addbyte(0xc9);
                addbyte(0xc1); /*SAR EAX, 12*/
                addbyte(0xf8);
                addbyte(12);
                addbyte(0x0f); /*CMOVS EAX, ECX*/
                addbyte(0x48);
                addbyte(0xc1);
                addbyte(0x39); /*CMP EAX, EBX*/
                addbyte(0xd8);
                addbyte(0x0f); /*CMOVA EAX, EBX*/
                addbyte(0x47);
                addbyte(0xc3);
        }
        
        if (params->fbzMode & FBZ_DEPTH_BIAS)
        {
                addbyte(0x03); /*ADD EAX, params->zaColor[ESI]*/
                addbyte(0x86);
                addlong(offsetof(voodoo_params_t, zaColor));                
                addbyte(0x25); /*AND EAX, 0xffff*/
                addlong(0xffff);
        }

        addbyte(0x89); /*MOV state->new_depth[EDI], EAX*/
        addbyte(0x87);
        addlong(offsetof(voodoo_state_t, new_depth));

        if ((params->fbzMode & FBZ_DEPTH_ENABLE) && (depthop != DEPTHOP_ALWAYS) && (depthop != DEPTHOP_NEVER))
        {
                addbyte(0x8b); /*MOV EBX, state->x[EDI]*/
                addbyte(0x9f);
                addlong(offsetof(voodoo_state_t, x));
                addbyte(0x8b);/*MOV ECX, aux_mem[EDI]*/
                addbyte(0x8f);
                addlong(offsetof(voodoo_state_t, aux_mem));
                addbyte(0x0f); /*MOVZX EBX, [ECX+EBX*2]*/
                addbyte(0xb7);
                addbyte(0x1c);
                addbyte(0x59);
                addbyte(0x39); /*CMP EAX, EBX*/
                addbyte(0xd8);
                if (depthop == DEPTHOP_LESSTHAN)
                {
                        addbyte(0x0f); /*JAE skip*/
                        addbyte(0x83);
                        z_skip_pos = block_pos;
                        addlong(0);
                }
                else if (depthop == DEPTHOP_EQUAL)
                {
                        addbyte(0x0f); /*JNE skip*/
                        addbyte(0x85);
                        z_skip_pos = block_pos;
                        addlong(0);
                }
                else if (depthop == DEPTHOP_LESSTHANEQUAL)
                {
                        addbyte(0x0f); /*JA skip*/
                        addbyte(0x87);
                        z_skip_pos = block_pos;
                        addlong(0);
                }
                else if (depthop == DEPTHOP_GREATERTHAN)
                {
                        addbyte(0x0f); /*JBE skip*/
                        addbyte(0x86);
                        z_skip_pos = block_pos;
                        addlong(0);
                }
                else if (depthop == DEPTHOP_NOTEQUAL)
                {
                        addbyte(0x0f); /*JE skip*/
                        addbyte(0x84);
                        z_skip_pos = block_pos;
                        addlong(0);
                }
                else if (depthop == DEPTHOP_GREATERTHANEQUAL)
                {
                        addbyte(0x0f); /*JB skip*/
                        addbyte(0x82);
                        z_skip_pos = block_pos;
                        addlong(0);
                }
                else
                        fatal("Bad depth_op\n");
        }
        else if ((params->fbzMode & FBZ_DEPTH_ENABLE) && (depthop == DEPTHOP_NEVER))
        {
                addbyte(0xC3); /*RET*/
//                addbyte(0x30); /*XOR EAX, EAX*/
//                addbyte(0xc0);
        }
//        else
//        {
//                addbyte(0xb0); /*MOV AL, 1*/
//                addbyte(1);
//        }


//        voodoo_combine = &code_block[block_pos];
        /*XMM0 = colour*/
        /*XMM2 = 0 (for unpacking*/
        
        /*EDI = state, ESI = params*/

        if (params->textureMode & 1)
        {
                /*CVTSI2SSq XMM6, state->w*/
                /*MOVSS XMM7, const_1_48*/
                /*DIVSS XMM7, XMM6*/
                /*CVTSI2SSq XMM4, state->tmu0_s*/
                /*MULSS XMM7, const_1_44*/
                /*CVTSI2SSq XMM5, state->tmu0_t*/
                /*MULSS XMM4, XMM7*/
                /*CVTSS2SIq state->tex_s, XMM4*/
                /*MULSS XMM5, XMM7*/
                /*CVTSS2SIq state->tex_t, XMM5*/
                
                addbyte(0xdf); /*FILDq state->tmu0_w*/
                addbyte(0xaf);
                addlong(offsetof(voodoo_state_t, tmu0_w));
                addbyte(0xdd); /*FLDq const_1_48*/
                addbyte(0x05);
                addlong(&const_1_48);
                addbyte(0xde); /*FDIV ST(1)*/
                addbyte(0xf1);
                addbyte(0xdf); /*FILDq state->tmu0_s*/
                addbyte(0xaf);
                addlong(offsetof(voodoo_state_t, tmu0_s));
                addbyte(0xdf); /*FILDq state->tmu0_t*/ /*ST(0)=t,   ST(1)=s,   ST(2)=1/w*/
                addbyte(0xaf);
                addlong(offsetof(voodoo_state_t, tmu0_t));
                addbyte(0xd9); /*FXCH ST(1)*/          /*ST(0)=s,   ST(1)=t,   ST(2)=1/w*/
                addbyte(0xc9);
                addbyte(0xd8); /*FMUL ST(2)*/          /*ST(0)=s/w, ST(1)=t,   ST(2)=1/w*/
                addbyte(0xca);
                addbyte(0xd9); /*FXCH ST(1)*/          /*ST(0)=t,   ST(1)=s/w, ST(2)=1/w*/
                addbyte(0xc9);
                addbyte(0xd8); /*FMUL ST(2)*/          /*ST(0)=t/w, ST(1)=s/w, ST(2)=1/w*/
                addbyte(0xca);
                addbyte(0xd9); /*FXCH ST(2)*/          /*ST(0)=1/w, ST(1)=s/w, ST(2)=t/w*/
                addbyte(0xca);
                addbyte(0xd9); /*FSTPs log_temp*/      /*ST(0)=s/w, ST(1)=t/w*/
                addbyte(0x9f);
                addlong(offsetof(voodoo_state_t, log_temp));
                addbyte(0xdf); /*FSITPq state->tex_s*/
                addbyte(0xbf);
                addlong(offsetof(voodoo_state_t, tex_s));
                addbyte(0x8b); /*MOV EAX, log_temp*/
                addbyte(0x87);
                addlong(offsetof(voodoo_state_t, log_temp));
                addbyte(0xdf); /*FSITPq state->tex_t*/
                addbyte(0xbf);
                addlong(offsetof(voodoo_state_t, tex_t));
                addbyte(0xc1); /*SHR EAX, 23-8*/
                addbyte(0xe8);
                addbyte(15);
                addbyte(0x0f); /*MOVZX EBX, AL*/
                addbyte(0xb6);
                addbyte(0xd8);
                addbyte(0x25); /*AND EAX, 0xff00*/
                addlong(0xff00);
                addbyte(0x2d); /*SUB EAX, (127-44)<<8*/
                addlong((127-44+19) << 8);
                addbyte(0x0f); /*MOVZX EBX, logtable[EBX]*/
                addbyte(0xb6);
                addbyte(0x9b);
                addlong(logtable);
                addbyte(0x09); /*OR EAX, EBX*/
                addbyte(0xd8);
//                addbyte(0x89); /*MOV state->lod_raw, EAX*/
//                addbyte(0x87);
//                addlong(offsetof(voodoo_state_t, lod_raw));
                addbyte(0x03); /*ADD EAX, state->lod*/
                addbyte(0x87);
                addlong(offsetof(voodoo_state_t, tmu[0].lod));
/*HACK*/
#if 0
                addbyte(0x8b); /*MOV EAX, state->lod_min*/
                addbyte(0x87);
                addlong(offsetof(voodoo_state_t, lod_min));
#endif
                addbyte(0x3b); /*CMP EAX, state->lod_min*/
                addbyte(0x87);
                addlong(offsetof(voodoo_state_t, lod_min));
                addbyte(0x0f); /*CMOVL EAX, state->lod_min*/
                addbyte(0x4c);
                addbyte(0x87);
                addlong(offsetof(voodoo_state_t, lod_min));
                addbyte(0x3b); /*CMP EAX, state->lod_max*/
                addbyte(0x87);
                addlong(offsetof(voodoo_state_t, lod_max));
                addbyte(0x0f); /*CMOVNL EAX, state->lod_max*/
                addbyte(0x4d);
                addbyte(0x87);
                addlong(offsetof(voodoo_state_t, lod_max));
                addbyte(0xc1); /*SHR EAX, 8*/
                addbyte(0xe8);
                addbyte(8);        
                addbyte(0x89); /*MOV state->lod, EAX*/
                addbyte(0x87);
                addlong(offsetof(voodoo_state_t, lod));
        }
        else
        {
                addbyte(0xf3); /*MOVQ XMM4, state->tmu0_s*/
                addbyte(0x0f);
                addbyte(0x7e);
                addbyte(0xa7);
                addlong(offsetof(voodoo_state_t, tmu0_s));
                addbyte(0xf3); /*MOVQ XMM5, state->tmu0_t*/
                addbyte(0x0f);
                addbyte(0x7e);
                addbyte(0xaf);
                addlong(offsetof(voodoo_state_t, tmu0_t));
                addbyte(0x66); /*SHRQ XMM4, 28*/
                addbyte(0x0f);
                addbyte(0x73);
                addbyte(0xd4);
                addbyte(28);
                addbyte(0x66); /*SHRQ XMM5, 28*/
                addbyte(0x0f);
                addbyte(0x73);
                addbyte(0xd5);
                addbyte(28);
                addbyte(0x66); /*MOVQ state->tex_s, XMM4*/
                addbyte(0x0f);
                addbyte(0xd6);
                addbyte(0xa7);
                addlong(offsetof(voodoo_state_t, tex_s));
                addbyte(0x66); /*MOVQ state->tex_t, XMM5*/
                addbyte(0x0f);
                addbyte(0xd6);
                addbyte(0xaf);
                addlong(offsetof(voodoo_state_t, tex_t));
                addbyte(0x8b); /*MOV EAX, state->lod_min*/
                addbyte(0x87);
                addlong(offsetof(voodoo_state_t, lod_min));
                addbyte(0xc1); /*SHR EAX, 8*/
                addbyte(0xe8);
                addbyte(8);        
                addbyte(0x89); /*MOV state->lod, EAX*/
                addbyte(0x87);
                addlong(offsetof(voodoo_state_t, lod));
        }


        if (voodoo->trexInit1 & (1 << 18))
        {
#if 0
                addbyte(0xc7); /*MOV state->tex_r, 0*/
                addbyte(0x87);
                addlong(offsetof(voodoo_state_t, tex_r));
                addlong(0);
                addbyte(0xc7); /*MOV state->tex_g, 0*/
                addbyte(0x87);
                addlong(offsetof(voodoo_state_t, tex_g));
                addlong(0);
                addbyte(0xc7); /*MOV state->tex_b, 1*/
                addbyte(0x87);
                addlong(offsetof(voodoo_state_t, tex_b));
                addlong(1);
#endif
                addbyte(0xb8); /*MOV EAX, 0x000001*/
                addlong(0x000001);
                addbyte(0x66); /*MOVD XMM0, EAX*/
                addbyte(0x0f);
                addbyte(0x6e);
                addbyte(0xc0);
        }
        else if (params->fbzColorPath & FBZCP_TEXTURE_ENABLED)
        {
                if (voodoo->bilinear_enabled && (params->textureMode & 6))
                {
                        addbyte(0x8b); /*MOV ECX, state->lod[EDI]*/
                        addbyte(0x8f);
                        addlong(offsetof(voodoo_state_t, lod));
                        addbyte(0xbd); /*MOV EBP, 1*/
                        addlong(1);
                        addbyte(0x8a); /*MOV DL, params->tex_shift[ESI+ECX*4]*/
                        addbyte(0x94);
                        addbyte(0x8e);
                        addlong(offsetof(voodoo_params_t, tex_shift));
                        addbyte(0xd3); /*SHL EBP, CL*/
                        addbyte(0xe5);
                        addbyte(0x8b); /*MOV EAX, state->tex_s[EDI]*/
                        addbyte(0x87);
                        addlong(offsetof(voodoo_state_t, tex_s));
                        addbyte(0xc1); /*SHL EBP, 3*/
                        addbyte(0xe5);
                        addbyte(3);
                        addbyte(0x8b); /*MOV EBX, state->tex_t[EDI]*/
                        addbyte(0x9f);
                        addlong(offsetof(voodoo_state_t, tex_t));
                        addbyte(0x29); /*SUB EAX, EBP*/
                        addbyte(0xe8);
                        addbyte(0x29); /*SUB EBX, EBP*/
                        addbyte(0xeb);
                        addbyte(0xd3); /*SAR EAX, CL*/
                        addbyte(0xf8);
                        addbyte(0xd3); /*SAR EBX, CL*/
                        addbyte(0xfb);
                        addbyte(0x89); /*MOV EBP, EAX*/
                        addbyte(0xc5);
                        addbyte(0x89); /*MOV ECX, EBX*/
                        addbyte(0xd9);
                        addbyte(0x83); /*AND EBP, 0xf*/
                        addbyte(0xe5);
                        addbyte(0xf);
                        addbyte(0xc1); /*SHL ECX, 4*/
                        addbyte(0xe1);
                        addbyte(4);
                        addbyte(0xc1); /*SAR EAX, 4*/
                        addbyte(0xf8);
                        addbyte(4);
                        addbyte(0x81); /*AND ECX, 0xf0*/
                        addbyte(0xe1);
                        addlong(0xf0);
                        addbyte(0xc1); /*SAR EBX, 4*/
                        addbyte(0xfb);
                        addbyte(4);
                        addbyte(0x09); /*OR EBP, ECX*/
                        addbyte(0xcd);
                        addbyte(0x8b); /*MOV ECX, state->lod[EDI]*/
                        addbyte(0x8f);
                        addlong(offsetof(voodoo_state_t, lod));
                        addbyte(0xc1); /*SHL EBP, 6*/
                        addbyte(0xe5);
                        addbyte(6);
                        /*EAX = S, EBX = T, ECX = LOD, EDX = tex_shift, ESI=params, EDI=state, EBP = bilinear shift*/
                        addbyte(0x8d); /*LEA ESI, [ESI+ECX*4]*/
                        addbyte(0x34);
                        addbyte(0x8e);
                        addbyte(0x89); /*MOV ebp_store, EBP*/
                        addbyte(0xaf);
                        addlong(offsetof(voodoo_state_t, ebp_store));
                        addbyte(0x8b); /*MOV EBP, state->tex[EDI+ECX*4]*/
                        addbyte(0xac);
                        addbyte(0x8f);
                        addlong(offsetof(voodoo_state_t, tex));
                        addbyte(0x88); /*MOV CL, DL*/
                        addbyte(0xd1);
                        addbyte(0x89); /*MOV EDX, EBX*/
                        addbyte(0xda);
                        if (!state->clamp_s)
                        {
                                addbyte(0x23); /*AND EAX, params->tex_w_mask[ESI]*/
                                addbyte(0x86);
                                addlong(offsetof(voodoo_params_t, tex_w_mask));
                        }
                        addbyte(0x83); /*ADD EDX, 1*/
                        addbyte(0xc2);
                        addbyte(1);
                        if (state->clamp_t)
                        {
                                addbyte(0x0f); /*CMOVS EDX, zero*/
                                addbyte(0x48);
                                addbyte(0x15);
                                addlong(&zero);
                                addbyte(0x3b); /*CMP EDX, params->tex_h_mask[ESI]*/
                                addbyte(0x96);
                                addlong(offsetof(voodoo_params_t, tex_h_mask));
                                addbyte(0x0f); /*CMOVA EDX, params->tex_h_mask[ESI]*/
                                addbyte(0x47);
                                addbyte(0x96);
                                addlong(offsetof(voodoo_params_t, tex_h_mask));
                                addbyte(0x85); /*TEST EBX,EBX*/
                                addbyte(0xdb);
                                addbyte(0x0f); /*CMOVS EBX, zero*/
                                addbyte(0x48);
                                addbyte(0x1d);
                                addlong(&zero);
                                addbyte(0x3b); /*CMP EBX, params->tex_h_mask[ESI]*/
                                addbyte(0x9e);
                                addlong(offsetof(voodoo_params_t, tex_h_mask));
                                addbyte(0x0f); /*CMOVA EBX, params->tex_h_mask[ESI]*/
                                addbyte(0x47);
                                addbyte(0x9e);
                                addlong(offsetof(voodoo_params_t, tex_h_mask));
                        }
                        else
                        {
                                addbyte(0x23); /*AND EDX, params->tex_h_mask[ESI]*/
                                addbyte(0x96);
                                addlong(offsetof(voodoo_params_t, tex_h_mask));
                                addbyte(0x23); /*AND EBX, params->tex_h_mask[ESI]*/
                                addbyte(0x9e);
                                addlong(offsetof(voodoo_params_t, tex_h_mask));
                        }
                        /*EAX = S, EBX = T0, EDX = T1*/
                        addbyte(0xd3); /*SHL EBX, CL*/
                        addbyte(0xe3);
                        addbyte(0xd3); /*SHL EDX, CL*/
                        addbyte(0xe2);
                        if (state->tformat & 8)
                        {
                                addbyte(0x8d); /*LEA EBX,[EBP+EBX*2]*/
                                addbyte(0x5c);
                                addbyte(0x5d);
                                addbyte(0);
                                addbyte(0x8d); /*LEA EDX,[EBP+EDX*2]*/
                                addbyte(0x54);
                                addbyte(0x55);
                                addbyte(0);
                        }
                        else
                        {
                                addbyte(0x01); /*ADD EBX, EBP*/
                                addbyte(0xeb);
                                addbyte(0x01); /*ADD EDX, EBP*/
                                addbyte(0xea);
                        }
                        if (state->clamp_s)
                        {
                                addbyte(0x8b); /*MOV EBP, params->tex_w_mask[ESI]*/
                                addbyte(0xae);
                                addlong(offsetof(voodoo_params_t, tex_w_mask));
                                addbyte(0x85); /*TEST EAX, EAX*/
                                addbyte(0xc0);
                                addbyte(0x8b); /*MOV ebp_store2, ESI*/
                                addbyte(0xb7);
                                addlong(offsetof(voodoo_state_t, ebp_store));
                                addbyte(0x0f); /*CMOVS EAX, zero*/
                                addbyte(0x48);
                                addbyte(0x05);
                                addlong(&zero);
                                addbyte(0x78); /*JS + - clamp on 0*/
                                addbyte(2+3+2+ ((state->tformat & 8) ? (3+3+2) : (4+4+2)));
                                addbyte(0x3b); /*CMP EAX, EBP*/
                                addbyte(0xc5);
                                addbyte(0x0f); /*CMOVAE EAX, EBP*/
                                addbyte(0x43);
                                addbyte(0xc5);
                                addbyte(0x73); /*JAE + - clamp on +*/
                                addbyte((state->tformat & 8) ? (3+3+2) : (4+4+2));
                        }
                        else
                        {
                                addbyte(0x3b); /*CMP EAX, params->tex_w_mask[ESI] - is S at texture edge (ie will wrap/clamp)?*/
                                addbyte(0x86);
                                addlong(offsetof(voodoo_params_t, tex_w_mask));
                                addbyte(0x8b); /*MOV ebp_store2, ESI*/
                                addbyte(0xb7);
                                addlong(offsetof(voodoo_state_t, ebp_store));
                                addbyte(0x74); /*JE +*/
                                addbyte((state->tformat & 8) ? (3+3+2) : (4+4+2));
                        }

                        if (state->tformat & 8)
                        {
                                addbyte(0x8b); /*MOV EDX,[EDX+EAX*2]*/
                                addbyte(0x14);
                                addbyte(0x42);
                                addbyte(0x8b); /*MOV EAX,[EBX+EAX*2]*/
                                addbyte(0x04);
                                addbyte(0x43);
                        }
                        else
                        {
                                addbyte(0x0f); /*MOVZX EDX,W[EDX+EAX]*/
                                addbyte(0xb7);
                                addbyte(0x14);
                                addbyte(0x02);
                                addbyte(0x0f); /*MOVZX EAX,W[EBX+EAX]*/
                                addbyte(0xb7);
                                addbyte(0x04);
                                addbyte(0x03);
                        }
                                               
                        if (state->clamp_s)
                        {
                                addbyte(0xeb); /*JMP +*/
                                addbyte((state->tformat & 8) ? (3+4+3+3+4+3) : (4+4+2+2));

                                /*S clamped - the two S coordinates are the same*/
                                if (state->tformat & 8)
                                {
                                        addbyte(0x8b); /*MOV ECX, [EDX+EAX*2]*/
                                        addbyte(0x0c);
                                        addbyte(0x42);
                                        addbyte(0x8b); /*MOV EDX, [EDX+EAX*2-2]*/
                                        addbyte(0x54);
                                        addbyte(0x42);
                                        addbyte(-2);
                                        addbyte(0x66); /*MOV DX, CX*/
                                        addbyte(0x89);
                                        addbyte(0xca);
                                        addbyte(0x8b); /*MOV ECX, [EBX+EAX*2]*/
                                        addbyte(0x0c);
                                        addbyte(0x43);
                                        addbyte(0x8b); /*MOV EAX, [EBX+EAX*2-2]*/
                                        addbyte(0x44);
                                        addbyte(0x43);
                                        addbyte(-2);
                                        addbyte(0x66); /*MOV AX, CX*/
                                        addbyte(0x89);
                                        addbyte(0xc8);
                                }
                                else
                                {
                                        addbyte(0x0f); /*MOVZX EDX,W[EDX+EAX]*/
                                        addbyte(0xb7);
                                        addbyte(0x14);
                                        addbyte(0x02);
                                        addbyte(0x0f); /*MOVZX EAX,W[EBX+EAX]*/
                                        addbyte(0xb7);
                                        addbyte(0x04);
                                        addbyte(0x03);
                                        addbyte(0x88); /*MOV DH, DL*/
                                        addbyte(0xd6);
                                        addbyte(0x88); /*MOV AH, AL*/
                                        addbyte(0xc4);
                                }
                        }
                        else
                        {
                                addbyte(0xeb); /*JMP +*/
                                addbyte((state->tformat & 8) ? (3+3+3+3+3+3) : (2+2+4+4+2+2));

                                /*S wrapped - the two S coordinates are not contiguous*/
                                if (state->tformat & 8)
                                {
                                        addbyte(0x8b); /*MOV ECX, [EDX+EAX*2]*/
                                        addbyte(0x0c);
                                        addbyte(0x42);
                                        addbyte(0x8b); /*MOV EDX, [EDX-2]*/
                                        addbyte(0x52);
                                        addbyte(-2);
                                        addbyte(0x66); /*MOV DX, CX*/
                                        addbyte(0x89);
                                        addbyte(0xca);
                                        addbyte(0x8b); /*MOV ECX, [EBX+EAX*2]*/
                                        addbyte(0x0c);
                                        addbyte(0x43);
                                        addbyte(0x8b); /*MOV EAX, [EBX-2]*/
                                        addbyte(0x43);
                                        addbyte(-2);
                                        addbyte(0x66); /*MOV AX, CX*/
                                        addbyte(0x89);
                                        addbyte(0xc8);
                                }
                                else
                                {
                                        addbyte(0x8a); /*MOV CL, [EDX]*/
                                        addbyte(0x0a);
                                        addbyte(0x8a); /*MOV CH, [EBX]*/
                                        addbyte(0x2b);
                                        addbyte(0x0f); /*MOVZX EDX,B[EDX+EAX]*/
                                        addbyte(0xb6);
                                        addbyte(0x14);
                                        addbyte(0x02);
                                        addbyte(0x0f); /*MOVZX EAX,B[EBX+EAX]*/
                                        addbyte(0xb6);
                                        addbyte(0x04);
                                        addbyte(0x03);
                                        addbyte(0x88); /*MOV DH, CL*/
                                        addbyte(0xce);
                                        addbyte(0x88); /*MOV AH, CH*/
                                        addbyte(0xec);
                                }
                        }

                        addbyte(0x81); /*ADD ESI, bilinear_lookup*/
                        addbyte(0xc6);
                        addlong(bilinear_lookup);

                        switch (state->tformat)
                        {
                                case TEX_RGB332:
                                addbyte(0x0f); /*MOVZX ECX, AL*/
                                addbyte(0xb6);
                                addbyte(0xc8);
                                addbyte(0x66); /*MOVD XMM0, rgb332[ECX*4]*/
                                addbyte(0x0f);
                                addbyte(0x6e);
                                addbyte(0x04);
                                addbyte(0x8d);
                                addlong(rgb332);
                                addbyte(0x0f); /*MOVZX ECX, AH*/
                                addbyte(0xb6);
                                addbyte(0xcc);
                                addbyte(0x66); /*PUNPCKLBW XMM0, XMM2*/
                                addbyte(0x0f);
                                addbyte(0x60);
                                addbyte(0xc2);
                                addbyte(0x66); /*MOVD XMM1, rgb332[ECX*4]*/
                                addbyte(0x0f);
                                addbyte(0x6e);
                                addbyte(0x0c);
                                addbyte(0x8d);
                                addlong(rgb332);
                                addbyte(0x66); /*PMULLW XMM0, bilinear_lookup[ESI]*/
                                addbyte(0x0f);
                                addbyte(0xd5);
                                addbyte(0x06);
                                addbyte(0x0f); /*MOVZX ECX, DL*/
                                addbyte(0xb6);
                                addbyte(0xca);
                                addbyte(0x66); /*PUNPCKLBW XMM1, XMM2*/
                                addbyte(0x0f);
                                addbyte(0x60);
                                addbyte(0xca);
                                addbyte(0x66); /*MOVD XMM3, rgb332[ECX*4]*/
                                addbyte(0x0f);
                                addbyte(0x6e);
                                addbyte(0x1c);
                                addbyte(0x8d);
                                addlong(rgb332);
                                addbyte(0x66); /*PMULLW XMM1, bilinear_lookup[ESI]+0x10*/
                                addbyte(0x0f);
                                addbyte(0xd5);
                                addbyte(0x4e);
                                addbyte(0x10);
                                addbyte(0x0f); /*MOVZX ECX, DH*/
                                addbyte(0xb6);
                                addbyte(0xce);
                                addbyte(0x66); /*PUNPCKLBW XMM3, XMM2*/
                                addbyte(0x0f);
                                addbyte(0x60);
                                addbyte(0xda);
                                addbyte(0x66); /*PADDW XMM0, XMM1*/
                                addbyte(0x0f);
                                addbyte(0xfd);
                                addbyte(0xc1);
                                addbyte(0x66); /*MOVD XMM1, rgb332[ECX*4]*/
                                addbyte(0x0f);
                                addbyte(0x6e);
                                addbyte(0x0c);
                                addbyte(0x8d);
                                addlong(rgb332);
                                addbyte(0x66); /*PMULLW XMM3, bilinear_lookup[ESI]+0x20*/
                                addbyte(0x0f);
                                addbyte(0xd5);
                                addbyte(0x5e);
                                addbyte(0x20);
                                addbyte(0x66); /*PUNPCKLBW XMM1, XMM2*/
                                addbyte(0x0f);
                                addbyte(0x60);
                                addbyte(0xca);
                                addbyte(0x66); /*PADDW XMM0, XMM3*/
                                addbyte(0x0f);
                                addbyte(0xfd);
                                addbyte(0xc3);
                                addbyte(0x66); /*PMULLW XMM1, bilinear_lookup[ESI]+0x20*/
                                addbyte(0x0f);
                                addbyte(0xd5);
                                addbyte(0x4e);
                                addbyte(0x30);
                                addbyte(0x66); /*PADDW XMM0, XMM1*/
                                addbyte(0x0f);
                                addbyte(0xfd);
                                addbyte(0xc1);
                                addbyte(0x66); /*PSRLW XMM0, 8*/
                                addbyte(0x0f);
                                addbyte(0x71);
                                addbyte(0xd0);
                                addbyte(8);
                                addbyte(0x66); /*PACKUSWB XMM0, XMM0*/
                                addbyte(0x0f);
                                addbyte(0x67);
                                addbyte(0xc0);
                                addbyte(0x66); /*MOV EAX, XMM0*/
                                addbyte(0x0f);
                                addbyte(0x7e);
                                addbyte(0xc0);
                                addbyte(0x0d); /*OR EAX, 0xff000000*/
                                addlong(0xff000000);
                                break;
                                                
                                case TEX_Y4I2Q2:
                                addbyte(0x8b); /*MOV EBP, state->palette[EDI]*/
                                addbyte(0xaf);
                                addlong(offsetof(voodoo_state_t, palette));
                                addbyte(0x0f); /*MOVZX ECX, AL*/
                                addbyte(0xb6);
                                addbyte(0xc8);
                                addbyte(0x66); /*MOVD XMM0, [EBP+ECX*4]*/
                                addbyte(0x0f);
                                addbyte(0x6e);
                                addbyte(0x44);
                                addbyte(0x8d);
                                addbyte(0);
                                addbyte(0x0f); /*MOVZX ECX, AH*/
                                addbyte(0xb6);
                                addbyte(0xcc);
                                addbyte(0x66); /*PUNPCKLBW XMM0, XMM2*/
                                addbyte(0x0f);
                                addbyte(0x60);
                                addbyte(0xc2);
                                addbyte(0x66); /*MOVD XMM1, [EBP+ECX*4]*/
                                addbyte(0x0f);
                                addbyte(0x6e);
                                addbyte(0x4c);
                                addbyte(0x8d);
                                addbyte(0);
                                addbyte(0x66); /*PMULLW XMM0, bilinear_lookup[ESI]*/
                                addbyte(0x0f);
                                addbyte(0xd5);
                                addbyte(0x06);
                                addbyte(0x0f); /*MOVZX ECX, DL*/
                                addbyte(0xb6);
                                addbyte(0xca);
                                addbyte(0x66); /*PUNPCKLBW XMM1, XMM2*/
                                addbyte(0x0f);
                                addbyte(0x60);
                                addbyte(0xca);
                                addbyte(0x66); /*MOVD XMM3, [EBP+ECX*4]*/
                                addbyte(0x0f);
                                addbyte(0x6e);
                                addbyte(0x5c);
                                addbyte(0x8d);
                                addbyte(0);
                                addbyte(0x66); /*PMULLW XMM1, bilinear_lookup[ESI]+0x10*/
                                addbyte(0x0f);
                                addbyte(0xd5);
                                addbyte(0x4e);
                                addbyte(0x10);
                                addbyte(0x0f); /*MOVZX ECX, DH*/
                                addbyte(0xb6);
                                addbyte(0xce);
                                addbyte(0x66); /*PUNPCKLBW XMM3, XMM2*/
                                addbyte(0x0f);
                                addbyte(0x60);
                                addbyte(0xda);
                                addbyte(0x66); /*PADDW XMM0, XMM1*/
                                addbyte(0x0f);
                                addbyte(0xfd);
                                addbyte(0xc1);
                                addbyte(0x66); /*MOVD XMM1, [EBP+ECX*4]*/
                                addbyte(0x0f);
                                addbyte(0x6e);
                                addbyte(0x4c);
                                addbyte(0x8d);
                                addbyte(0);
                                addbyte(0x66); /*PMULLW XMM3, bilinear_lookup[ESI]+0x20*/
                                addbyte(0x0f);
                                addbyte(0xd5);
                                addbyte(0x5e);
                                addbyte(0x20);
                                addbyte(0x66); /*PUNPCKLBW XMM1, XMM2*/
                                addbyte(0x0f);
                                addbyte(0x60);
                                addbyte(0xca);
                                addbyte(0x66); /*PADDW XMM0, XMM3*/
                                addbyte(0x0f);
                                addbyte(0xfd);
                                addbyte(0xc3);
                                addbyte(0x66); /*PMULLW XMM1, bilinear_lookup[ESI]+0x20*/
                                addbyte(0x0f);
                                addbyte(0xd5);
                                addbyte(0x4e);
                                addbyte(0x30);
                                addbyte(0x66); /*PADDW XMM0, XMM1*/
                                addbyte(0x0f);
                                addbyte(0xfd);
                                addbyte(0xc1);
                                addbyte(0x66); /*PSRLW XMM0, 8*/
                                addbyte(0x0f);
                                addbyte(0x71);
                                addbyte(0xd0);
                                addbyte(8);
                                addbyte(0x66); /*PACKUSWB XMM0, XMM0*/
                                addbyte(0x0f);
                                addbyte(0x67);
                                addbyte(0xc0);
                                addbyte(0x66); /*MOV EAX, XMM0*/
                                addbyte(0x0f);
                                addbyte(0x7e);
                                addbyte(0xc0);
                                addbyte(0x0d); /*OR EAX, 0xff000000*/
                                addlong(0xff000000);
                                break;
                                                        
                                case TEX_A8:
                                addbyte(0x66); /*MOVZX CX, AH*/
                                addbyte(0x0f);
                                addbyte(0xb6);
                                addbyte(0xcc);
                                addbyte(0x66); /*MOVZX AX, AL*/
                                addbyte(0x0f);
                                addbyte(0xb6);
                                addbyte(0xc0);
                                addbyte(0x66); /*IMUL CX, bilinear_lookup[ESI]+0x10*/
                                addbyte(0x0f);
                                addbyte(0xaf);
                                addbyte(0x4e);
                                addbyte(0x10);
                                addbyte(0x66); /*IMUL AX, bilinear_lookup[ESI]*/
                                addbyte(0x0f);
                                addbyte(0xaf);
                                addbyte(0x06);
                                addbyte(0x66); /*MOVZX BX, DH*/
                                addbyte(0x0f);
                                addbyte(0xb6);
                                addbyte(0xde);
                                addbyte(0x66); /*MOVZX DX, DL*/
                                addbyte(0x0f);
                                addbyte(0xb6);
                                addbyte(0xd2);
                                addbyte(0x66); /*ADD AX, CX*/
                                addbyte(0x01);
                                addbyte(0xc8);
                                addbyte(0x66); /*IMUL BX, bilinear_lookup[ESI]+0x30*/
                                addbyte(0x0f);
                                addbyte(0xaf);
                                addbyte(0x5e);
                                addbyte(0x30);
                                addbyte(0x66); /*IMUL DX, bilinear_lookup[ESI]+0x20*/
                                addbyte(0x0f);
                                addbyte(0xaf);
                                addbyte(0x56);
                                addbyte(0x20);
                                addbyte(0x66); /*ADD AX, BX*/
                                addbyte(0x01);
                                addbyte(0xd8);
                                addbyte(0x66); /*ADD AX, DX*/
                                addbyte(0x01);
                                addbyte(0xd0);
                                addbyte(0x88); /*MOV AL, AH*/
                                addbyte(0xe0);
                                addbyte(0x66); /*MOV BX, AX*/
                                addbyte(0x89);
                                addbyte(0xc3);
                                addbyte(0x0f); /*BSWAP EAX*/
                                addbyte(0xc8);
                                addbyte(0x66); /*MOV AX, BX*/
                                addbyte(0x89);
                                addbyte(0xd8);
                                break;

                                case TEX_I8:
                                addbyte(0x66); /*MOVZX CX, AH*/
                                addbyte(0x0f);
                                addbyte(0xb6);
                                addbyte(0xcc);
                                addbyte(0x66); /*MOVZX AX, AL*/
                                addbyte(0x0f);
                                addbyte(0xb6);
                                addbyte(0xc0);
                                addbyte(0x66); /*IMUL CX, bilinear_lookup[ESI]+0x10*/
                                addbyte(0x0f);
                                addbyte(0xaf);
                                addbyte(0x4e);
                                addbyte(0x10);
                                addbyte(0x66); /*IMUL AX, bilinear_lookup[ESI]*/
                                addbyte(0x0f);
                                addbyte(0xaf);
                                addbyte(0x06);
                                addbyte(0x66); /*MOVZX BX, DH*/
                                addbyte(0x0f);
                                addbyte(0xb6);
                                addbyte(0xde);
                                addbyte(0x66); /*MOVZX DX, DL*/
                                addbyte(0x0f);
                                addbyte(0xb6);
                                addbyte(0xd2);
                                addbyte(0x66); /*ADD AX, CX*/
                                addbyte(0x01);
                                addbyte(0xc8);
                                addbyte(0x66); /*IMUL BX, bilinear_lookup[ESI]+0x30*/
                                addbyte(0x0f);
                                addbyte(0xaf);
                                addbyte(0x5e);
                                addbyte(0x30);
                                addbyte(0x66); /*IMUL DX, bilinear_lookup[ESI]+0x20*/
                                addbyte(0x0f);
                                addbyte(0xaf);
                                addbyte(0x56);
                                addbyte(0x20);
                                addbyte(0x66); /*ADD AX, BX*/
                                addbyte(0x01);
                                addbyte(0xd8);
                                addbyte(0x66); /*ADD AX, DX*/
                                addbyte(0x01);
                                addbyte(0xd0);
                                addbyte(0x88); /*MOV AL, AH*/
                                addbyte(0xe0);
                                addbyte(0xc1); /*SHL EAX, 8*/
                                addbyte(0xe0);
                                addbyte(8);
                                addbyte(0x88); /*MOV AL, AH*/
                                addbyte(0xe0);
                                addbyte(0x0d); /*OR EAX, 0xff000000*/
                                addlong(0xff000000);
                                break;

                                case TEX_AI8:
                                addbyte(0x0f); /*MOVZX ECX, AL*/
                                addbyte(0xb6);
                                addbyte(0xc8);
                                addbyte(0x66); /*MOVD XMM0, ai44[ECX*4]*/
                                addbyte(0x0f);
                                addbyte(0x6e);
                                addbyte(0x04);
                                addbyte(0x8d);
                                addlong(ai44);
                                addbyte(0x0f); /*MOVZX ECX, AH*/
                                addbyte(0xb6);
                                addbyte(0xcc);
                                addbyte(0x66); /*PUNPCKLBW XMM0, XMM2*/
                                addbyte(0x0f);
                                addbyte(0x60);
                                addbyte(0xc2);
                                addbyte(0x66); /*MOVD XMM1, ai44[ECX*4]*/
                                addbyte(0x0f);
                                addbyte(0x6e);
                                addbyte(0x0c);
                                addbyte(0x8d);
                                addlong(ai44);
                                addbyte(0x66); /*PMULLW XMM0, bilinear_lookup[ESI]*/
                                addbyte(0x0f);
                                addbyte(0xd5);
                                addbyte(0x06);
                                addbyte(0x0f); /*MOVZX ECX, DL*/
                                addbyte(0xb6);
                                addbyte(0xca);
                                addbyte(0x66); /*PUNPCKLBW XMM1, XMM2*/
                                addbyte(0x0f);
                                addbyte(0x60);
                                addbyte(0xca);
                                addbyte(0x66); /*MOVD XMM3, ai44[ECX*4]*/
                                addbyte(0x0f);
                                addbyte(0x6e);
                                addbyte(0x1c);
                                addbyte(0x8d);
                                addlong(ai44);
                                addbyte(0x66); /*PMULLW XMM1, bilinear_lookup[ESI]+0x10*/
                                addbyte(0x0f);
                                addbyte(0xd5);
                                addbyte(0x4e);
                                addbyte(0x10);
                                addbyte(0x0f); /*MOVZX ECX, DH*/
                                addbyte(0xb6);
                                addbyte(0xce);
                                addbyte(0x66); /*PUNPCKLBW XMM3, XMM2*/
                                addbyte(0x0f);
                                addbyte(0x60);
                                addbyte(0xda);
                                addbyte(0x66); /*PADDW XMM0, XMM1*/
                                addbyte(0x0f);
                                addbyte(0xfd);
                                addbyte(0xc1);
                                addbyte(0x66); /*MOVD XMM1, ai44[ECX*4]*/
                                addbyte(0x0f);
                                addbyte(0x6e);
                                addbyte(0x0c);
                                addbyte(0x8d);
                                addlong(ai44);
                                addbyte(0x66); /*PMULLW XMM3, bilinear_lookup[ESI]+0x20*/
                                addbyte(0x0f);
                                addbyte(0xd5);
                                addbyte(0x5e);
                                addbyte(0x20);
                                addbyte(0x66); /*PUNPCKLBW XMM1, XMM2*/
                                addbyte(0x0f);
                                addbyte(0x60);
                                addbyte(0xca);
                                addbyte(0x66); /*PADDW XMM0, XMM3*/
                                addbyte(0x0f);
                                addbyte(0xfd);
                                addbyte(0xc3);
                                addbyte(0x66); /*PMULLW XMM1, bilinear_lookup[ESI]+0x20*/
                                addbyte(0x0f);
                                addbyte(0xd5);
                                addbyte(0x4e);
                                addbyte(0x30);
                                addbyte(0x66); /*PADDW XMM0, XMM1*/
                                addbyte(0x0f);
                                addbyte(0xfd);
                                addbyte(0xc1);
                                addbyte(0x66); /*PSRLW XMM0, 8*/
                                addbyte(0x0f);
                                addbyte(0x71);
                                addbyte(0xd0);
                                addbyte(8);
                                addbyte(0x66); /*PACKUSWB XMM0, XMM0*/
                                addbyte(0x0f);
                                addbyte(0x67);
                                addbyte(0xc0);
                                addbyte(0x66); /*MOV EAX, XMM0*/
                                addbyte(0x0f);
                                addbyte(0x7e);
                                addbyte(0xc0);
                                break;
                                                
                                case TEX_PAL8:
                                addbyte(0x8b); /*MOV EBP, state->palette[EDI]*/
                                addbyte(0xaf);
                                addlong(offsetof(voodoo_state_t, palette));
                                addbyte(0x0f); /*MOVZX ECX, AL*/
                                addbyte(0xb6);
                                addbyte(0xc8);
                                addbyte(0x66); /*MOVD XMM0, [EBP+ECX*4]*/
                                addbyte(0x0f);
                                addbyte(0x6e);
                                addbyte(0x44);
                                addbyte(0x8d);
                                addbyte(0);
                                addbyte(0x0f); /*MOVZX ECX, AH*/
                                addbyte(0xb6);
                                addbyte(0xcc);
                                addbyte(0x66); /*PUNPCKLBW XMM0, XMM2*/
                                addbyte(0x0f);
                                addbyte(0x60);
                                addbyte(0xc2);
                                addbyte(0x66); /*MOVD XMM1, [EBP+ECX*4]*/
                                addbyte(0x0f);
                                addbyte(0x6e);
                                addbyte(0x4c);
                                addbyte(0x8d);
                                addbyte(0);
                                addbyte(0x66); /*PMULLW XMM0, bilinear_lookup[ESI]*/
                                addbyte(0x0f);
                                addbyte(0xd5);
                                addbyte(0x06);
                                addbyte(0x0f); /*MOVZX ECX, DL*/
                                addbyte(0xb6);
                                addbyte(0xca);
                                addbyte(0x66); /*PUNPCKLBW XMM1, XMM2*/
                                addbyte(0x0f);
                                addbyte(0x60);
                                addbyte(0xca);
                                addbyte(0x66); /*MOVD XMM3, [EBP+ECX*4]*/
                                addbyte(0x0f);
                                addbyte(0x6e);
                                addbyte(0x5c);
                                addbyte(0x8d);
                                addbyte(0);
                                addbyte(0x66); /*PMULLW XMM1, bilinear_lookup[ESI]+0x10*/
                                addbyte(0x0f);
                                addbyte(0xd5);
                                addbyte(0x4e);
                                addbyte(0x10);
                                addbyte(0x0f); /*MOVZX ECX, DH*/
                                addbyte(0xb6);
                                addbyte(0xce);
                                addbyte(0x66); /*PUNPCKLBW XMM3, XMM2*/
                                addbyte(0x0f);
                                addbyte(0x60);
                                addbyte(0xda);
                                addbyte(0x66); /*PADDW XMM0, XMM1*/
                                addbyte(0x0f);
                                addbyte(0xfd);
                                addbyte(0xc1);
                                addbyte(0x66); /*MOVD XMM1, [EBP+ECX*4]*/
                                addbyte(0x0f);
                                addbyte(0x6e);
                                addbyte(0x4c);
                                addbyte(0x8d);
                                addbyte(0);
                                addbyte(0x66); /*PMULLW XMM3, bilinear_lookup[ESI]+0x20*/
                                addbyte(0x0f);
                                addbyte(0xd5);
                                addbyte(0x5e);
                                addbyte(0x20);
                                addbyte(0x66); /*PUNPCKLBW XMM1, XMM2*/
                                addbyte(0x0f);
                                addbyte(0x60);
                                addbyte(0xca);
                                addbyte(0x66); /*PADDW XMM0, XMM3*/
                                addbyte(0x0f);
                                addbyte(0xfd);
                                addbyte(0xc3);
                                addbyte(0x66); /*PMULLW XMM1, bilinear_lookup[ESI]+0x30*/
                                addbyte(0x0f);
                                addbyte(0xd5);
                                addbyte(0x4e);
                                addbyte(0x30);
                                addbyte(0x66); /*PADDW XMM0, XMM1*/
                                addbyte(0x0f);
                                addbyte(0xfd);
                                addbyte(0xc1);
                                addbyte(0x66); /*PSRLW XMM0, 8*/
                                addbyte(0x0f);
                                addbyte(0x71);
                                addbyte(0xd0);
                                addbyte(8);
                                addbyte(0x66); /*PACKUSWB XMM0, XMM0*/
                                addbyte(0x0f);
                                addbyte(0x67);
                                addbyte(0xc0);
                                addbyte(0x66); /*MOV EAX, XMM0*/
                                addbyte(0x0f);
                                addbyte(0x7e);
                                addbyte(0xc0);
                                addbyte(0x0d); /*OR EAX, 0xff000000*/
                                addlong(0xff000000);
                                break;
                                                        
                                case TEX_R5G6B5:
                                addbyte(0x0f); /*MOVZX ECX, AX*/
                                addbyte(0xb7);
                                addbyte(0xc8);
                                addbyte(0xc1); /*SHR EAX, 16*/
                                addbyte(0xe8);
                                addbyte(16);
                                addbyte(0x66); /*MOVD XMM0, rgb565[ECX*4]*/
                                addbyte(0x0f);
                                addbyte(0x6e);
                                addbyte(0x04);
                                addbyte(0x8d);
                                addlong(rgb565);
                                addbyte(0x66); /*MOVD XMM1, rgb565[EAX*4]*/
                                addbyte(0x0f);
                                addbyte(0x6e);
                                addbyte(0x0c);
                                addbyte(0x85);
                                addlong(rgb565);
                                addbyte(0x66); /*PUNPCKLBW XMM0, XMM2*/
                                addbyte(0x0f);
                                addbyte(0x60);
                                addbyte(0xc2);
                                addbyte(0x0f); /*MOVZX ECX, DX*/
                                addbyte(0xb7);
                                addbyte(0xca);
                                addbyte(0xc1); /*SHR EDX, 16*/
                                addbyte(0xea);
                                addbyte(16);
                                addbyte(0x66); /*PMULLW XMM0, bilinear_lookup[ESI]*/
                                addbyte(0x0f);
                                addbyte(0xd5);
                                addbyte(0x06);
                                addbyte(0x66); /*PUNPCKLBW XMM1, XMM2*/
                                addbyte(0x0f);
                                addbyte(0x60);
                                addbyte(0xca);
                                addbyte(0x66); /*MOVD XMM3, rgb565[ECX*4]*/
                                addbyte(0x0f);
                                addbyte(0x6e);
                                addbyte(0x1c);
                                addbyte(0x8d);
                                addlong(rgb565);
                                addbyte(0x66); /*PMULLW XMM1, bilinear_lookup[ESI]+0x10*/
                                addbyte(0x0f);
                                addbyte(0xd5);
                                addbyte(0x4e);
                                addbyte(0x10);
                                addbyte(0x66); /*PUNPCKLBW XMM3, XMM2*/
                                addbyte(0x0f);
                                addbyte(0x60);
                                addbyte(0xda);
                                addbyte(0x66); /*PADDW XMM0, XMM1*/
                                addbyte(0x0f);
                                addbyte(0xfd);
                                addbyte(0xc1);
                                addbyte(0x66); /*MOVD XMM1, rgb565[EDX*4]*/
                                addbyte(0x0f);
                                addbyte(0x6e);
                                addbyte(0x0c);
                                addbyte(0x95);
                                addlong(rgb565);
                                addbyte(0x66); /*PMULLW XMM3, bilinear_lookup[ESI]+0x20*/
                                addbyte(0x0f);
                                addbyte(0xd5);
                                addbyte(0x5e);
                                addbyte(0x20);
                                addbyte(0x66); /*PUNPCKLBW XMM1, XMM2*/
                                addbyte(0x0f);
                                addbyte(0x60);
                                addbyte(0xca);
                                addbyte(0x66); /*PADDW XMM0, XMM3*/
                                addbyte(0x0f);
                                addbyte(0xfd);
                                addbyte(0xc3);
                                addbyte(0x66); /*PMULLW XMM1, bilinear_lookup[ESI]+0x30*/
                                addbyte(0x0f);
                                addbyte(0xd5);
                                addbyte(0x4e);
                                addbyte(0x30);
                                addbyte(0x66); /*PADDW XMM0, XMM1*/
                                addbyte(0x0f);
                                addbyte(0xfd);
                                addbyte(0xc1);
                                addbyte(0x66); /*PSRLW XMM0, 8*/
                                addbyte(0x0f);
                                addbyte(0x71);
                                addbyte(0xd0);
                                addbyte(8);
                                addbyte(0x66); /*PACKUSWB XMM0, XMM0*/
                                addbyte(0x0f);
                                addbyte(0x67);
                                addbyte(0xc0);
                                addbyte(0x66); /*MOV EAX, XMM0*/
                                addbyte(0x0f);
                                addbyte(0x7e);
                                addbyte(0xc0);
                                addbyte(0x0d); /*OR EAX, 0xff000000*/
                                addlong(0xff000000);
                                break;

                                case TEX_ARGB1555:
                                addbyte(0x0f); /*MOVZX ECX, AX*/
                                addbyte(0xb7);
                                addbyte(0xc8);
                                addbyte(0xc1); /*SHR EAX, 16*/
                                addbyte(0xe8);
                                addbyte(16);
                                addbyte(0x66); /*MOVD XMM0, argb1555[ECX*4]*/
                                addbyte(0x0f);
                                addbyte(0x6e);
                                addbyte(0x04);
                                addbyte(0x8d);
                                addlong(argb1555);
                                addbyte(0x66); /*MOVD XMM1, argb1555[EAX*4]*/
                                addbyte(0x0f);
                                addbyte(0x6e);
                                addbyte(0x0c);
                                addbyte(0x85);
                                addlong(argb1555);
                                addbyte(0x66); /*PUNPCKLBW XMM0, XMM2*/
                                addbyte(0x0f);
                                addbyte(0x60);
                                addbyte(0xc2);
                                addbyte(0x0f); /*MOVZX ECX, DX*/
                                addbyte(0xb7);
                                addbyte(0xca);
                                addbyte(0xc1); /*SHR EDX, 16*/
                                addbyte(0xea);
                                addbyte(16);
                                addbyte(0x66); /*PMULLW XMM0, bilinear_lookup[ESI]*/
                                addbyte(0x0f);
                                addbyte(0xd5);
                                addbyte(0x06);
                                addbyte(0x66); /*PUNPCKLBW XMM1, XMM2*/
                                addbyte(0x0f);
                                addbyte(0x60);
                                addbyte(0xca);
                                addbyte(0x66); /*MOVD XMM3, argb1555[ECX*4]*/
                                addbyte(0x0f);
                                addbyte(0x6e);
                                addbyte(0x1c);
                                addbyte(0x8d);
                                addlong(argb1555);
                                addbyte(0x66); /*PMULLW XMM1, bilinear_lookup[ESI]+0x10*/
                                addbyte(0x0f);
                                addbyte(0xd5);
                                addbyte(0x4e);
                                addbyte(0x10);
                                addbyte(0x66); /*PUNPCKLBW XMM3, XMM2*/
                                addbyte(0x0f);
                                addbyte(0x60);
                                addbyte(0xda);
                                addbyte(0x66); /*PADDW XMM0, XMM1*/
                                addbyte(0x0f);
                                addbyte(0xfd);
                                addbyte(0xc1);
                                addbyte(0x66); /*MOVD XMM1, argb1555[EDX*4]*/
                                addbyte(0x0f);
                                addbyte(0x6e);
                                addbyte(0x0c);
                                addbyte(0x95);
                                addlong(argb1555);
                                addbyte(0x66); /*PMULLW XMM3, bilinear_lookup[ESI]+0x20*/
                                addbyte(0x0f);
                                addbyte(0xd5);
                                addbyte(0x5e);
                                addbyte(0x20);
                                addbyte(0x66); /*PUNPCKLBW XMM1, XMM2*/
                                addbyte(0x0f);
                                addbyte(0x60);
                                addbyte(0xca);
                                addbyte(0x66); /*PADDW XMM0, XMM3*/
                                addbyte(0x0f);
                                addbyte(0xfd);
                                addbyte(0xc3);
                                addbyte(0x66); /*PMULLW XMM1, bilinear_lookup[ESI]+0x20*/
                                addbyte(0x0f);
                                addbyte(0xd5);
                                addbyte(0x4e);
                                addbyte(0x30);
                                addbyte(0x66); /*PADDW XMM0, XMM1*/
                                addbyte(0x0f);
                                addbyte(0xfd);
                                addbyte(0xc1);
                                addbyte(0x66); /*PSRLW XMM0, 8*/
                                addbyte(0x0f);
                                addbyte(0x71);
                                addbyte(0xd0);
                                addbyte(8);
                                addbyte(0x66); /*PACKUSWB XMM0, XMM0*/
                                addbyte(0x0f);
                                addbyte(0x67);
                                addbyte(0xc0);
                                addbyte(0x66); /*MOV EAX, XMM0*/
                                addbyte(0x0f);
                                addbyte(0x7e);
                                addbyte(0xc0);
                                break;

                                case TEX_ARGB4444:
                                addbyte(0x0f); /*MOVZX ECX, AX*/
                                addbyte(0xb7);
                                addbyte(0xc8);
                                addbyte(0xc1); /*SHR EAX, 16*/
                                addbyte(0xe8);
                                addbyte(16);
                                addbyte(0x66); /*MOVD XMM0, argb4444[ECX*4]*/
                                addbyte(0x0f);
                                addbyte(0x6e);
                                addbyte(0x04);
                                addbyte(0x8d);
                                addlong(argb4444);
                                addbyte(0x66); /*MOVD XMM1, argb4444[EAX*4]*/
                                addbyte(0x0f);
                                addbyte(0x6e);
                                addbyte(0x0c);
                                addbyte(0x85);
                                addlong(argb4444);
                                addbyte(0x66); /*PUNPCKLBW XMM0, XMM2*/
                                addbyte(0x0f);
                                addbyte(0x60);
                                addbyte(0xc2);
                                addbyte(0x0f); /*MOVZX ECX, DX*/
                                addbyte(0xb7);
                                addbyte(0xca);
                                addbyte(0xc1); /*SHR EDX, 16*/
                                addbyte(0xea);
                                addbyte(16);
                                addbyte(0x66); /*PMULLW XMM0, bilinear_lookup[ESI]*/
                                addbyte(0x0f);
                                addbyte(0xd5);
                                addbyte(0x06);
                                addbyte(0x66); /*PUNPCKLBW XMM1, XMM2*/
                                addbyte(0x0f);
                                addbyte(0x60);
                                addbyte(0xca);
                                addbyte(0x66); /*MOVD XMM3, argb4444[ECX*4]*/
                                addbyte(0x0f);
                                addbyte(0x6e);
                                addbyte(0x1c);
                                addbyte(0x8d);
                                addlong(argb4444);
                                addbyte(0x66); /*PMULLW XMM1, bilinear_lookup[ESI]+0x10*/
                                addbyte(0x0f);
                                addbyte(0xd5);
                                addbyte(0x4e);
                                addbyte(0x10);
                                addbyte(0x66); /*PUNPCKLBW XMM3, XMM2*/
                                addbyte(0x0f);
                                addbyte(0x60);
                                addbyte(0xda);
                                addbyte(0x66); /*PADDW XMM0, XMM1*/
                                addbyte(0x0f);
                                addbyte(0xfd);
                                addbyte(0xc1);
                                addbyte(0x66); /*MOVD XMM1, argb4444[EDX*4]*/
                                addbyte(0x0f);
                                addbyte(0x6e);
                                addbyte(0x0c);
                                addbyte(0x95);
                                addlong(argb4444);
                                addbyte(0x66); /*PMULLW XMM3, bilinear_lookup[ESI]+0x20*/
                                addbyte(0x0f);
                                addbyte(0xd5);
                                addbyte(0x5e);
                                addbyte(0x20);
                                addbyte(0x66); /*PUNPCKLBW XMM1, XMM2*/
                                addbyte(0x0f);
                                addbyte(0x60);
                                addbyte(0xca);
                                addbyte(0x66); /*PADDW XMM0, XMM3*/
                                addbyte(0x0f);
                                addbyte(0xfd);
                                addbyte(0xc3);
                                addbyte(0x66); /*PMULLW XMM1, bilinear_lookup[ESI]+0x20*/
                                addbyte(0x0f);
                                addbyte(0xd5);
                                addbyte(0x4e);
                                addbyte(0x30);
                                addbyte(0x66); /*PADDW XMM0, XMM1*/
                                addbyte(0x0f);
                                addbyte(0xfd);
                                addbyte(0xc1);
                                addbyte(0x66); /*PSRLW XMM0, 8*/
                                addbyte(0x0f);
                                addbyte(0x71);
                                addbyte(0xd0);
                                addbyte(8);
                                addbyte(0x66); /*PACKUSWB XMM0, XMM0*/
                                addbyte(0x0f);
                                addbyte(0x67);
                                addbyte(0xc0);
                                addbyte(0x66); /*MOV EAX, XMM0*/
                                addbyte(0x0f);
                                addbyte(0x7e);
                                addbyte(0xc0);
                                break;
                                                        
                                case TEX_A8I8:
                                addbyte(0x0f); /*MOVZX ECX, AX*/
                                addbyte(0xb7);
                                addbyte(0xc8);
                                addbyte(0xc1); /*SHR EAX, 16*/
                                addbyte(0xe8);
                                addbyte(16);
                                addbyte(0x66); /*MOVD XMM0, ai88[ECX*4]*/
                                addbyte(0x0f);
                                addbyte(0x6e);
                                addbyte(0x04);
                                addbyte(0x8d);
                                addlong(ai88);
                                addbyte(0x66); /*MOVD XMM1, ai88[EAX*4]*/
                                addbyte(0x0f);
                                addbyte(0x6e);
                                addbyte(0x0c);
                                addbyte(0x85);
                                addlong(ai88);
                                addbyte(0x66); /*PUNPCKLBW XMM0, XMM2*/
                                addbyte(0x0f);
                                addbyte(0x60);
                                addbyte(0xc2);
                                addbyte(0x0f); /*MOVZX ECX, DX*/
                                addbyte(0xb7);
                                addbyte(0xca);
                                addbyte(0xc1); /*SHR EDX, 16*/
                                addbyte(0xea);
                                addbyte(16);
                                addbyte(0x66); /*PMULLW XMM0, bilinear_lookup[ESI]*/
                                addbyte(0x0f);
                                addbyte(0xd5);
                                addbyte(0x06);
                                addbyte(0x66); /*PUNPCKLBW XMM1, XMM2*/
                                addbyte(0x0f);
                                addbyte(0x60);
                                addbyte(0xca);
                                addbyte(0x66); /*MOVD XMM3, ai88[ECX*4]*/
                                addbyte(0x0f);
                                addbyte(0x6e);
                                addbyte(0x1c);
                                addbyte(0x8d);
                                addlong(ai88);
                                addbyte(0x66); /*PMULLW XMM1, bilinear_lookup[ESI]+0x10*/
                                addbyte(0x0f);
                                addbyte(0xd5);
                                addbyte(0x4e);
                                addbyte(0x10);
                                addbyte(0x66); /*PUNPCKLBW XMM3, XMM2*/
                                addbyte(0x0f);
                                addbyte(0x60);
                                addbyte(0xda);
                                addbyte(0x66); /*PADDW XMM0, XMM1*/
                                addbyte(0x0f);
                                addbyte(0xfd);
                                addbyte(0xc1);
                                addbyte(0x66); /*MOVD XMM1, ai88[EDX*4]*/
                                addbyte(0x0f);
                                addbyte(0x6e);
                                addbyte(0x0c);
                                addbyte(0x95);
                                addlong(ai88);
                                addbyte(0x66); /*PMULLW XMM3, bilinear_lookup[ESI]+0x20*/
                                addbyte(0x0f);
                                addbyte(0xd5);
                                addbyte(0x5e);
                                addbyte(0x20);
                                addbyte(0x66); /*PUNPCKLBW XMM1, XMM2*/
                                addbyte(0x0f);
                                addbyte(0x60);
                                addbyte(0xca);
                                addbyte(0x66); /*PADDW XMM0, XMM3*/
                                addbyte(0x0f);
                                addbyte(0xfd);
                                addbyte(0xc3);
                                addbyte(0x66); /*PMULLW XMM1, bilinear_lookup[ESI]+0x20*/
                                addbyte(0x0f);
                                addbyte(0xd5);
                                addbyte(0x4e);
                                addbyte(0x30);
                                addbyte(0x66); /*PADDW XMM0, XMM1*/
                                addbyte(0x0f);
                                addbyte(0xfd);
                                addbyte(0xc1);
                                addbyte(0x66); /*PSRLW XMM0, 8*/
                                addbyte(0x0f);
                                addbyte(0x71);
                                addbyte(0xd0);
                                addbyte(8);
                                addbyte(0x66); /*PACKUSWB XMM0, XMM0*/
                                addbyte(0x0f);
                                addbyte(0x67);
                                addbyte(0xc0);
                                addbyte(0x66); /*MOV EAX, XMM0*/
                                addbyte(0x0f);
                                addbyte(0x7e);
                                addbyte(0xc0);
                                break;

                                case TEX_APAL88:
                                addbyte(0x8b); /*MOV EBP, state->palette[EDI]*/
                                addbyte(0xaf);
                                addlong(offsetof(voodoo_state_t, palette));
                                addbyte(0x0f); /*MOVZX ECX, AL*/
                                addbyte(0xb6);
                                addbyte(0xc8);
                                addbyte(0x66); /*MOVD XMM0, [EBP+ECX*4]*/
                                addbyte(0x0f);
                                addbyte(0x6e);
                                addbyte(0x44);
                                addbyte(0x8d);
                                addbyte(0);
                                addbyte(0x0f); /*MOVZX ECX, AH*/
                                addbyte(0xb6);
                                addbyte(0xcc);
                                addbyte(0x66); /*PUNPCKLBW XMM0, XMM2*/
                                addbyte(0x0f);
                                addbyte(0x60);
                                addbyte(0xc2);
                                addbyte(0xc1); /*SHR EAX, 16*/
                                addbyte(0xe8);
                                addbyte(16);
                                addbyte(0x66); /*PINSRW XMM0, ECX, 3*/
                                addbyte(0x0f);
                                addbyte(0xc4);
                                addbyte(0xc1);
                                addbyte(3);
                                addbyte(0x0f); /*MOVZX ECX, AL*/
                                addbyte(0xb6);
                                addbyte(0xc8);
                                addbyte(0x0f); /*MOVZX EAX, AH*/
                                addbyte(0xb6);
                                addbyte(0xc4);
                                addbyte(0x66); /*MOVD XMM1, [EBP+ECX*4]*/
                                addbyte(0x0f);
                                addbyte(0x6e);
                                addbyte(0x4c);
                                addbyte(0x8d);
                                addbyte(0);
                                addbyte(0x66); /*PMULLW XMM0, bilinear_lookup[ESI]*/
                                addbyte(0x0f);
                                addbyte(0xd5);
                                addbyte(0x06);
                                addbyte(0x0f); /*MOVZX ECX, DL*/
                                addbyte(0xb6);
                                addbyte(0xca);
                                addbyte(0x66); /*PUNPCKLBW XMM1, XMM2*/
                                addbyte(0x0f);
                                addbyte(0x60);
                                addbyte(0xca);
                                addbyte(0x66); /*PINSRW XMM1, EAX, 3*/
                                addbyte(0x0f);
                                addbyte(0xc4);
                                addbyte(0xc8);
                                addbyte(3);
                                addbyte(0x66); /*MOVD XMM3, [EBP+ECX*4]*/
                                addbyte(0x0f);
                                addbyte(0x6e);
                                addbyte(0x5c);
                                addbyte(0x8d);
                                addbyte(0);
                                addbyte(0x0f); /*MOVZX ECX, DH*/
                                addbyte(0xb6);
                                addbyte(0xce);
                                addbyte(0xc1); /*SHR EDX, 16*/
                                addbyte(0xea);
                                addbyte(16);
                                addbyte(0x66); /*PUNPCKLBW XMM3, XMM2*/
                                addbyte(0x0f);
                                addbyte(0x60);
                                addbyte(0xda);
                                addbyte(0x66); /*PMULLW XMM1, bilinear_lookup[ESI]+0x10*/
                                addbyte(0x0f);
                                addbyte(0xd5);
                                addbyte(0x4e);
                                addbyte(0x10);
                                addbyte(0x66); /*PINSRW XMM3, ECX, 3*/
                                addbyte(0x0f);
                                addbyte(0xc4);
                                addbyte(0xd9);
                                addbyte(3);
                                addbyte(0x0f); /*MOVZX ECX, DL*/
                                addbyte(0xb6);
                                addbyte(0xca);
                                addbyte(0x66); /*PADDW XMM0, XMM1*/
                                addbyte(0x0f);
                                addbyte(0xfd);
                                addbyte(0xc1);
                                addbyte(0x66); /*MOVD XMM1, [EBP+ECX*4]*/
                                addbyte(0x0f);
                                addbyte(0x6e);
                                addbyte(0x4c);
                                addbyte(0x8d);
                                addbyte(0);
                                addbyte(0x0f); /*MOVZX ECX, DH*/
                                addbyte(0xb6);
                                addbyte(0xce);
                                addbyte(0x66); /*PUNPCKLBW XMM1, XMM2*/
                                addbyte(0x0f);
                                addbyte(0x60);
                                addbyte(0xca);
                                addbyte(0x66); /*PMULLW XMM3, bilinear_lookup[ESI]+0x20*/
                                addbyte(0x0f);
                                addbyte(0xd5);
                                addbyte(0x5e);
                                addbyte(0x20);
                                addbyte(0x66); /*PINSR1 XMM1, ECX, 3*/
                                addbyte(0x0f);
                                addbyte(0xc4);
                                addbyte(0xc9);
                                addbyte(3);
                                addbyte(0x66); /*PADDW XMM0, XMM3*/
                                addbyte(0x0f);
                                addbyte(0xfd);
                                addbyte(0xc3);
                                addbyte(0x66); /*PMULLW XMM1, bilinear_lookup[ESI]+0x30*/
                                addbyte(0x0f);
                                addbyte(0xd5);
                                addbyte(0x4e);
                                addbyte(0x30);
                                addbyte(0x66); /*PADDW XMM0, XMM1*/
                                addbyte(0x0f);
                                addbyte(0xfd);
                                addbyte(0xc1);
                                addbyte(0x66); /*PSRLW XMM0, 8*/
                                addbyte(0x0f);
                                addbyte(0x71);
                                addbyte(0xd0);
                                addbyte(8);
                                addbyte(0x66); /*PACKUSWB XMM0, XMM0*/
                                addbyte(0x0f);
                                addbyte(0x67);
                                addbyte(0xc0);
                                addbyte(0x66); /*MOV EAX, XMM0*/
                                addbyte(0x0f);
                                addbyte(0x7e);
                                addbyte(0xc0);
                                break;
                                                        
                                default:
                                fatal("Unknown texture format %i\n", state->tformat);
                        }
                        
                        addbyte(0x8b); /*MOV ESI, [ESP+8]*/
                        addbyte(0x74);
                        addbyte(0x24);
                        addbyte(8+16); /*CHECK!*/
                }
                else
                {
                        addbyte(0x8b); /*MOV ECX, state->lod[EDI]*/
                        addbyte(0x8f);
                        addlong(offsetof(voodoo_state_t, lod));
                        addbyte(0x8a); /*MOV DL, params->tex_shift[ESI+ECX*4]*/
                        addbyte(0x94);
                        addbyte(0x8e);
                        addlong(offsetof(voodoo_params_t, tex_shift));
                        addbyte(0x8b); /*MOV EBP, state->tex[EDI+ECX*4]*/
                        addbyte(0xac);
                        addbyte(0x8f);
                        addlong(offsetof(voodoo_state_t, tex));
                        addbyte(0x80); /*ADD CL, 4*/
                        addbyte(0xc1);
                        addbyte(4);
                        addbyte(0x8b); /*MOV EAX, state->tex_s[EDI]*/
                        addbyte(0x87);
                        addlong(offsetof(voodoo_state_t, tex_s));
                        addbyte(0x8b); /*MOV EBX, state->tex_t[EDI]*/
                        addbyte(0x9f);
                        addlong(offsetof(voodoo_state_t, tex_t));
                        addbyte(0xd3); /*SHR EAX, CL*/
                        addbyte(0xe8);
                        addbyte(0xd3); /*SHR EBX, CL*/
                        addbyte(0xeb);
                        if (state->clamp_s)
                        {
                                addbyte(0x85); /*TEST EAX, EAX*/
                                addbyte(0xc0);
                                addbyte(0x0f); /*CMOVS EAX, zero*/
                                addbyte(0x48);
                                addbyte(0x05);
                                addlong(&zero);
                                addbyte(0x3b); /*CMP EAX, params->tex_w_mask[ESI+ECX*4]*/
                                addbyte(0x84);
                                addbyte(0x8e);
                                addlong(offsetof(voodoo_params_t, tex_w_mask) - 0x10);
                                addbyte(0x0f); /*CMOVAE EAX, params->tex_w_mask[ESI+ECX*4]*/
                                addbyte(0x43);
                                addbyte(0x84);
                                addbyte(0x8e);
                                addlong(offsetof(voodoo_params_t, tex_w_mask) - 0x10);

                        }
                        else
                        {
                                addbyte(0x23); /*AND EAX, params->tex_w_mask-0x10[ESI+ECX*4]*/
                                addbyte(0x84);
                                addbyte(0x8e);
                                addlong(offsetof(voodoo_params_t, tex_w_mask) - 0x10);
                        }
                        if (state->clamp_t)
                        {
                                addbyte(0x85); /*TEST EBX, EBX*/
                                addbyte(0xdb);
                                addbyte(0x0f); /*CMOVS EBX, zero*/
                                addbyte(0x48);
                                addbyte(0x1d);
                                addlong(&zero);
                                addbyte(0x3b); /*CMP EBX, params->tex_h_mask[ESI+ECX*4]*/
                                addbyte(0x9c);
                                addbyte(0x8e);
                                addlong(offsetof(voodoo_params_t, tex_h_mask) - 0x10);
                                addbyte(0x0f); /*CMOVAE EBX, params->tex_h_mask[ESI+ECX*4]*/
                                addbyte(0x43);
                                addbyte(0x9c);
                                addbyte(0x8e);
                                addlong(offsetof(voodoo_params_t, tex_h_mask) - 0x10);
                        }
                        else
                        {
                                addbyte(0x23); /*AND EBX, params->tex_h_mask-0x10[ESI+ECX*4]*/
                                addbyte(0x9c);
                                addbyte(0x8e);
                                addlong(offsetof(voodoo_params_t, tex_h_mask) - 0x10);
                        }
                        addbyte(0x88); /*MOV CL, DL*/
                        addbyte(0xd1);
                        addbyte(0xd3); /*SHL EBX, CL*/
                        addbyte(0xe3);
                        addbyte(0x01); /*ADD EBX, EAX*/
                        addbyte(0xc3);

                        if (state->tformat & 8)
                        {
                                addbyte(0x0f); /*MOVZX EAX,W[EBP+EBX*2]*/
                                addbyte(0xb7);
                                addbyte(0x44);
                                addbyte(0x5d);
                                addbyte(0);
                        }
                        else
                        {
                                addbyte(0x0f); /*MOVZX EAX,B[EBP+EBX]*/
                                addbyte(0xb6);
                                addbyte(0x44);
                                addbyte(0x1d);
                                addbyte(0);
                        }

                        switch (state->tformat)
                        {
                                case TEX_RGB332:
                                addbyte(0x8b); /*MOV EAX, rgb332[EAX*4]*/
                                addbyte(0x04);
                                addbyte(0x85);
                                addlong(rgb332);
                                addbyte(0x0d); /*OR EAX, 0xff000000*/
                                addlong(0xff000000);
                                break;
                                                
                                case TEX_Y4I2Q2:
                                addbyte(0x8b); /*MOV EBP, state->palette[EDI]*/
                                addbyte(0xaf);
                                addlong(offsetof(voodoo_state_t, palette));
                                addbyte(0x8b); /*MOV EAX, [EBP+EAX*4]*/
                                addbyte(0x44);
                                addbyte(0x85);
                                addbyte(0);
//                                addbyte(0x0f); /*BSWAP EAX*/
//                                addbyte(0xc8);
                                addbyte(0x0d); /*OR EAX, 0xff000000*/
                                addlong(0xff000000);
                                break;
                                                        
                                case TEX_A8:
                                addbyte(0x88); /*MOV AH, AL*/
                                addbyte(0xc4);
                                addbyte(0x66); /*MOV BX, AX*/
                                addbyte(0x89);
                                addbyte(0xc3);
                                addbyte(0x0f); /*BSWAP EAX*/
                                addbyte(0xc8);
                                addbyte(0x66); /*MOV AX, BX*/
                                addbyte(0x89);
                                addbyte(0xd8);
                                break;

                                case TEX_I8:
                                addbyte(0x88); /*MOV AH, AL*/
                                addbyte(0xc4);
                                addbyte(0xc1); /*SHL EAX, 8*/
                                addbyte(0xe0);
                                addbyte(8);
                                addbyte(0x88); /*MOV AL, AH*/
                                addbyte(0xe0);
                                addbyte(0x0d); /*OR EAX, 0xff000000*/
                                addlong(0xff000000);
//                                addbyte(0x25); /*AND EAX, 0x00ffffff*/
//                                addlong(0x00000000);
                                break;

                                case TEX_AI8:
                                addbyte(0x89); /*MOV EBX, EAX*/
                                addbyte(0xc3);
                                addbyte(0x83); /*AND EAX, 0x0f*/
                                addbyte(0xe0);
                                addbyte(0x0f);
                                addbyte(0x81); /*AND EBX, 0xf0*/
                                addbyte(0xe3);
                                addlong(0xf0);
                                addbyte(0x89); /*MOV ECX, EAX*/
                                addbyte(0xc1);
                                addbyte(0x89); /*MOV EDX, EBX*/
                                addbyte(0xda);
                                addbyte(0xc1); /*SHL ECX, 4*/
                                addbyte(0xe1);
                                addbyte(4);
                                addbyte(0xc1); /*SHR EDX, 4*/
                                addbyte(0xe2);
                                addbyte(4);
                                addbyte(0x09); /*OR EAX, ECX*/
                                addbyte(0xc8);
                                addbyte(0x09); /*OR EBX, EDX*/
                                addbyte(0xd3);
                                addbyte(0x88); /*MOV AH, AL*/
                                addbyte(0xc4);
                                addbyte(0xc1); /*SHL EBX, 24*/
                                addbyte(0xe3);
                                addbyte(24);
                                addbyte(0xc1); /*SHL EAX, 8*/
                                addbyte(0xe0);
                                addbyte(8);
                                addbyte(0x88); /*MOV AL, AH*/
                                addbyte(0xe0);
                                addbyte(0x09); /*OR EAX, EBX*/
                                addbyte(0xd8);
                                break;
                                                
                                case TEX_PAL8:
                                addbyte(0x8b); /*MOV EBP, state->palette[EDI]*/
                                addbyte(0xaf);
                                addlong(offsetof(voodoo_state_t, palette));
                                addbyte(0x8b); /*MOV EAX, [EBP+EAX*4]*/
                                addbyte(0x44);
                                addbyte(0x85);
                                addbyte(0);
//                                addbyte(0x0f); /*BSWAP EAX*/
//                                addbyte(0xc8);
                                addbyte(0x0d); /*OR EAX, 0xff000000*/
                                addlong(0xff000000);
                                break;
                                                        
                                case TEX_R5G6B5:
                                addbyte(0x8b); /*MOV EAX, rgb565[EAX*4]*/
                                addbyte(0x04);
                                addbyte(0x85);
                                addlong(rgb565);
                                addbyte(0x0d); /*OR EAX, 0xff000000*/
                                addlong(0xff000000);
                                break;

                                case TEX_ARGB1555:
                                addbyte(0x8b); /*MOV EAX, argb1555[EAX*4]*/
                                addbyte(0x04);
                                addbyte(0x85);
                                addlong(argb1555);
                                break;

                                case TEX_ARGB4444:
                                addbyte(0x8b); /*MOV EAX, argb4444[EAX*4]*/
                                addbyte(0x04);
                                addbyte(0x85);
                                addlong(argb4444);
                                break;
                                                        
                                case TEX_A8I8:
                                addbyte(0x89); /*MOV EBX, EAX*/
                                addbyte(0xc3);
                                addbyte(0xc1); /*SHL EAX, 16*/
                                addbyte(0xe0);
                                addbyte(16);
                                addbyte(0x88); /*MOV AL, BL*/
                                addbyte(0xd8);
                                addbyte(0x88); /*MOV AH, BL*/
                                addbyte(0xdc);
                                break;

                                case TEX_APAL88:
                                addbyte(0x8b); /*MOV EBP, state->palette[EDI]*/
                                addbyte(0xaf);
                                addlong(offsetof(voodoo_state_t, palette));
                                addbyte(0x89); /*MOV EBX, EAX*/
                                addbyte(0xc3);
                                addbyte(0x25); /*AND EAX, 0x000000ff*/
                                addlong(0x000000ff);
                                addbyte(0x8b); /*MOV EAX, [EBP+EAX*4]*/
                                addbyte(0x44);
                                addbyte(0x85);
                                addbyte(0);
                                addbyte(0xc1); /*SHL EBX, 16*/
                                addbyte(0xe3);
                                addbyte(16);
//                                addbyte(0x0f); /*BSWAP EAX*/
//                                addbyte(0xc8);
                                addbyte(0x81); /*AND EBX, 0xff000000*/
                                addbyte(0xe3);
                                addlong(0xff000000);
                                addbyte(0x25); /*AND EAX, 0x00ffffff*/
                                addlong(0x00ffffff);
                                addbyte(0x09); /*OR EAX, EBX*/
                                addbyte(0xd8);

//                                addbyte(0x25); /*AND EAX, 0x00ffffff*/
//                                addlong(0x00000000);
                                break;
                                                        
                                default:
                                fatal("Unknown texture format %i\n", state->tformat);
                        }
                }
                if ((params->fbzMode & FBZ_CHROMAKEY))
                {
                        addbyte(0x8b); /*MOV EBX, params->chromaKey[ESI]*/
                        addbyte(0x9e);
                        addlong(offsetof(voodoo_params_t, chromaKey));
                        addbyte(0x31); /*XOR EBX, EAX*/
                        addbyte(0xc3);
                        addbyte(0x81); /*AND EBX, 0xffffff*/
                        addbyte(0xe3);
                        addlong(0xffffff);
                        addbyte(0x0f); /*JE skip*/
                        addbyte(0x84);
                        chroma_skip_pos = block_pos;
                        addlong(0);
                }
#if 0
                addbyte(0x0f); /*MOVZX EBX, AL*/
                addbyte(0xb6);
                addbyte(0xd8);
                addbyte(0x89); /*MOV state->tex_b[EDI], EBX*/
                addbyte(0x9f);
                addlong(offsetof(voodoo_state_t, tex_b));
                addbyte(0x0f); /*MOVZX EBX, AH*/
                addbyte(0xb6);
                addbyte(0xdc);
                addbyte(0xc1); /*SHR EAX, 16*/
                addbyte(0xe8);
                addbyte(16);
                addbyte(0x89); /*MOV state->tex_g[EDI], EBX*/
                addbyte(0x9f);
                addlong(offsetof(voodoo_state_t, tex_g));
                addbyte(0x0f); /*MOVZX EBX, AL*/
                addbyte(0xb6);
                addbyte(0xd8);
                addbyte(0x89); /*MOV state->tex_r[EDI], EBX*/
                addbyte(0x9f);
                addlong(offsetof(voodoo_state_t, tex_r));
                addbyte(0x0f); /*MOVZX EBX, AH*/
                addbyte(0xb6);
                addbyte(0xdc);
                addbyte(0x89); /*MOV state->tex_a[EDI], EBX*/
                addbyte(0x9f);
                addlong(offsetof(voodoo_state_t, tex_a));
#endif
//#if 0
//                addbyte(0x89); /*MOV state->tex_out[EDI], EAX*/
//                addbyte(0x87);
//                addlong(offsetof(voodoo_state_t, tex_out));
                addbyte(0x66); /*MOVD XMM0, EAX*/
                addbyte(0x0f);
                addbyte(0x6e);
                addbyte(0xc0);
                addbyte(0xc1); /*SHR EAX, 24*/
                addbyte(0xe8);
                addbyte(24);
                addbyte(0x89); /*MOV state->tex_a[EDI], EAX*/
                addbyte(0x87);
                addlong(offsetof(voodoo_state_t, tex_a));
//#endif
        }


        if (params->alphaMode & ((1 << 0) | (1 << 4)))
        {
                /*EBX = a_other*/
                switch (a_sel)
                {
                        case A_SEL_ITER_A:
                        addbyte(0x8b); /*MOV EBX, state->ia*/
                        addbyte(0x9f);
                        addlong(offsetof(voodoo_state_t, ia));
                        addbyte(0x31); /*XOR EAX, EAX*/
                        addbyte(0xc0);
                        addbyte(0xba); /*MOV EDX, 0xff*/
                        addlong(0xff);
                        addbyte(0xc1); /*SAR EBX, 12*/
                        addbyte(0xfb);
                        addbyte(12);
                        addbyte(0x0f); /*CMOVS EBX, EAX*/
                        addbyte(0x48);
                        addbyte(0xd8);
                        addbyte(0x39); /*CMP EBX, EDX*/
                        addbyte(0xd3);
                        addbyte(0x0f); /*CMOVA EBX, EDX*/
                        addbyte(0x47);
                        addbyte(0xda);
                        break;
                        case A_SEL_TEX:
                        addbyte(0x8b); /*MOV EBX, state->tex_a*/
                        addbyte(0x9f);
                        addlong(offsetof(voodoo_state_t, tex_a));
                        break;
                        case A_SEL_COLOR1:
                        addbyte(0x0f); /*MOVZX EBX, params->color1+3*/
                        addbyte(0xb6);
                        addbyte(0x9e);
                        addlong(offsetof(voodoo_params_t, color1)+3);
                        break;
                        default:
                        addbyte(0x31); /*XOR EBX, EBX*/
                        addbyte(0xdb);
                        break;
                }
                /*ECX = a_local*/
                switch (cca_localselect)
                {
                        case CCA_LOCALSELECT_ITER_A:
                        if (a_sel == A_SEL_ITER_A)
                        {
                                addbyte(0x89); /*MOV ECX, EBX*/
                                addbyte(0xd9);
                        }
                        else
                        {
                                addbyte(0x8b); /*MOV ECX, state->ia*/
                                addbyte(0x8f);
                                addlong(offsetof(voodoo_state_t, ia));
                                addbyte(0x31); /*XOR EAX, EAX*/
                                addbyte(0xc0); 
                                addbyte(0xba); /*MOV EDX, 0xff*/
                                addlong(0xff);
                                addbyte(0xc1);/*SAR ECX, 12*/
                                addbyte(0xf9);
                                addbyte(12);
                                addbyte(0x0f); /*CMOVS ECX, EAX*/
                                addbyte(0x48);
                                addbyte(0xc8);
                                addbyte(0x39); /*CMP ECX, EDX*/
                                addbyte(0xd1);
                                addbyte(0x0f); /*CMOVA ECX, EDX*/
                                addbyte(0x47);
                                addbyte(0xca);
                        }
                        break;
                        case CCA_LOCALSELECT_COLOR0:
                        addbyte(0x0f); /*MOVZX ECX, params->color0+3*/
                        addbyte(0xb6);
                        addbyte(0x8e);
                        addlong(offsetof(voodoo_params_t, color0)+3);
                        break;
                        case CCA_LOCALSELECT_ITER_Z:
                        addbyte(0x8b); /*MOV ECX, state->z*/
                        addbyte(0x8f);
                        addlong(offsetof(voodoo_state_t, z));
                        if (a_sel != A_SEL_ITER_A)
                        {
                                addbyte(0x31); /*XOR EAX, EAX*/
                                addbyte(0xc0); 
                                addbyte(0xba); /*MOV EDX, 0xff*/
                                addlong(0xff);
                        }
                        addbyte(0xc1);/*SAR ECX, 20*/
                        addbyte(0xf9);
                        addbyte(20);
                        addbyte(0x0f); /*CMOVS ECX, EAX*/
                        addbyte(0x48);
                        addbyte(0xc8);
                        addbyte(0x39); /*CMP ECX, EDX*/
                        addbyte(0xd1);
                        addbyte(0x0f); /*CMOVA ECX, EDX*/
                        addbyte(0x47);
                        addbyte(0xca);
                        break;
                                        
                        default:
                        addbyte(0xb9); /*MOV ECX, 0xff*/
                        addlong(0xff);
                        break;
                }

                if (cca_zero_other)
                {
                        addbyte(0x31); /*XOR EDX, EDX*/
                        addbyte(0xd2);
                }
                else
                {
                        addbyte(0x89); /*MOV EDX, EBX*/
                        addbyte(0xda);
                }
        
                if (cca_sub_clocal)
                {
                        addbyte(0x29); /*SUB EDX, ECX*/
                        addbyte(0xca);
                }
        }

        if (cc_sub_clocal || cc_mselect == 1 || cc_add == 1)
        {
                /*XMM1 = local*/
                if (!cc_localselect_override)
                {
                        if (cc_localselect)
                        {
                                addbyte(0x66); /*MOVD XMM1, params->color0*/
                                addbyte(0x0f);
                                addbyte(0x6e);
                                addbyte(0x8e);
                                addlong(offsetof(voodoo_params_t, color0));
                        }
                        else
                        {
                                addbyte(0xf3); /*MOVDQU XMM1, ib*/ /* ir, ig and ib must be in same dqword!*/
                                addbyte(0x0f);
                                addbyte(0x6f);
                                addbyte(0x8f);
                                addlong(offsetof(voodoo_state_t, ib));
                                addbyte(0x66); /*PSRAD XMM1, 12*/
                                addbyte(0x0f);
                                addbyte(0x72);
                                addbyte(0xe1);
                                addbyte(12);
                                addbyte(0x66); /*PACKSSDW XMM1, XMM1*/
                                addbyte(0x0f);
                                addbyte(0x6b);
                                addbyte(0xc9);
                                addbyte(0x66); /*PACKUSWB XMM1, XMM1*/
                                addbyte(0x0f);
                                addbyte(0x67);
                                addbyte(0xc9);
                        }
                }
                else
                {
                        addbyte(0xf6); /*TEST state->tex_a, 0x80*/
                        addbyte(0x87);
                        addbyte(0x23);
                        addlong(offsetof(voodoo_state_t, tex_a));
                        addbyte(0x80);
                        addbyte(0x74);/*JZ !cc_localselect*/
                        addbyte(8+2);
                                addbyte(0x66); /*MOVD XMM1, params->color0*/
                                addbyte(0x0f);
                                addbyte(0x6e);
                                addbyte(0x8e);
                                addlong(offsetof(voodoo_params_t, color0));
                                /*JMP +*/
                        /*!cc_localselect:*/
                                addbyte(0xf3); /*MOVDQU XMM1, ib*/ /* ir, ig and ib must be in same dqword!*/
                                addbyte(0x0f);
                                addbyte(0x6f);
                                addbyte(0x8f);
                                addlong(offsetof(voodoo_state_t, ib));
                                addbyte(0x66); /*PSRAD XMM1, 12*/
                                addbyte(0x0f);
                                addbyte(0x72);
                                addbyte(0xe1);
                                addbyte(12);
                                addbyte(0x66); /*PACKSSDW XMM1, XMM1*/
                                addbyte(0x0f);
                                addbyte(0x6b);
                                addbyte(0xc9);
                                addbyte(0x66); /*PACKUSWB XMM1, XMM1*/
                                addbyte(0x0f);
                                addbyte(0x67);
                                addbyte(0xc9);
                }
                addbyte(0x66); /*PUNPCKLBW XMM1, XMM2*/
                addbyte(0x0f);
                addbyte(0x60);
                addbyte(0xca);
        }
        if (!cc_zero_other)
        {
                if (_rgb_sel == CC_LOCALSELECT_ITER_RGB)
                {
                        addbyte(0xf3); /*MOVDQU XMM0, ib*/ /* ir, ig and ib must be in same dqword!*/
                        addbyte(0x0f);
                        addbyte(0x6f);
                        addbyte(0x87);
                        addlong(offsetof(voodoo_state_t, ib));
                        addbyte(0x66); /*PSRAD XMM0, 12*/
                        addbyte(0x0f);
                        addbyte(0x72);
                        addbyte(0xe0);
                        addbyte(12);
                        addbyte(0x66); /*PACKSSDW XMM0, XMM0*/
                        addbyte(0x0f);
                        addbyte(0x6b);
                        addbyte(0xc0);
                        addbyte(0x66); /*PACKUSWB XMM0, XMM0*/
                        addbyte(0x0f);
                        addbyte(0x67);
                        addbyte(0xc0);
                }
                else if (_rgb_sel == CC_LOCALSELECT_TEX)
                {
#if 0
                        addbyte(0xf3); /*MOVDQU XMM0, state->tex_b*/
                        addbyte(0x0f);
                        addbyte(0x6f);
                        addbyte(0x87);
                        addlong(offsetof(voodoo_state_t, tex_b));
                        addbyte(0x66); /*PACKSSDW XMM0, XMM0*/
                        addbyte(0x0f);
                        addbyte(0x6b);
                        addbyte(0xc0);
                        addbyte(0x66); /*PACKUSWB XMM0, XMM0*/
                        addbyte(0x0f);
                        addbyte(0x67);
                        addbyte(0xc0);
#endif
                }
                else if (_rgb_sel == CC_LOCALSELECT_COLOR1)
                {
                        addbyte(0x66); /*MOVD XMM0, params->color1*/
                        addbyte(0x0f);
                        addbyte(0x6e);
                        addbyte(0x86);
                        addlong(offsetof(voodoo_params_t, color1));
                }
                else
                {
                        /*MOVD XMM0, src_r*/
                }
                addbyte(0x66); /*PUNPCKLBW XMM0, XMM2*/
                addbyte(0x0f);
                addbyte(0x60);
                addbyte(0xc2);
                if (cc_sub_clocal)
                {
                        addbyte(0x66); /*PSUBW XMM0, XMM1*/
                        addbyte(0x0f);
                        addbyte(0xf9);
                        addbyte(0xc1);
                }
        }
        else
        {
                addbyte(0x66); /*PXOR XMM0, XMM0*/
                addbyte(0x0f);
                addbyte(0xef);
                addbyte(0xc0);
                if (cc_sub_clocal)
                {
                        addbyte(0x66); /*PSUBW XMM0, XMM1*/
                        addbyte(0x0f);
                        addbyte(0xf9);
                        addbyte(0xc1);
                }
        }

        if (params->alphaMode & ((1 << 0) | (1 << 4)))
        {
                if (!(cca_mselect == 0 && cca_reverse_blend == 0))
                {
                        switch (cca_mselect)
                        {
                                case CCA_MSELECT_ALOCAL:
                                addbyte(0x89); /*MOV EAX, ECX*/
                                addbyte(0xc8);
                                break;
                                case CCA_MSELECT_AOTHER:
                                addbyte(0x89); /*MOV EAX, EBX*/
                                addbyte(0xd8);
                                break;
                                case CCA_MSELECT_ALOCAL2:
                                addbyte(0x89); /*MOV EAX, ECX*/
                                addbyte(0xc8);
                                break;
                                case CCA_MSELECT_TEX:
                                addbyte(0x0f); /*MOVZX EAX, state->tex_a*/
                                addbyte(0xb6);
                                addbyte(0x87);
                                addlong(offsetof(voodoo_state_t, tex_a));
                                break;

                                case CCA_MSELECT_ZERO:
                                default:
                                addbyte(0x31); /*XOR EAX, EAX*/
                                addbyte(0xc0);
                                break;
                        }
                        if (!cca_reverse_blend)
                        {
                                addbyte(0x35); /*XOR EAX, 0xff*/
                                addlong(0xff);
                        }
                        addbyte(0x83); /*ADD EAX, 1*/
                        addbyte(0xc0);
                        addbyte(1);
                        addbyte(0x0f); /*IMUL EDX, EAX*/
                        addbyte(0xaf);
                        addbyte(0xd0);
                        addbyte(0xc1); /*SHR EDX, 8*/
                        addbyte(0xea);
                        addbyte(8);
                }
        }

        if ((params->alphaMode & ((1 << 0) | (1 << 4))))
        {
                addbyte(0x31); /*XOR EAX, EAX*/
                addbyte(0xc0);
        }
        
        if (!(cc_mselect == 0 && cc_reverse_blend == 0) && cc_mselect == CC_MSELECT_AOTHER)
        {
                /*Copy a_other to XMM3 before it gets modified*/
                addbyte(0x66); /*MOVD XMM3, EDX*/
                addbyte(0x0f);
                addbyte(0x6e);
                addbyte(0xda);
                addbyte(0xf2); /*PSHUFLW XMM3, XMM3, 0*/
                addbyte(0x0f);
                addbyte(0x70);
                addbyte(0xdb);
                addbyte(0x00);
        }
        
        if (cca_add && (params->alphaMode & ((1 << 0) | (1 << 4))))
        {
                addbyte(0x01); /*ADD EDX, ECX*/
                addbyte(0xca);
        }
        
        if ((params->alphaMode & ((1 << 0) | (1 << 4))))
        {
                addbyte(0x85); /*TEST EDX, EDX*/
                addbyte(0xd2);
                addbyte(0x0f); /*CMOVS EDX, EAX*/
                addbyte(0x48);
                addbyte(0xd0);
                addbyte(0xb8); /*MOV EAX, 0xff*/
                addlong(0xff);
                addbyte(0x81); /*CMP EDX, 0xff*/
                addbyte(0xfa);
                addlong(0xff);
                addbyte(0x0f); /*CMOVA EDX, EAX*/
                addbyte(0x47);
                addbyte(0xd0);

                if (cca_invert_output)
                {
                        addbyte(0x81); /*XOR EDX, 0xff*/
                        addbyte(0xf2);
                        addlong(0xff);
                }
        }

        if (!(cc_mselect == 0 && cc_reverse_blend == 0))
        {
                switch (cc_mselect)
                {
                        case CC_MSELECT_ZERO:
                        addbyte(0x66); /*PXOR XMM3, XMM3*/
                        addbyte(0x0f);
                        addbyte(0xef);
                        addbyte(0xdb);
                        break;
                        case CC_MSELECT_CLOCAL:
                        addbyte(0xf3); /*MOV XMM3, XMM1*/
                        addbyte(0x0f);
                        addbyte(0x7e);
                        addbyte(0xd9);
                        break;
                        case CC_MSELECT_ALOCAL:
                        addbyte(0x66); /*MOVD XMM3, ECX*/
                        addbyte(0x0f);
                        addbyte(0x6e);
                        addbyte(0xd9);
                        addbyte(0xf2); /*PSHUFLW XMM3, XMM3, 0*/
                        addbyte(0x0f);
                        addbyte(0x70);
                        addbyte(0xdb);
                        addbyte(0x00);
                        break;
                        case CC_MSELECT_AOTHER:
                        /*Handled above*/
                        break;
                        case CC_MSELECT_TEX:
                        addbyte(0x66); /*PINSRW XMM3, state->tex_a, 0*/
                        addbyte(0x0f);
                        addbyte(0xc4);
                        addbyte(0x9f);
                        addlong(offsetof(voodoo_state_t, tex_a));
                        addbyte(0);
                        addbyte(0x66); /*PINSRW XMM3, state->tex_a, 1*/
                        addbyte(0x0f);
                        addbyte(0xc4);
                        addbyte(0x9f);
                        addlong(offsetof(voodoo_state_t, tex_a));
                        addbyte(1);
                        addbyte(0x66); /*PINSRW XMM3, state->tex_a, 2*/
                        addbyte(0x0f);
                        addbyte(0xc4);
                        addbyte(0x9f);
                        addlong(offsetof(voodoo_state_t, tex_a));
                        addbyte(2);
                        break;
                        default:
                        addbyte(0x66); /*PXOR XMM3, XMM3*/
                        addbyte(0x0f);
                        addbyte(0xef);
                        addbyte(0xdb);
                        break;
                }
                addbyte(0xf3); /*MOV XMM4, XMM0*/
                addbyte(0x0f);
                addbyte(0x7e);
                addbyte(0xe0);
                if (!cc_reverse_blend)
                {
                        addbyte(0x66); /*PXOR XMM3, 0xff*/
                        addbyte(0x0f);
                        addbyte(0xef);
                        addbyte(0x1d);
                        addlong(&xmm_ff_w);
                }
                addbyte(0x66); /*PADDW XMM3, 1*/
                addbyte(0x0f);
                addbyte(0xfd);
                addbyte(0x1d);
                addlong(&xmm_01_w);
                addbyte(0x66); /*PMULLW XMM0, XMM3*/
                addbyte(0x0f);
                addbyte(0xd5);
                addbyte(0xc3);
                addbyte(0x66); /*PMULHW XMM4, XMM3*/
                addbyte(0x0f);
                addbyte(0xe5);
                addbyte(0xe3);
                addbyte(0x66); /*PUNPCKLWD XMM0, XMM4*/
                addbyte(0x0f);
                addbyte(0x61);
                addbyte(0xc4);
                addbyte(0x66); /*PSRLD XMM0, 8*/
                addbyte(0x0f);
                addbyte(0x72);
                addbyte(0xe0);
                addbyte(8);
                addbyte(0x66); /*PACKSSDW XMM0, XMM0*/
                addbyte(0x0f);
                addbyte(0x6b);
                addbyte(0xc0);
        }
        
        if (cc_add == 1)
        {
                addbyte(0x66); /*PADDW XMM0, XMM1*/
                addbyte(0x0f);
                addbyte(0xfd);
                addbyte(0xc1);
        }

        addbyte(0x66); /*PACKUSWB XMM0, XMM0*/
        addbyte(0x0f);
        addbyte(0x67);
        addbyte(0xc0);

        if (cc_invert_output)
        {
                addbyte(0x66); /*PXOR XMM0, 0xff*/
                addbyte(0x0f);
                addbyte(0xef);
                addbyte(0x05);
                addlong(&xmm_ff_b);
        }
//#if 0
//        addbyte(0x66); /*MOVD state->out[EDI], XMM0*/
//        addbyte(0x0f);
//        addbyte(0x7e);
//        addbyte(0x87);
//        addlong(offsetof(voodoo_state_t, out));
        if (params->fogMode & FOG_ENABLE)
        {
                if (params->fogMode & FOG_CONSTANT)                     
                {                                                       
                        addbyte(0x66); /*MOVD XMM3, params->fogColor[ESI]*/
                        addbyte(0x0f);
                        addbyte(0x6e);
                        addbyte(0x9e);
                        addlong(offsetof(voodoo_params_t, fogColor));
                        addbyte(0x66); /*PADDUSB XMM0, XMM3*/
                        addbyte(0x0f);
                        addbyte(0xdc);
                        addbyte(0xc3);
/*                        src_r += params->fogColor.r;                    
                        src_g += params->fogColor.g;                    
                        src_b += params->fogColor.b;                    */
                }                                                       
                else                                                    
                {                                                       
                        /*int fog_r, fog_g, fog_b, fog_a;                 */
                                                                        
                        addbyte(0x66); /*PUNPCKLBW XMM0, XMM2*/
                        addbyte(0x0f);
                        addbyte(0x60);
                        addbyte(0xc2);

                        if (!(params->fogMode & FOG_ADD))               
                        {
                                addbyte(0x66); /*MOVD XMM3, params->fogColor[ESI]*/
                                addbyte(0x0f);
                                addbyte(0x6e);
                                addbyte(0x9e);
                                addlong(offsetof(voodoo_params_t, fogColor));
                                addbyte(0x66); /*PUNPCKLBW XMM3, XMM2*/
                                addbyte(0x0f);
                                addbyte(0x60);
                                addbyte(0xda);
                        }                                               
                        else
                        {
                                addbyte(0x66); /*PXOR XMM3, XMM3*/
                                addbyte(0x0f);
                                addbyte(0xef);
                                addbyte(0xdb);
                        }
                                                                        
                        if (!(params->fogMode & FOG_MULT))
                        {
                                addbyte(0x66); /*PSUBW XMM3, XMM0*/
                                addbyte(0x0f);
                                addbyte(0xf9);
                                addbyte(0xd8);
                        }

                        /*Divide by 2 to prevent overflow on multiply*/
                        addbyte(0x66); /*PSRAW XMM3, 1*/
                        addbyte(0x0f);
                        addbyte(0x71);
                        addbyte(0xe3);
                        addbyte(1);

                        if (params->fogMode & FOG_Z)
                        {
                                addbyte(0x8b); /*MOV EAX, state->z[EDI]*/
                                addbyte(0x87);
                                addlong(offsetof(voodoo_state_t, z));
                                addbyte(0xc1); /*SHR EAX, 12*/
                                addbyte(0xe8);
                                addbyte(12);
                                addbyte(0x25); /*AND EAX, 0xff*/
                                addlong(0xff);
//                                fog_a = (z >> 20) & 0xff;
                        }
                        else if (params->fogMode & FOG_ALPHA)
                        {
                                addbyte(0x8b); /*MOV EAX, state->ia[EDI]*/
                                addbyte(0x87);
                                addlong(offsetof(voodoo_state_t, ia));
                                addbyte(0x31); /*XOR EBX, EBX*/
                                addbyte(0xdb);
                                addbyte(0xc1); /*SAR EAX, 12*/
                                addbyte(0xf8);
                                addbyte(12);
                                addbyte(0x0f); /*CMOVS EAX, EBX*/
                                addbyte(0x48);
                                addbyte(0xc3);
                                addbyte(0xbb); /*MOV EBX, 0xff*/
                                addlong(0xff);
                                addbyte(0x3d); /*CMP EAX, 0xff*/
                                addlong(0xff);
                                addbyte(0x0f); /*CMOVAE EAX, EBX*/
                                addbyte(0x43);
                                addbyte(0xc3);
//                                fog_a = CLAMP(ia >> 12);
                        }
                        else
                        {
                                addbyte(0x8b); /*MOV EBX, state->w_depth[EDI]*/
                                addbyte(0x9f);
                                addlong(offsetof(voodoo_state_t, w_depth));
                                addbyte(0x89); /*MOV EAX, EBX*/
                                addbyte(0xd8);
                                addbyte(0xc1); /*SHR EBX, 10*/
                                addbyte(0xeb);
                                addbyte(10);
                                addbyte(0xc1); /*SHR EAX, 2*/
                                addbyte(0xe8);
                                addbyte(2);
                                addbyte(0x83); /*AND EBX, 0x3f*/
                                addbyte(0xe3);
                                addbyte(0x3f);
                                addbyte(0x25); /*AND EAX, 0xff*/
                                addlong(0xff);
                                addbyte(0xf6); /*MUL params->fogTable+1[ESI+EBX*2]*/
                                addbyte(0xa4);
                                addbyte(0x5e);
                                addlong(offsetof(voodoo_params_t, fogTable)+1);
                                addbyte(0x0f); /*MOVZX EBX, params->fogTable[ESI+EBX*2]*/
                                addbyte(0xb6);
                                addbyte(0x9c);
                                addbyte(0x5e);
                                addlong(offsetof(voodoo_params_t, fogTable));
                                addbyte(0xc1); /*SHR EAX, 10*/
                                addbyte(0xe8);
                                addbyte(10);
                                addbyte(0x01); /*ADD EAX, EBX*/
                                addbyte(0xd8);

/*                                int fog_idx = (w_depth >> 10) & 0x3f;

                                fog_a = params->fogTable[fog_idx].fog;
                                fog_a += (params->fogTable[fog_idx].dfog * ((w_depth >> 2) & 0xff)) >> 10;*/
                        }
                        addbyte(0x01); /*ADD EAX, EAX*/
                        addbyte(0xc0);
//                        fog_a++;

                        addbyte(0x66); /*PMULLW XMM3, alookup+4[EAX*8]*/
                        addbyte(0x0f);
                        addbyte(0xd5);
                        addbyte(0x1c);
                        addbyte(0xc5);
                        addlong(((uintptr_t)alookup) + 16);
                        addbyte(0x66); /*PSRAW XMM3, 7*/
                        addbyte(0x0f);
                        addbyte(0x71);
                        addbyte(0xe3);
                        addbyte(7);
/*                        fog_r = (fog_r * fog_a) >> 8;
                        fog_g = (fog_g * fog_a) >> 8;
                        fog_b = (fog_b * fog_a) >> 8;*/

                        if (params->fogMode & FOG_MULT)
                        {
                                addbyte(0xf3); /*MOV XMM0, XMM3*/
                                addbyte(0x0f);
                                addbyte(0x7e);
                                addbyte(0xc3);
                        }
                        else
                        {
                                addbyte(0x66); /*PADDW XMM0, XMM3*/
                                addbyte(0x0f);
                                addbyte(0xfd);
                                addbyte(0xc3);
/*                                src_r += fog_r;
                                src_g += fog_g;
                                src_b += fog_b;*/
                        }
                        addbyte(0x66); /*PACKUSWB XMM0, XMM0*/
                        addbyte(0x0f);
                        addbyte(0x67);
                        addbyte(0xc0);
                }

/*                src_r = CLAMP(src_r);
                src_g = CLAMP(src_g);
                src_b = CLAMP(src_b);*/
        }

        if ((params->alphaMode & 1) && (alpha_func != AFUNC_NEVER) && (alpha_func != AFUNC_ALWAYS))
        {
                addbyte(0x0f); /*MOVZX ECX, params->alphaMode+3*/
                addbyte(0xb6);
                addbyte(0x8e);
                addlong(offsetof(voodoo_params_t, alphaMode) + 3);
                addbyte(0x39); /*CMP EDX, ECX*/
                addbyte(0xca);

                switch (alpha_func)
                {
                        case AFUNC_LESSTHAN:
                        addbyte(0x0f); /*JAE skip*/
                        addbyte(0x83);
                        a_skip_pos = block_pos;
                        addlong(0);
                        break;
                        case AFUNC_EQUAL:
                        addbyte(0x0f); /*JNE skip*/
                        addbyte(0x85);
                        a_skip_pos = block_pos;
                        addlong(0);
                        break;
                        case AFUNC_LESSTHANEQUAL:
                        addbyte(0x0f); /*JA skip*/
                        addbyte(0x87);
                        a_skip_pos = block_pos;
                        addlong(0);
                        break;
                        case AFUNC_GREATERTHAN:
                        addbyte(0x0f); /*JBE skip*/
                        addbyte(0x86);
                        a_skip_pos = block_pos;
                        addlong(0);
                        break;
                        case AFUNC_NOTEQUAL:
                        addbyte(0x0f); /*JE skip*/
                        addbyte(0x84);
                        a_skip_pos = block_pos;
                        addlong(0);
                        break;
                        case AFUNC_GREATERTHANEQUAL:
                        addbyte(0x0f); /*JB skip*/
                        addbyte(0x82);
                        a_skip_pos = block_pos;
                        addlong(0);
                        break;
                }
        }
        else if ((params->alphaMode & 1) && (alpha_func == AFUNC_NEVER))
        {
                addbyte(0xC3); /*RET*/
        }
        
        if (params->alphaMode & (1 << 4))
        {
                addbyte(0x8b); /*MOV EAX, state->x[EDI]*/
                addbyte(0x87);
                addlong(offsetof(voodoo_state_t, x));
                addbyte(0x8b); /*MOV EBP, fb_mem*/
                addbyte(0xaf);
                addlong(offsetof(voodoo_state_t, fb_mem));
                addbyte(0x01); /*ADD EDX, EDX*/
                addbyte(0xd2);
                addbyte(0x0f); /*MOVZX EAX, [EBP+EAX*2]*/
                addbyte(0xb7);
                addbyte(0x44);
                addbyte(0x45);
                addbyte(0);
                addbyte(0x66); /*PUNPCKLBW XMM0, XMM2*/
                addbyte(0x0f);
                addbyte(0x60);
                addbyte(0xc2);
                addbyte(0x66); /*MOVD XMM4, rgb565[EAX*4]*/
                addbyte(0x0f);
                addbyte(0x6e);
                addbyte(0x24);
                addbyte(0x85);
                addlong(rgb565);
                addbyte(0x66); /*PUNPCKLBW XMM4, XMM2*/
                addbyte(0x0f);
                addbyte(0x60);
                addbyte(0xe2);
                addbyte(0xf3); /*MOV XMM6, XMM4*/
                addbyte(0x0f);
                addbyte(0x7e);
                addbyte(0xf4);
                
                switch (dest_afunc)
                {
                        case AFUNC_AZERO:
                        addbyte(0x66); /*PXOR XMM4, XMM4*/
                        addbyte(0x0f);
                        addbyte(0xef);
                        addbyte(0xe4);
                        break;
                        case AFUNC_ASRC_ALPHA:
                        addbyte(0x66); /*PMULLW XMM4, alookup[EDX*8]*/
                        addbyte(0x0f);
                        addbyte(0xd5);
                        addbyte(0x24);
                        addbyte(0xd5);
                        addlong(alookup);
                        addbyte(0xf3); /*MOVQ XMM5, XMM4*/
                        addbyte(0x0f);
                        addbyte(0x7e);
                        addbyte(0xec);
                        addbyte(0x66); /*PADDW XMM4, alookup[1*8]*/
                        addbyte(0x0f);
                        addbyte(0xfd);
                        addbyte(0x25);
                        addlong((uint32_t)alookup + 16);
                        addbyte(0x66); /*PSRLW XMM5, 8*/
                        addbyte(0x0f);
                        addbyte(0x71);
                        addbyte(0xd5);
                        addbyte(8);
                        addbyte(0x66); /*PADDW XMM4, XMM5*/
                        addbyte(0x0f);
                        addbyte(0xfd);
                        addbyte(0xe5);
                        addbyte(0x66); /*PSRLW XMM4, 8*/
                        addbyte(0x0f);
                        addbyte(0x71);
                        addbyte(0xd4);
                        addbyte(8);
                        break;
                        case AFUNC_A_COLOR:
                        addbyte(0x66); /*PMULLW XMM4, XMM0*/
                        addbyte(0x0f);
                        addbyte(0xd5);
                        addbyte(0xe0);
                        addbyte(0xf3); /*MOVQ XMM5, XMM4*/
                        addbyte(0x0f);
                        addbyte(0x7e);
                        addbyte(0xec);
                        addbyte(0x66); /*PADDW XMM4, alookup[1*8]*/
                        addbyte(0x0f);
                        addbyte(0xfd);
                        addbyte(0x25);
                        addlong((uint32_t)alookup + 16);
                        addbyte(0x66); /*PSRLW XMM5, 8*/
                        addbyte(0x0f);
                        addbyte(0x71);
                        addbyte(0xd5);
                        addbyte(8);
                        addbyte(0x66); /*PADDW XMM4, XMM5*/
                        addbyte(0x0f);
                        addbyte(0xfd);
                        addbyte(0xe5);
                        addbyte(0x66); /*PSRLW XMM4, 8*/
                        addbyte(0x0f);
                        addbyte(0x71);
                        addbyte(0xd4);
                        addbyte(8);
                        break;
                        case AFUNC_ADST_ALPHA:
                        break;
                        case AFUNC_AONE:
                        break;
                        case AFUNC_AOMSRC_ALPHA:
                        addbyte(0x66); /*PMULLW XMM4, aminuslookup[EDX*8]*/
                        addbyte(0x0f);
                        addbyte(0xd5);
                        addbyte(0x24);
                        addbyte(0xd5);
                        addlong(aminuslookup);
                        addbyte(0xf3); /*MOVQ XMM5, XMM4*/
                        addbyte(0x0f);
                        addbyte(0x7e);
                        addbyte(0xec);
                        addbyte(0x66); /*PADDW XMM4, alookup[1*8]*/
                        addbyte(0x0f);
                        addbyte(0xfd);
                        addbyte(0x25);
                        addlong((uint32_t)alookup + 16);
                        addbyte(0x66); /*PSRLW XMM5, 8*/
                        addbyte(0x0f);
                        addbyte(0x71);
                        addbyte(0xd5);
                        addbyte(8);
                        addbyte(0x66); /*PADDW XMM4, XMM5*/
                        addbyte(0x0f);
                        addbyte(0xfd);
                        addbyte(0xe5);
                        addbyte(0x66); /*PSRLW XMM4, 8*/
                        addbyte(0x0f);
                        addbyte(0x71);
                        addbyte(0xd4);
                        addbyte(8);
                        break;
                        case AFUNC_AOM_COLOR:
                        addbyte(0xf3); /*MOVQ XMM5, xmm_ff_w*/
                        addbyte(0x0f);
                        addbyte(0x7e);
                        addbyte(0x2d);
                        addlong(&xmm_ff_w);
                        addbyte(0x66); /*PSUBW XMM5, XMM0*/
                        addbyte(0x0f);
                        addbyte(0xf9);
                        addbyte(0xe8);
                        addbyte(0x66); /*PMULLW XMM4, XMM5*/
                        addbyte(0x0f);
                        addbyte(0xd5);
                        addbyte(0xe5);
                        addbyte(0xf3); /*MOVQ XMM5, XMM4*/
                        addbyte(0x0f);
                        addbyte(0x7e);
                        addbyte(0xec);
                        addbyte(0x66); /*PADDW XMM4, alookup[1*8]*/
                        addbyte(0x0f);
                        addbyte(0xfd);
                        addbyte(0x25);
                        addlong((uint32_t)alookup + 16);
                        addbyte(0x66); /*PSRLW XMM5, 8*/
                        addbyte(0x0f);
                        addbyte(0x71);
                        addbyte(0xd5);
                        addbyte(8);
                        addbyte(0x66); /*PADDW XMM4, XMM5*/
                        addbyte(0x0f);
                        addbyte(0xfd);
                        addbyte(0xe5);
                        addbyte(0x66); /*PSRLW XMM4, 8*/
                        addbyte(0x0f);
                        addbyte(0x71);
                        addbyte(0xd4);
                        addbyte(8);
                        break;
                        case AFUNC_AOMDST_ALPHA:
                        addbyte(0x66); /*PXOR XMM4, XMM4*/
                        addbyte(0x0f);
                        addbyte(0xef);
                        addbyte(0xe4);
                        break;
                        case AFUNC_ASATURATE:
                        addbyte(0x66); /*PMULLW XMM4, minus_254*/
                        addbyte(0x0f);
                        addbyte(0xd5);
                        addbyte(0x25);
                        addlong(&minus_254);
                        addbyte(0xf3); /*MOVQ XMM5, XMM4*/
                        addbyte(0x0f);
                        addbyte(0x7e);
                        addbyte(0xec);
                        addbyte(0x66); /*PADDW XMM4, alookup[1*8]*/
                        addbyte(0x0f);
                        addbyte(0xfd);
                        addbyte(0x25);
                        addlong((uint32_t)alookup + 16);
                        addbyte(0x66); /*PSRLW XMM5, 8*/
                        addbyte(0x0f);
                        addbyte(0x71);
                        addbyte(0xd5);
                        addbyte(8);
                        addbyte(0x66); /*PADDW XMM4, XMM5*/
                        addbyte(0x0f);
                        addbyte(0xfd);
                        addbyte(0xe5);
                        addbyte(0x66); /*PSRLW XMM4, 8*/
                        addbyte(0x0f);
                        addbyte(0x71);
                        addbyte(0xd4);
                        addbyte(8);
                }

                switch (src_afunc)
                {
                        case AFUNC_AZERO:
                        addbyte(0x66); /*PXOR XMM0, XMM0*/
                        addbyte(0x0f);
                        addbyte(0xef);
                        addbyte(0xc0);
                        break;
                        case AFUNC_ASRC_ALPHA:
                        addbyte(0x66); /*PMULLW XMM0, alookup[EDX*8]*/
                        addbyte(0x0f);
                        addbyte(0xd5);
                        addbyte(0x04);
                        addbyte(0xd5);
                        addlong(alookup);
                        addbyte(0xf3); /*MOVQ XMM5, XMM0*/
                        addbyte(0x0f);
                        addbyte(0x7e);
                        addbyte(0xe8);
                        addbyte(0x66); /*PADDW XMM0, alookup[1*8]*/
                        addbyte(0x0f);
                        addbyte(0xfd);
                        addbyte(0x05);
                        addlong((uint32_t)alookup + 16);
                        addbyte(0x66); /*PSRLW XMM5, 8*/
                        addbyte(0x0f);
                        addbyte(0x71);
                        addbyte(0xd5);
                        addbyte(8);
                        addbyte(0x66); /*PADDW XMM0, XMM5*/
                        addbyte(0x0f);
                        addbyte(0xfd);
                        addbyte(0xc5);
                        addbyte(0x66); /*PSRLW XMM0, 8*/
                        addbyte(0x0f);
                        addbyte(0x71);
                        addbyte(0xd0);
                        addbyte(8);
                        break;
                        case AFUNC_A_COLOR:
                        addbyte(0x66); /*PMULLW XMM0, XMM6*/
                        addbyte(0x0f);
                        addbyte(0xd5);
                        addbyte(0xc6);
                        addbyte(0xf3); /*MOVQ XMM5, XMM0*/
                        addbyte(0x0f);
                        addbyte(0x7e);
                        addbyte(0xe8);
                        addbyte(0x66); /*PADDW XMM0, alookup[1*8]*/
                        addbyte(0x0f);
                        addbyte(0xfd);
                        addbyte(0x05);
                        addlong((uint32_t)alookup + 16);
                        addbyte(0x66); /*PSRLW XMM5, 8*/
                        addbyte(0x0f);
                        addbyte(0x71);
                        addbyte(0xd5);
                        addbyte(8);
                        addbyte(0x66); /*PADDW XMM0, XMM5*/
                        addbyte(0x0f);
                        addbyte(0xfd);
                        addbyte(0xc5);
                        addbyte(0x66); /*PSRLW XMM0, 8*/
                        addbyte(0x0f);
                        addbyte(0x71);
                        addbyte(0xd0);
                        addbyte(8);
                        break;
                        case AFUNC_ADST_ALPHA:
                        break;
                        case AFUNC_AONE:
                        break;
                        case AFUNC_AOMSRC_ALPHA:
                        addbyte(0x66); /*PMULLW XMM0, aminuslookup[EDX*8]*/
                        addbyte(0x0f);
                        addbyte(0xd5);
                        addbyte(0x04);
                        addbyte(0xd5);
                        addlong(aminuslookup);
                        addbyte(0xf3); /*MOVQ XMM5, XMM0*/
                        addbyte(0x0f);
                        addbyte(0x7e);
                        addbyte(0xe8);
                        addbyte(0x66); /*PADDW XMM0, alookup[1*8]*/
                        addbyte(0x0f);
                        addbyte(0xfd);
                        addbyte(0x05);
                        addlong((uint32_t)alookup + 16);
                        addbyte(0x66); /*PSRLW XMM5, 8*/
                        addbyte(0x0f);
                        addbyte(0x71);
                        addbyte(0xd5);
                        addbyte(8);
                        addbyte(0x66); /*PADDW XMM0, XMM5*/
                        addbyte(0x0f);
                        addbyte(0xfd);
                        addbyte(0xc5);
                        addbyte(0x66); /*PSRLW XMM0, 8*/
                        addbyte(0x0f);
                        addbyte(0x71);
                        addbyte(0xd0);
                        addbyte(8);
                        break;
                        case AFUNC_AOM_COLOR:
                        addbyte(0xf3); /*MOVQ XMM5, xmm_ff_w*/
                        addbyte(0x0f);
                        addbyte(0x7e);
                        addbyte(0x2d);
                        addlong(&xmm_ff_w);
                        addbyte(0x66); /*PSUBW XMM5, XMM6*/
                        addbyte(0x0f);
                        addbyte(0xf9);
                        addbyte(0xee);
                        addbyte(0x66); /*PMULLW XMM0, XMM5*/
                        addbyte(0x0f);
                        addbyte(0xd5);
                        addbyte(0xc5);
                        addbyte(0xf3); /*MOVQ XMM5, XMM0*/
                        addbyte(0x0f);
                        addbyte(0x7e);
                        addbyte(0xe8);
                        addbyte(0x66); /*PADDW XMM0, alookup[1*8]*/
                        addbyte(0x0f);
                        addbyte(0xfd);
                        addbyte(0x05);
                        addlong((uint32_t)alookup + 16);
                        addbyte(0x66); /*PSRLW XMM5, 8*/
                        addbyte(0x0f);
                        addbyte(0x71);
                        addbyte(0xd5);
                        addbyte(8);
                        addbyte(0x66); /*PADDW XMM0, XMM5*/
                        addbyte(0x0f);
                        addbyte(0xfd);
                        addbyte(0xc5);
                        addbyte(0x66); /*PSRLW XMM0, 8*/
                        addbyte(0x0f);
                        addbyte(0x71);
                        addbyte(0xd0);
                        addbyte(8);
                        break;
                        case AFUNC_AOMDST_ALPHA:
                        addbyte(0x66); /*PXOR XMM0, XMM0*/
                        addbyte(0x0f);
                        addbyte(0xef);
                        addbyte(0xc0);
                        break;
                        case AFUNC_ACOLORBEFOREFOG:
                        break;
                }
                
                addbyte(0x66); /*PADDW XMM0, XMM4*/
                addbyte(0x0f);
                addbyte(0xfd);
                addbyte(0xc4);

                addbyte(0x66); /*PACKUSWB XMM0, XMM0*/
                addbyte(0x0f);
                addbyte(0x67);
                addbyte(0xc0);
        }
//#endif        

//        addbyte(0x8b); /*MOV EDX, x (ESP+12)*/
//        addbyte(0x54);
//        addbyte(0x24);
//        addbyte(12);


        addbyte(0x8b); /*MOV EDX, state->x[EDI]*/
        addbyte(0x97);
        addlong(offsetof(voodoo_state_t, x));
               
        addbyte(0x66); /*MOV EAX, XMM0*/
        addbyte(0x0f);
        addbyte(0x7e);
        addbyte(0xc0);
        
        if (params->fbzMode & FBZ_RGB_WMASK)
        {
//                addbyte(0x89); /*MOV state->rgb_out[EDI], EAX*/
//                addbyte(0x87);
//                addlong(offsetof(voodoo_state_t, rgb_out));
                
                if (dither)
                {
                        addbyte(0x8b); /*MOV ESI, real_y (ESP+16)*/
                        addbyte(0x74);
                        addbyte(0x24);
                        addbyte(16+16);
                        addbyte(0x0f); /*MOVZX EBX, AH*/ /*G*/
                        addbyte(0xb6);
                        addbyte(0xdc);
                        if (dither2x2)
                        {
                                addbyte(0x83); /*AND EDX, 1*/
                                addbyte(0xe2);
                                addbyte(1);
                                addbyte(0x83); /*AND ESI, 1*/
                                addbyte(0xe6);
                                addbyte(1);
                                addbyte(0xc1); /*SHL EBX, 2*/
                                addbyte(0xe3);
                                addbyte(2);
                        }
                        else
                        {
                                addbyte(0x83); /*AND EDX, 3*/
                                addbyte(0xe2);
                                addbyte(3);
                                addbyte(0x83); /*AND ESI, 3*/
                                addbyte(0xe6);
                                addbyte(3);
                                addbyte(0xc1); /*SHL EBX, 4*/
                                addbyte(0xe3);
                                addbyte(4);
                        }
                        addbyte(0x0f); /*MOVZX ECX, AL*/ /*R*/
                        addbyte(0xb6);
                        addbyte(0xc8);
                        if (dither2x2)
                        {
                                addbyte(0xc1); /*SHR EAX, 14*/
                                addbyte(0xe8);
                                addbyte(14);
                                addbyte(0x8d); /*LEA ESI, EDX+ESI*2*/
                                addbyte(0x34);
                                addbyte(0x72);
                        }
                        else
                        {
                                addbyte(0xc1); /*SHR EAX, 12*/
                                addbyte(0xe8);
                                addbyte(12);
                                addbyte(0x8d); /*LEA ESI, EDX+ESI*4*/
                                addbyte(0x34);
                                addbyte(0xb2);
                        }
                        addbyte(0x8b); /*MOV EDX, state->x[EDI]*/
                        addbyte(0x97);
                        addlong(offsetof(voodoo_state_t, x));
                        if (dither2x2)
                        {
                                addbyte(0xc1); /*SHL ECX, 2*/
                                addbyte(0xe1);
                                addbyte(2);
                                addbyte(0x25); /*AND EAX, 0x3fc*/ /*B*/
                                addlong(0x3fc);
                        }
                        else
                        {
                                addbyte(0xc1); /*SHL ECX, 4*/
                                addbyte(0xe1);
                                addbyte(4);
                                addbyte(0x25); /*AND EAX, 0xff0*/ /*B*/
                                addlong(0xff0);
                        }
                        addbyte(0x0f); /*MOVZX EBX, dither_g[EBX+ESI]*/
                        addbyte(0xb6);
                        addbyte(0x9c);
                        addbyte(0x33);
                        addlong(dither2x2 ? dither_g2x2 : dither_g);
                        addbyte(0x0f); /*MOVZX ECX, dither_rb[ECX+ESI]*/
                        addbyte(0xb6);
                        addbyte(0x8c);
                        addbyte(0x31);
                        addlong(dither2x2 ? dither_rb2x2 : dither_rb);
                        addbyte(0x0f); /*MOVZX EAX, dither_rb[EAX+ESI]*/
                        addbyte(0xb6);
                        addbyte(0x84);
                        addbyte(0x30);
                        addlong(dither2x2 ? dither_rb2x2 : dither_rb);
                        addbyte(0xc1); /*SHL EBX, 5*/
                        addbyte(0xe3);
                        addbyte(5);
                        addbyte(0xc1); /*SHL EAX, 11*/
                        addbyte(0xe0);
                        addbyte(11);
                        addbyte(0x09); /*OR EAX, EBX*/
                        addbyte(0xd8);
                        addbyte(0x09); /*OR EAX, ECX*/
                        addbyte(0xc8);
                }
                else
                {
                        addbyte(0x89); /*MOV EBX, EAX*/
                        addbyte(0xc3);
                        addbyte(0x0f); /*MOVZX ECX, AH*/
                        addbyte(0xb6);
                        addbyte(0xcc);
                        addbyte(0xc1); /*SHR EAX, 3*/
                        addbyte(0xe8);
                        addbyte(3);
                        addbyte(0xc1); /*SHR EBX, 8*/
                        addbyte(0xeb);
                        addbyte(8);
                        addbyte(0xc1); /*SHL ECX, 3*/
                        addbyte(0xe1);
                        addbyte(3);
                        addbyte(0x81); /*AND EAX, 0x001f*/
                        addbyte(0xe0);
                        addlong(0x001f);
                        addbyte(0x81); /*AND EBX, 0xf800*/
                        addbyte(0xe3);
                        addlong(0xf800);
                        addbyte(0x81); /*AND ECX, 0x07e0*/
                        addbyte(0xe1);
                        addlong(0x07e0);
                        addbyte(0x09); /*OR EAX, EBX*/
                        addbyte(0xd8);
                        addbyte(0x09); /*OR EAX, ECX*/
                        addbyte(0xc8);
                }
                addbyte(0x8b); /*MOV ESI, fb_mem*/
                addbyte(0xb7);
                addlong(offsetof(voodoo_state_t, fb_mem));
                addbyte(0x66); /*MOV [ESI+EDX*2], AX*/
                addbyte(0x89);
                addbyte(0x04);
                addbyte(0x56);
        }

        if (params->fbzMode & FBZ_DEPTH_WMASK)
        {
                addbyte(0x66); /*MOV AX, new_depth*/
                addbyte(0x8b);
                addbyte(0x87);
                addlong(offsetof(voodoo_state_t, new_depth));
                addbyte(0x8b); /*MOV ESI, aux_mem*/
                addbyte(0xb7);
                addlong(offsetof(voodoo_state_t, aux_mem));
                addbyte(0x66); /*MOV [ESI+EDX*2], AX*/
                addbyte(0x89);
                addbyte(0x04);
                addbyte(0x56);
        }

        if (z_skip_pos)
                *(uint32_t *)&code_block[z_skip_pos] = (block_pos - z_skip_pos) - 4;
        if (a_skip_pos)
                *(uint32_t *)&code_block[a_skip_pos] = (block_pos - a_skip_pos) - 4;
        if (chroma_skip_pos)
                *(uint32_t *)&code_block[chroma_skip_pos] = (block_pos - chroma_skip_pos) - 4;


        addbyte(0x8b); /*MOV ESI, [ESP+8]*/
        addbyte(0x74);
        addbyte(0x24);
        addbyte(8+16);

        addbyte(0xf3); /*MOVDQU XMM1, state->ib[EDI]*/
        addbyte(0x0f);
        addbyte(0x6f);
        addbyte(0x8f);
        addlong(offsetof(voodoo_state_t, ib));
        addbyte(0xf3); /*MOVDQU XMM3, state->tmu0_s[EDI]*/
        addbyte(0x0f);
        addbyte(0x6f);
        addbyte(0x9f);
        addlong(offsetof(voodoo_state_t, tmu0_s));
        addbyte(0xf3); /*MOVQ XMM4, state->tmu0_w[EDI]*/
        addbyte(0x0f);
        addbyte(0x7e);
        addbyte(0xa7);
        addlong(offsetof(voodoo_state_t, tmu0_w));
        addbyte(0xf3); /*MOVDQU XMM0, params->dBdX[ESI]*/
        addbyte(0x0f);
        addbyte(0x6f);
        addbyte(0x86);
        addlong(offsetof(voodoo_params_t, dBdX));       
        addbyte(0x8b); /*MOV EAX, params->dZdX[ESI]*/
        addbyte(0x86);
        addlong(offsetof(voodoo_params_t, dZdX));
        addbyte(0xf3); /*MOVDQU XMM5, params->tmu[0].dSdX[ESI]*/
        addbyte(0x0f);
        addbyte(0x6f);
        addbyte(0xae);
        addlong(offsetof(voodoo_params_t, tmu[0].dSdX));
        addbyte(0xf3); /*MOVQ XMM6, params->tmu[0].dWdX[ESI]*/
        addbyte(0x0f);
        addbyte(0x7e);
        addbyte(0xb6);
        addlong(offsetof(voodoo_params_t, tmu[0].dWdX));

        if (state->xdir > 0)
        {
                addbyte(0x66); /*PADDD XMM1, XMM0*/
                addbyte(0x0f);
                addbyte(0xfe);
                addbyte(0xc8);
        }
        else
        {
                addbyte(0x66); /*PSUBD XMM1, XMM0*/
                addbyte(0x0f);
                addbyte(0xfa);
                addbyte(0xc8);
        }

        addbyte(0xf3); /*MOVQ XMM0, state->w*/
        addbyte(0x0f);
        addbyte(0x7e);
        addbyte(0x87);
        addlong(offsetof(voodoo_state_t, w));
        addbyte(0xf3); /*MOVDQU state->ib, XMM1*/
        addbyte(0x0f);
        addbyte(0x7f);
        addbyte(0x8f);
        addlong(offsetof(voodoo_state_t, ib));
        addbyte(0xf3); /*MOVQ XMM7, params->dWdX*/
        addbyte(0x0f);
        addbyte(0x7e);
        addbyte(0xbe);
        addlong(offsetof(voodoo_params_t, dWdX));

        if (state->xdir > 0)
        {
                addbyte(0x66); /*PADDQ XMM3, XMM5*/
                addbyte(0x0f);
                addbyte(0xd4);
                addbyte(0xdd);
                addbyte(0x66); /*PADDQ XMM4, XMM6*/
                addbyte(0x0f);
                addbyte(0xd4);
                addbyte(0xe6);
                addbyte(0x66); /*PADDQ XMM0, XMM7*/
                addbyte(0x0f);
                addbyte(0xd4);
                addbyte(0xc7);
                addbyte(0x01); /*ADD state->z[EDI], EAX*/
                addbyte(0x87);
                addlong(offsetof(voodoo_state_t, z));        
        }
        else
        {
                addbyte(0x66); /*PSUBQ XMM3, XMM5*/
                addbyte(0x0f);
                addbyte(0xfb);
                addbyte(0xdd);
                addbyte(0x66); /*PSUBQ XMM4, XMM6*/
                addbyte(0x0f);
                addbyte(0xfb);
                addbyte(0xe6);
                addbyte(0x66); /*PSUBQ XMM0, XMM7*/
                addbyte(0x0f);
                addbyte(0xfb);
                addbyte(0xc7);
                addbyte(0x29); /*SUB state->z[EDI], EAX*/
                addbyte(0x87);
                addlong(offsetof(voodoo_state_t, z));        
        }

        addbyte(0xf3); /*MOVDQU state->tmu0_s, XMM3*/
        addbyte(0x0f);
        addbyte(0x7f);
        addbyte(0x9f);
        addlong(offsetof(voodoo_state_t, tmu0_s));
        addbyte(0x66); /*MOVQ state->tmu0_w, XMM4*/
        addbyte(0x0f);
        addbyte(0xd6);
        addbyte(0xa7);
        addlong(offsetof(voodoo_state_t, tmu0_w));
        addbyte(0x66); /*MOVQ state->w, XMM0*/
        addbyte(0x0f);
        addbyte(0xd6);
        addbyte(0x87);
        addlong(offsetof(voodoo_state_t, w));
        
        addbyte(0x83); /*ADD state->pixel_count[EDI], 1*/
        addbyte(0x87);
        addlong(offsetof(voodoo_state_t, pixel_count));
        addbyte(1);

        addbyte(0x8b); /*MOV EAX, state->x[EDI]*/
        addbyte(0x87);
        addlong(offsetof(voodoo_state_t, x));
        
        if (state->xdir > 0)
        {
                addbyte(0x83); /*ADD state->x[EDI], 1*/
                addbyte(0x87);
                addlong(offsetof(voodoo_state_t, x));
                addbyte(1);
        }
        else
        {
                addbyte(0x83); /*SUB state->x[EDI], 1*/
                addbyte(0xaf);
                addlong(offsetof(voodoo_state_t, x));
                addbyte(1);
        }

        addbyte(0x3b); /*CMP EAX, state->x2[EDI]*/
        addbyte(0x87);
        addlong(offsetof(voodoo_state_t, x2));
        addbyte(0x0f); /*JNZ loop_jump_pos*/
        addbyte(0x85);
        addlong(loop_jump_pos - (block_pos + 4));
        
        addbyte(0x5b); /*POP EBX*/        
        addbyte(0x5e); /*POP ESI*/
        addbyte(0x5f); /*POP EDI*/
        addbyte(0x5d); /*POP EBP*/
        
        addbyte(0xC3); /*RET*/
}
static int voodoo_recomp = 0;
static inline void *voodoo_get_block(voodoo_t *voodoo, voodoo_params_t *params, voodoo_state_t *state, int odd_even)
{
        int c;
        int b = last_block[odd_even];
        voodoo_x86_data_t *data;
        voodoo_x86_data_t *codegen_data = voodoo->codegen_data;
        
        for (c = 0; c < 8; c++)
        {
                data = &codegen_data[odd_even + b*2];
                
                if (state->xdir == data->xdir &&
                    params->alphaMode == data->alphaMode &&
                    params->fbzMode == data->fbzMode &&
                    params->fogMode == data->fogMode &&
                    params->fbzColorPath == data->fbzColorPath &&
                    (voodoo->trexInit1 & (1 << 18)) == data->trexInit1 &&
                    params->textureMode == data->textureMode)
                {
                        last_block[odd_even] = b;
                        return data->code_block;
                }
                
                b = (b + 1) & 7;
        }
voodoo_recomp++;
        data = &codegen_data[odd_even + next_block_to_write[odd_even]*2];
//        code_block = data->code_block;
        
        voodoo_generate(data->code_block, voodoo, params, state, depth_op);

        data->xdir = state->xdir;
        data->alphaMode = params->alphaMode;
        data->fbzMode = params->fbzMode;
        data->fogMode = params->fogMode;
        data->fbzColorPath = params->fbzColorPath;
        data->trexInit1 = voodoo->trexInit1 & (1 << 18);
        data->textureMode = params->textureMode;

        next_block_to_write[odd_even] = (next_block_to_write[odd_even] + 1) & 7;
        
        return data->code_block;
}

static void voodoo_codegen_init(voodoo_t *voodoo)
{
        int c;
#ifdef __linux__
	void *start;
	size_t len;
	long pagesize = sysconf(_SC_PAGESIZE);
	long pagemask = ~(pagesize - 1);
#endif

#if defined WIN32 || defined _WIN32 || defined _WIN32
        voodoo->codegen_data = VirtualAlloc(NULL, sizeof(voodoo_x86_data_t) * BLOCK_NUM*2, MEM_COMMIT, PAGE_EXECUTE_READWRITE);
#else
        voodoo->codegen_data = malloc(sizeof(voodoo_x86_data_t) * BLOCK_NUM*2);
#endif

#ifdef __linux__
	start = (void *)((long)voodoo->codegen_data & pagemask);
	len = ((sizeof(voodoo_x86_data_t) * BLOCK_NUM) + pagesize) & pagemask;
	if (mprotect(start, len, PROT_READ | PROT_WRITE | PROT_EXEC) != 0)
	{
		perror("mprotect");
		exit(-1);
	}
#endif

        for (c = 0; c < 256; c++)
        {
                int d[4];
                int _ds = c & 0xf;
                int dt = c >> 4;
                
                alookup[c] = _mm_set_epi32(0, 0, c | (c << 16), c | (c << 16));
                aminuslookup[c] = _mm_set_epi32(0, 0, (255-c) | ((255-c) << 16), (255-c) | ((255-c) << 16));

                d[0] = (16 - _ds) * (16 - dt);
                d[1] =  _ds * (16 - dt);
                d[2] = (16 - _ds) * dt;
                d[3] = _ds * dt;

                bilinear_lookup[c*4]     = _mm_set_epi32(0, 0, d[0] | (d[0] << 16), d[0] | (d[0] << 16));
                bilinear_lookup[c*4 + 1] = _mm_set_epi32(0, 0, d[1] | (d[1] << 16), d[1] | (d[1] << 16));
                bilinear_lookup[c*4 + 2] = _mm_set_epi32(0, 0, d[2] | (d[2] << 16), d[2] | (d[2] << 16));
                bilinear_lookup[c*4 + 3] = _mm_set_epi32(0, 0, d[3] | (d[3] << 16), d[3] | (d[3] << 16));
        }
        alookup[256] = _mm_set_epi32(0, 0, 256 | (256 << 16), 256 | (256 << 16));
}

static void voodoo_codegen_close(voodoo_t *voodoo)
{
#if defined WIN32 || defined _WIN32 || defined _WIN32
        VirtualFree(voodoo->codegen_data, 0, MEM_RELEASE);
#else
        free(voodoo->codegen_data);
#endif
}
