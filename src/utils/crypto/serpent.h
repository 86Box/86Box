#ifndef HEADER_Crypto_Serpent
#define HEADER_Crypto_Serpent

#include <inttypes.h>

#ifdef __cplusplus
extern "C"
{
#endif

void serpent_set_key(const uint8_t userKey[], int keylen, uint8_t *ks);
void serpent_encrypt(const uint8_t *inBlock, uint8_t *outBlock, uint8_t *ks);
void serpent_decrypt(const uint8_t *inBlock,  uint8_t *outBlock, uint8_t *ks);

#ifdef __cplusplus
}
#endif

#endif // HEADER_Crypto_Serpent
