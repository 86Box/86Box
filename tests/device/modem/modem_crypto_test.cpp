#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <string>
#include <gtest/gtest.h>
#include <86box/modem/modem_crypto.h>

static std::string
digest_hex(const uint8_t *digest, size_t length)
{
    std::string hex(length * 2, '0');

    for (size_t i = 0; i < length; i++)
        std::snprintf(&hex[i * 2], 3, "%02x", (unsigned int) digest[i]);
    return hex;
}

TEST(ModemCrypto, ConstantTimeEqualityChecksAllBytes)
{
    const uint8_t expected[] = { 0x10, 0x20, 0x30, 0x40 };
    const uint8_t matching[] = { 0x10, 0x20, 0x30, 0x40 };
    const uint8_t mismatch_first[] = { 0x11, 0x20, 0x30, 0x40 };
    const uint8_t mismatch_last[] = { 0x10, 0x20, 0x30, 0x41 };

    EXPECT_TRUE(modem_constant_time_equal(expected, matching, sizeof(expected)));
    EXPECT_FALSE(modem_constant_time_equal(expected, mismatch_first, sizeof(expected)));
    EXPECT_FALSE(modem_constant_time_equal(expected, mismatch_last, sizeof(expected)));
    EXPECT_FALSE(modem_constant_time_equal(nullptr, matching, sizeof(matching)));
}

TEST(ModemCrypto, MD4KnownVectors)
{
    uint8_t digest[MD4_DIGEST_LENGTH];

    modem_md4(nullptr, 0, digest);
    EXPECT_EQ(digest_hex(digest, sizeof(digest)), "31d6cfe0d16ae931b73c59d7e0c089c0");

    modem_md4(reinterpret_cast<const uint8_t *>("abc"), 3, digest);
    EXPECT_EQ(digest_hex(digest, sizeof(digest)), "a448017aaf21d8525fc10ae87aa6729d");
}

TEST(ModemCrypto, MD4IncrementalUpdate)
{
    uint8_t digest[MD4_DIGEST_LENGTH];
    modem_md4_ctx_t ctx;

    modem_md4_init(&ctx);
    modem_md4_update(&ctx, reinterpret_cast<const uint8_t *>("a"), 1);
    modem_md4_update(&ctx, reinterpret_cast<const uint8_t *>("bc"), 2);
    modem_md4_final(&ctx, digest);

    EXPECT_EQ(digest_hex(digest, sizeof(digest)), "a448017aaf21d8525fc10ae87aa6729d");
}

TEST(ModemCrypto, MD5KnownVectors)
{
    uint8_t digest[MD5_DIGEST_LENGTH];

    modem_md5(nullptr, 0, digest);
    EXPECT_EQ(digest_hex(digest, sizeof(digest)), "d41d8cd98f00b204e9800998ecf8427e");

    modem_md5(reinterpret_cast<const uint8_t *>("abc"), 3, digest);
    EXPECT_EQ(digest_hex(digest, sizeof(digest)), "900150983cd24fb0d6963f7d28e17f72");
}

TEST(ModemCrypto, MD5IncrementalUpdate)
{
    uint8_t digest[MD5_DIGEST_LENGTH];
    modem_md5_ctx_t ctx;

    modem_md5_init(&ctx);
    modem_md5_update(&ctx, reinterpret_cast<const uint8_t *>("a"), 1);
    modem_md5_update(&ctx, reinterpret_cast<const uint8_t *>("bc"), 2);
    modem_md5_final(&ctx, digest);

    EXPECT_EQ(digest_hex(digest, sizeof(digest)), "900150983cd24fb0d6963f7d28e17f72");
}

TEST(ModemCrypto, SHA1KnownVectors)
{
    uint8_t digest[SHA1_DIGEST_LENGTH];

    modem_sha1(nullptr, 0, digest);
    EXPECT_EQ(digest_hex(digest, sizeof(digest)), "da39a3ee5e6b4b0d3255bfef95601890afd80709");

    modem_sha1(reinterpret_cast<const uint8_t *>("abc"), 3, digest);
    EXPECT_EQ(digest_hex(digest, sizeof(digest)), "a9993e364706816aba3e25717850c26c9cd0d89d");
}

TEST(ModemCrypto, SHA256KnownVectors)
{
    uint8_t digest[SHA256_DIGEST_LENGTH];

    modem_sha256(nullptr, 0, digest);
    EXPECT_EQ(digest_hex(digest, sizeof(digest)), "e3b0c44298fc1c149afbf4c8996fb924"
                                                  "27ae41e4649b934ca495991b7852b855");

    modem_sha256(reinterpret_cast<const uint8_t *>("abc"), 3, digest);
    EXPECT_EQ(digest_hex(digest, sizeof(digest)), "ba7816bf8f01cfea414140de5dae2223"
                                                  "b00361a396177a9cb410ff61f20015ad");

    static const char boundary_message[] =
        "abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq";
    modem_sha256(reinterpret_cast<const uint8_t *>(boundary_message), sizeof(boundary_message) - 1, digest);
    EXPECT_EQ(digest_hex(digest, sizeof(digest)), "248d6a61d20638b8e5c026930c3e6039"
                                                  "a33ce45964ff2167f6ecedd419db06c1");
}

