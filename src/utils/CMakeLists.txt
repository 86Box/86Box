#
# 86Box    A hypervisor and IBM PC system emulator that specializes in
#          running old operating systems and software designed for IBM
#          PC systems and compatibles from 1981 through fairly recent
#          system designs based on the PCI bus.
#
#          This file is part of the 86Box distribution.
#
#          CMake build script.
#
# Authors: David Hrdlička, <hrdlickadavid@outlook.com>
#          Jasmine Iwanek, <jriwanek@gmail.com>
#
#          Copyright 2020-2021 David Hrdlička.
#          Copyright 2024      Jasmine Iwanek.
#

add_library(utils OBJECT

    # Core
    cJSON.c
    crc.c
    crc32.c
    decode.c
    edc.c
    fifo.c
    fifo8.c
    ini.c
    log.c
    random.c
    utils.c

    # Common
    common/crc.c
    common/crypto.c
    common/endian.c
    common/gfmul.c
    common/pkcs5.c
    common/xts.c

    # Crypto
    crypto/aeskey.c
    crypto/aessmall.c
    crypto/aestab.c
    crypto/bf_ecb.c
    crypto/bf_enc.c
    crypto/bf_skey.c
    crypto/c_ecb.c
    crypto/c_enc.c
    crypto/c_skey.c
    crypto/des.c
    crypto/des_enc.c
    crypto/ecb3_enc.c
    crypto/rmd160.c
    crypto/serpent.c
    crypto/set_key.c
    crypto/sha1.c
    crypto/sha2.c
    crypto/twofish.c
    crypto/whirlpool.c

)
