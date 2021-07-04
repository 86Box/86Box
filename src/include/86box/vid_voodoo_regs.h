enum
{
        SST_status = 0x000,
        SST_intrCtrl = 0x004,

        SST_vertexAx = 0x008,
        SST_vertexAy = 0x00c,
        SST_vertexBx = 0x010,
        SST_vertexBy = 0x014,
        SST_vertexCx = 0x018,
        SST_vertexCy = 0x01c,

        SST_startR   = 0x0020,
        SST_startG   = 0x0024,
        SST_startB   = 0x0028,
        SST_startZ   = 0x002c,
        SST_startA   = 0x0030,
        SST_startS   = 0x0034,
        SST_startT   = 0x0038,
        SST_startW   = 0x003c,

        SST_dRdX     = 0x0040,
        SST_dGdX     = 0x0044,
        SST_dBdX     = 0x0048,
        SST_dZdX     = 0x004c,
        SST_dAdX     = 0x0050,
        SST_dSdX     = 0x0054,
        SST_dTdX     = 0x0058,
        SST_dWdX     = 0x005c,

        SST_dRdY     = 0x0060,
        SST_dGdY     = 0x0064,
        SST_dBdY     = 0x0068,
        SST_dZdY     = 0x006c,
        SST_dAdY     = 0x0070,
        SST_dSdY     = 0x0074,
        SST_dTdY     = 0x0078,
        SST_dWdY     = 0x007c,

        SST_triangleCMD = 0x0080,

        SST_fvertexAx = 0x088,
        SST_fvertexAy = 0x08c,
        SST_fvertexBx = 0x090,
        SST_fvertexBy = 0x094,
        SST_fvertexCx = 0x098,
        SST_fvertexCy = 0x09c,

        SST_fstartR   = 0x00a0,
        SST_fstartG   = 0x00a4,
        SST_fstartB   = 0x00a8,
        SST_fstartZ   = 0x00ac,
        SST_fstartA   = 0x00b0,
        SST_fstartS   = 0x00b4,
        SST_fstartT   = 0x00b8,
        SST_fstartW   = 0x00bc,

        SST_fdRdX     = 0x00c0,
        SST_fdGdX     = 0x00c4,
        SST_fdBdX     = 0x00c8,
        SST_fdZdX     = 0x00cc,
        SST_fdAdX     = 0x00d0,
        SST_fdSdX     = 0x00d4,
        SST_fdTdX     = 0x00d8,
        SST_fdWdX     = 0x00dc,

        SST_fdRdY     = 0x00e0,
        SST_fdGdY     = 0x00e4,
        SST_fdBdY     = 0x00e8,
        SST_fdZdY     = 0x00ec,
        SST_fdAdY     = 0x00f0,
        SST_fdSdY     = 0x00f4,
        SST_fdTdY     = 0x00f8,
        SST_fdWdY     = 0x00fc,

        SST_ftriangleCMD = 0x0100,

        SST_fbzColorPath = 0x104,
        SST_fogMode = 0x108,

        SST_alphaMode = 0x10c,
        SST_fbzMode = 0x110,
        SST_lfbMode = 0x114,

        SST_clipLeftRight = 0x118,
        SST_clipLowYHighY = 0x11c,

        SST_nopCMD = 0x120,
        SST_fastfillCMD = 0x124,
        SST_swapbufferCMD = 0x128,

        SST_fogColor = 0x12c,
        SST_zaColor = 0x130,
        SST_chromaKey = 0x134,

        SST_userIntrCMD = 0x13c,
        SST_stipple = 0x140,
        SST_color0 = 0x144,
        SST_color1 = 0x148,

        SST_fbiPixelsIn = 0x14c,
        SST_fbiChromaFail = 0x150,
        SST_fbiZFuncFail = 0x154,
        SST_fbiAFuncFail = 0x158,
        SST_fbiPixelsOut = 0x15c,

        SST_fogTable00 = 0x160,
        SST_fogTable01 = 0x164,
        SST_fogTable02 = 0x168,
        SST_fogTable03 = 0x16c,
        SST_fogTable04 = 0x170,
        SST_fogTable05 = 0x174,
        SST_fogTable06 = 0x178,
        SST_fogTable07 = 0x17c,
        SST_fogTable08 = 0x180,
        SST_fogTable09 = 0x184,
        SST_fogTable0a = 0x188,
        SST_fogTable0b = 0x18c,
        SST_fogTable0c = 0x190,
        SST_fogTable0d = 0x194,
        SST_fogTable0e = 0x198,
        SST_fogTable0f = 0x19c,
        SST_fogTable10 = 0x1a0,
        SST_fogTable11 = 0x1a4,
        SST_fogTable12 = 0x1a8,
        SST_fogTable13 = 0x1ac,
        SST_fogTable14 = 0x1b0,
        SST_fogTable15 = 0x1b4,
        SST_fogTable16 = 0x1b8,
        SST_fogTable17 = 0x1bc,
        SST_fogTable18 = 0x1c0,
        SST_fogTable19 = 0x1c4,
        SST_fogTable1a = 0x1c8,
        SST_fogTable1b = 0x1cc,
        SST_fogTable1c = 0x1d0,
        SST_fogTable1d = 0x1d4,
        SST_fogTable1e = 0x1d8,
        SST_fogTable1f = 0x1dc,

        SST_cmdFifoBaseAddr = 0x1e0,
        SST_cmdFifoBump = 0x1e4,
        SST_cmdFifoRdPtr = 0x1e8,
        SST_cmdFifoAMin = 0x1ec,
        SST_cmdFifoAMax = 0x1f0,
        SST_cmdFifoDepth = 0x1f4,
        SST_cmdFifoHoles = 0x1f8,
        
        SST_colBufferAddr = 0x1ec,   /*Banshee*/
        SST_colBufferStride = 0x1f0, /*Banshee*/
        SST_auxBufferAddr = 0x1f4,   /*Banshee*/
        SST_auxBufferStride = 0x1f8, /*Banshee*/

        SST_clipLeftRight1 = 0x200, /*Banshee*/
        SST_clipTopBottom1 = 0x204, /*Banshee*/

        SST_fbiInit4 = 0x200,
        SST_vRetrace = 0x204,
        SST_backPorch = 0x208,
        SST_videoDimensions = 0x20c,
        SST_fbiInit0 = 0x210,
        SST_fbiInit1 = 0x214,
        SST_fbiInit2 = 0x218,
        SST_fbiInit3 = 0x21c,
        SST_hSync = 0x220,
        SST_vSync = 0x224,
        SST_clutData = 0x228,
        SST_dacData = 0x22c,

	SST_scrFilter = 0x230,

        SST_hvRetrace = 0x240,
        SST_fbiInit5 = 0x244,
        SST_fbiInit6 = 0x248,
        SST_fbiInit7 = 0x24c,
        
        SST_swapPending = 0x24c, /*Banshee*/
        SST_leftOverlayBuf = 0x250, /*Banshee*/
        
        SST_sSetupMode = 0x260,
        SST_sVx    = 0x264,
        SST_sVy    = 0x268,
        SST_sARGB  = 0x26c,
        SST_sRed   = 0x270,
        SST_sGreen = 0x274,
        SST_sBlue  = 0x278,
        SST_sAlpha = 0x27c,
        SST_sVz    = 0x280,
        SST_sWb    = 0x284,
        SST_sW0    = 0x288,
        SST_sS0    = 0x28c,
        SST_sT0    = 0x290,
        SST_sW1    = 0x294,
        SST_sS1    = 0x298,
        SST_sT1    = 0x29c,

        SST_sDrawTriCMD = 0x2a0,
        SST_sBeginTriCMD = 0x2a4,

        SST_bltSrcBaseAddr = 0x2c0,
        SST_bltDstBaseAddr = 0x2c4,
        SST_bltXYStrides = 0x2c8,
        SST_bltSrcChromaRange = 0x2cc,
        SST_bltDstChromaRange = 0x2d0,
        SST_bltClipX = 0x2d4,
        SST_bltClipY = 0x2d8,

        SST_bltSrcXY = 0x2e0,
        SST_bltDstXY = 0x2e4,
        SST_bltSize = 0x2e8,
        SST_bltRop = 0x2ec,
        SST_bltColor = 0x2f0,

        SST_bltCommand = 0x2f8,
        SST_bltData = 0x2fc,

        SST_textureMode = 0x300,
        SST_tLOD = 0x304,
        SST_tDetail = 0x308,
        SST_texBaseAddr = 0x30c,
        SST_texBaseAddr1 = 0x310,
        SST_texBaseAddr2 = 0x314,
        SST_texBaseAddr38 = 0x318,

        SST_trexInit1 = 0x320,

        SST_nccTable0_Y0 = 0x324,
        SST_nccTable0_Y1 = 0x328,
        SST_nccTable0_Y2 = 0x32c,
        SST_nccTable0_Y3 = 0x330,
        SST_nccTable0_I0 = 0x334,
        SST_nccTable0_I1 = 0x338,
        SST_nccTable0_I2 = 0x33c,
        SST_nccTable0_I3 = 0x340,
        SST_nccTable0_Q0 = 0x344,
        SST_nccTable0_Q1 = 0x348,
        SST_nccTable0_Q2 = 0x34c,
        SST_nccTable0_Q3 = 0x350,

        SST_nccTable1_Y0 = 0x354,
        SST_nccTable1_Y1 = 0x358,
        SST_nccTable1_Y2 = 0x35c,
        SST_nccTable1_Y3 = 0x360,
        SST_nccTable1_I0 = 0x364,
        SST_nccTable1_I1 = 0x368,
        SST_nccTable1_I2 = 0x36c,
        SST_nccTable1_I3 = 0x370,
        SST_nccTable1_Q0 = 0x374,
        SST_nccTable1_Q1 = 0x378,
        SST_nccTable1_Q2 = 0x37c,
        SST_nccTable1_Q3 = 0x380,

        SST_remap_status = 0x000 | 0x400,

        SST_remap_vertexAx = 0x008 | 0x400,
        SST_remap_vertexAy = 0x00c | 0x400,
        SST_remap_vertexBx = 0x010 | 0x400,
        SST_remap_vertexBy = 0x014 | 0x400,
        SST_remap_vertexCx = 0x018 | 0x400,
        SST_remap_vertexCy = 0x01c | 0x400,

        SST_remap_startR   = 0x0020 | 0x400,
        SST_remap_startG   = 0x002c | 0x400,
        SST_remap_startB   = 0x0038 | 0x400,
        SST_remap_startZ   = 0x0044 | 0x400,
        SST_remap_startA   = 0x0050 | 0x400,
        SST_remap_startS   = 0x005c | 0x400,
        SST_remap_startT   = 0x0068 | 0x400,
        SST_remap_startW   = 0x0074 | 0x400,

        SST_remap_dRdX     = 0x0024 | 0x400,
        SST_remap_dGdX     = 0x0030 | 0x400,
        SST_remap_dBdX     = 0x003c | 0x400,
        SST_remap_dZdX     = 0x0048 | 0x400,
        SST_remap_dAdX     = 0x0054 | 0x400,
        SST_remap_dSdX     = 0x0060 | 0x400,
        SST_remap_dTdX     = 0x006c | 0x400,
        SST_remap_dWdX     = 0x0078 | 0x400,

        SST_remap_dRdY     = 0x0028 | 0x400,
        SST_remap_dGdY     = 0x0034 | 0x400,
        SST_remap_dBdY     = 0x0040 | 0x400,
        SST_remap_dZdY     = 0x004c | 0x400,
        SST_remap_dAdY     = 0x0058 | 0x400,
        SST_remap_dSdY     = 0x0064 | 0x400,
        SST_remap_dTdY     = 0x0070 | 0x400,
        SST_remap_dWdY     = 0x007c | 0x400,

        SST_remap_triangleCMD = 0x0080 | 0x400,

        SST_remap_fvertexAx = 0x088 | 0x400,
        SST_remap_fvertexAy = 0x08c | 0x400,
        SST_remap_fvertexBx = 0x090 | 0x400,
        SST_remap_fvertexBy = 0x094 | 0x400,
        SST_remap_fvertexCx = 0x098 | 0x400,
        SST_remap_fvertexCy = 0x09c | 0x400,

        SST_remap_fstartR   = 0x00a0 | 0x400,
        SST_remap_fstartG   = 0x00ac | 0x400,
        SST_remap_fstartB   = 0x00b8 | 0x400,
        SST_remap_fstartZ   = 0x00c4 | 0x400,
        SST_remap_fstartA   = 0x00d0 | 0x400,
        SST_remap_fstartS   = 0x00dc | 0x400,
        SST_remap_fstartT   = 0x00e8 | 0x400,
        SST_remap_fstartW   = 0x00f4 | 0x400,

        SST_remap_fdRdX     = 0x00a4 | 0x400,
        SST_remap_fdGdX     = 0x00b0 | 0x400,
        SST_remap_fdBdX     = 0x00bc | 0x400,
        SST_remap_fdZdX     = 0x00c8 | 0x400,
        SST_remap_fdAdX     = 0x00d4 | 0x400,
        SST_remap_fdSdX     = 0x00e0 | 0x400,
        SST_remap_fdTdX     = 0x00ec | 0x400,
        SST_remap_fdWdX     = 0x00f8 | 0x400,

        SST_remap_fdRdY     = 0x00a8 | 0x400,
        SST_remap_fdGdY     = 0x00b4 | 0x400,
        SST_remap_fdBdY     = 0x00c0 | 0x400,
        SST_remap_fdZdY     = 0x00cc | 0x400,
        SST_remap_fdAdY     = 0x00d8 | 0x400,
        SST_remap_fdSdY     = 0x00e4 | 0x400,
        SST_remap_fdTdY     = 0x00f0 | 0x400,
        SST_remap_fdWdY     = 0x00fc | 0x400,
};