TEST(ModemCrypto, SHA256IncrementalUpdate)
{
    uint8_t digest[SHA256_DIGEST_LENGTH];
    modem_sha256_ctx_t ctx;

    modem_sha256_init(&ctx);
    modem_sha256_update(&ctx, reinterpret_cast<const uint8_t *>("a"), 1);
    modem_sha256_update(&ctx, reinterpret_cast<const uint8_t *>("b"), 1);
    modem_sha256_update(&ctx, reinterpret_cast<const uint8_t *>("c"), 1);
    modem_sha256_final(&ctx, digest);

    EXPECT_EQ(digest_hex(digest, sizeof(digest)), "ba7816bf8f01cfea414140de5dae2223"
                                                  "b00361a396177a9cb410ff61f20015ad");
}

TEST(ModemCrypto, SHA384KnownVectors)
{
    uint8_t digest[SHA384_DIGEST_LENGTH];

    modem_sha384(nullptr, 0, digest);
    EXPECT_EQ(digest_hex(digest, sizeof(digest)), "38b060a751ac96384cd9327eb1b1e36a"
                                                  "21fdb71114be07434c0cc7bf63f6e1da"
                                                  "274edebfe76f65fbd51ad2f14898b95b");

    modem_sha384(reinterpret_cast<const uint8_t *>("abc"), 3, digest);
    EXPECT_EQ(digest_hex(digest, sizeof(digest)), "cb00753f45a35e8bb5a03d699ac65007"
                                                  "272c32ab0eded1631a8b605a43ff5bed"
                                                  "8086072ba1e7cc2358baeca134c825a7");
}

TEST(ModemCrypto, SHA512KnownVectors)
{
    uint8_t digest[SHA512_DIGEST_LENGTH];

    modem_sha512(nullptr, 0, digest);
    EXPECT_EQ(digest_hex(digest, sizeof(digest)), "cf83e1357eefb8bdf1542850d66d8007"
                                                  "d620e4050b5715dc83f4a921d36ce9ce"
                                                  "47d0d13c5d85f2b0ff8318d2877eec2f"
                                                  "63b931bd47417a81a538327af927da3e");

    modem_sha512(reinterpret_cast<const uint8_t *>("abc"), 3, digest);
    EXPECT_EQ(digest_hex(digest, sizeof(digest)), "ddaf35a193617abacc417349ae204131"
                                                  "12e6fa4e89a97ea20a9eeee64b55d39a"
                                                  "2192992a274fc1a836ba3c23a3feebbd"
                                                  "454d4423643ce80e2a9ac94fa54ca49f");
}

TEST(ModemCrypto, SHA384SHA512BoundaryAndIncremental)
{
    static const char message[] =
        "abcdefghbcdefghicdefghijdefghijkefghijklfghijklmghijklmnhijklmno"
        "ijklmnopjklmnopqklmnopqrlmnopqrsmnopqrstnopqrstu";
    const uint8_t *data = reinterpret_cast<const uint8_t *>(message);
    constexpr size_t message_length = sizeof(message) - 1;
    uint8_t sha384_digest[SHA384_DIGEST_LENGTH];
    uint8_t sha512_digest[SHA512_DIGEST_LENGTH];
    modem_sha384_ctx_t sha384;
    modem_sha512_ctx_t sha512;

    modem_sha384(data, message_length, sha384_digest);
    EXPECT_EQ(digest_hex(sha384_digest, sizeof(sha384_digest)),
              "09330c33f71147e83d192fc782cd1b4753111b173b3b05d22fa08086e3b0f712"
              "fcc7c71a557e2db966c3e9fa91746039");
    modem_sha512(data, message_length, sha512_digest);
    EXPECT_EQ(digest_hex(sha512_digest, sizeof(sha512_digest)),
              "8e959b75dae313da8cf4f72814fc143f8f7779c6eb9f7fa17299aeadb6889018"
              "501d289e4900f7e4331b99dec4b5433ac7d329eeb6dd26545e96e55b874be909");

    modem_sha384_init(&sha384);
    modem_sha384_update(&sha384, data, 111);
    modem_sha384_update(&sha384, data + 111, message_length - 111);
    modem_sha384_final(&sha384, sha384_digest);
    EXPECT_EQ(digest_hex(sha384_digest, sizeof(sha384_digest)),
              "09330c33f71147e83d192fc782cd1b4753111b173b3b05d22fa08086e3b0f712"
              "fcc7c71a557e2db966c3e9fa91746039");

    modem_sha512_init(&sha512);
    modem_sha512_update(&sha512, data, 111);
    modem_sha512_update(&sha512, data + 111, message_length - 111);
    modem_sha512_final(&sha512, sha512_digest);
    EXPECT_EQ(digest_hex(sha512_digest, sizeof(sha512_digest)),
              "8e959b75dae313da8cf4f72814fc143f8f7779c6eb9f7fa17299aeadb6889018"
              "501d289e4900f7e4331b99dec4b5433ac7d329eeb6dd26545e96e55b874be909");
}

TEST(ModemCrypto, DESKnownVectors)
{
    const uint8_t zero_key[7] = { 0 };
    const uint8_t zero_plaintext[8] = { 0 };
    const uint8_t key[7] = { 0x12, 0x69, 0x5B, 0xC9, 0xB7, 0xB7, 0xF8 };
    const uint8_t plaintext[8] = { 0x01, 0x23, 0x45, 0x67, 0x89, 0xAB, 0xCD, 0xEF };
    uint8_t ciphertext[8];

    modem_des_encrypt_block(zero_key, zero_plaintext, ciphertext);
    EXPECT_EQ(digest_hex(ciphertext, sizeof(ciphertext)), "8ca64de9c1b123a7");

    modem_des_encrypt_block(key, plaintext, ciphertext);
    EXPECT_EQ(digest_hex(ciphertext, sizeof(ciphertext)), "85e813540f0ab405");
}

