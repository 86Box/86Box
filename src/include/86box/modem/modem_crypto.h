/*
 * 86Box    A hypervisor and IBM PC system emulator that specializes in
 *          running old operating systems and software designed for IBM
 *          PC systems and compatibles from 1981 through fairly recent
 *          system designs based on the PCI bus.
 *
 *          This file is part of the 86Box distribution.
 *
 *          Cryptographic primitives for modem authentication protocols.
 *          MD4 (RFC 1320), MD5 (RFC 1321), SHA-1 (RFC 3174), SHA-2
 *          (RFC 6234), and DES (FIPS 46-3) primitives used by CHAP,
 *          MS-CHAP (RFC 2433), and MS-CHAPv2 (RFC 2759).
 *
 * Authors: Jasmine Iwanek, <jriwanek@gmail.com>
 *
 *          Copyright 2025-2026 Jasmine Iwanek.
 */
#ifndef EMU_MODEM_CRYPTO_H
#define EMU_MODEM_CRYPTO_H

#ifdef __cplusplus
extern "C" {
#endif

bool modem_constant_time_equal(const uint8_t *left, const uint8_t *right, size_t length);

/* MD4 - used by MS-CHAP NT hash */
#define MD4_DIGEST_LENGTH 16
#define MD4_BLOCK_LENGTH  64

typedef struct {
    uint32_t state[4];
    uint64_t count;
    uint8_t  buffer[MD4_BLOCK_LENGTH];
} modem_md4_ctx_t;

void modem_md4_init(modem_md4_ctx_t *ctx);
void modem_md4_update(modem_md4_ctx_t *ctx, const uint8_t *data, size_t len);
void modem_md4_final(modem_md4_ctx_t *ctx, uint8_t digest[MD4_DIGEST_LENGTH]);
void modem_md4(const uint8_t *data, size_t len, uint8_t digest[MD4_DIGEST_LENGTH]);

/* MD5 - used by CHAP */
#define MD5_DIGEST_LENGTH 16
#define MD5_BLOCK_LENGTH  64

typedef struct {
    uint32_t state[4];
    uint64_t count;
    uint8_t  buffer[MD5_BLOCK_LENGTH];
} modem_md5_ctx_t;

void modem_md5_init(modem_md5_ctx_t *ctx);
void modem_md5_update(modem_md5_ctx_t *ctx, const uint8_t *data, size_t len);
void modem_md5_final(modem_md5_ctx_t *ctx, uint8_t digest[MD5_DIGEST_LENGTH]);
void modem_md5(const uint8_t *data, size_t len, uint8_t digest[MD5_DIGEST_LENGTH]);

/* SHA-1 - used by MS-CHAPv2 */
#define SHA1_DIGEST_LENGTH 20
#define SHA1_BLOCK_LENGTH  64

typedef struct {
    uint32_t state[5];
    uint64_t count;
    uint8_t  buffer[SHA1_BLOCK_LENGTH];
} modem_sha1_ctx_t;

void modem_sha1_init(modem_sha1_ctx_t *ctx);
void modem_sha1_update(modem_sha1_ctx_t *ctx, const uint8_t *data, size_t len);
void modem_sha1_final(modem_sha1_ctx_t *ctx, uint8_t digest[SHA1_DIGEST_LENGTH]);
void modem_sha1(const uint8_t *data, size_t len, uint8_t digest[SHA1_DIGEST_LENGTH]);

/* SHA-256 - used by CHAP */
#define SHA256_DIGEST_LENGTH 32
#define SHA256_BLOCK_LENGTH  64

typedef struct {
    uint32_t state[8];
    uint64_t count;
    uint8_t  buffer[SHA256_BLOCK_LENGTH];
} modem_sha256_ctx_t;

void modem_sha256_init(modem_sha256_ctx_t *ctx);
void modem_sha256_update(modem_sha256_ctx_t *ctx, const uint8_t *data, size_t len);
void modem_sha256_final(modem_sha256_ctx_t *ctx, uint8_t digest[SHA256_DIGEST_LENGTH]);
void modem_sha256(const uint8_t *data, size_t len, uint8_t digest[SHA256_DIGEST_LENGTH]);

/* SHA-384 and SHA-512 - used by CHAP */
#define SHA384_DIGEST_LENGTH 48
#define SHA512_DIGEST_LENGTH 64
#define SHA512_BLOCK_LENGTH  128

typedef struct {
    uint64_t state[8];
    uint64_t count_hi;
    uint64_t count_lo;
    uint8_t  buffer[SHA512_BLOCK_LENGTH];
} modem_sha512_ctx_t;

typedef modem_sha512_ctx_t modem_sha384_ctx_t;

void modem_sha384_init(modem_sha384_ctx_t *ctx);
void modem_sha384_update(modem_sha384_ctx_t *ctx, const uint8_t *data, size_t len);
void modem_sha384_final(modem_sha384_ctx_t *ctx, uint8_t digest[SHA384_DIGEST_LENGTH]);
void modem_sha384(const uint8_t *data, size_t len, uint8_t digest[SHA384_DIGEST_LENGTH]);

void modem_sha512_init(modem_sha512_ctx_t *ctx);
void modem_sha512_update(modem_sha512_ctx_t *ctx, const uint8_t *data, size_t len);
void modem_sha512_final(modem_sha512_ctx_t *ctx, uint8_t digest[SHA512_DIGEST_LENGTH]);
void modem_sha512(const uint8_t *data, size_t len, uint8_t digest[SHA512_DIGEST_LENGTH]);

/* DES - used by MS-CHAP challenge-response */
void modem_des_encrypt_block(const uint8_t key[7], const uint8_t plaintext[8], uint8_t ciphertext[8]);

#ifdef __cplusplus
}
#endif

#endif /* MODEM_CRYPTO_H */
