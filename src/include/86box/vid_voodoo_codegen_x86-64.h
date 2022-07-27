/*Registers :

  alphaMode
  fbzMode & 0x1f3fff
  fbzColorPath
*/

#ifndef VIDEO_VOODOO_CODEGEN_X86_64_H
# define VIDEO_VOODOO_CODEGEN_X86_64_H

#ifdef _MSC_VER
#include <intrin.h>
#else
#include <xmmintrin.h>
#endif

#define BLOCK_NUM 8
#define BLOCK_MASK (BLOCK_NUM-1)
#define BLOCK_SIZE 8192

#define LOD_MASK (LOD_TMIRROR_S | LOD_TMIRROR_T)

/* Suppress a false positive warning on gcc that causes excessive build log spam */
#if __GNUC__ >= 10
#pragma GCC diagnostic ignored "-Wstringop-overflow"
#endif

typedef struct voodoo_x86_data_t
{
        uint8_t code_block[BLOCK_SIZE];
        int xdir;
        uint32_t alphaMode;
        uint32_t fbzMode;
        uint32_t fogMode;
        uint32_t fbzColorPath;
        uint32_t textureMode[2];
        uint32_t tLOD[2];
        uint32_t trexInit1;
	int is_tiled;
} voodoo_x86_data_t;

//static voodoo_x86_data_t voodoo_x86_data[2][BLOCK_NUM];

static int last_block[4] = {0, 0};
static int next_block_to_write[4] = {0, 0};

#define addbyte(val)                                            \
        do {                                                    \
                code_block[block_pos++] = val;                  \
                if (block_pos >= BLOCK_SIZE)                    \
                        fatal("Over!\n");                       \
        } while (0)

#define addword(val)                                            \
        do {                                                    \
                *(uint16_t *)&code_block[block_pos] = val;      \
                block_pos += 2;                                 \
                if (block_pos >= BLOCK_SIZE)                    \
                        fatal("Over!\n");                       \
        } while (0)

#define addlong(val)                                            \
        do {                                                    \
                *(uint32_t *)&code_block[block_pos] = val;      \
                block_pos += 4;                                 \
                if (block_pos >= BLOCK_SIZE)                    \
                        fatal("Over!\n");                       \
        } while (0)

#define addquad(val)                                            \
        do {                                                    \
                *(uint64_t *)&code_block[block_pos] = val;      \
                block_pos += 8;                                 \
                if (block_pos >= BLOCK_SIZE)                    \
                        fatal("Over!\n");                       \
        } while (0)


static __m128i xmm_01_w;// = 0x0001000100010001ull;
static __m128i xmm_ff_w;// = 0x00ff00ff00ff00ffull;
static __m128i xmm_ff_b;// = 0x00000000ffffffffull;

static __m128i alookup[257], aminuslookup[256];
static __m128i minus_254;// = 0xff02ff02ff02ff02ull;
static __m128i bilinear_lookup[256*2];
static __m128i xmm_00_ff_w[2];
static uint32_t i_00_ff_w[2] = {0, 0xff};

