#ifndef MDS_H
#define MDS_H

#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#include "defines.h"
#include "common/crypto.h"

typedef struct Decoder_t
{
    u8 dg[32];
    GfCtx gf_ctx;
    aes_encrypt_ctx encr;
    aes_decrypt_ctx decr;
    u8 bsize;

    int mode;
    int ctr;
} Decoder;



enum TRACK_TYPE
{
	TRK_T_MAINTENANCE = 0,
	TRK_T_AUDIO = 1,
	TRK_T_MODE1 = 2,
	TRK_T_MODE2 = 3,
	TRK_T_MODE2_FORM1 = 4,
	TRK_T_MODE2_FORM2 = 5
};

enum TRACK_FLAG
{
	TRK_F_TYPE_MASK = 7,
	TRK_F_EDC = 8,
	TRK_F_10 = 0x10,
	TRK_F_HEADER = 0x20,
	TRK_F_SUBHEADER = 0x40,
	TRK_F_SYNC = 0x80
};


typedef struct __attribute__((packed))
{
	u32 f0;
	u32 f1;
	u64 f3; //or two u32?
} UNK4;

typedef struct __attribute__((packed))
{
	char signature[16];
	u8 major; // 0x10
	u8 minor;
	u16 medium_type; // 0x12
	u16 num_sessions; // 0x14
	u16 _unk1_;
	u16 _unk2_size_; // 0x18
	u16 bca_len;
	u16 _unk3_size_; // 0x1c
	u16 _unk4_size_; // 0x1e
	u32 _unk2_offset_; // 0x20
	u32 bca_data_offset; // 0x24
	u32 _unk3_offset_; // 0x28
	u32 _unk4_offset_; // 0x2c    0x10 byte elements UNK4
	u8  _unk5_;        // 0x30
	u32 _unk6_;       // 0x31
	u8  _unk7_;        // 0x35
	u64 _unk8_;        // 0x36
	u16 _unk9_;        // 0x3e
	u32 disc_structures_offset; // 0x40
	u32 _unk10_offset_; // 0x44
	u16 _unk10_size_; // 0x48
	u8 _dummy1_[6]; // 0x4a   not used by DT
	u32 sessions_blocks_offset; // 0x50
	u32 dpm_blocks_offset; // 0x54
	u32 encryption_block_offset; // 0x58
	u8 _dummy2_[4]; // 0x5c
} MDX_Header; // 0x60

typedef struct __attribute__((packed))
{
	u64 session_start;
	u16 session_number; // 0x8
	u8 num_all_blocks; // 0xa
	u8 num_nontrack_blocks; // 0xb
	u16 first_track; // 0xc
	u16 last_track; // 0xe
	u32 _dummy_; // 0x10
	u32 tracks_blocks_offset; // 0x14
	u64 session_end; // 0x18
} MDX_SessionBlock; // 0x20

typedef struct __attribute__((packed))
{
	u8 mode;
	u8 subchannel;
	u8 adr_ctl;
	u8 tno;
	u8 point; // 4
	u8 min;
	u8 sec;
	u8 frame;
	u8 zero; // 8
	u8 pmin;
	u8 psec;
	u8 pframe;
	u32 extra_offset; // 0xc
	u16 file_block_size; // 0x10   original name. represent full size of data and additional data per sector
	u8 _unk1_; // 0x12
	u8 _dummy1_[5]; // 0x13
	u32 _unk2_; // 0x18;
	u32 _unk3_; // 0x1c;
	u32 _unk4_; // 0x20;
	u32 start_sector; // 0x24
	u64 start_offset; // 0x28
	u32 footer_count; // 0x30
	u32 footer_offset; // 0x34
	u64 start_sector64; // 0x38   major >= 2
	u64 track_size64; // 0x40   major >= 2
	u8 _dummy2_[8]; // 0x48
} MDX_TrackBlock; // 0x50

typedef struct __attribute__((packed))
{
	u32 filename_offset;
	u8 flags; // 4
	u8 _dummy1_; // 5
	u16 _unk1_size_; // 6
	u32 _unk2_size_; // 8
	u32 blocks_in_compression_group; // c     major >= 2
	u64 track_data_length; // 10   major >= 2
	u64 compress_table_offset; // 18
} MDX_Footer; // 0x20




// decode.c
void DecryptBlock(u8 *buf,	TC_LARGEST_COMPILER_UINT len, u32 secSz, u64 secN, u8 flags, PCRYPTO_INFO cryptoInfo);

int decode1(u8 *data, const char *pass, PCRYPTO_INFO *ci);

void decryptMdxData(Decoder *ctx, u8 *buffer, u32 length, u64 blockSize, u64 blockIndex);


// utils.c
inline static u64 getU64(const void *mem)
{
	const u8 *mem8 = (const u8 *)mem;
	return ((u64)mem8[0] | ((u64)mem8[1] << 8) | ((u64)mem8[2] << 16) | ((u64)mem8[3] << 24) | ((u64)mem8[4] << 32) | ((u64)mem8[5] << 40) | ((u64)mem8[6] << 48) | ((u64)mem8[7] << 56));
}

inline static u32 getU32(const void *mem)
{
	const u8 *mem8 = (const u8 *)mem;
	return ((u32)mem8[0] | ((u32)mem8[1] << 8) | ((u32)mem8[2] << 16) | ((u32)mem8[3] << 24));
}

inline static u16 getU16(const void *mem)
{
	const u8 *mem8 = (const u8 *)mem;
	return ((u16)mem8[0] | ((u16)mem8[1] << 8));
}

inline static u8 getU8(const void *mem)
{
	const u8 *mem8 = (const u8 *)mem;
	return (u8)mem8[0];
}

inline static void setU32(void *mem, u32 val)
{
	u8 *mem8 = (u8 *)mem;
	mem8[0] = val & 0xff;
	mem8[1] = (val >> 8) & 0xff;
	mem8[2] = (val >> 16) & 0xff;
	mem8[3] = (val >> 24) & 0xff;
}

u32 freadU32(FILE *f);
u64 freadU64(FILE *f);
void printHex(void *data, int num);

#endif