enum
{
        LFB_WRITE_FRONT = 0x0000,
        LFB_WRITE_BACK  = 0x0010,
        LFB_WRITE_MASK  = 0x0030
};

enum
{
        LFB_READ_FRONT = 0x0000,
        LFB_READ_BACK  = 0x0040,
        LFB_READ_AUX   = 0x0080,
        LFB_READ_MASK  = 0x00c0
};

enum
{
        LFB_FORMAT_RGB565 = 0,
        LFB_FORMAT_RGB555 = 1,
        LFB_FORMAT_ARGB1555 = 2,
        LFB_FORMAT_ARGB8888 = 5,
        LFB_FORMAT_DEPTH = 15,
        LFB_FORMAT_MASK = 15
};

enum
{
        LFB_WRITE_COLOUR = 1,
        LFB_WRITE_DEPTH = 2
};

enum
{
        FBZ_CHROMAKEY = (1 << 1),
        FBZ_W_BUFFER = (1 << 3),
        FBZ_DEPTH_ENABLE = (1 << 4),

        FBZ_DITHER      = (1 << 8),
        FBZ_RGB_WMASK   = (1 << 9),
        FBZ_DEPTH_WMASK = (1 << 10),
        FBZ_DITHER_2x2  = (1 << 11),

        FBZ_DRAW_FRONT = 0x0000,
        FBZ_DRAW_BACK  = 0x4000,
        FBZ_DRAW_MASK  = 0xc000,

