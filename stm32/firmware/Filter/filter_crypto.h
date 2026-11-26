#ifndef FILTER_CRYPTO_H
#define FILTER_CRYPTO_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define FILTER_SHA256_SIZE 32u
#define FILTER_HMAC_KEY_MAX 64u

void filter_sha256(const uint8_t *data, uint32_t len,
                   uint8_t out[FILTER_SHA256_SIZE]);

/* HMAC over two segments avoids copying header + bytecode into a second
 * staging buffer. Keys longer than 64 bytes are hashed first. */
void filter_hmac_sha256_2(const uint8_t *key, uint32_t key_len,
                          const uint8_t *a, uint32_t a_len,
                          const uint8_t *b, uint32_t b_len,
                          uint8_t out[FILTER_SHA256_SIZE]);

int filter_constant_time_equal(const uint8_t *a, const uint8_t *b, uint32_t len);

#ifdef __cplusplus
}
#endif

#endif /* FILTER_CRYPTO_H */