static inline int codegen_texture_fetch(uint8_t *code_block, voodoo_t *voodoo, voodoo_params_t *params, voodoo_state_t *state, int block_pos, int tmu)
{
        if (params->textureMode[tmu] & 1)
        {
                addbyte(0x48); /*MOV RBX, state->tmu0_s*/
                addbyte(0x8b);
                addbyte(0x9f);
                addlong(tmu ? offsetof(voodoo_state_t, tmu1_s) : offsetof(voodoo_state_t, tmu0_s));
                addbyte(0x48); /*MOV RAX, (1 << 48)*/
                addbyte(0xb8);
                addquad(1ULL << 48);
                addbyte(0x48); /*XOR RDX, RDX*/
                addbyte(0x31);
                addbyte(0xd2);
                addbyte(0x48); /*MOV RCX, state->tmu0_t*/
                addbyte(0x8b);
                addbyte(0x8f);
                addlong(tmu ? offsetof(voodoo_state_t, tmu1_t) : offsetof(voodoo_state_t, tmu0_t));
                addbyte(0x48); /*CMP state->tmu_w, 0*/
                addbyte(0x83);
                addbyte(0xbf);
                addlong(tmu ? offsetof(voodoo_state_t, tmu1_w) : offsetof(voodoo_state_t, tmu0_w));
                addbyte(0);
                addbyte(0x74); /*JZ +*/
                addbyte(7);
                addbyte(0x48); /*IDIV state->tmu_w*/
                addbyte(0xf7);
                addbyte(0xbf);
                addlong(tmu ? offsetof(voodoo_state_t, tmu1_w) : offsetof(voodoo_state_t, tmu0_w));
                addbyte(0x48); /*SAR RBX, 14*/
                addbyte(0xc1);
                addbyte(0xfb);
                addbyte(14);
                addbyte(0x48); /*SAR RCX, 14*/
                addbyte(0xc1);
                addbyte(0xf9);
                addbyte(14);
                addbyte(0x48); /*IMUL RBX, RAX*/
                addbyte(0x0f);
                addbyte(0xaf);
                addbyte(0xd8);
                addbyte(0x48); /*IMUL RCX, RAX*/
                addbyte(0x0f);
                addbyte(0xaf);
                addbyte(0xc8);
                addbyte(0x48); /*SAR RBX, 30*/
                addbyte(0xc1);
                addbyte(0xfb);
                addbyte(30);
                addbyte(0x48); /*SAR RCX, 30*/
                addbyte(0xc1);
                addbyte(0xf9);
                addbyte(30);
                addbyte(0x48); /*BSR EDX, RAX*/
                addbyte(0x0f);
                addbyte(0xbd);
                addbyte(0xd0);
                addbyte(0x48); /*SHL RAX, 8*/
                addbyte(0xc1);
                addbyte(0xe0);
                addbyte(8);
                addbyte(0x89); /*MOV state->tex_t, ECX*/
                addbyte(0x8f);
                addlong(offsetof(voodoo_state_t, tex_t));
                addbyte(0x89); /*MOV ECX, EDX*/
                addbyte(0xd1);
                addbyte(0x83); /*SUB EDX, 19*/
                addbyte(0xea);
                addbyte(19);
                addbyte(0x48); /*SHR RAX, CL*/
                addbyte(0xd3);
                addbyte(0xe8);
                addbyte(0xc1); /*SHL EDX, 8*/
                addbyte(0xe2);
                addbyte(8);
                addbyte(0x25); /*AND EAX, 0xff*/
                addlong(0xff);
                addbyte(0x89); /*MOV state->tex_s, EBX*/
                addbyte(0x9f);
                addlong(offsetof(voodoo_state_t, tex_s));
                addbyte(0x41); /*MOVZX EAX, R9(logtable)[RAX]*/
                addbyte(0x0f);
                addbyte(0xb6);
                addbyte(0x04);
                addbyte(0x01);
                addbyte(0x09); /*OR EAX, EDX*/
                addbyte(0xd0);
                addbyte(0x03); /*ADD EAX, state->lod*/
                addbyte(0x87);
                addlong(offsetof(voodoo_state_t, tmu[tmu].lod));
                addbyte(0x3b); /*CMP EAX, state->lod_min*/
                addbyte(0x87);
                addlong(offsetof(voodoo_state_t, lod_min[tmu]));
                addbyte(0x0f); /*CMOVL EAX, state->lod_min*/
                addbyte(0x4c);
                addbyte(0x87);
                addlong(offsetof(voodoo_state_t, lod_min[tmu]));
                addbyte(0x3b); /*CMP EAX, state->lod_max*/
                addbyte(0x87);
                addlong(offsetof(voodoo_state_t, lod_max[tmu]));
                addbyte(0x0f); /*CMOVNL EAX, state->lod_max*/
                addbyte(0x4d);
                addbyte(0x87);
                addlong(offsetof(voodoo_state_t, lod_max[tmu]));
                addbyte(0xc1); /*SHR EAX, 8*/
                addbyte(0xe8);
                addbyte(8);
                addbyte(0x89); /*MOV state->lod, EAX*/
                addbyte(0x87);
                addlong(offsetof(voodoo_state_t, lod));
        }
        else
        {
                addbyte(0x48); /*MOV RAX, state->tmu0_s*/
                addbyte(0x8b);
                addbyte(0x87);
                addlong(tmu ? offsetof(voodoo_state_t, tmu1_s) : offsetof(voodoo_state_t, tmu0_s));
                addbyte(0x48); /*MOV RCX, state->tmu0_t*/
                addbyte(0x8b);
                addbyte(0x8f);
                addlong(tmu ? offsetof(voodoo_state_t, tmu1_t) : offsetof(voodoo_state_t, tmu0_t));
                addbyte(0x48); /*SHR RAX, 28*/
                addbyte(0xc1);
                addbyte(0xe8);
                addbyte(28);
                addbyte(0x8b); /*MOV EBX, state->lod_min*/
                addbyte(0x9f);
                addlong(offsetof(voodoo_state_t, lod_min[tmu]));
                addbyte(0x48); /*SHR RCX, 28*/
                addbyte(0xc1);
                addbyte(0xe9);
                addbyte(28);
                addbyte(0x48); /*MOV state->tex_s, RAX*/
                addbyte(0x89);
                addbyte(0x87);
                addlong(offsetof(voodoo_state_t, tex_s));
                addbyte(0xc1); /*SHR EBX, 8*/
                addbyte(0xeb);
                addbyte(8);
                addbyte(0x48); /*MOV state->tex_t, RCX*/
                addbyte(0x89);
                addbyte(0x8f);
                addlong(offsetof(voodoo_state_t, tex_t));
                addbyte(0x89); /*MOV state->lod, EBX*/
                addbyte(0x9f);
                addlong(offsetof(voodoo_state_t, lod));
        }

        if (params->fbzColorPath & FBZCP_TEXTURE_ENABLED)
        {
                if (voodoo->bilinear_enabled && (params->textureMode[tmu] & 6))
                {
                        addbyte(0xb2); /*MOV DL, 8*/
                        addbyte(8);
                        addbyte(0x8b); /*MOV ECX, state->lod[RDI]*/
                        addbyte(0x8f);
                        addlong(offsetof(voodoo_state_t, lod));
                        addbyte(0xbd); /*MOV EBP, 1*/
                        addlong(1);
                        addbyte(0x28); /*SUB DL, CL*/
                        addbyte(0xca);
//                        addbyte(0x8a); /*MOV DL, params->tex_shift[RSI+ECX*4]*/
//                        addbyte(0x94);
//                        addbyte(0x8e);
//                        addlong(offsetof(voodoo_params_t, tex_shift));
                        addbyte(0xd3); /*SHL EBP, CL*/
                        addbyte(0xe5);
                        addbyte(0x8b); /*MOV EAX, state->tex_s[RDI]*/
                        addbyte(0x87);
                        addlong(offsetof(voodoo_state_t, tex_s));
                        addbyte(0xc1); /*SHL EBP, 3*/
                        addbyte(0xe5);
                        addbyte(3);
                        addbyte(0x8b); /*MOV EBX, state->tex_t[RDI]*/
                        addbyte(0x9f);
                        addlong(offsetof(voodoo_state_t, tex_t));
                        if (params->tLOD[tmu] & LOD_TMIRROR_S)
                        {
                                addbyte(0xa9); /*TEST EAX, 0x1000*/
                                addlong(0x1000);
                                addbyte(0x74); /*JZ +*/
                                addbyte(2);
                                addbyte(0xf7); /*NOT EAX*/
                                addbyte(0xd0);
                        }
                        if (params->tLOD[tmu] & LOD_TMIRROR_T)
                        {
                                addbyte(0xf7); /*TEST EBX, 0x1000*/
                                addbyte(0xc3);
                                addlong(0x1000);
                                addbyte(0x74); /*JZ +*/
                                addbyte(2);
                                addbyte(0xf7); /*NOT EBX*/
                                addbyte(0xd3);
                        }
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
                        addbyte(0x8b); /*MOV ECX, state->lod[RDI]*/
                        addbyte(0x8f);
                        addlong(offsetof(voodoo_state_t, lod));
                        addbyte(0xc1); /*SHL EBP, 5*/
                        addbyte(0xe5);
                        addbyte(5);
                        /*EAX = S, EBX = T, ECX = LOD, EDX = tex_shift, ESI=params, EDI=state, EBP = bilinear shift*/
                        addbyte(0x48); /*LEA RSI, [RSI+RCX*4]*/
                        addbyte(0x8d);
                        addbyte(0x34);
                        addbyte(0x8e);
                        addbyte(0x89); /*MOV ebp_store, EBP*/
                        addbyte(0xaf);
                        addlong(offsetof(voodoo_state_t, ebp_store));
                        addbyte(0x48); /*MOV RBP, state->tex[RDI+RCX*8]*/
                        addbyte(0x8b);
                        addbyte(0xac);
                        addbyte(0xcf);
                        addlong(offsetof(voodoo_state_t, tex[tmu]));
                        addbyte(0x88); /*MOV CL, DL*/
                        addbyte(0xd1);
                        addbyte(0x89); /*MOV EDX, EBX*/
                        addbyte(0xda);
                        if (!state->clamp_s[tmu])
                        {
                                addbyte(0x23); /*AND EAX, params->tex_w_mask[ESI]*/
                                addbyte(0x86);
                                addlong(offsetof(voodoo_params_t, tex_w_mask[tmu]));
                        }
                        addbyte(0x83); /*ADD EDX, 1*/
                        addbyte(0xc2);
                        addbyte(1);
                        if (state->clamp_t[tmu])
                        {
                                addbyte(0x41); /*CMOVS EDX, R10(alookup[0](zero))*/
                                addbyte(0x0f);
                                addbyte(0x48);
                                addbyte(0x12);
                                addbyte(0x3b); /*CMP EDX, params->tex_h_mask[ESI]*/
                                addbyte(0x96);
                                addlong(offsetof(voodoo_params_t, tex_h_mask[tmu]));
                                addbyte(0x0f); /*CMOVA EDX, params->tex_h_mask[ESI]*/
                                addbyte(0x47);
                                addbyte(0x96);
                                addlong(offsetof(voodoo_params_t, tex_h_mask[tmu]));
                                addbyte(0x85); /*TEST EBX,EBX*/
                                addbyte(0xdb);
                                addbyte(0x41); /*CMOVS EBX, R10(alookup[0](zero))*/
                                addbyte(0x0f);
                                addbyte(0x48);
                                addbyte(0x1a);
                                addbyte(0x3b); /*CMP EBX, params->tex_h_mask[ESI]*/
                                addbyte(0x9e);
                                addlong(offsetof(voodoo_params_t, tex_h_mask[tmu]));
                                addbyte(0x0f); /*CMOVA EBX, params->tex_h_mask[ESI]*/
                                addbyte(0x47);
                                addbyte(0x9e);
                                addlong(offsetof(voodoo_params_t, tex_h_mask[tmu]));
                        }
                        else
                        {
                                addbyte(0x23); /*AND EDX, params->tex_h_mask[ESI]*/
                                addbyte(0x96);
                                addlong(offsetof(voodoo_params_t, tex_h_mask[tmu]));
                                addbyte(0x23); /*AND EBX, params->tex_h_mask[ESI]*/
                                addbyte(0x9e);
                                addlong(offsetof(voodoo_params_t, tex_h_mask[tmu]));
                        }
                        /*EAX = S, EBX = T0, EDX = T1*/
                        addbyte(0xd3); /*SHL EBX, CL*/
                        addbyte(0xe3);
                        addbyte(0xd3); /*SHL EDX, CL*/
                        addbyte(0xe2);
                        addbyte(0x48); /*LEA RBX,[RBP+RBX*4]*/
                        addbyte(0x8d);
                        addbyte(0x5c);
                        addbyte(0x9d);
                        addbyte(0);
                        addbyte(0x48); /*LEA RDX,[RBP+RDX*4]*/
                        addbyte(0x8d);
                        addbyte(0x54);
                        addbyte(0x95);
                        addbyte(0);
                        if (state->clamp_s[tmu])
                        {
                                addbyte(0x8b); /*MOV EBP, params->tex_w_mask[ESI]*/
                                addbyte(0xae);
                                addlong(offsetof(voodoo_params_t, tex_w_mask[tmu]));
                                addbyte(0x85); /*TEST EAX, EAX*/
                                addbyte(0xc0);
                                addbyte(0x8b); /*MOV ebp_store2, RSI*/
                                addbyte(0xb7);
                                addlong(offsetof(voodoo_state_t, ebp_store));
                                addbyte(0x41); /*CMOVS EAX, R10(alookup[0](zero))*/
                                addbyte(0x0f);
                                addbyte(0x48);
                                addbyte(0x02);
                                addbyte(0x78); /*JS + - clamp on 0*/
                                addbyte(2+3+2+ 5+5+2);
                                addbyte(0x3b); /*CMP EAX, EBP*/
                                addbyte(0xc5);
                                addbyte(0x0f); /*CMOVAE EAX, EBP*/
                                addbyte(0x43);
                                addbyte(0xc5);
                                addbyte(0x73); /*JAE + - clamp on +*/
                                addbyte(5+5+2);
                        }
                        else
                        {
                                addbyte(0x3b); /*CMP EAX, params->tex_w_mask[ESI] - is S at texture edge (ie will wrap/clamp)?*/
                                addbyte(0x86);
                                addlong(offsetof(voodoo_params_t, tex_w_mask[tmu]));
                                addbyte(0x8b); /*MOV ebp_store2, ESI*/
                                addbyte(0xb7);
                                addlong(offsetof(voodoo_state_t, ebp_store));
                                addbyte(0x74); /*JE +*/
                                addbyte(5+5+2);
                        }

                        addbyte(0xf3); /*MOVQ XMM0, [RBX+RAX*4]*/
                        addbyte(0x0f);
                        addbyte(0x7e);
                        addbyte(0x04);
                        addbyte(0x83);
                        addbyte(0xf3); /*MOVQ XMM1, [RDX+RAX*4]*/
                        addbyte(0x0f);
                        addbyte(0x7e);
                        addbyte(0x0c);
                        addbyte(0x82);

                        if (state->clamp_s[tmu])
                        {
                                addbyte(0xeb); /*JMP +*/
                                addbyte(5+5+4+4);

                                /*S clamped - the two S coordinates are the same*/
                                addbyte(0x66); /*MOVD XMM0, [RBX+RAX*4]*/
                                addbyte(0x0f);
                                addbyte(0x6e);
                                addbyte(0x04);
                                addbyte(0x83);
                                addbyte(0x66); /*MOVD XMM1, [RDX+RAX*4]*/
                                addbyte(0x0f);
                                addbyte(0x6e);
                                addbyte(0x0c);
                                addbyte(0x82);
                                addbyte(0x66); /*PUNPCKLDQ XMM0, XMM0*/
                                addbyte(0x0f);
                                addbyte(0x62);
                                addbyte(0xc0);
                                addbyte(0x66); /*PUNPCKLDQ XMM1, XMM1*/
                                addbyte(0x0f);
                                addbyte(0x62);
                                addbyte(0xc9);
                        }
                        else
                        {
                                addbyte(0xeb); /*JMP +*/
                                addbyte(5+5+5+5+6+6);

                                /*S wrapped - the two S coordinates are not contiguous*/
                                addbyte(0x66); /*MOVD XMM0, [RBX+EAX*4]*/
                                addbyte(0x0f);
                                addbyte(0x6e);
                                addbyte(0x04);
                                addbyte(0x83);
                                addbyte(0x66); /*MOVD XMM1, [RDX+EAX*4]*/
                                addbyte(0x0f);
                                addbyte(0x6e);
                                addbyte(0x0c);
                                addbyte(0x82);
                                addbyte(0x66); /*PINSRW XMM0, [RBX], 2*/
                                addbyte(0x0f);
                                addbyte(0xc4);
                                addbyte(0x03);
                                addbyte(0x02);
                                addbyte(0x66); /*PINSRW XMM1, [RDX], 2*/
                                addbyte(0x0f);
                                addbyte(0xc4);
                                addbyte(0x0a);
                                addbyte(0x02);
                                addbyte(0x66); /*PINSRW XMM0, 2[RBX], 3*/
                                addbyte(0x0f);
                                addbyte(0xc4);
                                addbyte(0x43);
                                addbyte(0x02);
                                addbyte(0x03);
                                addbyte(0x66); /*PINSRW XMM1, 2[RDX], 3*/
                                addbyte(0x0f);
                                addbyte(0xc4);
                                addbyte(0x4a);
                                addbyte(0x02);
                                addbyte(0x03);
                        }

                        addbyte(0x49); /*MOV R8, bilinear_lookup*/
                        addbyte(0xb8);
                        addquad((uintptr_t)bilinear_lookup);

                        addbyte(0x66); /*PUNPCKLBW XMM0, XMM2*/
                        addbyte(0x0f);
                        addbyte(0x60);
                        addbyte(0xc2);
                        addbyte(0x66); /*PUNPCKLBW XMM1, XMM2*/
                        addbyte(0x0f);
                        addbyte(0x60);
                        addbyte(0xca);

                        addbyte(0x4c); /*ADD RSI, R8*/
                        addbyte(0x01);
                        addbyte(0xc6);

                        addbyte(0x66); /*PMULLW XMM0, bilinear_lookup[ESI]*/
                        addbyte(0x0f);
                        addbyte(0xd5);
                        addbyte(0x06);
                        addbyte(0x66); /*PMULLW XMM1, bilinear_lookup[ESI]+0x10*/
                        addbyte(0x0f);
                        addbyte(0xd5);
                        addbyte(0x4e);
                        addbyte(0x10);
                        addbyte(0x66); /*PADDW XMM0, XMM1*/
                        addbyte(0x0f);
                        addbyte(0xfd);
                        addbyte(0xc0 | 1 | (0 << 3));
                        addbyte(0x66); /*MOV XMM1, XMM0*/
                        addbyte(0x0f);
                        addbyte(0x6f);
                        addbyte(0xc0 | 0 | (1 << 3));
                        addbyte(0x66); /*PSRLDQ XMM0, 64*/
                        addbyte(0x0f);
                        addbyte(0x73);
                        addbyte(0xd8);
                        addbyte(8);
                        addbyte(0x66); /*PADDW XMM0, XMM1*/
                        addbyte(0x0f);
                        addbyte(0xfd);
                        addbyte(0xc0 | 1 | (0 << 3));
                        addbyte(0x66); /*PSRLW XMM0, 8*/
                        addbyte(0x0f);
                        addbyte(0x71);
                        addbyte(0xd0 | 0);
                        addbyte(8);
                        addbyte(0x66); /*PACKUSWB XMM0, XMM0*/
                        addbyte(0x0f);
                        addbyte(0x67);
                        addbyte(0xc0);

                        addbyte(0x4c); /*MOV RSI, R15*/
                        addbyte(0x89);
                        addbyte(0xfe);

                        addbyte(0x66); /*MOV EAX, XMM0*/
                        addbyte(0x0f);
                        addbyte(0x7e);
                        addbyte(0xc0);
                }
                else
                {
                        addbyte(0xb2); /*MOV DL, 8*/
                        addbyte(8);
                        addbyte(0x8b); /*MOV ECX, state->lod[RDI]*/
                        addbyte(0x8f);
                        addlong(offsetof(voodoo_state_t, lod));
                        addbyte(0x48); /*MOV RBP, state->tex[RDI+RCX*8]*/
                        addbyte(0x8b);
                        addbyte(0xac);
                        addbyte(0xcf);
                        addlong(offsetof(voodoo_state_t, tex[tmu]));
                        addbyte(0x28); /*SUB DL, CL*/
                        addbyte(0xca);
                        addbyte(0x80); /*ADD CL, 4*/
                        addbyte(0xc1);
                        addbyte(4);
                        addbyte(0x8b); /*MOV EAX, state->tex_s[EDI]*/
                        addbyte(0x87);
                        addlong(offsetof(voodoo_state_t, tex_s));
                        addbyte(0x8b); /*MOV EBX, state->tex_t[EDI]*/
                        addbyte(0x9f);
                        addlong(offsetof(voodoo_state_t, tex_t));
                        if (params->tLOD[tmu] & LOD_TMIRROR_S)
                        {
                                addbyte(0xa9); /*TEST EAX, 0x1000*/
                                addlong(0x1000);
                                addbyte(0x74); /*JZ +*/
                                addbyte(2);
                                addbyte(0xf7); /*NOT EAX*/
                                addbyte(0xd0);
                        }
                        if (params->tLOD[tmu] & LOD_TMIRROR_T)
                        {
                                addbyte(0xf7); /*TEST EBX, 0x1000*/
                                addbyte(0xc3);
                                addlong(0x1000);
                                addbyte(0x74); /*JZ +*/
                                addbyte(2);
                                addbyte(0xf7); /*NOT EBX*/
                                addbyte(0xd3);
                        }
                        addbyte(0xd3); /*SHR EAX, CL*/
                        addbyte(0xe8);
                        addbyte(0xd3); /*SHR EBX, CL*/
                        addbyte(0xeb);
                        if (state->clamp_s[tmu])
                        {
                                addbyte(0x85); /*TEST EAX, EAX*/
                                addbyte(0xc0);
                                addbyte(0x41); /*CMOVS EAX, R10(alookup[0](zero))*/
                                addbyte(0x0f);
                                addbyte(0x48);
                                addbyte(0x02);
                                addbyte(0x3b); /*CMP EAX, params->tex_w_mask[ESI+ECX*4]*/
                                addbyte(0x84);
                                addbyte(0x8e);
                                addlong(offsetof(voodoo_params_t, tex_w_mask[tmu]) - 0x10);
                                addbyte(0x0f); /*CMOVAE EAX, params->tex_w_mask[ESI+ECX*4]*/
                                addbyte(0x43);
                                addbyte(0x84);
                                addbyte(0x8e);
                                addlong(offsetof(voodoo_params_t, tex_w_mask[tmu]) - 0x10);

                        }
                        else
                        {
                                addbyte(0x23); /*AND EAX, params->tex_w_mask-0x10[ESI+ECX*4]*/
                                addbyte(0x84);
                                addbyte(0x8e);
                                addlong(offsetof(voodoo_params_t, tex_w_mask[tmu]) - 0x10);
                        }
                        if (state->clamp_t[tmu])
                        {
                                addbyte(0x85); /*TEST EBX, EBX*/
                                addbyte(0xdb);
                                addbyte(0x41); /*CMOVS EBX, R10(alookup[0](zero))*/
                                addbyte(0x0f);
                                addbyte(0x48);
                                addbyte(0x1a);
                                addbyte(0x3b); /*CMP EBX, params->tex_h_mask[ESI+ECX*4]*/
                                addbyte(0x9c);
                                addbyte(0x8e);
                                addlong(offsetof(voodoo_params_t, tex_h_mask[tmu]) - 0x10);
                                addbyte(0x0f); /*CMOVAE EBX, params->tex_h_mask[ESI+ECX*4]*/
                                addbyte(0x43);
                                addbyte(0x9c);
                                addbyte(0x8e);
                                addlong(offsetof(voodoo_params_t, tex_h_mask[tmu]) - 0x10);
                        }
                        else
                        {
                                addbyte(0x23); /*AND EBX, params->tex_h_mask-0x10[ESI+ECX*4]*/
                                addbyte(0x9c);
                                addbyte(0x8e);
                                addlong(offsetof(voodoo_params_t, tex_h_mask[tmu]) - 0x10);
                        }
                        addbyte(0x88); /*MOV CL, DL*/
                        addbyte(0xd1);
                        addbyte(0xd3); /*SHL EBX, CL*/
                        addbyte(0xe3);
                        addbyte(0x01); /*ADD EBX, EAX*/
                        addbyte(0xc3);

                        addbyte(0x8b); /*MOV EAX, [RBP+RBX*4]*/
                        addbyte(0x44);
                        addbyte(0x9d);
                        addbyte(0);
                }
        }

        return block_pos;
}

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
        addbyte(0x55); /*PUSH RBP*/
        addbyte(0x57); /*PUSH RDI*/
        addbyte(0x56); /*PUSH RSI*/
        addbyte(0x53); /*PUSH RBX*/
        addbyte(0x41); /*PUSH R12*/
        addbyte(0x54);
        addbyte(0x41); /*PUSH R13*/
        addbyte(0x55);
        addbyte(0x41); /*PUSH R14*/
        addbyte(0x56);
        addbyte(0x41); /*PUSH R15*/
        addbyte(0x57);

        addbyte(0x49); /*MOV R15, xmm_01_w*/
        addbyte(0xbf);
        addquad((uint64_t)(uintptr_t)&xmm_01_w);
        addbyte(0x66); /*MOVDQA XMM8, [R15]*/
        addbyte(0x45);
        addbyte(0x0f);
        addbyte(0x6f);
        addbyte(0x07 | (0 << 3));
        addbyte(0x49); /*MOV R15, xmm_ff_w*/
        addbyte(0xbf);
        addquad((uint64_t)(uintptr_t)&xmm_ff_w);
        addbyte(0x66); /*MOVDQA XMM9, [R15]*/
        addbyte(0x45);
        addbyte(0x0f);
        addbyte(0x6f);
        addbyte(0x07 | (1 << 3));
        addbyte(0x49); /*MOV R15, xmm_ff_b*/
        addbyte(0xbf);
        addquad((uint64_t)(uintptr_t)&xmm_ff_b);
        addbyte(0x66); /*MOVDQA XMM10, [R15]*/
        addbyte(0x45);
        addbyte(0x0f);
        addbyte(0x6f);
        addbyte(0x07 | (2 << 3));
        addbyte(0x49); /*MOV R15, minus_254*/
        addbyte(0xbf);
        addquad((uint64_t)(uintptr_t)&minus_254);
        addbyte(0x66); /*MOVDQA XMM11, [R15]*/
        addbyte(0x45);
        addbyte(0x0f);
        addbyte(0x6f);
        addbyte(0x07 | (3 << 3));

