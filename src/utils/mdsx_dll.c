/*
 * 86Box    A hypervisor and IBM PC system emulator that specializes in
 *          running old operating systems and software designed for IBM
 *          PC systems and compatibles from 1981 through fairly recent
 *          system designs based on the PCI bus.
 *
 *          This file is part of the 86Box distribution.
 *
 *          Implementation of a generic PostScript printer and a
 *          generic PCL 5e printer.
 *
 * Authors: David Hrdlička, <hrdlickadavid@outlook.com>
 *          Cacodemon345
 *
 *          Copyright 2019 David Hrdlička.
 *          Copyright 2024 Cacodemon345.
 */
#include <inttypes.h>
#include <memory.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>
#include <86box/86box.h>
#include <86box/device.h>
#include <86box/timer.h>
#include <86box/device.h>
#include <86box/lpt.h>
#include <86box/pit.h>
#include <86box/path.h>
#include <86box/plat.h>
#include <86box/plat_dynld.h>
#include <86box/ui.h>
#include <86box/prt_devs.h>
#include "cpu.h"
#include "defines.h"
#include "mds.h"

#ifdef _WIN32
#    define PATH_MDSX_DLL "mdsx.dll"
#elif defined __APPLE__
#    define PATH_MDSX_DLL "mdsx.dylib"
#else
#    define PATH_MDSX_DLL "mdsx.so"
#endif

void(MDSXDLLAPI *DecryptBlock)(u8 *buf,	TC_LARGEST_COMPILER_UINT len, u32 secSz, u64 secN, u8 flags, PCRYPTO_INFO cryptoInfo);
int(MDSXDLLAPI *decode1)(u8 *data, const char *pass, PCRYPTO_INFO *ci);
void(MDSXDLLAPI *decryptMdxData)(Decoder *ctx, u8 *buffer, u32 length, u64 blockSize, u64 blockIndex);
int(MDSXDLLAPI *Gf128Tab64Init)(uint8_t *a, GfCtx *ctx);
AES_RETURN(MDSXDLLAPI *aes_encrypt_key)(const unsigned char *key, int key_len, aes_encrypt_ctx cx[1]);
AES_RETURN(MDSXDLLAPI *aes_decrypt_key)(const unsigned char *key, int key_len, aes_decrypt_ctx cx[1]);

static dllimp_t mdsx_imports[] = {
  // clang-format off
    { "DecryptBlock",    &DecryptBlock    },
    { "decode1",         &decode1         },
    { "decryptMdxData",  &decryptMdxData  },
    { "Gf128Tab64Init",  &Gf128Tab64Init  },
    { "aes_encrypt_key", &aes_encrypt_key },
    { "aes_decrypt_key", &aes_decrypt_key },
    { NULL,              NULL             }
  // clang-format on
};

static void *mdsx_handle = NULL;

void
mdsx_close(void)
{
    if (mdsx_handle != NULL) {
        dynld_close(mdsx_handle);
        mdsx_handle = NULL;
    }
}

int
mdsx_init(void)
{
    /* Try loading the DLL. */
    mdsx_handle = dynld_module(PATH_MDSX_DLL, mdsx_imports);

    if (mdsx_handle == NULL) {
        warning("Unable to load %s\n", PATH_MDSX_DLL);
        return 0;
    }

    return 1;
}
