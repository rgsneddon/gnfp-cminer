/*
 * GNFPHash-v1 — 1 stem SHA-256 + 8 sequential rounds (GPU/ASIC brake).
 * 8-way AVX2 hashes eight nonces at once; rounds stay serial per nonce.
 */
#include "gnfphash.h"

#include "sha256.h"

#include <string.h>

static const char PERSONAL[] = "GNFPHash-v1";
static const char ALGO[] = "GNFPHash";
#define PERSONAL_LEN 11
#define ALGO_LEN 8

static const char HEX[16] = "0123456789abcdef";

void gnfp_nonce_hex(uint64_t n, char out[GNFP_NONCE_LEN]) {
  for (int i = 15; i >= 0; i--) {
    out[i] = HEX[n & 15ull];
    n >>= 4;
  }
}

int gnfp_meets_target(const uint8_t hash[GNFP_HASH_LEN], int bits) {
  if (bits <= 0) return 1;
  if (bits > 256) bits = 256;
  int full = bits / 8;
  int rem = bits % 8;
  for (int i = 0; i < full; i++) {
    if (hash[i] != 0) return 0;
  }
  if (!rem) return 1;
  return hash[full] < (uint8_t)(1u << (8 - rem));
}

void gnfp_hash(const uint8_t *pre, size_t pre_len, const char nonce[GNFP_NONCE_LEN], uint8_t out[GNFP_HASH_LEN]) {
  if (pre_len > GNFP_MAX_PRE) pre_len = GNFP_MAX_PRE;
  uint8_t buf[32 + PERSONAL_LEN + 1 + GNFP_MAX_PRE + GNFP_NONCE_LEN];
  uint8_t acc[GNFP_HASH_LEN];
  size_t n = 0;
  memcpy(buf + n, PERSONAL, PERSONAL_LEN);
  n += PERSONAL_LEN;
  memcpy(buf + n, ALGO, ALGO_LEN);
  n += ALGO_LEN;
  if (pre_len) {
    memcpy(buf + n, pre, pre_len);
    n += pre_len;
  }
  memcpy(buf + n, nonce, GNFP_NONCE_LEN);
  n += GNFP_NONCE_LEN;
  sha256_oneshot(buf, n, acc);
  for (int r = 0; r < 8; r++) {
    n = 0;
    memcpy(buf + n, acc, 32);
    n += 32;
    memcpy(buf + n, PERSONAL, PERSONAL_LEN);
    n += PERSONAL_LEN;
    buf[n++] = (uint8_t)('0' + r);
    if (pre_len) {
      memcpy(buf + n, pre, pre_len);
      n += pre_len;
    }
    memcpy(buf + n, nonce, GNFP_NONCE_LEN);
    n += GNFP_NONCE_LEN;
    sha256_oneshot(buf, n, acc);
  }
  memcpy(out, acc, GNFP_HASH_LEN);
}

void gnfp_hash_x8(const uint8_t *pre, size_t pre_len, const char nonce[GNFP_X8][GNFP_NONCE_LEN],
                  uint8_t out[GNFP_X8][GNFP_HASH_LEN]) {
  if (pre_len > GNFP_MAX_PRE) pre_len = GNFP_MAX_PRE;
  uint8_t msg[GNFP_X8][32 + PERSONAL_LEN + 1 + GNFP_MAX_PRE + GNFP_NONCE_LEN];
  const uint8_t *ptr[GNFP_X8];
  size_t first_len = (size_t)PERSONAL_LEN + ALGO_LEN + pre_len + GNFP_NONCE_LEN;
  size_t round_len = 32u + PERSONAL_LEN + 1u + pre_len + GNFP_NONCE_LEN;
  for (int lane = 0; lane < GNFP_X8; lane++) {
    size_t n = 0;
    memcpy(msg[lane] + n, PERSONAL, PERSONAL_LEN);
    n += PERSONAL_LEN;
    memcpy(msg[lane] + n, ALGO, ALGO_LEN);
    n += ALGO_LEN;
    if (pre_len) {
      memcpy(msg[lane] + n, pre, pre_len);
      n += pre_len;
    }
    memcpy(msg[lane] + n, nonce[lane], GNFP_NONCE_LEN);
    ptr[lane] = msg[lane];
  }
  sha256_oneshot_x8(ptr, first_len, out);
  for (int r = 0; r < 8; r++) {
    for (int lane = 0; lane < GNFP_X8; lane++) {
      size_t n = 0;
      memcpy(msg[lane] + n, out[lane], 32);
      n += 32;
      memcpy(msg[lane] + n, PERSONAL, PERSONAL_LEN);
      n += PERSONAL_LEN;
      msg[lane][n++] = (uint8_t)('0' + r);
      if (pre_len) {
        memcpy(msg[lane] + n, pre, pre_len);
        n += pre_len;
      }
      memcpy(msg[lane] + n, nonce[lane], GNFP_NONCE_LEN);
      ptr[lane] = msg[lane];
    }
    sha256_oneshot_x8(ptr, round_len, out);
  }
}

const char *gnfp_hash_backend(void) {
  return sha256_have_avx2() ? "avx2-x8" : "scalar";
}