#if _WIN64
        addbyte(0x48); /*MOV RDI, RCX (voodoo_state)*/
        addbyte(0x89);
        addbyte(0xcf);
        addbyte(0x49); /*MOV R15, RDX (voodoo_params)*/
        addbyte(0x89);
        addbyte(0xd7);
        addbyte(0x4d); /*MOV R14, R9 (real_y)*/
        addbyte(0x89);
        addbyte(0xce);
#else
        addbyte(0x49); /*MOV R14, RCX (real_y)*/
        addbyte(0x89);
        addbyte(0xce);
	addbyte(0x49); /*MOV R15, RSI (voodoo_state)*/
	addbyte(0x89);
	addbyte(0xf7);
#endif

        addbyte(0x49); /*MOV R9, logtable*/
        addbyte(0xb8 | (9 & 7));
        addquad((uint64_t)(uintptr_t)&logtable);
        addbyte(0x49); /*MOV R10, alookup*/
        addbyte(0xb8 | (10 & 7));
        addquad((uint64_t)(uintptr_t)&alookup);
        addbyte(0x49); /*MOV R11, aminuslookup*/
        addbyte(0xb8 | (11 & 7));
        addquad((uint64_t)(uintptr_t)&aminuslookup);
        addbyte(0x49); /*MOV R12, xmm_00_ff_w*/
        addbyte(0xb8 | (12 & 7));
        addquad((uint64_t)(uintptr_t)&xmm_00_ff_w);
        addbyte(0x49); /*MOV R13, i_00_ff_w*/
        addbyte(0xb8 | (13 & 7));
        addquad((uint64_t)(uintptr_t)&i_00_ff_w);

        loop_jump_pos = block_pos;
        addbyte(0x4c); /*MOV RSI, R15*/
        addbyte(0x89);
        addbyte(0xfe);
        if (params->col_tiled || params->aux_tiled)
        {
                addbyte(0x8b); /*MOV EAX, state->x[EDI]*/
                addbyte(0x87);
                addlong(offsetof(voodoo_state_t, x));
                addbyte(0x89); /*MOV EBX, EAX*/
                addbyte(0xc3);
                addbyte(0x83); /*AND EAX, 63*/
                addbyte(0xe0);
                addbyte(63);
                addbyte(0xc1); /*SHR EBX, 6*/
                addbyte(0xeb);
                addbyte(6);
                addbyte(0xc1); /*SHL EBX, 11  - tile is 128*32, << 12, div 2 because word index*/
                addbyte(0xe3);
                addbyte(11);
                addbyte(0x01); /*ADD EAX, EBX*/
                addbyte(0xd8);
                addbyte(0x89); /*MOV state->x_tiled[EDI], EAX*/
                addbyte(0x87);
                addlong(offsetof(voodoo_state_t, x_tiled));
        }
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
                addbyte(0x67); /*LEA EAX, 1[EDX, EBX]*/
                addbyte(0x8d);
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
                if (params->aux_tiled)
                        addlong(offsetof(voodoo_state_t, x_tiled));
                else
                        addlong(offsetof(voodoo_state_t, x));
                addbyte(0x48); /*MOV RCX, aux_mem[RDI]*/
                addbyte(0x8b);
                addbyte(0x8f);
                addlong(offsetof(voodoo_state_t, aux_mem));
                addbyte(0x0f); /*MOVZX EBX, [ECX+EBX*2]*/
                addbyte(0xb7);
                addbyte(0x1c);
                addbyte(0x59);
                if (params->fbzMode & FBZ_DEPTH_SOURCE)
                {
                        addbyte(0x0f); /*MOVZX EAX, zaColor[RSI]*/
                        addbyte(0xb7);
                        addbyte(0x86);
                        addlong(offsetof(voodoo_params_t, zaColor));
                }
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
        }

        /*XMM0 = colour*/
        /*XMM2 = 0 (for unpacking*/

        /*EDI = state, ESI = params*/

        if ((params->textureMode[0] & TEXTUREMODE_LOCAL_MASK) == TEXTUREMODE_LOCAL || !voodoo->dual_tmus)
        {
                /*TMU0 only sampling local colour or only one TMU, only sample TMU0*/
                block_pos = codegen_texture_fetch(code_block, voodoo, params, state, block_pos, 0);

                addbyte(0x66); /*MOVD XMM0, EAX*/
                addbyte(0x0f);
                addbyte(0x6e);
                addbyte(0xc0);
                addbyte(0xc1); /*SHR EAX, 24*/
                addbyte(0xe8);
                addbyte(24);
                addbyte(0x89); /*MOV state->tex_a[RDI], EAX*/
                addbyte(0x87);
                addlong(offsetof(voodoo_state_t, tex_a));
        }
        else if ((params->textureMode[0] & TEXTUREMODE_MASK) == TEXTUREMODE_PASSTHROUGH)
        {
                /*TMU0 in pass-through mode, only sample TMU1*/
                block_pos = codegen_texture_fetch(code_block, voodoo, params, state, block_pos, 1);

                addbyte(0x66); /*MOVD XMM0, EAX*/
                addbyte(0x0f);
                addbyte(0x6e);
                addbyte(0xc0);
                addbyte(0xc1); /*SHR EAX, 24*/
                addbyte(0xe8);
                addbyte(24);
                addbyte(0x89); /*MOV state->tex_a[RDI], EAX*/
                addbyte(0x87);
                addlong(offsetof(voodoo_state_t, tex_a));
        }
        else
        {
                block_pos = codegen_texture_fetch(code_block, voodoo, params, state, block_pos, 1);

                addbyte(0x66); /*MOVD XMM3, EAX*/
                addbyte(0x0f);
                addbyte(0x6e);
                addbyte(0xd8);
                if ((params->textureMode[1] & TEXTUREMODE_TRILINEAR) && tc_sub_clocal_1)
                {
                        addbyte(0x8b); /*MOV EAX, state->lod*/
                        addbyte(0x87);
                        addlong(offsetof(voodoo_state_t, lod));
                        if (!tc_reverse_blend_1)
                        {
                                addbyte(0xbb); /*MOV EBX, 1*/
                                addlong(1);
                        }
                        else
                        {
                                addbyte(0x31); /*XOR EBX, EBX*/
                                addbyte(0xdb);
                        }
                        addbyte(0x83); /*AND EAX, 1*/
                        addbyte(0xe0);
                        addbyte(1);
                        if (!tca_reverse_blend_1)
                        {
                                addbyte(0xb9); /*MOV ECX, 1*/
                                addlong(1);
                        }
                        else
                        {
                                addbyte(0x31); /*XOR ECX, ECX*/
                                addbyte(0xc9);
                        }
                        addbyte(0x31); /*XOR EBX, EAX*/
                        addbyte(0xc3);
                        addbyte(0x31); /*XOR ECX, EAX*/
                        addbyte(0xc1);
                        addbyte(0xc1); /*SHL EBX, 4*/
                        addbyte(0xe3);
                        addbyte(4);
                        /*EBX = tc_reverse_blend, ECX=tca_reverse_blend*/
                }
                addbyte(0x66); /*PUNPCKLBW XMM3, XMM2*/
                addbyte(0x0f);
                addbyte(0x60);
                addbyte(0xda);
                if (tc_sub_clocal_1)
                {
                        switch (tc_mselect_1)
                        {
                                case TC_MSELECT_ZERO:
                                addbyte(0x66); /*PXOR XMM0, XMM0*/
                                addbyte(0x0f);
                                addbyte(0xef);
                                addbyte(0xc0);
                                break;
                                case TC_MSELECT_CLOCAL:
                                addbyte(0xf3); /*MOVQ XMM0, XMM3*/
                                addbyte(0x0f);
                                addbyte(0x7e);
                                addbyte(0xc3);
                                break;
                                case TC_MSELECT_AOTHER:
                                addbyte(0x66); /*PXOR XMM0, XMM0*/
                                addbyte(0x0f);
                                addbyte(0xef);
                                addbyte(0xc0);
                                break;
                                case TC_MSELECT_ALOCAL:
                                addbyte(0xf2); /*PSHUFLW XMM0, XMM3, 0xff*/
                                addbyte(0x0f);
                                addbyte(0x70);
                                addbyte(0xc3);
                                addbyte(0xff);
                                break;
                                case TC_MSELECT_DETAIL:
                                addbyte(0xb8); /*MOV EAX, params->detail_bias[1]*/
                                addlong(params->detail_bias[1]);
                                addbyte(0x2b); /*SUB EAX, state->lod*/
                                addbyte(0x87);
                                addlong(offsetof(voodoo_state_t, lod));
                                addbyte(0xba); /*MOV EDX, params->detail_max[1]*/
                                addlong(params->detail_max[1]);
                                addbyte(0xc1); /*SHL EAX, params->detail_scale[1]*/
                                addbyte(0xe0);
                                addbyte(params->detail_scale[1]);
                                addbyte(0x39); /*CMP EAX, EDX*/
                                addbyte(0xd0);
                                addbyte(0x0f); /*CMOVNL EAX, EDX*/
                                addbyte(0x4d);
                                addbyte(0xc2);
                                addbyte(0x66); /*MOVD XMM0, EAX*/
                                addbyte(0x0f);
                                addbyte(0x6e);
                                addbyte(0xc0);
                                addbyte(0xf2); /*PSHUFLW XMM0, XMM0, 0*/
                                addbyte(0x0f);
                                addbyte(0x70);
                                addbyte(0xc0);
                                addbyte(0);
                                break;
                                case TC_MSELECT_LOD_FRAC:
                                addbyte(0x66); /*MOVD XMM0, state->lod_frac[1]*/
                                addbyte(0x0f);
                                addbyte(0x6e);
                                addbyte(0x87);
                                addlong(offsetof(voodoo_state_t, lod_frac[1]));
                                addbyte(0xf2); /*PSHUFLW XMM0, XMM0, 0*/
                                addbyte(0x0f);
                                addbyte(0x70);
                                addbyte(0xc0);
                                addbyte(0);
                                break;
                        }
                        if (params->textureMode[1] & TEXTUREMODE_TRILINEAR)
                        {
                                addbyte(0x66); /*PXOR XMM0, R12(xmm_00_ff_w)[EBX]*/
                                addbyte(0x41);
                                addbyte(0x0f);
                                addbyte(0xef);
                                addbyte(0x04);
                                addbyte(0x1c);
                        }
                        else if (!tc_reverse_blend_1)
                        {
                                addbyte(0x66); /*PXOR XMM0, XMM9(xmm_ff_w)*/
                                addbyte(0x41);
                                addbyte(0x0f);
                                addbyte(0xef);
                                addbyte(0xc1);
                        }
                        addbyte(0x66); /*PADDW XMM0, XMM8(xmm_01_w)*/
                        addbyte(0x41);
                        addbyte(0x0f);
                        addbyte(0xfd);
                        addbyte(0xc0);
                        addbyte(0xf3); /*MOVQ XMM1, XMM2*/
                        addbyte(0x0f);
                        addbyte(0x7e);
                        addbyte(0xca);
                        addbyte(0xf3); /*MOVQ XMM5, XMM0*/
                        addbyte(0x0f);
                        addbyte(0x7e);
                        addbyte(0xe8);
                        addbyte(0x66); /*PMULLW XMM0, XMM3*/
                        addbyte(0x0f);
                        addbyte(0xd5);
                        addbyte(0xc3);
                        addbyte(0x66); /*PMULHW XMM5, XMM3*/
                        addbyte(0x0f);
                        addbyte(0xe5);
                        addbyte(0xeb);
                        addbyte(0x66); /*PUNPCKLWD XMM0, XMM5*/
                        addbyte(0x0f);
                        addbyte(0x61);
                        addbyte(0xc5);
                        addbyte(0x66); /*PSRAD XMM0, 8*/
                        addbyte(0x0f);
                        addbyte(0x72);
                        addbyte(0xe0);
                        addbyte(8);
                        addbyte(0x66); /*PACKSSDW XMM0, XMM0*/
                        addbyte(0x0f);
                        addbyte(0x6b);
                        addbyte(0xc0);
                        addbyte(0x66); /*PSUBW XMM1, XMM0*/
                        addbyte(0x0f);
                        addbyte(0xf9);
                        addbyte(0xc8);
                        if (tc_add_clocal_1)
                        {
                                addbyte(0x66); /*PADDW XMM1, XMM3*/
                                addbyte(0x0f);
                                addbyte(0xfd);
                                addbyte(0xcb);
                        }
                        else if (tc_add_alocal_1)
                        {
                                addbyte(0xf2); /*PSHUFLW XMM0, XMM3, 0xff*/
                                addbyte(0x0f);
                                addbyte(0x70);
                                addbyte(0xc3);
                                addbyte(0xff);
                                addbyte(0x66); /*PADDW XMM1, XMM0*/
                                addbyte(0x0f);
                                addbyte(0xfd);
                                addbyte(0xc8);
                        }
                        addbyte(0x66); /*PACKUSWB XMM3, XMM1*/
                        addbyte(0x0f);
                        addbyte(0x67);
                        addbyte(0xd9);
                        if (tca_sub_clocal_1)
                        {
                                addbyte(0x66); /*MOVD EBX, XMM3*/
                                addbyte(0x0f);
                                addbyte(0x7e);
                                addbyte(0xdb);
                        }
                        addbyte(0x66); /*PUNPCKLBW XMM3, XMM2*/
                        addbyte(0x0f);
                        addbyte(0x60);
                        addbyte(0xda);
                }

                if (tca_sub_clocal_1)
                {
                        addbyte(0xc1); /*SHR EBX, 24*/
                        addbyte(0xeb);
                        addbyte(24);
                        switch (tca_mselect_1)
                        {
                                case TCA_MSELECT_ZERO:
                                addbyte(0x31); /*XOR EAX, EAX*/
                                addbyte(0xc0);
                                break;
                                case TCA_MSELECT_CLOCAL:
                                addbyte(0x89); /*MOV EAX, EBX*/
                                addbyte(0xd8);
                                break;
                                case TCA_MSELECT_AOTHER:
                                addbyte(0x31); /*XOR EAX, EAX*/
                                addbyte(0xc0);
                                break;
                                case TCA_MSELECT_ALOCAL:
                                addbyte(0x89); /*MOV EAX, EBX*/
                                addbyte(0xd8);
                                break;
                                case TCA_MSELECT_DETAIL:
                                addbyte(0xb8); /*MOV EAX, params->detail_bias[1]*/
                                addlong(params->detail_bias[1]);
                                addbyte(0x2b); /*SUB EAX, state->lod*/
                                addbyte(0x87);
                                addlong(offsetof(voodoo_state_t, lod));
                                addbyte(0xba); /*MOV EDX, params->detail_max[1]*/
                                addlong(params->detail_max[1]);
                                addbyte(0xc1); /*SHL EAX, params->detail_scale[1]*/
                                addbyte(0xe0);
                                addbyte(params->detail_scale[1]);
                                addbyte(0x39); /*CMP EAX, EDX*/
                                addbyte(0xd0);
                                addbyte(0x0f); /*CMOVNL EAX, EDX*/
                                addbyte(0x4d);
                                addbyte(0xc2);
                                break;
                                case TCA_MSELECT_LOD_FRAC:
                                addbyte(0x8b); /*MOV EAX, state->lod_frac[1]*/
                                addbyte(0x87);
                                addlong(offsetof(voodoo_state_t, lod_frac[1]));
                                break;
                        }
                        if (params->textureMode[1] & TEXTUREMODE_TRILINEAR)
                        {
                                addbyte(0x41); /*XOR EAX, R13(i_00_ff_w)[ECX*4]*/
                                addbyte(0x33);
                                addbyte(0x44);
                                addbyte(0x8d);
                                addbyte(0);
                        }
                        else if (!tc_reverse_blend_1)
                        {
                                addbyte(0x35); /*XOR EAX, 0xff*/
                                addlong(0xff);
                        }
                        addbyte(0x8e); /*ADD EAX, 1*/
                        addbyte(0xc0);
                        addbyte(1);
                        addbyte(0x0f); /*IMUL EAX, EBX*/
                        addbyte(0xaf);
                        addbyte(0xc3);
                        addbyte(0xb9); /*MOV ECX, 0xff*/
                        addlong(0xff);
                        addbyte(0xf7); /*NEG EAX*/
                        addbyte(0xd8);
                        addbyte(0xc1); /*SAR EAX, 8*/
                        addbyte(0xf8);
                        addbyte(8);
                        if (tca_add_clocal_1 || tca_add_alocal_1)
                        {
                                addbyte(0x01); /*ADD EAX, EBX*/
                                addbyte(0xd8);
                        }
                        addbyte(0x39); /*CMP ECX, EAX*/
                        addbyte(0xc1);
                        addbyte(0x0f); /*CMOVA ECX, EAX*/
                        addbyte(0x47);
                        addbyte(0xc8);
                        addbyte(0x66); /*PINSRW 3, XMM3, XMM0*/
                        addbyte(0x0f);
                        addbyte(0xc4);
                        addbyte(0xd8);
                        addbyte(3);
                }

                block_pos = codegen_texture_fetch(code_block, voodoo, params, state, block_pos, 0);

                addbyte(0x66); /*MOVD XMM0, EAX*/
                addbyte(0x0f);
                addbyte(0x6e);
                addbyte(0xc0);
                addbyte(0x66); /*MOVD XMM7, EAX*/
                addbyte(0x0f);
                addbyte(0x6e);
                addbyte(0xf8);

                if (params->textureMode[0] & TEXTUREMODE_TRILINEAR)
                {
                        addbyte(0x8b); /*MOV EAX, state->lod*/
                        addbyte(0x87);
                        addlong(offsetof(voodoo_state_t, lod));
                        if (!tc_reverse_blend)
                        {
                                addbyte(0xbb); /*MOV EBX, 1*/
                                addlong(1);
                        }
                        else
                        {
                                addbyte(0x31); /*XOR EBX, EBX*/
                                addbyte(0xdb);
                        }
                        addbyte(0x83); /*AND EAX, 1*/
                        addbyte(0xe0);
                        addbyte(1);
                        if (!tca_reverse_blend)
                        {
                                addbyte(0xb9); /*MOV ECX, 1*/
                                addlong(1);
                        }
                        else
                        {
                                addbyte(0x31); /*XOR ECX, ECX*/
                                addbyte(0xc9);
                        }
                        addbyte(0x31); /*XOR EBX, EAX*/
                        addbyte(0xc3);
                        addbyte(0x31); /*XOR ECX, EAX*/
                        addbyte(0xc1);
                        addbyte(0xc1); /*SHL EBX, 4*/
                        addbyte(0xe3);
                        addbyte(4);
                        /*EBX = tc_reverse_blend, ECX=tca_reverse_blend*/
                }

                /*XMM0 = TMU0 output, XMM3 = TMU1 output*/

                addbyte(0x66); /*PUNPCKLBW XMM0, XMM2*/
                addbyte(0x0f);
                addbyte(0x60);
                addbyte(0xc2);
                if (tc_zero_other)
                {
                        addbyte(0x66); /*PXOR XMM1, XMM1*/
                        addbyte(0x0f);
                        addbyte(0xef);
                        addbyte(0xc9);
                }
                else
                {
                        addbyte(0xf3); /*MOV XMM1, XMM3*/
                        addbyte(0x0f);
                        addbyte(0x7e);
                        addbyte(0xcb);
                }
                if (tc_sub_clocal)
                {
                        addbyte(0x66); /*PSUBW XMM1, XMM0*/
                        addbyte(0x0f);
                        addbyte(0xf9);
                        addbyte(0xc8);
                }

                switch (tc_mselect)
                {
                        case TC_MSELECT_ZERO:
                        addbyte(0x66); /*PXOR XMM4, XMM4*/
                        addbyte(0x0f);
                        addbyte(0xef);
                        addbyte(0xe4);
                        break;
                        case TC_MSELECT_CLOCAL:
                        addbyte(0xf3); /*MOV XMM4, XMM0*/
                        addbyte(0x0f);
                        addbyte(0x7e);
                        addbyte(0xe0);
                        break;
                        case TC_MSELECT_AOTHER:
                        addbyte(0xf2); /*PSHUFLW XMM4, XMM3, 3, 3, 3, 3*/
                        addbyte(0x0f);
                        addbyte(0x70);
                        addbyte(0xe3);
                        addbyte(0xff);
                        break;
                        case TC_MSELECT_ALOCAL:
                        addbyte(0xf2); /*PSHUFLW XMM4, XMM0, 3, 3, 3, 3*/
                        addbyte(0x0f);
                        addbyte(0x70);
                        addbyte(0xe0);
                        addbyte(0xff);
                        break;
                        case TC_MSELECT_DETAIL:
                        addbyte(0xb8); /*MOV EAX, params->detail_bias[0]*/
                        addlong(params->detail_bias[0]);
                        addbyte(0x2b); /*SUB EAX, state->lod*/
                        addbyte(0x87);
                        addlong(offsetof(voodoo_state_t, lod));
                        addbyte(0xba); /*MOV EDX, params->detail_max[0]*/
                        addlong(params->detail_max[0]);
                        addbyte(0xc1); /*SHL EAX, params->detail_scale[0]*/
                        addbyte(0xe0);
                        addbyte(params->detail_scale[0]);
                        addbyte(0x39); /*CMP EAX, EDX*/
                        addbyte(0xd0);
                        addbyte(0x0f); /*CMOVNL EAX, EDX*/
                        addbyte(0x4d);
                        addbyte(0xc2);
                        addbyte(0x66); /*MOVD XMM4, EAX*/
                        addbyte(0x0f);
                        addbyte(0x6e);
                        addbyte(0xe0);
                        addbyte(0xf2); /*PSHUFLW XMM4, XMM4, 0*/
                        addbyte(0x0f);
                        addbyte(0x70);
                        addbyte(0xe4);
                        addbyte(0);
                        break;
                        case TC_MSELECT_LOD_FRAC:
                        addbyte(0x66); /*MOVD XMM0, state->lod_frac[0]*/
                        addbyte(0x0f);
                        addbyte(0x6e);
                        addbyte(0xa7);
                        addlong(offsetof(voodoo_state_t, lod_frac[0]));
                        addbyte(0xf2); /*PSHUFLW XMM0, XMM0, 0*/
                        addbyte(0x0f);
                        addbyte(0x70);
                        addbyte(0xe4);
                        addbyte(0);
                        break;
                }
                if (params->textureMode[0] & TEXTUREMODE_TRILINEAR)
                {
                        addbyte(0x66); /*PXOR XMM4, R12(xmm_00_ff_w)[EBX]*/
                        addbyte(0x41);
                        addbyte(0x0f);
                        addbyte(0xef);
                        addbyte(0x24);
                        addbyte(0x1c);
                }
                else if (!tc_reverse_blend)
                {
                        addbyte(0x66); /*PXOR XMM4, XMM9(xmm_ff_w)*/
                        addbyte(0x41);
                        addbyte(0x0f);
                        addbyte(0xef);
                        addbyte(0xe1);
                }
                addbyte(0x66); /*PADDW XMM4, XMM8(xmm_01_w)*/
                addbyte(0x41);
                addbyte(0x0f);
                addbyte(0xfd);
                addbyte(0xe0);
                addbyte(0xf3); /*MOVQ XMM5, XMM1*/
                addbyte(0x0f);
                addbyte(0x7e);
                addbyte(0xe9);
                addbyte(0x66); /*PMULLW XMM1, XMM4*/
                addbyte(0x0f);
                addbyte(0xd5);
                addbyte(0xcc);

                if (tca_sub_clocal)
                {
                        addbyte(0x66); /*MOV EBX, XMM7*/
                        addbyte(0x0f);
                        addbyte(0x7e);
                        addbyte(0xfb);
                }

                addbyte(0x66); /*PMULHW XMM5, XMM4*/
                addbyte(0x0f);
                addbyte(0xe5);
                addbyte(0xec);
                addbyte(0x66); /*PUNPCKLWD XMM1, XMM5*/
                addbyte(0x0f);
                addbyte(0x61);
                addbyte(0xcd);
                addbyte(0x66); /*PSRAD XMM1, 8*/
                addbyte(0x0f);
                addbyte(0x72);
                addbyte(0xe1);
                addbyte(8);
                addbyte(0x66); /*PACKSSDW XMM1, XMM1*/
                addbyte(0x0f);
                addbyte(0x6b);
                addbyte(0xc9);

                if (tca_sub_clocal)
                {
                        addbyte(0xc1); /*SHR EBX, 24*/
                        addbyte(0xeb);
                        addbyte(24);
                }

                if (tc_add_clocal)
                {
                        addbyte(0x66); /*PADDW XMM1, XMM0*/
                        addbyte(0x0f);
                        addbyte(0xfd);
                        addbyte(0xc8);
                }
                else if (tc_add_alocal)
                {
                        addbyte(0xf2); /*PSHUFLW XMM4, XMM0, 3, 3, 3, 3*/
                        addbyte(0x0f);
                        addbyte(0x70);
                        addbyte(0xe0);
                        addbyte(0xff);
                        addbyte(0x66); /*PADDW XMM1, XMM4*/
                        addbyte(0x0f);
                        addbyte(0xfc);
                        addbyte(0xcc);
                }
                if (tc_invert_output)
                {
                        addbyte(0x66); /*PXOR XMM1, XMM9(xmm_ff_w)*/
                        addbyte(0x41);
                        addbyte(0x0f);
                        addbyte(0xef);
                        addbyte(0xc9);
                }

                addbyte(0x66); /*PACKUSWB XMM0, XMM0*/
                addbyte(0x0f);
                addbyte(0x67);
                addbyte(0xc0);
                addbyte(0x66); /*PACKUSWB XMM3, XMM3*/
                addbyte(0x0f);
                addbyte(0x67);
                addbyte(0xdb);
                addbyte(0x66); /*PACKUSWB XMM1, XMM1*/
                addbyte(0x0f);
                addbyte(0x67);
                addbyte(0xc9);

                if (tca_zero_other)
                {
                        addbyte(0x31); /*XOR EAX, EAX*/
                        addbyte(0xc0);
                }
                else
                {
                        addbyte(0x66); /*MOV EAX, XMM3*/
                        addbyte(0x0f);
                        addbyte(0x7e);
                        addbyte(0xd8);
                        addbyte(0xc1); /*SHR EAX, 24*/
                        addbyte(0xe8);
                        addbyte(24);
                }
                if (tca_sub_clocal)
                {
                        addbyte(0x29); /*SUB EAX, EBX*/
                        addbyte(0xd8);
                }
                switch (tca_mselect)
                {
                        case TCA_MSELECT_ZERO:
                        addbyte(0x31); /*XOR EBX, EBX*/
                        addbyte(0xdb);
                        break;
                        case TCA_MSELECT_CLOCAL:
                        addbyte(0x66); /*MOV EBX, XMM7*/
                        addbyte(0x0f);
                        addbyte(0x7e);
                        addbyte(0xfb);
                        addbyte(0xc1); /*SHR EBX, 24*/
                        addbyte(0xeb);
                        addbyte(24);
                        break;
                        case TCA_MSELECT_AOTHER:
                        addbyte(0x66); /*MOV EBX, XMM3*/
                        addbyte(0x0f);
                        addbyte(0x7e);
                        addbyte(0xdb);
                        addbyte(0xc1); /*SHR EBX, 24*/
                        addbyte(0xeb);
                        addbyte(24);
                        break;
                        case TCA_MSELECT_ALOCAL:
                        addbyte(0x66); /*MOV EBX, XMM7*/
                        addbyte(0x0f);
                        addbyte(0x7e);
                        addbyte(0xfb);
                        addbyte(0xc1); /*SHR EBX, 24*/
                        addbyte(0xeb);
                        addbyte(24);
                        break;
                        case TCA_MSELECT_DETAIL:
                        addbyte(0xbb); /*MOV EBX, params->detail_bias[1]*/
                        addlong(params->detail_bias[1]);
                        addbyte(0x2b); /*SUB EBX, state->lod*/
                        addbyte(0x9f);
                        addlong(offsetof(voodoo_state_t, lod));
                        addbyte(0xba); /*MOV EDX, params->detail_max[1]*/
                        addlong(params->detail_max[1]);
                        addbyte(0xc1); /*SHL EBX, params->detail_scale[1]*/
                        addbyte(0xe3);
                        addbyte(params->detail_scale[1]);
                        addbyte(0x39); /*CMP EBX, EDX*/
                        addbyte(0xd3);
                        addbyte(0x0f); /*CMOVNL EBX, EDX*/
                        addbyte(0x4d);
                        addbyte(0xda);
                        break;
                        case TCA_MSELECT_LOD_FRAC:
                        addbyte(0x8b); /*MOV EBX, state->lod_frac[0]*/
                        addbyte(0x9f);
                        addlong(offsetof(voodoo_state_t, lod_frac[0]));
                        break;
                }
                if (params->textureMode[0] & TEXTUREMODE_TRILINEAR)
                {
                        addbyte(0x41); /*XOR EBX, R13(i_00_ff_w)[ECX*4]*/
                        addbyte(0x33);
                        addbyte(0x5c);
                        addbyte(0x8d);
                        addbyte(0);
                }
                else if (!tca_reverse_blend)
                {
                        addbyte(0x81); /*XOR EBX, 0xFF*/
                        addbyte(0xf3);
                        addlong(0xff);
                }

                addbyte(0x83); /*ADD EBX, 1*/
                addbyte(0xc3);
                addbyte(1);
                addbyte(0x0f); /*IMUL EAX, EBX*/
                addbyte(0xaf);
                addbyte(0xc3);
                addbyte(0x31); /*XOR EDX, EDX*/
                addbyte(0xd2);
                addbyte(0xc1); /*SAR EAX, 8*/
                addbyte(0xf8);
                addbyte(8);
                if (tca_add_clocal || tca_add_alocal)
                {
                        addbyte(0x66); /*MOV EBX, XMM7*/
                        addbyte(0x0f);
                        addbyte(0x7e);
                        addbyte(0xfb);
                        addbyte(0xc1); /*SHR EBX, 24*/
                        addbyte(0xeb);
                        addbyte(24);
                        addbyte(0x01); /*ADD EAX, EBX*/
                        addbyte(0xd8);
                }
                addbyte(0x0f); /*CMOVS EAX, EDX*/
                addbyte(0x48);
                addbyte(0xc2);
                addbyte(0xba); /*MOV EDX, 0xff*/
                addlong(0xff);
                addbyte(0x3d); /*CMP EAX, 0xff*/
                addlong(0xff);
                addbyte(0x0f); /*CMOVA EAX, EDX*/
                addbyte(0x47);
                addbyte(0xc2);
                if (tca_invert_output)
                {
                        addbyte(0x35); /*XOR EAX, 0xff*/
                        addlong(0xff);
                }

                addbyte(0x89); /*MOV state->tex_a[EDI], EAX*/
                addbyte(0x87);
                addlong(offsetof(voodoo_state_t, tex_a));

                addbyte(0xf3); /*MOVQ XMM0, XMM1*/
                addbyte(0x0f);
                addbyte(0x7e);
                addbyte(0xc1);
        }
        if (cc_mselect == CC_MSELECT_TEXRGB)
        {
                addbyte(0xf3); /*MOVD XMM4, XMM0*/
                addbyte(0x0f);
                addbyte(0x7e);
                addbyte(0xe0);
        }

        if ((params->fbzMode & FBZ_CHROMAKEY))
        {
                switch (_rgb_sel)
                {
			case CC_LOCALSELECT_ITER_RGB:
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
                        addbyte(0x66); /*MOVD EAX, XMM0*/
                        addbyte(0x0f);
                        addbyte(0x7e);
                        addbyte(0xc0);
                        break;
                        case CC_LOCALSELECT_COLOR1:
                        addbyte(0x8b); /*MOV EAX, params->color1[RSI]*/
                        addbyte(0x86);
                        addlong(offsetof(voodoo_params_t, color1));
                        break;
                        case CC_LOCALSELECT_TEX:
                        addbyte(0x66); /*MOVD EAX, XMM0*/
                        addbyte(0x0f);
                        addbyte(0x7e);
                        addbyte(0xc0);
                        break;
                }
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

        if (voodoo->trexInit1[0] & (1 << 18))
        {
                addbyte(0xb8); /*MOV EAX, tmuConfig*/
                addlong(voodoo->tmuConfig);
                addbyte(0x66); /*MOVD XMM0, EAX*/
                addbyte(0x0f);
                addbyte(0x6e);
                addbyte(0xc0);
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
                                addbyte(0xeb); /*JMP +*/
                                addbyte(8+5+4+4);
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
                        case CC_MSELECT_TEXRGB:
                        addbyte(0x66); /*PUNPCKLBW XMM4, XMM2*/
                        addbyte(0x0f);
                        addbyte(0x60);
                        addbyte(0xe2);
                        addbyte(0xf3); /*MOVQ XMM3, XMM4*/
                        addbyte(0x0f);
                        addbyte(0x7e);
                        addbyte(0xdc);
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
                        addbyte(0x66); /*PXOR XMM3, XMM9(xmm_ff_w)*/
                        addbyte(0x41);
                        addbyte(0x0f);
                        addbyte(0xef);
                        addbyte(0xd9);
                }
                addbyte(0x66); /*PADDW XMM3, XMM8(xmm_01_w)*/
                addbyte(0x41);
                addbyte(0x0f);
                addbyte(0xfd);
                addbyte(0xd8);
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
                addbyte(0x66); /*PXOR XMM0, XMM10(xmm_ff_b)*/
                addbyte(0x41);
                addbyte(0x0f);
                addbyte(0xef);
                addbyte(0xc2);
        }

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
                }
                else
                {
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

                        switch (params->fogMode & (FOG_Z|FOG_ALPHA))
                        {
                                case 0:
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
                                break;

                                case FOG_Z:
                                addbyte(0x8b); /*MOV EAX, state->z[EDI]*/
                                addbyte(0x87);
                                addlong(offsetof(voodoo_state_t, z));
                                addbyte(0xc1); /*SHR EAX, 12*/
                                addbyte(0xe8);
                                addbyte(12);
                                addbyte(0x25); /*AND EAX, 0xff*/
                                addlong(0xff);
//                                fog_a = (z >> 20) & 0xff;
                                break;

                                case FOG_ALPHA:
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
                                break;

                                case FOG_W:
                                addbyte(0x8b); /*MOV EAX, state->w[EDI]+4*/
                                addbyte(0x87);
                                addlong(offsetof(voodoo_state_t, w)+4);
                                addbyte(0x31); /*XOR EBX, EBX*/
                                addbyte(0xdb);
                                addbyte(0x09); /*OR EAX, EAX*/
                                addbyte(0xc0);
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
//                                fog_a = CLAMP(w >> 32);
                                break;
                        }
                        addbyte(0x01); /*ADD EAX, EAX*/
                        addbyte(0xc0);

                        addbyte(0x66); /*PMULLW XMM3, alookup+4[EAX*8]*/
                        addbyte(0x41);
                        addbyte(0x0f);
                        addbyte(0xd5);
                        addbyte(0x5c);
                        addbyte(0xc2);
                        addbyte(16);
                        addbyte(0x66); /*PSRAW XMM3, 7*/
                        addbyte(0x0f);
                        addbyte(0x71);
                        addbyte(0xe3);
                        addbyte(7);

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
                        }
                        addbyte(0x66); /*PACKUSWB XMM0, XMM0*/
                        addbyte(0x0f);
                        addbyte(0x67);
                        addbyte(0xc0);
                }
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
                addbyte(0x49); /*MOV R8, rgb565*/
                addbyte(0xb8);
                addquad((uintptr_t)rgb565);
                addbyte(0x8b); /*MOV EAX, state->x[EDI]*/
                addbyte(0x87);
                if (params->col_tiled)
                        addlong(offsetof(voodoo_state_t, x_tiled));
                else
                        addlong(offsetof(voodoo_state_t, x));
                addbyte(0x48); /*MOV RBP, fb_mem*/
                addbyte(0x8b);
                addbyte(0xaf);
                addlong(offsetof(voodoo_state_t, fb_mem));
                addbyte(0x01); /*ADD EDX, EDX*/
                addbyte(0xd2);
                addbyte(0x0f); /*MOVZX EAX, [RBP+RAX*2]*/
                addbyte(0xb7);
                addbyte(0x44);
                addbyte(0x45);
                addbyte(0);
                addbyte(0x66); /*PUNPCKLBW XMM0, XMM2*/
                addbyte(0x0f);
                addbyte(0x60);
                addbyte(0xc2);
                addbyte(0x66); /*MOVD XMM4, rgb565[EAX*4]*/
                addbyte(0x41);
                addbyte(0x0f);
                addbyte(0x6e);
                addbyte(0x24);
                addbyte(0x80);
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
                        addbyte(0x66); /*PMULLW XMM4, R10(alookup)[EDX*8]*/
                        addbyte(0x41);
                        addbyte(0x0f);
                        addbyte(0xd5);
                        addbyte(0x24);
                        addbyte(0xd2);
                        addbyte(0xf3); /*MOVQ XMM5, XMM4*/
                        addbyte(0x0f);
                        addbyte(0x7e);
                        addbyte(0xec);
                        addbyte(0x66); /*PADDW XMM4, R10(alookup)[1*8]*/
                        addbyte(0x41);
                        addbyte(0x0f);
                        addbyte(0xfd);
                        addbyte(0x62);
                        addbyte(8*2);
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
                        addbyte(0x66); /*PADDW XMM4, R10(alookup)[1*8]*/
                        addbyte(0x41);
                        addbyte(0x0f);
                        addbyte(0xfd);
                        addbyte(0x62);
                        addbyte(8*2);
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
                        addbyte(0x66); /*PMULLW XMM4, R11(aminuslookup)[EDX*8]*/
                        addbyte(0x41);
                        addbyte(0x0f);
                        addbyte(0xd5);
                        addbyte(0x24);
                        addbyte(0xd3);
                        addbyte(0xf3); /*MOVQ XMM5, XMM4*/
                        addbyte(0x0f);
                        addbyte(0x7e);
                        addbyte(0xec);
                        addbyte(0x66); /*PADDW XMM4, R10(alookup)[1*8]*/
                        addbyte(0x41);
                        addbyte(0x0f);
                        addbyte(0xfd);
                        addbyte(0x62);
                        addbyte(8*2);
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
                        addbyte(0xf3); /*MOVQ XMM5, XMM9(xmm_ff_w)*/
                        addbyte(0x41);
                        addbyte(0x0f);
                        addbyte(0x7e);
                        addbyte(0xe9);
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
                        addbyte(0x41);
                        addbyte(0x0f);
                        addbyte(0xfd);
                        addbyte(0x62);
                        addbyte(8*2);
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
                        addbyte(0x66); /*PMULLW XMM4, XMM11(minus_254)*/
                        addbyte(0x41);
                        addbyte(0x0f);
                        addbyte(0xd5);
                        addbyte(0xe3);
                        addbyte(0xf3); /*MOVQ XMM5, XMM4*/
                        addbyte(0x0f);
                        addbyte(0x7e);
                        addbyte(0xec);
                        addbyte(0x66); /*PADDW XMM4, alookup[1*8]*/
                        addbyte(0x41);
                        addbyte(0x0f);
                        addbyte(0xfd);
                        addbyte(0x62);
                        addbyte(8*2);
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
                        addbyte(0x66); /*PMULLW XMM0, R10(alookup)[EDX*8]*/
                        addbyte(0x41);
                        addbyte(0x0f);
                        addbyte(0xd5);
                        addbyte(0x04);
                        addbyte(0xd2);
                        addbyte(0xf3); /*MOVQ XMM5, XMM0*/
                        addbyte(0x0f);
                        addbyte(0x7e);
                        addbyte(0xe8);
                        addbyte(0x66); /*PADDW XMM0, R10(alookup)[1*8]*/
                        addbyte(0x41);
                        addbyte(0x0f);
                        addbyte(0xfd);
                        addbyte(0x42);
                        addbyte(8*2);
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
                        addbyte(0x66); /*PADDW XMM0, R10(alookup)[1*8]*/
                        addbyte(0x41);
                        addbyte(0x0f);
                        addbyte(0xfd);
                        addbyte(0x42);
                        addbyte(8*2);
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
                        addbyte(0x66); /*PMULLW XMM0, R11(aminuslookup)[EDX*8]*/
                        addbyte(0x41);
                        addbyte(0x0f);
                        addbyte(0xd5);
                        addbyte(0x04);
                        addbyte(0xd3);
                        addbyte(0xf3); /*MOVQ XMM5, XMM0*/
                        addbyte(0x0f);
                        addbyte(0x7e);
                        addbyte(0xe8);
                        addbyte(0x66); /*PADDW XMM0, alookup[1*8]*/
                        addbyte(0x41);
                        addbyte(0x0f);
                        addbyte(0xfd);
                        addbyte(0x42);
                        addbyte(8*2);
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
                        addbyte(0xf3); /*MOVQ XMM5, XMM9(xmm_ff_w)*/
                        addbyte(0x41);
                        addbyte(0x0f);
                        addbyte(0x7e);
                        addbyte(0xe9);
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
                        addbyte(0x41);
                        addbyte(0x0f);
                        addbyte(0xfd);
                        addbyte(0x42);
                        addbyte(8*2);
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

        addbyte(0x8b); /*MOV EDX, state->x[EDI]*/
        addbyte(0x97);
        if (params->col_tiled)
                addlong(offsetof(voodoo_state_t, x_tiled));
        else
                addlong(offsetof(voodoo_state_t, x));

        addbyte(0x66); /*MOV EAX, XMM0*/
        addbyte(0x0f);
        addbyte(0x7e);
        addbyte(0xc0);

        if (params->fbzMode & FBZ_RGB_WMASK)
        {
                if (dither)
                {
                        addbyte(0x49); /*MOV R8, dither_rb*/
                        addbyte(0xb8);
                        addquad(dither2x2 ? (uintptr_t)dither_rb2x2 : (uintptr_t)dither_rb);
                        addbyte(0x4c); /*MOV ESI, real_y (R14)*/
                        addbyte(0x89);
                        addbyte(0xf6);
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
                                addbyte(0x8d); /*LEA ESI, RDX+RSI*2*/
                                addbyte(0x34);
                                addbyte(0x72);
                        }
                        else
                        {
                                addbyte(0xc1); /*SHR EAX, 12*/
                                addbyte(0xe8);
                                addbyte(12);
                                addbyte(0x8d); /*LEA ESI, RDX+RSI*4*/
                                addbyte(0x34);
                                addbyte(0xb2);
                        }
                        addbyte(0x8b); /*MOV EDX, state->x[EDI]*/
                        addbyte(0x97);
                        if (voodoo->col_tiled)
                                addlong(offsetof(voodoo_state_t, x_tiled));
                        else
                                addlong(offsetof(voodoo_state_t, x));
                        addbyte(0x4c); /*ADD RSI, R8*/
                        addbyte(0x01);
                        addbyte(0xc6);
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
                        addbyte(0x1e);
                        addlong(dither2x2 ? ((uintptr_t)dither_g2x2 - (uintptr_t)dither_rb2x2) : ((uintptr_t)dither_g - (uintptr_t)dither_rb));
                        addbyte(0x0f); /*MOVZX ECX, dither_rb[RCX+RSI]*/
                        addbyte(0xb6);
                        addbyte(0x0c);
                        addbyte(0x0e);
                        addbyte(0x0f); /*MOVZX EAX, dither_rb[RAX+RSI]*/
                        addbyte(0xb6);
                        addbyte(0x04);
                        addbyte(0x06);
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
                addbyte(0x48); /*MOV RSI, fb_mem*/
                addbyte(0x8b);
                addbyte(0xb7);
                addlong(offsetof(voodoo_state_t, fb_mem));
                addbyte(0x66); /*MOV [ESI+EDX*2], AX*/
                addbyte(0x89);
                addbyte(0x04);
                addbyte(0x56);
        }

        if ((params->fbzMode & (FBZ_DEPTH_WMASK | FBZ_DEPTH_ENABLE)) == (FBZ_DEPTH_WMASK | FBZ_DEPTH_ENABLE))
        {
                addbyte(0x8b); /*MOV EDX, state->x[EDI]*/
                addbyte(0x97);
                if (params->aux_tiled)
                        addlong(offsetof(voodoo_state_t, x_tiled));
                else
                        addlong(offsetof(voodoo_state_t, x));
                addbyte(0x66); /*MOV AX, new_depth*/
                addbyte(0x8b);
                addbyte(0x87);
                addlong(offsetof(voodoo_state_t, new_depth));
                addbyte(0x48); /*MOV RSI, aux_mem*/
                addbyte(0x8b);
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

        addbyte(0x4c); /*MOV RSI, R15*/
        addbyte(0x89);
        addbyte(0xfe);

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

        if (voodoo->dual_tmus)
        {
                addbyte(0xf3); /*MOVDQU XMM5, params->tmu[1].dSdX[ESI]*/
                addbyte(0x0f);
                addbyte(0x6f);
                addbyte(0xae);
                addlong(offsetof(voodoo_params_t, tmu[1].dSdX));
                addbyte(0xf3); /*MOVQ XMM6, params->tmu[1].dWdX[ESI]*/
                addbyte(0x0f);
                addbyte(0x7e);
                addbyte(0xb6);
                addlong(offsetof(voodoo_params_t, tmu[1].dWdX));
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

        if (voodoo->dual_tmus)
        {
                addbyte(0xf3); /*MOVDQU XMM3, state->tmu1_s[EDI]*/
                addbyte(0x0f);
                addbyte(0x6f);
                addbyte(0x9f);
                addlong(offsetof(voodoo_state_t, tmu1_s));
                addbyte(0xf3); /*MOVQ XMM4, state->tmu1_w[EDI]*/
                addbyte(0x0f);
                addbyte(0x7e);
                addbyte(0xa7);
                addlong(offsetof(voodoo_state_t, tmu1_w));

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
                }

                addbyte(0xf3); /*MOVDQU state->tmu1_s, XMM3*/
                addbyte(0x0f);
                addbyte(0x7f);
                addbyte(0x9f);
                addlong(offsetof(voodoo_state_t, tmu1_s));
                addbyte(0x66); /*MOVQ state->tmu1_w, XMM4*/
                addbyte(0x0f);
                addbyte(0xd6);
                addbyte(0xa7);
                addlong(offsetof(voodoo_state_t, tmu1_w));
        }

        addbyte(0x83); /*ADD state->pixel_count[EDI], 1*/
        addbyte(0x87);
        addlong(offsetof(voodoo_state_t, pixel_count));
        addbyte(1);

        if (params->fbzColorPath & FBZCP_TEXTURE_ENABLED)
        {
                if ((params->textureMode[0] & TEXTUREMODE_MASK) == TEXTUREMODE_PASSTHROUGH ||
                    (params->textureMode[0] & TEXTUREMODE_LOCAL_MASK) == TEXTUREMODE_LOCAL)
                {
                        addbyte(0x83); /*ADD state->texel_count[EDI], 1*/
                        addbyte(0x87);
                        addlong(offsetof(voodoo_state_t, texel_count));
                        addbyte(1);
                }
                else
                {
                        addbyte(0x83); /*ADD state->texel_count[EDI], 2*/
                        addbyte(0x87);
                        addlong(offsetof(voodoo_state_t, texel_count));
                        addbyte(2);
                }
        }

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

        addbyte(0x41); /*POP R15*/
        addbyte(0x5f);
        addbyte(0x41); /*POP R14*/
        addbyte(0x5e);
        addbyte(0x41); /*POP R13*/
        addbyte(0x5d);
        addbyte(0x41); /*POP R12*/
        addbyte(0x5c);
        addbyte(0x5b); /*POP RBX*/
        addbyte(0x5e); /*POP RSI*/
        addbyte(0x5f); /*POP RDI*/
        addbyte(0x5d); /*POP RBP*/

        addbyte(0xC3); /*RET*/
}
int voodoo_recomp = 0;
static inline void *voodoo_get_block(voodoo_t *voodoo, voodoo_params_t *params, voodoo_state_t *state, int odd_even)
{
        int c;
        int b = last_block[odd_even];
        voodoo_x86_data_t *voodoo_x86_data = voodoo->codegen_data;
        voodoo_x86_data_t *data;

        for (c = 0; c < 8; c++)
        {
                data = &voodoo_x86_data[odd_even + c*4]; //&voodoo_x86_data[odd_even][b];

                if (state->xdir == data->xdir &&
                    params->alphaMode == data->alphaMode &&
                    params->fbzMode == data->fbzMode &&
                    params->fogMode == data->fogMode &&
                    params->fbzColorPath == data->fbzColorPath &&
                    (voodoo->trexInit1[0] & (1 << 18)) == data->trexInit1 &&
                    params->textureMode[0] == data->textureMode[0] &&
                    params->textureMode[1] == data->textureMode[1] &&
                    (params->tLOD[0] & LOD_MASK) == data->tLOD[0] &&
                    (params->tLOD[1] & LOD_MASK) == data->tLOD[1] &&
                    ((params->col_tiled || params->aux_tiled) ? 1 : 0) == data->is_tiled)
                {
                        last_block[odd_even] = b;
                        return data->code_block;
                }

                b = (b + 1) & 7;
        }
voodoo_recomp++;
        data = &voodoo_x86_data[odd_even + next_block_to_write[odd_even]*4];
//        code_block = data->code_block;

        voodoo_generate(data->code_block, voodoo, params, state, depth_op);

        data->xdir = state->xdir;
        data->alphaMode = params->alphaMode;
        data->fbzMode = params->fbzMode;
        data->fogMode = params->fogMode;
        data->fbzColorPath = params->fbzColorPath;
        data->trexInit1 = voodoo->trexInit1[0] & (1 << 18);
        data->textureMode[0] = params->textureMode[0];
        data->textureMode[1] = params->textureMode[1];
        data->tLOD[0] = params->tLOD[0] & LOD_MASK;
        data->tLOD[1] = params->tLOD[1] & LOD_MASK;
	data->is_tiled = (params->col_tiled || params->aux_tiled) ? 1 : 0;

        next_block_to_write[odd_even] = (next_block_to_write[odd_even] + 1) & 7;

        return data->code_block;
}

