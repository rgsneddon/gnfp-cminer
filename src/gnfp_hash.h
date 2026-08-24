#ifndef GNFP_HASH_H
#define GNFP_HASH_H

#include <stddef.h>
#include <stdint.h>

#define CPU_HASH_ROUNDS 8
#define CPU_NONCE_HEX_LEN 16
#define GNFP_X8 8
#define GNFP_MAX_PRE 256
#define GNFP_PERSONAL "GNFPHash-v1"
#define GNFP_ALGO "GNFPHash"
#define GNFP_SELFTEST_PRE "test-prework"
#define GNFP_SELFTEST_NONCE "0000000000000001"
#define GNFP_SELFTEST_HASH "986437c40fee8a876e0ca3f1e58b14fa38785a179f57f98ebbb0fb03102bd4eb"

void gnfp_work_hash(const char *pre, const char nonce[CPU_NONCE_HEX_LEN],
                    const char *solution, unsigned char out[32]);
void gnfp_hash_hex(const unsigned char hash[32], char hex[65]);
int gnfp_selftest(char got_hex[65]);
int gnfp_meets_target(const unsigned char hash[32], int bits);
void gnfp_nonce_hex16(uint64_t n, char out[CPU_NONCE_HEX_LEN]);
void gnfp_hash_x8(const char *pre, const char nonce[GNFP_X8][CPU_NONCE_HEX_LEN],
                  unsigned char out[GNFP_X8][32]);
const char *gnfp_hash_backend(void);

#endif