        FBZ_DEPTH_BIAS = (1 << 16),
		FBZ_DITHER_SUB = (1 << 19),

        FBZ_DEPTH_SOURCE = (1 << 20),

        FBZ_PARAM_ADJUST = (1 << 26)
};

enum
{
        TEX_RGB332 = 0x0,
        TEX_Y4I2Q2 = 0x1,
        TEX_A8 = 0x2,
        TEX_I8 = 0x3,
        TEX_AI8 = 0x4,
        TEX_PAL8 = 0x5,
        TEX_APAL8 = 0x6,
        TEX_ARGB8332 = 0x8,
        TEX_A8Y4I2Q2 = 0x9,
        TEX_R5G6B5 = 0xa,
        TEX_ARGB1555 = 0xb,
        TEX_ARGB4444 = 0xc,
        TEX_A8I8 = 0xd,
        TEX_APAL88 = 0xe
};

enum
{
        TEXTUREMODE_NCC_SEL = (1 << 5),
        TEXTUREMODE_TCLAMPS = (1 << 6),
        TEXTUREMODE_TCLAMPT = (1 << 7),
        TEXTUREMODE_TRILINEAR = (1 << 30)
};

enum
{
        FBIINIT0_VGA_PASS = 1,
        FBIINIT0_GRAPHICS_RESET = (1 << 1)
};