void voodoo_codegen_init(voodoo_t *voodoo)
{
        int c;

        voodoo->codegen_data = plat_mmap(sizeof(voodoo_x86_data_t) * BLOCK_NUM*4, 1);

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

                bilinear_lookup[c*2]     = _mm_set_epi32(d[1] | (d[1] << 16), d[1] | (d[1] << 16), d[0] | (d[0] << 16), d[0] | (d[0] << 16));
                bilinear_lookup[c*2 + 1] = _mm_set_epi32(d[3] | (d[3] << 16), d[3] | (d[3] << 16), d[2] | (d[2] << 16), d[2] | (d[2] << 16));
        }
        alookup[256] = _mm_set_epi32(0, 0, 256 | (256 << 16), 256 | (256 << 16));
        xmm_00_ff_w[0] = _mm_set_epi32(0, 0, 0, 0);
        xmm_00_ff_w[1] = _mm_set_epi32(0, 0, 0xff | (0xff << 16), 0xff | (0xff << 16));
}

void voodoo_codegen_close(voodoo_t *voodoo)
{
        plat_munmap(voodoo->codegen_data, sizeof(voodoo_x86_data_t) * BLOCK_NUM*4);
}

#endif /*VIDEO_VOODOO_CODEGEN_X86_64_H*/
