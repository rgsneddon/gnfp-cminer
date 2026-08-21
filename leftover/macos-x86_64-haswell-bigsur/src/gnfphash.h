#ifndef GNFP_HASH_H
#define GNFP_HASH_H

#include <stddef.h>
#include <stdint.h>

#define GNFP_NONCE_LEN 16
#define GNFP_HASH_LEN 32
#define GNFP_MAX_PRE 256
#define GNFP_X8 8

void gnfp_hash(const uint8_t *pre, size_t pre_len, const char nonce[GNFP_NONCE_LEN], uint8_t out[GNFP_HASH_LEN]);
void gnfp_hash_x8(const uint8_t *pre, size_t pre_len, const char nonce[GNFP_X8][GNFP_NONCE_LEN], uint8_t out[GNFP_X8][GNFP_HASH_LEN]);

int gnfp_meets_target(const uint8_t hash[GNFP_HASH_LEN], int bits);
void gnfp_nonce_hex(uint64_t n, char out[GNFP_NONCE_LEN]);

const char *gnfp_hash_backend(void);

#endif