enum
{
        FBIINIT1_MULTI_SST = (1 << 2), /*Voodoo Graphics only*/
        FBIINIT1_VIDEO_RESET = (1 << 8),
        FBIINIT1_SLI_ENABLE = (1 << 23)
};

enum
{
        FBIINIT2_SWAP_ALGORITHM_MASK = (3 << 9)
};

enum
{
        FBIINIT2_SWAP_ALGORITHM_DAC_VSYNC      = (0 << 9),
        FBIINIT2_SWAP_ALGORITHM_DAC_DATA       = (1 << 9),
        FBIINIT2_SWAP_ALGORITHM_PCI_FIFO_STALL = (2 << 9),
        FBIINIT2_SWAP_ALGORITHM_SLI_SYNC       = (3 << 9)
};

enum
{
        FBIINIT3_REMAP = 1
};

enum
{
        FBIINIT5_MULTI_CVG = (1 << 14)
};

enum
{
        FBIINIT7_CMDFIFO_ENABLE = (1 << 8)
};

enum
{
        CC_LOCALSELECT_ITER_RGB = 0,
        CC_LOCALSELECT_TEX = 1,
        CC_LOCALSELECT_COLOR1 = 2,
        CC_LOCALSELECT_LFB = 3
};

enum
{
        CCA_LOCALSELECT_ITER_A = 0,
        CCA_LOCALSELECT_COLOR0 = 1,
        CCA_LOCALSELECT_ITER_Z = 2
};

