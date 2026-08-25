#include "gnfp_hash.h"
#include "sha256.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

#if defined(__APPLE__)
#include <CommonCrypto/CommonDigest.h>
static void sha256(const void *data, size_t len, unsigned char out[32]) {
  CC_SHA256(data, (CC_LONG)len, out);
}
#else
/*
 * Per-call SHA256_CTX (stack). OpenSSL SHA256() oneshot goes through the
 * default provider and serialized a 12-thread farm to ~1-thread H/s.
 * Init/Update/Final keeps SHA-NI/ASM speed without that lock.
 */
#include <openssl/sha.h>
#if defined(__GNUC__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wdeprecated-declarations"
#endif
static void sha256(const void *data, size_t len, unsigned char out[32]) {
  SHA256_CTX ctx;
  SHA256_Init(&ctx);
  SHA256_Update(&ctx, data, len);
  SHA256_Final(out, &ctx);
}
#if defined(__GNUC__)
#pragma GCC diagnostic pop
#endif
#endif

void gnfp_nonce_hex16(uint64_t n, char out[CPU_NONCE_HEX_LEN]) {
  static const char *hex = "0123456789abcdef";
  for (int i = CPU_NONCE_HEX_LEN - 1; i >= 0; i--) {
    out[i] = hex[n & 15];
    n >>= 4;
  }
}

void gnfp_hash_hex(const unsigned char hash[32], char hex[65]) {
  static const char *digits = "0123456789abcdef";
  for (int i = 0; i < 32; i++) {
    hex[i * 2] = digits[hash[i] >> 4];
    hex[i * 2 + 1] = digits[hash[i] & 15];
  }
  hex[64] = 0;
}

void gnfp_work_hash(const char *pre, const char nonce[CPU_NONCE_HEX_LEN],
                    const char *solution, unsigned char out[32]) {
  const char *sol = solution ? solution : "";
  size_t pre_len = pre ? strlen(pre) : 0;
  size_t sol_len = strlen(sol);
  size_t cap = 32 + sizeof(GNFP_PERSONAL) + sizeof(GNFP_ALGO) + 8 + pre_len +
               CPU_NONCE_HEX_LEN + sol_len + 16;
  unsigned char buf[1024];
  unsigned char *use = buf;
  unsigned char *heap = NULL;
  if (cap > sizeof(buf)) {
    heap = (unsigned char *)malloc(cap);
    use = heap ? heap : buf;
    if (!heap) cap = sizeof(buf);
  }
  size_t n = 0;
  memcpy(use + n, GNFP_PERSONAL, strlen(GNFP_PERSONAL));
  n += strlen(GNFP_PERSONAL);
  memcpy(use + n, GNFP_ALGO, strlen(GNFP_ALGO));
  n += strlen(GNFP_ALGO);
  if (pre_len && n + pre_len < cap) {
    memcpy(use + n, pre, pre_len);
    n += pre_len;
  }
  memcpy(use + n, nonce, CPU_NONCE_HEX_LEN);
  n += CPU_NONCE_HEX_LEN;
  if (sol_len && n + sol_len < cap) {
    memcpy(use + n, sol, sol_len);
    n += sol_len;
  }
  sha256(use, n, out);
  for (int r = 0; r < CPU_HASH_ROUNDS; r++) {
    n = 0;
    memcpy(use + n, out, 32);
    n += 32;
    memcpy(use + n, GNFP_PERSONAL, strlen(GNFP_PERSONAL));
    n += strlen(GNFP_PERSONAL);
    use[n++] = (unsigned char)('0' + r);
    if (pre_len && n + pre_len < cap) {
      memcpy(use + n, pre, pre_len);
      n += pre_len;
    }
    memcpy(use + n, nonce, CPU_NONCE_HEX_LEN);
    n += CPU_NONCE_HEX_LEN;
    sha256(use, n, out);
  }
  if (heap) free(heap);
}

