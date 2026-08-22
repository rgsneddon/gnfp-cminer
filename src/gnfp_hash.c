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
/* Lock-free SHA-256. OpenSSL SHA256() serializes 12-thread farms. */
static const uint32_t SHA_K[64] = {
    0x428a2f98u, 0x71374491u, 0xb5c0fbcfu, 0xe9b5dba5u, 0x3956c25bu, 0x59f111f1u,
    0x923f82a4u, 0xab1c5ed5u, 0xd807aa98u, 0x12835b01u, 0x243185beu, 0x550c7dc3u,
    0x72be5d74u, 0x80deb1feu, 0x9bdc06a7u, 0xc19bf174u, 0xe49b69c1u, 0xefbe4786u,
    0x0fc19dc6u, 0x240ca1ccu, 0x2de92c6fu, 0x4a7484aau, 0x5cb0a9dcu, 0x76f988dau,
    0x983e5152u, 0xa831c66du, 0xb00327c8u, 0xbf597fc7u, 0xc6e00bf3u, 0xd5a79147u,
    0x06ca6351u, 0x14292967u, 0x27b70a85u, 0x2e1b2138u, 0x4d2c6dfcu, 0x53380d13u,
    0x650a7354u, 0x766a0abbu, 0x81c2c92eu, 0x92722c85u, 0xa2bfe8a1u, 0xa81a664bu,
    0xc24b8b70u, 0xc76c51a3u, 0xd192e819u, 0xd6990624u, 0xf40e3585u, 0x106aa070u,
    0x19a4c116u, 0x1e376c08u, 0x2748774cu, 0x34b0bcb5u, 0x391c0cb3u, 0x4ed8aa4au,
    0x5b9cca4fu, 0x682e6ff3u, 0x748f82eeu, 0x78a5636fu, 0x84c87814u, 0x8cc70208u,
    0x90befffau, 0xa4506cebu, 0xbef9a3f7u, 0xc67178f2u,
};
#define ROTR32(x, n) (((uint32_t)(x) >> (n)) | ((uint32_t)(x) << (32 - (n))))
static void sha256_compress(uint32_t s[8], const unsigned char block[64]) {
  uint32_t w[64];
  for (int i = 0; i < 16; i++) {
    w[i] = ((uint32_t)block[4 * i] << 24) | ((uint32_t)block[4 * i + 1] << 16) |
           ((uint32_t)block[4 * i + 2] << 8) | (uint32_t)block[4 * i + 3];
  }
  for (int i = 16; i < 64; i++) {
    uint32_t s0 = ROTR32(w[i - 15], 7) ^ ROTR32(w[i - 15], 18) ^ (w[i - 15] >> 3);
    uint32_t s1 = ROTR32(w[i - 2], 17) ^ ROTR32(w[i - 2], 19) ^ (w[i - 2] >> 10);
    w[i] = w[i - 16] + s0 + w[i - 7] + s1;
  }
  uint32_t a = s[0], b = s[1], c = s[2], d = s[3], e = s[4], f = s[5], g = s[6], h = s[7];
  for (int i = 0; i < 64; i++) {
    uint32_t S1 = ROTR32(e, 6) ^ ROTR32(e, 11) ^ ROTR32(e, 25);
    uint32_t ch = (e & f) ^ ((~e) & g);
    uint32_t t1 = h + S1 + ch + SHA_K[i] + w[i];
    uint32_t S0 = ROTR32(a, 2) ^ ROTR32(a, 13) ^ ROTR32(a, 22);
    uint32_t maj = (a & b) ^ (a & c) ^ (b & c);
    uint32_t t2 = S0 + maj;
    h = g;
    g = f;
    f = e;
    e = d + t1;
    d = c;
    c = b;
    b = a;
    a = t1 + t2;
  }
  s[0] += a;
  s[1] += b;
  s[2] += c;
  s[3] += d;
  s[4] += e;
  s[5] += f;
  s[6] += g;
  s[7] += h;
}
static void sha256(const void *data, size_t len, unsigned char out[32]) {
  uint32_t s[8] = {
      0x6a09e667u, 0xbb67ae85u, 0x3c6ef372u, 0xa54ff53au,
      0x510e527fu, 0x9b05688cu, 0x1f83d9abu, 0x5be0cd19u,
  };
  const unsigned char *p = (const unsigned char *)data;
  size_t i = 0;
  for (; i + 64 <= len; i += 64) sha256_compress(s, p + i);
  unsigned char tail[128];
  memset(tail, 0, sizeof(tail));
  size_t rem = len - i;
  if (rem) memcpy(tail, p + i, rem);
  tail[rem] = 0x80;
  size_t blocks = (rem + 1 + 8 <= 64) ? 1 : 2;
  uint64_t bits = (uint64_t)len * 8ull;
  size_t off = blocks * 64 - 8;
  for (int b = 7; b >= 0; b--) tail[off + (size_t)(7 - b)] = (unsigned char)(bits >> (b * 8));
  sha256_compress(s, tail);
  if (blocks == 2) sha256_compress(s, tail + 64);
  for (int j = 0; j < 8; j++) {
    out[4 * j] = (unsigned char)(s[j] >> 24);
    out[4 * j + 1] = (unsigned char)(s[j] >> 16);
    out[4 * j + 2] = (unsigned char)(s[j] >> 8);
    out[4 * j + 3] = (unsigned char)s[j];
  }
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