enum
{
        C_SEL_ITER_RGB = 0,
        C_SEL_TEX      = 1,
        C_SEL_COLOR1   = 2,
        C_SEL_LFB      = 3
};

enum
{
        A_SEL_ITER_A = 0,
        A_SEL_TEX    = 1,
        A_SEL_COLOR1 = 2,
        A_SEL_LFB    = 3
};

enum
{
        CC_MSELECT_ZERO   = 0,
        CC_MSELECT_CLOCAL = 1,
        CC_MSELECT_AOTHER = 2,
        CC_MSELECT_ALOCAL = 3,
        CC_MSELECT_TEX    = 4,
        CC_MSELECT_TEXRGB = 5
};

enum
{
        CCA_MSELECT_ZERO    = 0,
        CCA_MSELECT_ALOCAL  = 1,
        CCA_MSELECT_AOTHER  = 2,
        CCA_MSELECT_ALOCAL2 = 3,
        CCA_MSELECT_TEX     = 4
};

enum
{
        TC_MSELECT_ZERO     = 0,
        TC_MSELECT_CLOCAL   = 1,
        TC_MSELECT_AOTHER   = 2,
        TC_MSELECT_ALOCAL   = 3,
        TC_MSELECT_DETAIL   = 4,
        TC_MSELECT_LOD_FRAC = 5
};

enum
{
        TCA_MSELECT_ZERO     = 0,
        TCA_MSELECT_CLOCAL   = 1,
        TCA_MSELECT_AOTHER   = 2,
        TCA_MSELECT_ALOCAL   = 3,
        TCA_MSELECT_DETAIL   = 4,
        TCA_MSELECT_LOD_FRAC = 5
};

