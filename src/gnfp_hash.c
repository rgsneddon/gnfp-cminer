#include "gnfp_hash.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#if defined(__APPLE__)
#include <CommonCrypto/CommonDigest.h>
static void sha256(const void *data, size_t len, unsigned char out[32]) {
  CC_SHA256(data, (CC_LONG)len, out);
}
#else
#include <openssl/sha.h>
static void sha256(const void *data, size_t len, unsigned char out[32]) {
  SHA256((const unsigned char *)data, len, out);
}
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

int gnfp_selftest(char got_hex[65]) {
  unsigned char hash[32];
  gnfp_work_hash(GNFP_SELFTEST_PRE, GNFP_SELFTEST_NONCE, "", hash);
  gnfp_hash_hex(hash, got_hex);
  return strcmp(got_hex, GNFP_SELFTEST_HASH) == 0;
}