int gnfp_meets_target(const unsigned char hash[32], int bits) {
  if (bits <= 0) return 1;
  if (bits > 256) bits = 256;
  int full = bits / 8;
  int rem = bits % 8;
  for (int i = 0; i < full; i++) {
    if (hash[i] != 0) return 0;
  }
  if (!rem) return 1;
  return hash[full] < (1 << (8 - rem));
}

void gnfp_hash_x8(const char *pre, const char nonce[GNFP_X8][CPU_NONCE_HEX_LEN],
                  unsigned char out[GNFP_X8][32]) {
  size_t pre_len = pre ? strlen(pre) : 0;
  if (pre_len > GNFP_MAX_PRE) pre_len = GNFP_MAX_PRE;
  unsigned char msg[GNFP_X8][32 + 11 + 1 + GNFP_MAX_PRE + CPU_NONCE_HEX_LEN];
  const uint8_t *ptr[GNFP_X8];
  size_t first_len = strlen(GNFP_PERSONAL) + strlen(GNFP_ALGO) + pre_len + CPU_NONCE_HEX_LEN;
  size_t round_len = 32u + strlen(GNFP_PERSONAL) + 1u + pre_len + CPU_NONCE_HEX_LEN;
  for (int lane = 0; lane < GNFP_X8; lane++) {
    size_t n = 0;
    memcpy(msg[lane] + n, GNFP_PERSONAL, strlen(GNFP_PERSONAL));
    n += strlen(GNFP_PERSONAL);
    memcpy(msg[lane] + n, GNFP_ALGO, strlen(GNFP_ALGO));
    n += strlen(GNFP_ALGO);
    if (pre_len) {
      memcpy(msg[lane] + n, pre, pre_len);
      n += pre_len;
    }
    memcpy(msg[lane] + n, nonce[lane], CPU_NONCE_HEX_LEN);
    ptr[lane] = msg[lane];
  }
  sha256_oneshot_x8(ptr, first_len, out);
  for (int r = 0; r < CPU_HASH_ROUNDS; r++) {
    for (int lane = 0; lane < GNFP_X8; lane++) {
      size_t n = 0;
      memcpy(msg[lane] + n, out[lane], 32);
      n += 32;
      memcpy(msg[lane] + n, GNFP_PERSONAL, strlen(GNFP_PERSONAL));
      n += strlen(GNFP_PERSONAL);
      msg[lane][n++] = (unsigned char)('0' + r);
      if (pre_len) {
        memcpy(msg[lane] + n, pre, pre_len);
        n += pre_len;
      }
      memcpy(msg[lane] + n, nonce[lane], CPU_NONCE_HEX_LEN);
      ptr[lane] = msg[lane];
    }
    sha256_oneshot_x8(ptr, round_len, out);
  }
}

const char *gnfp_hash_backend(void) {
  return sha256_have_avx2() ? "avx2-x8" : "scalar-x8";
}

int gnfp_selftest(char got_hex[65]) {
  unsigned char hash[32];
  gnfp_work_hash(GNFP_SELFTEST_PRE, GNFP_SELFTEST_NONCE, "", hash);
  gnfp_hash_hex(hash, got_hex);
  if (strcmp(got_hex, GNFP_SELFTEST_HASH) != 0) return 0;
  char n8[GNFP_X8][CPU_NONCE_HEX_LEN];
  unsigned char o8[GNFP_X8][32];
  unsigned char ref[32];
  for (int i = 0; i < GNFP_X8; i++) gnfp_nonce_hex16((uint64_t)(1000 + i), n8[i]);
  gnfp_hash_x8("pre-x8-check", n8, o8);
  for (int i = 0; i < GNFP_X8; i++) {
    gnfp_work_hash("pre-x8-check", n8[i], "", ref);
    if (memcmp(ref, o8[i], 32) != 0) return 0;
  }
  return 1;
}