enum
{
        CC_ADD_CLOCAL = 1,
        CC_ADD_ALOCAL = 2
};

enum
{
        CCA_ADD_CLOCAL = 1,
        CCA_ADD_ALOCAL = 2
};

enum
{
        AFUNC_AZERO = 0x0,
        AFUNC_ASRC_ALPHA = 0x1,
        AFUNC_A_COLOR = 0x2,
        AFUNC_ADST_ALPHA = 0x3,
        AFUNC_AONE = 0x4,
        AFUNC_AOMSRC_ALPHA = 0x5,
        AFUNC_AOM_COLOR = 0x6,
        AFUNC_AOMDST_ALPHA = 0x7,
        AFUNC_ASATURATE = 0xf
};

enum
{
        AFUNC_ACOLORBEFOREFOG = 0xf
};

enum
{
        AFUNC_NEVER    = 0,
        AFUNC_LESSTHAN = 1,
        AFUNC_EQUAL = 2,
        AFUNC_LESSTHANEQUAL = 3,
        AFUNC_GREATERTHAN = 4,
        AFUNC_NOTEQUAL = 5,
        AFUNC_GREATERTHANEQUAL = 6,
        AFUNC_ALWAYS = 7
};

enum
{
        DEPTHOP_NEVER    = 0,
        DEPTHOP_LESSTHAN = 1,
        DEPTHOP_EQUAL = 2,
        DEPTHOP_LESSTHANEQUAL = 3,
        DEPTHOP_GREATERTHAN = 4,
        DEPTHOP_NOTEQUAL = 5,
        DEPTHOP_GREATERTHANEQUAL = 6,
        DEPTHOP_ALWAYS = 7
};

enum
{
        FOG_ENABLE   = 0x01,
        FOG_ADD      = 0x02,
        FOG_MULT     = 0x04,
        FOG_ALPHA    = 0x08,
        FOG_Z        = 0x10,
        FOG_W        = 0x18,
        FOG_CONSTANT = 0x20
};

enum
{
        LOD_ODD            = (1 << 18),
        LOD_SPLIT          = (1 << 19),
        LOD_S_IS_WIDER     = (1 << 20),
        LOD_TMULTIBASEADDR = (1 << 24),
        LOD_TMIRROR_S      = (1 << 28),
        LOD_TMIRROR_T      = (1 << 29)
};
enum
{
        CMD_INVALID = 0,
        CMD_DRAWTRIANGLE,
        CMD_FASTFILL,
        CMD_SWAPBUF
};

enum
{
        FBZCP_TEXTURE_ENABLED = (1 << 27)
};

enum
{
        BLTCMD_SRC_TILED = (1 << 14),
        BLTCMD_DST_TILED = (1 << 15)
};

enum
{
        INITENABLE_SLI_MASTER_SLAVE = (1 << 11)
};

enum
{
        SETUPMODE_RGB   = (1 << 0),
        SETUPMODE_ALPHA = (1 << 1),
        SETUPMODE_Z     = (1 << 2),
        SETUPMODE_Wb    = (1 << 3),
        SETUPMODE_W0    = (1 << 4),
        SETUPMODE_S0_T0 = (1 << 5),
        SETUPMODE_W1    = (1 << 6),
        SETUPMODE_S1_T1 = (1 << 7),

        SETUPMODE_STRIP_MODE = (1 << 16),
        SETUPMODE_CULLING_ENABLE = (1 << 17),
        SETUPMODE_CULLING_SIGN = (1 << 18),
        SETUPMODE_DISABLE_PINGPONG = (1 << 19)
};

#define TEXTUREMODE_MASK 0x3ffff000
#define TEXTUREMODE_PASSTHROUGH 0

#define TEXTUREMODE_LOCAL_MASK 0x00643000
#define TEXTUREMODE_LOCAL  0x00241000


#define SLI_ENABLED (voodoo->fbiInit1 & FBIINIT1_SLI_ENABLE)
#define TRIPLE_BUFFER ((voodoo->fbiInit2 & 0x10) || (voodoo->fbiInit5 & 0x600) == 0x400)


#define _rgb_sel                 ( params->fbzColorPath & 3)
#define a_sel                   ( (params->fbzColorPath >> 2) & 3)
#define cc_localselect          ( params->fbzColorPath & (1 << 4))
#define cca_localselect         ( (params->fbzColorPath >> 5) & 3)
#define cc_localselect_override ( params->fbzColorPath & (1 << 7))
#define cc_zero_other           ( params->fbzColorPath & (1 << 8))
#define cc_sub_clocal           ( params->fbzColorPath & (1 << 9))
#define cc_mselect              ( (params->fbzColorPath >> 10) & 7)
#define cc_reverse_blend        ( params->fbzColorPath & (1 << 13))
#define cc_add                  ( (params->fbzColorPath >> 14) & 3)
#define cc_add_alocal           ( params->fbzColorPath & (1 << 15))
#define cc_invert_output        ( params->fbzColorPath & (1 << 16))
#define cca_zero_other          ( params->fbzColorPath & (1 << 17))
#define cca_sub_clocal          ( params->fbzColorPath & (1 << 18))
#define cca_mselect             ( (params->fbzColorPath >> 19) & 7)
#define cca_reverse_blend       ( params->fbzColorPath & (1 << 22))
#define cca_add                 ( (params->fbzColorPath >> 23) & 3)
#define cca_invert_output       ( params->fbzColorPath & (1 << 25))
#define tc_zero_other (params->textureMode[0] & (1 << 12))
#define tc_sub_clocal (params->textureMode[0] & (1 << 13))
#define tc_mselect    ((params->textureMode[0] >> 14) & 7)
#define tc_reverse_blend (params->textureMode[0] & (1 << 17))
#define tc_add_clocal (params->textureMode[0] & (1 << 18))
#define tc_add_alocal (params->textureMode[0] & (1 << 19))
#define tc_invert_output (params->textureMode[0] & (1 << 20))
#define tca_zero_other (params->textureMode[0] & (1 << 21))
#define tca_sub_clocal (params->textureMode[0] & (1 << 22))
#define tca_mselect    ((params->textureMode[0] >> 23) & 7)
#define tca_reverse_blend (params->textureMode[0] & (1 << 26))
#define tca_add_clocal (params->textureMode[0] & (1 << 27))
#define tca_add_alocal (params->textureMode[0] & (1 << 28))
#define tca_invert_output (params->textureMode[0] & (1 << 29))

#define tc_sub_clocal_1 (params->textureMode[1] & (1 << 13))
#define tc_mselect_1    ((params->textureMode[1] >> 14) & 7)
#define tc_reverse_blend_1 (params->textureMode[1] & (1 << 17))
#define tc_add_clocal_1 (params->textureMode[1] & (1 << 18))
#define tc_add_alocal_1 (params->textureMode[1] & (1 << 19))
#define tca_sub_clocal_1 (params->textureMode[1] & (1 << 22))
#define tca_mselect_1    ((params->textureMode[1] >> 23) & 7)
#define tca_reverse_blend_1 (params->textureMode[1] & (1 << 26))
#define tca_add_clocal_1 (params->textureMode[1] & (1 << 27))
#define tca_add_alocal_1 (params->textureMode[1] & (1 << 28))

#define src_afunc ( (params->alphaMode >> 8) & 0xf)
#define dest_afunc ( (params->alphaMode >> 12) & 0xf)
#define alpha_func ( (params->alphaMode >> 1) & 7)
#define a_ref ( params->alphaMode >> 24)
#define depth_op ( (params->fbzMode >> 5) & 7)
#define dither ( params->fbzMode & FBZ_DITHER)
#define dither2x2 (params->fbzMode & FBZ_DITHER_2x2)
#define dithersub (params->fbzMode & FBZ_DITHER_SUB)