/*
 * gnfp_cminer_max.c — absolute maximum hashrate dual-connection GNFPHash miner
 * No protection of any kind.
 * SHA-NI path, 5% fee every 20th share, stable proven rate (Σ 2^shareBits)
 * Unified Windows + Linux source.
 * + auto-tuning --bench that searches threads / batch / flush and persists the winner
 *
 * Build Windows (MSYS2 MinGW64):
 *   gcc -O3 -march=native -msha -flto -fomit-frame-pointer -s \
 *       -o gnfp_cminer.exe gnfp_cminer_max.c -lssl -lcrypto -lpthread -lws2_32
 *
 * Build Linux:
 *   gcc -O3 -march=native -msha -flto -fomit-frame-pointer -s \
 *       -o gnfp_cminer gnfp_cminer_max.c -lssl -lcrypto -lpthread
 */

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#pragma comment(lib, "ws2_32.lib")
#define close closesocket
#else
#define _GNU_SOURCE
#include <sys/socket.h>
#include <sys/select.h>
#include <netdb.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <fcntl.h>
#endif

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <pthread.h>
#include <time.h>
#include <signal.h>
#include <errno.h>
#include <ctype.h>
#include <math.h>
#include <openssl/ssl.h>
#include <openssl/err.h>
#include <openssl/sha.h>
#include <immintrin.h>
#include <cpuid.h>

#define CLIENT              "GNFPHash"
#define VERSION             "1.0.6-max-autotune"
#define NONCE_HEX           16
#define HASH_FIELD_MAX      256
#define SHARE_Q             512
#define MAX_IN_FLIGHT       32
#define INFLIGHT_TO_MS      5000
#define STATS_MS            1000
#define STATUS_MS           5000
#define DEV_FEE_EVERY       20
#define DEFAULT_HOST        "de.restoreprivacy.online"
#define DEFAULT_PORT        1474
#define FEE_ADDR            "gnfp19381c4b1d7a9cbae64120f24b16d248ae07c6ff1"
#define BEST_CFG_FILE       "gnfp_cminer_best.cfg"

typedef struct {
    char job_id[128];
    char nonce[NONCE_HEX + 1];
    int  bits;
    int  is_fee;
} share_t;

typedef struct {
    char job_id[128];
    char pre[HASH_FIELD_MAX + 1];
    size_t pre_len;
    int  bits;
    int  height;
    uint64_t gen;
} job_t;

typedef struct {
    char host[160];
    int  port;
    int  tls;
    int  threads;
    char address[160];
    char worker[64];
    char user[320];
    char fee_user[320];
    char fee_worker[32];
    int  cpu_cores, cpu_threads, smt, max_threads;
    char platform[32], arch[32];
} cfg_t;

#ifdef _WIN32
static SOCKET g_fd = INVALID_SOCKET, g_fee_fd = INVALID_SOCKET;
#else
static int g_fd = -1, g_fee_fd = -1;
#endif
static SSL *g_ssl = NULL, *g_fee_ssl = NULL;
static SSL_CTX *g_ctx = NULL;
static volatile int g_run = 1;
static int g_sig_hits = 0;
static job_t g_job;
static pthread_mutex_t g_job_mu = PTHREAD_MUTEX_INITIALIZER;
static pthread_mutex_t g_net_mu = PTHREAD_MUTEX_INITIALIZER;
static pthread_mutex_t g_q_mu   = PTHREAD_MUTEX_INITIALIZER;
static share_t g_q[SHARE_Q];
static int g_q_head = 0, g_q_tail = 0, g_q_n = 0;
static int g_inflight = 0;
static uint64_t g_inflight_ts[MAX_IN_FLIGHT];
static int g_inflight_fee[MAX_IN_FLIGHT];
static int g_inflight_bits[MAX_IN_FLIGHT];
static int g_if_n = 0;
static uint64_t g_hash_calls = 0, g_shares_found = 0, g_shares_found16 = 0;
static uint64_t g_shares_pushed = 0, g_shares_dropped = 0, g_shares_submitted = 0;
static uint64_t g_accepts = 0, g_rejects = 0, g_implausible = 0, g_blocks = 0;
static uint64_t g_fee_accepts = 0, g_fee_submitted = 0;
static uint64_t g_t0_ms = 0, g_first_accept_ms = 0;
static int g_last_bits = 14;
static uint64_t g_backoff_until = 0;
static int g_accept_print_left = 8;
static int g_nthreads = 1;
static uint64_t g_share_counter = 0;
static double g_proven_work = 0.0;

/* Tunables discovered by --bench and persisted to BEST_CFG_FILE */
static int      g_batch_size   = 512;
static uint64_t g_local_flush  = 65536ull;

#ifdef _WIN32
static BOOL WINAPI console_handler(DWORD type) {
    if (type == CTRL_C_EVENT || type == CTRL_BREAK_EVENT || type == CTRL_CLOSE_EVENT) {
        g_run = 0;
        if (++g_sig_hits >= 2) ExitProcess(1);
        return TRUE;
    }
    return FALSE;
}
#else
static void on_sig(int s) {
    (void)s;
    g_run = 0;
    if (++g_sig_hits >= 2) _exit(1);
}
#endif

static uint64_t now_ms(void) {
#ifdef _WIN32
    return GetTickCount64();
#else
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000ull + (uint64_t)ts.tv_nsec / 1000000ull;
#endif
}

static void msleep(unsigned ms) {
#ifdef _WIN32
    Sleep(ms);
#else
    usleep(ms * 1000);
#endif
}

static void nonce_hex16(uint64_t n, char out[NONCE_HEX + 1]) {
    static const char *h = "0123456789abcdef";
    for (int i = NONCE_HEX - 1; i >= 0; i--) {
        out[i] = h[n & 15];
        n >>= 4;
    }
    out[NONCE_HEX] = 0;
}

static int meets_target(const unsigned char *hash, int bits) {
    if (bits <= 0) return 1;
    if (bits > 256) bits = 256;
    int full = bits / 8, rem = bits % 8;
    for (int i = 0; i < full; i++) if (hash[i] != 0) return 0;
    if (!rem) return 1;
    return hash[full] < (1 << (8 - rem));
}

/* ---------- SHA-NI (tightest single-path version) ---------- */
static int g_has_sha_ni = -1;
static inline int cpu_has_sha_ni(void) {
    if (g_has_sha_ni >= 0) return g_has_sha_ni;
    unsigned eax, ebx, ecx, edx;
    g_has_sha_ni = (__get_cpuid(7, &eax, &ebx, &ecx, &edx) && (ebx & (1u << 29))) ? 1 : 0;
    return g_has_sha_ni;
}

static inline void sha256_block_ni(const unsigned char block[64], unsigned char out[32]) {
    __m128i STATE0, STATE1, MSG, TMP, MSG0, MSG1, MSG2, MSG3;
    __m128i ABEF_SAVE, CDGH_SAVE;
    const __m128i MASK = _mm_set_epi64x(0x0c0d0e0f08090a0bULL, 0x0405060700010203ULL);

    STATE0 = _mm_set_epi32(0x6a09e667, 0xbb67ae85, 0x3c6ef372, 0xa54ff53a);
    STATE1 = _mm_set_epi32(0x510e527f, 0x9b05688c, 0x1f83d9ab, 0x5be0cd19);
    ABEF_SAVE = STATE0; CDGH_SAVE = STATE1;

    MSG0 = _mm_shuffle_epi8(_mm_loadu_si128((const __m128i*)(block)), MASK);
    MSG  = _mm_add_epi32(MSG0, _mm_set_epi64x(0xE9B5DBA5B5C0FBCFULL, 0x71374491428A2F98ULL));
    STATE1 = _mm_sha256rnds2_epu32(STATE1, STATE0, MSG);
    MSG  = _mm_shuffle_epi32(MSG, 0x0E);
    STATE0 = _mm_sha256rnds2_epu32(STATE0, STATE1, MSG);

    MSG1 = _mm_shuffle_epi8(_mm_loadu_si128((const __m128i*)(block+16)), MASK);
    MSG  = _mm_add_epi32(MSG1, _mm_set_epi64x(0xAB1C5ED5923F82A4ULL, 0x59F111F13956C25BULL));
    STATE1 = _mm_sha256rnds2_epu32(STATE1, STATE0, MSG);
    MSG  = _mm_shuffle_epi32(MSG, 0x0E);
    STATE0 = _mm_sha256rnds2_epu32(STATE0, STATE1, MSG);
    MSG0 = _mm_sha256msg1_epu32(MSG0, MSG1);

    MSG2 = _mm_shuffle_epi8(_mm_loadu_si128((const __m128i*)(block+32)), MASK);
    MSG  = _mm_add_epi32(MSG2, _mm_set_epi64x(0x550C7DC3243185BEULL, 0x12835B01D807AA98ULL));
    STATE1 = _mm_sha256rnds2_epu32(STATE1, STATE0, MSG);
    MSG  = _mm_shuffle_epi32(MSG, 0x0E);
    STATE0 = _mm_sha256rnds2_epu32(STATE0, STATE1, MSG);
    MSG1 = _mm_sha256msg1_epu32(MSG1, MSG2);

    MSG3 = _mm_shuffle_epi8(_mm_loadu_si128((const __m128i*)(block+48)), MASK);
    MSG  = _mm_add_epi32(MSG3, _mm_set_epi64x(0xC19BF1749BDC06A7ULL, 0x80DEB1FE72BE5D74ULL));
    STATE1 = _mm_sha256rnds2_epu32(STATE1, STATE0, MSG);
    TMP  = _mm_alignr_epi8(MSG3, MSG2, 4);
    MSG0 = _mm_add_epi32(MSG0, TMP);
    MSG0 = _mm_sha256msg2_epu32(MSG0, MSG3);
    MSG  = _mm_shuffle_epi32(MSG, 0x0E);
    STATE0 = _mm_sha256rnds2_epu32(STATE0, STATE1, MSG);
    MSG2 = _mm_sha256msg1_epu32(MSG2, MSG3);

    MSG  = _mm_add_epi32(MSG0, _mm_set_epi64x(0x240CA1CC0FC19DC6ULL, 0xEFBE4786E49B69C1ULL));
    STATE1 = _mm_sha256rnds2_epu32(STATE1, STATE0, MSG);
    TMP  = _mm_alignr_epi8(MSG0, MSG3, 4);
    MSG1 = _mm_add_epi32(MSG1, TMP);
    MSG1 = _mm_sha256msg2_epu32(MSG1, MSG0);
    MSG  = _mm_shuffle_epi32(MSG, 0x0E);
    STATE0 = _mm_sha256rnds2_epu32(STATE0, STATE1, MSG);
    MSG3 = _mm_sha256msg1_epu32(MSG3, MSG0);

    MSG  = _mm_add_epi32(MSG1, _mm_set_epi64x(0x76F988DA5CB0A9DCULL, 0x4A7484AA2DE92C6FULL));
    STATE1 = _mm_sha256rnds2_epu32(STATE1, STATE0, MSG);
    TMP  = _mm_alignr_epi8(MSG1, MSG0, 4);
    MSG2 = _mm_add_epi32(MSG2, TMP);
    MSG2 = _mm_sha256msg2_epu32(MSG2, MSG1);
    MSG  = _mm_shuffle_epi32(MSG, 0x0E);
    STATE0 = _mm_sha256rnds2_epu32(STATE0, STATE1, MSG);
    MSG0 = _mm_sha256msg1_epu32(MSG0, MSG1);

    MSG  = _mm_add_epi32(MSG2, _mm_set_epi64x(0xBF597FC7B00327C8ULL, 0xA831C66D983E5152ULL));
    STATE1 = _mm_sha256rnds2_epu32(STATE1, STATE0, MSG);
    TMP  = _mm_alignr_epi8(MSG2, MSG1, 4);
    MSG3 = _mm_add_epi32(MSG3, TMP);
    MSG3 = _mm_sha256msg2_epu32(MSG3, MSG2);
    MSG  = _mm_shuffle_epi32(MSG, 0x0E);
    STATE0 = _mm_sha256rnds2_epu32(STATE0, STATE1, MSG);
    MSG1 = _mm_sha256msg1_epu32(MSG1, MSG2);

    MSG  = _mm_add_epi32(MSG3, _mm_set_epi64x(0x1429296706CA6351ULL, 0xD5A79147C6E00BF3ULL));
    STATE1 = _mm_sha256rnds2_epu32(STATE1, STATE0, MSG);
    TMP  = _mm_alignr_epi8(MSG3, MSG2, 4);
    MSG0 = _mm_add_epi32(MSG0, TMP);
    MSG0 = _mm_sha256msg2_epu32(MSG0, MSG3);
    MSG  = _mm_shuffle_epi32(MSG, 0x0E);
    STATE0 = _mm_sha256rnds2_epu32(STATE0, STATE1, MSG);
    MSG2 = _mm_sha256msg1_epu32(MSG2, MSG3);

    MSG  = _mm_add_epi32(MSG0, _mm_set_epi64x(0x2e1b213827B70A85ULL, 0x19a4c1161e376c08ULL));
    STATE1 = _mm_sha256rnds2_epu32(STATE1, STATE0, MSG);
    TMP  = _mm_alignr_epi8(MSG0, MSG3, 4);
    MSG1 = _mm_add_epi32(MSG1, TMP);
    MSG1 = _mm_sha256msg2_epu32(MSG1, MSG0);
    MSG  = _mm_shuffle_epi32(MSG, 0x0E);
    STATE0 = _mm_sha256rnds2_epu32(STATE0, STATE1, MSG);
    MSG3 = _mm_sha256msg1_epu32(MSG3, MSG0);

    MSG  = _mm_add_epi32(MSG1, _mm_set_epi64x(0x53380d134D2C6DFCULL, 0x4ed8aa4a391c0cb3ULL));
    STATE1 = _mm_sha256rnds2_epu32(STATE1, STATE0, MSG);
    TMP  = _mm_alignr_epi8(MSG1, MSG0, 4);
    MSG2 = _mm_add_epi32(MSG2, TMP);
    MSG2 = _mm_sha256msg2_epu32(MSG2, MSG1);
    MSG  = _mm_shuffle_epi32(MSG, 0x0E);
    STATE0 = _mm_sha256rnds2_epu32(STATE0, STATE1, MSG);
    MSG0 = _mm_sha256msg1_epu32(MSG0, MSG1);

    MSG  = _mm_add_epi32(MSG2, _mm_set_epi64x(0x92722c8581c2c92eULL, 0x766a0abb650a7354ULL));
    STATE1 = _mm_sha256rnds2_epu32(STATE1, STATE0, MSG);
    TMP  = _mm_alignr_epi8(MSG2, MSG1, 4);
    MSG3 = _mm_add_epi32(MSG3, TMP);
    MSG3 = _mm_sha256msg2_epu32(MSG3, MSG2);
    MSG  = _mm_shuffle_epi32(MSG, 0x0E);
    STATE0 = _mm_sha256rnds2_epu32(STATE0, STATE1, MSG);
    MSG1 = _mm_sha256msg1_epu32(MSG1, MSG2);

    MSG  = _mm_add_epi32(MSG3, _mm_set_epi64x(0xc6e00bf3a81a664bULL, 0xa2bfe8a14e853b5cULL));
    STATE1 = _mm_sha256rnds2_epu32(STATE1, STATE0, MSG);
    TMP  = _mm_alignr_epi8(MSG3, MSG2, 4);
    MSG0 = _mm_add_epi32(MSG0, TMP);
    MSG0 = _mm_sha256msg2_epu32(MSG0, MSG3);
    MSG  = _mm_shuffle_epi32(MSG, 0x0E);
    STATE0 = _mm_sha256rnds2_epu32(STATE0, STATE1, MSG);
    MSG2 = _mm_sha256msg1_epu32(MSG2, MSG3);

    MSG  = _mm_add_epi32(MSG0, _mm_set_epi64x(0x106aa070f40e3585ULL, 0xd6990624d192e819ULL));
    STATE1 = _mm_sha256rnds2_epu32(STATE1, STATE0, MSG);
    TMP  = _mm_alignr_epi8(MSG0, MSG3, 4);
    MSG1 = _mm_add_epi32(MSG1, TMP);
    MSG1 = _mm_sha256msg2_epu32(MSG1, MSG0);
    MSG  = _mm_shuffle_epi32(MSG, 0x0E);
    STATE0 = _mm_sha256rnds2_epu32(STATE0, STATE1, MSG);
    MSG3 = _mm_sha256msg1_epu32(MSG3, MSG0);

    MSG  = _mm_add_epi32(MSG1, _mm_set_epi64x(0x19a4c11606ca6351ULL, 0x1e376c08142829abULL));
    STATE1 = _mm_sha256rnds2_epu32(STATE1, STATE0, MSG);
    TMP  = _mm_alignr_epi8(MSG1, MSG0, 4);
    MSG2 = _mm_add_epi32(MSG2, TMP);
    MSG2 = _mm_sha256msg2_epu32(MSG2, MSG1);
    MSG  = _mm_shuffle_epi32(MSG, 0x0E);
    STATE0 = _mm_sha256rnds2_epu32(STATE0, STATE1, MSG);

    MSG  = _mm_add_epi32(MSG2, _mm_set_epi64x(0x748f82ee5cb0a9dcULL, 0x4a7484aa2748774cULL));
    STATE1 = _mm_sha256rnds2_epu32(STATE1, STATE0, MSG);
    TMP  = _mm_alignr_epi8(MSG2, MSG1, 4);
    MSG3 = _mm_add_epi32(MSG3, TMP);
    MSG3 = _mm_sha256msg2_epu32(MSG3, MSG2);
    MSG  = _mm_shuffle_epi32(MSG, 0x0E);
    STATE0 = _mm_sha256rnds2_epu32(STATE0, STATE1, MSG);

    MSG  = _mm_add_epi32(MSG3, _mm_set_epi64x(0x6c44198c4A3BF1A5ULL, 0x5cb0a9dc431D67C4ULL));
    STATE1 = _mm_sha256rnds2_epu32(STATE1, STATE0, MSG);
    MSG  = _mm_shuffle_epi32(MSG, 0x0E);
    STATE0 = _mm_sha256rnds2_epu32(STATE0, STATE1, MSG);

    STATE0 = _mm_add_epi32(STATE0, ABEF_SAVE);
    STATE1 = _mm_add_epi32(STATE1, CDGH_SAVE);

    TMP    = _mm_shuffle_epi32(STATE0, 0x1B);
    STATE1 = _mm_shuffle_epi32(STATE1, 0xB1);
    STATE0 = _mm_blend_epi16(TMP, STATE1, 0xF0);
    STATE1 = _mm_alignr_epi8(STATE1, TMP, 8);
    _mm_storeu_si128((__m128i*)out, STATE0);
    _mm_storeu_si128((__m128i*)(out+16), STATE1);

    for (int i = 0; i < 32; i += 4) {
        unsigned char t = out[i]; out[i] = out[i+3]; out[i+3] = t;
        t = out[i+1]; out[i+1] = out[i+2]; out[i+2] = t;
    }
}

static inline void sha256_fast_ni(const unsigned char *msg, size_t len, unsigned char out[32]) {
    unsigned char block[64] __attribute__((aligned(16)));
    memset(block, 0, 64);
    memcpy(block, msg, len);
    block[len] = 0x80;
    uint64_t bits = (uint64_t)len * 8;
    block[63] = (unsigned char)bits;
    block[62] = (unsigned char)(bits >> 8);
    block[61] = (unsigned char)(bits >> 16);
    block[60] = (unsigned char)(bits >> 24);
    block[59] = (unsigned char)(bits >> 32);
    block[58] = (unsigned char)(bits >> 40);
    block[57] = (unsigned char)(bits >> 48);
    block[56] = (unsigned char)(bits >> 56);
    sha256_block_ni(block, out);
}

static void gnfp_hash(const char *pre, size_t pre_len, const char nonce[16], unsigned char out[32]) {
    if (cpu_has_sha_ni() && pre_len < 40) {
        unsigned char acc[32], buf[128];
        size_t n;

        n = 0;
        memcpy(buf+n, "GNFPHash-v1", 11); n += 11;
        memcpy(buf+n, "GNFPHash",  8); n +=  8;
        memcpy(buf+n, pre, pre_len); n += pre_len;
        memcpy(buf+n, nonce, 16); n += 16;
        sha256_fast_ni(buf, n, acc);

        for (int r = 0; r < 8; r++) {
            n = 0;
            memcpy(buf+n, acc, 32); n += 32;
            memcpy(buf+n, "GNFPHash-v1", 11); n += 11;
            buf[n++] = (unsigned char)('0' + r);
            memcpy(buf+n, pre, pre_len); n += pre_len;
            memcpy(buf+n, nonce, 16); n += 16;
            sha256_fast_ni(buf, n, acc);
        }
        memcpy(out, acc, 32);
        return;
    }

    /* OpenSSL fallback */
    unsigned char acc[32];
    SHA256_CTX c;
    SHA256_Init(&c);
    SHA256_Update(&c, "GNFPHash-v1", 11);
    SHA256_Update(&c, "GNFPHash", 8);
    SHA256_Update(&c, pre, pre_len);
    SHA256_Update(&c, nonce, 16);
    SHA256_Final(acc, &c);
    for (int r = 0; r < 8; r++) {
        char tag = (char)('0' + r);
        SHA256_Init(&c);
        SHA256_Update(&c, acc, 32);
        SHA256_Update(&c, "GNFPHash-v1", 11);
        SHA256_Update(&c, &tag, 1);
        SHA256_Update(&c, pre, pre_len);
        SHA256_Update(&c, nonce, 16);
        SHA256_Final(acc, &c);
    }
    memcpy(out, acc, 32);
}

/* ---------- hash_to_hex + JSON helpers ---------- */
static void hash_to_hex(const unsigned char *h, char out[65]) {
    static const char *d = "0123456789abcdef";
    for (int i = 0; i < 32; i++) {
        out[i*2]     = d[h[i] >> 4];
        out[i*2 + 1] = d[h[i] & 15];
    }
    out[64] = 0;
}

static int json_str(const char *line, const char *key, char *dst, size_t cap) {
    char pat[80];
    snprintf(pat, sizeof(pat), "\"%s\"", key);
    const char *p = strstr(line, pat);
    if (!p) return 0;
    p = strchr(p + strlen(pat), ':');
    if (!p) return 0;
    p++;
    while (*p == ' ' || *p == '\t') p++;
    if (*p != '"') return 0;
    p++;
    size_t n = 0;
    while (*p && *p != '"' && n + 1 < cap) {
        if (*p == '\\' && p[1]) p++;
        dst[n++] = *p++;
    }
    dst[n] = 0;
    return 1;
}

static int json_int(const char *line, const char *key, long *out) {
    char pat[80];
    snprintf(pat, sizeof(pat), "\"%s\"", key);
    const char *p = strstr(line, pat);
    if (!p) return 0;
    p = strchr(p + strlen(pat), ':');
    if (!p) return 0;
    p++;
    while (*p == ' ' || *p == '\t') p++;
    char *end = NULL;
    long v = strtol(p, &end, 10);
    if (end == p) return 0;
    *out = v;
    return 1;
}

/* ---------- share queue ---------- */
static int share_seen(const char *job_id, const char *nonce) {
    for (int i = 0, idx = g_q_head; i < g_q_n; i++, idx = (idx + 1) % SHARE_Q)
        if (strcmp(g_q[idx].job_id, job_id) == 0 &&
            memcmp(g_q[idx].nonce, nonce, NONCE_HEX) == 0)
            return 1;
    return 0;
}

static int share_push(const char *job_id, const char *nonce, int bits, int is_fee) {
    int ok = 0;
    pthread_mutex_lock(&g_q_mu);
    if (g_q_n < SHARE_Q && job_id && job_id[0] && nonce && !share_seen(job_id, nonce)) {
        share_t *s = &g_q[g_q_tail];
        snprintf(s->job_id, sizeof(s->job_id), "%s", job_id);
        memcpy(s->nonce, nonce, NONCE_HEX);
        s->nonce[NONCE_HEX] = 0;
        s->bits = bits;
        s->is_fee = is_fee;
        g_q_tail = (g_q_tail + 1) % SHARE_Q;
        g_q_n++;
        ok = 1;
    }
    pthread_mutex_unlock(&g_q_mu);
    if (ok) __sync_fetch_and_add(&g_shares_pushed, 1);
    else    __sync_fetch_and_add(&g_shares_dropped, 1);
    return ok;
}

static int share_pop(share_t *out) {
    pthread_mutex_lock(&g_q_mu);
    if (g_q_n <= 0) { pthread_mutex_unlock(&g_q_mu); return 0; }
    *out = g_q[g_q_head];
    g_q_head = (g_q_head + 1) % SHARE_Q;
    g_q_n--;
    pthread_mutex_unlock(&g_q_mu);
    return 1;
}

static void share_drop_job_mismatch(const char *live_id) {
    pthread_mutex_lock(&g_q_mu);
    int n = g_q_n, head = g_q_head;
    g_q_head = g_q_tail = g_q_n = 0;
    for (int i = 0; i < n; i++) {
        share_t *s = &g_q[(head + i) % SHARE_Q];
        if (live_id && strcmp(s->job_id, live_id) == 0) {
            g_q[g_q_tail] = *s;
            g_q_tail = (g_q_tail + 1) % SHARE_Q;
            g_q_n++;
        }
    }
    pthread_mutex_unlock(&g_q_mu);
}

/* ---------- inflight (with bits for proven rate) ---------- */
static void inflight_release_timeouts(void) {
    uint64_t now = now_ms();
    while (g_if_n > 0 && now - g_inflight_ts[0] > INFLIGHT_TO_MS) {
        for (int i = 0; i < g_if_n - 1; i++) {
            g_inflight_ts[i]   = g_inflight_ts[i + 1];
            g_inflight_fee[i]  = g_inflight_fee[i + 1];
            g_inflight_bits[i] = g_inflight_bits[i + 1];
        }
        g_if_n--;
        g_inflight--;
    }
}

static void inflight_add(int is_fee, int bits) {
    if (g_if_n >= MAX_IN_FLIGHT) return;
    g_inflight_ts[g_if_n]   = now_ms();
    g_inflight_fee[g_if_n]  = is_fee;
    g_inflight_bits[g_if_n] = bits;
    g_if_n++;
    g_inflight++;
}

static void inflight_ack(int *is_fee, int *bits) {
    if (g_if_n <= 0) {
        if (is_fee) *is_fee = 0;
        if (bits)   *bits   = 0;
        return;
    }
    if (is_fee) *is_fee = g_inflight_fee[0];
    if (bits)   *bits   = g_inflight_bits[0];
    for (int i = 0; i < g_if_n - 1; i++) {
        g_inflight_ts[i]   = g_inflight_ts[i + 1];
        g_inflight_fee[i]  = g_inflight_fee[i + 1];
        g_inflight_bits[i] = g_inflight_bits[i + 1];
    }
    g_if_n--;
    g_inflight--;
}

/* ---------- print_status (stable proven) ---------- */
static void print_status(const cfg_t *cfg, int extra) {
    uint64_t t = now_ms();
    uint64_t dt = t > g_t0_ms ? t - g_t0_ms : 1;
    double sec = dt / 1000.0;
    uint64_t hc = g_hash_calls, sf = g_shares_found;
    uint64_t ss = g_shares_submitted, ac = g_accepts, rj = g_rejects;
    uint64_t imp = g_implausible, dr = g_shares_dropped;
    uint64_t f16 = g_shares_found16, fa = g_fee_accepts;
    int bits = g_last_bits > 0 ? g_last_bits : 14;
    double call = hc / sec;
    double find = sf / sec;

    uint64_t proven_dt = g_first_accept_ms
        ? (t > g_first_accept_ms ? t - g_first_accept_ms : 1)
        : dt;
    double proven = g_proven_work / (proven_dt / 1000.0);

    int qn, inf;
    pthread_mutex_lock(&g_q_mu);
    qn = g_q_n;
    inf = g_inflight;
    pthread_mutex_unlock(&g_q_mu);

    printf(
        "call=%.0f H/s proven=%.0f H/s find=%.2f/s sub=%.2f/s "
        "accepted=%llu rejected=%llu implausible=%llu bits=%d threads=%d "
        "batch=%d flush=%llu q=%d inflight=%d dropped=%llu fee_acc=%llu found14=%llu found16=%llu\n",
        call, proven, find, ss / sec,
        (unsigned long long)ac, (unsigned long long)rj, (unsigned long long)imp,
        bits, cfg->threads, g_batch_size, (unsigned long long)g_local_flush,
        qn, inf, (unsigned long long)dr, (unsigned long long)fa,
        (unsigned long long)sf, (unsigned long long)f16);
    if (extra) {
        printf("  hashes=%llu pushed=%llu submitted=%llu fee_sub=%llu worker=%s fee_worker=%s\n",
               (unsigned long long)hc, (unsigned long long)g_shares_pushed,
               (unsigned long long)ss, (unsigned long long)g_fee_submitted,
               cfg->worker, cfg->fee_worker);
    }
    fflush(stdout);
}

/* ---------- network helpers ---------- */
static int net_send_line_fd(
#ifdef _WIN32
    SOCKET fd
#else
    int fd
#endif
, SSL *ssl, const char *msg) {
    size_t len = strlen(msg);
    char line[2048];
    if (len + 2 >= sizeof(line)) return -1;
    memcpy(line, msg, len);
    line[len] = '\n';
    line[len+1] = 0;
    len += 1;

    pthread_mutex_lock(&g_net_mu);
    int n = 0;
    if (ssl) n = SSL_write(ssl, line, (int)len);
    else {
#ifdef _WIN32
        n = send(fd, line, (int)len, 0);
#else
        n = (int)write(fd, line, len);
#endif
    }
    pthread_mutex_unlock(&g_net_mu);
    return (n == (int)len) ? 0 : -1;
}

static void ident_json(char *dst, size_t cap, const cfg_t *cfg, const char *login) {
    snprintf(dst, cap,
             "\"id\":\"%s\",\"user\":\"%s\",\"worker\":\"%s\","
             "\"client\":\"%s\",\"version\":\"%s\",\"algo\":\"%s\","
             "\"threads\":%d,\"cpuCores\":%d,\"cpuThreads\":%d,"
             "\"platform\":\"%s\",\"arch\":\"%s\"",
             login, cfg->address, cfg->worker,
             CLIENT, VERSION, "GNFPHash",
             cfg->threads, cfg->cpu_cores, cfg->cpu_threads,
             cfg->platform, cfg->arch);
}

static int net_connect_fd(const cfg_t *cfg, int is_fee) {
#ifdef _WIN32
    SOCKET *pfd = is_fee ? &g_fee_fd : &g_fd;
    SSL **pssl = is_fee ? &g_fee_ssl : &g_ssl;
#else
    int *pfd = is_fee ? &g_fee_fd : &g_fd;
    SSL **pssl = is_fee ? &g_fee_ssl : &g_ssl;
#endif
    if (*pfd != 
#ifdef _WIN32
        INVALID_SOCKET
#else
        -1
#endif
    ) return 0;

    struct addrinfo hints = {0}, *res = NULL;
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    char portstr[16];
    snprintf(portstr, sizeof(portstr), "%d", cfg->port);
    if (getaddrinfo(cfg->host, portstr, &hints, &res) != 0) return -1;

#ifdef _WIN32
    SOCKET fd = socket(res->ai_family, res->ai_socktype, res->ai_protocol);
    if (fd == INVALID_SOCKET) { freeaddrinfo(res); return -1; }
#else
    int fd = socket(res->ai_family, res->ai_socktype, res->ai_protocol);
    if (fd < 0) { freeaddrinfo(res); return -1; }
#endif

    if (connect(fd, res->ai_addr, (int)res->ai_addrlen) != 0) {
        close(fd);
        freeaddrinfo(res);
        return -1;
    }
    freeaddrinfo(res);

    if (cfg->tls) {
        if (!g_ctx) {
            SSL_library_init();
            OpenSSL_add_all_algorithms();
            g_ctx = SSL_CTX_new(TLS_client_method());
            if (!g_ctx) { close(fd); return -1; }
            SSL_CTX_set_verify(g_ctx, SSL_VERIFY_NONE, NULL);
        }
        SSL *ssl = SSL_new(g_ctx);
        SSL_set_fd(ssl, (int)fd);
        if (SSL_connect(ssl) != 1) {
            SSL_free(ssl);
            close(fd);
            return -1;
        }
        *pssl = ssl;
    }
    *pfd = fd;
    return 0;
}

static int net_connect_main(const cfg_t *cfg) { return net_connect_fd(cfg, 0); }
static int net_connect_fee(const cfg_t *cfg)  { return net_connect_fd(cfg, 1); }

static void net_close_fd(int is_fee) {
#ifdef _WIN32
    SOCKET *pfd = is_fee ? &g_fee_fd : &g_fd;
    SSL **pssl = is_fee ? &g_fee_ssl : &g_ssl;
#else
    int *pfd = is_fee ? &g_fee_fd : &g_fd;
    SSL **pssl = is_fee ? &g_fee_ssl : &g_ssl;
#endif
    if (*pssl) { SSL_shutdown(*pssl); SSL_free(*pssl); *pssl = NULL; }
    if (*pfd != 
#ifdef _WIN32
        INVALID_SOCKET
#else
        -1
#endif
    ) { close(*pfd); *pfd = 
#ifdef _WIN32
        INVALID_SOCKET
#else
        -1
#endif
    ; }
}
static void net_close_main(void) { net_close_fd(0); }
static void net_close_fee(void)  { net_close_fd(1); }

static int send_login_fd(
#ifdef _WIN32
    SOCKET fd
#else
    int fd
#endif
, SSL *ssl, const cfg_t *cfg, const char *login) {
    char id[768], msg[1600];
    ident_json(id, sizeof(id), cfg, login);
    snprintf(msg, sizeof(msg),
             "{\"method\":\"login\",%s,\"jsonrpc\":\"2.0\"}", id);
    return net_send_line_fd(fd, ssl, msg);
}

static int send_stats(const cfg_t *cfg, double hashrate, uint64_t hashes,
                      const char *job_id, int height) {
    char id[768], msg[1600];
    ident_json(id, sizeof(id), cfg, cfg->user);
    snprintf(msg, sizeof(msg),
             "{\"method\":\"stats\",%s,\"hashrate\":%.0f,\"hashes\":%llu,"
             "\"jobId\":\"%s\",\"height\":%d,\"jsonrpc\":\"2.0\"}",
             id, hashrate, (unsigned long long)hashes,
             job_id && job_id[0] ? job_id : "", height);
    return net_send_line_fd(g_fd, g_ssl, msg);
}

static int send_submit(const cfg_t *cfg, const share_t *s) {
    char id[768], msg[1600];
    const char *login = s->is_fee ? cfg->fee_user : cfg->user;
#ifdef _WIN32
    SOCKET fd = s->is_fee ? g_fee_fd : g_fd;
#else
    int fd = s->is_fee ? g_fee_fd : g_fd;
#endif
    SSL *ssl = s->is_fee ? g_fee_ssl : g_ssl;
#ifdef _WIN32
    if (fd == INVALID_SOCKET) return -1;
#else
    if (fd < 0) return -1;
#endif
    ident_json(id, sizeof(id), cfg, login);
    snprintf(msg, sizeof(msg),
             "{\"method\":\"submit\",%s,\"id\":\"%s\",\"nonce\":\"%s\","
             "\"output\":\"\",\"jobId\":\"%s\",\"jsonrpc\":\"2.0\"}",
             id, s->job_id, s->nonce, s->job_id);
    return net_send_line_fd(fd, ssl, msg);
}

static int classify_reply(const char *line, char *why, size_t why_cap) {
    char desc[160] = {0};
    json_str(line, "description", desc, sizeof(desc));
    if (!desc[0]) json_str(line, "result", desc, sizeof(desc));
    if (!desc[0]) json_str(line, "error", desc, sizeof(desc));
    for (char *p = desc; *p; p++) *p = (char)tolower((unsigned char)*p);
    long code = 0;
    json_int(line, "code", &code);
    if (strstr(line, "\"formed\":true") || strstr(desc, "block found")) {
        snprintf(why, why_cap, "%s", desc[0] ? desc : "block");
        return 3;
    }
    if (strstr(desc, "accepted") || code == 1) {
        snprintf(why, why_cap, "%s", desc[0] ? desc : "accepted");
        return 1;
    }
    if (strstr(desc, "login")) return 4;
    if (strstr(desc, "stats")) return 5;
    if (desc[0] && (strstr(desc, "reject") || code < 0 || strstr(line, "\"error\""))) {
        snprintf(why, why_cap, "%s", desc);
        return 2;
    }
    if (strstr(line, "\"error\"") && !strstr(line, "\"error\":null")) {
        snprintf(why, why_cap, "%s", desc[0] ? desc : "error");
        return 2;
    }
    return 0;
}

static void apply_job_line(const char *line) {
    job_t j = {0};
    if (!json_str(line, "jobId", j.job_id, sizeof(j.job_id)))
        json_str(line, "id", j.job_id, sizeof(j.job_id));
    if (!json_str(line, "input", j.pre, sizeof(j.pre)))
        json_str(line, "preWork", j.pre, sizeof(j.pre));
    j.pre_len = strlen(j.pre);
    if (j.pre_len > HASH_FIELD_MAX) {
        j.pre_len = HASH_FIELD_MAX;
        j.pre[HASH_FIELD_MAX] = 0;
    }
    long bits = 14, height = 0;
    if (!json_int(line, "difficulty", &bits))
        json_int(line, "bits", &bits);
    if (bits < 1) bits = 1;
    if (bits > 256) bits = 256;
    json_int(line, "height", &height);
    j.bits = (int)bits;
    j.height = (int)height;
    if (!j.job_id[0] || !j.pre_len) return;
    pthread_mutex_lock(&g_job_mu);
    j.gen = g_job.gen + 1;
    g_job = j;
    pthread_mutex_unlock(&g_job_mu);
    g_last_bits = j.bits;
    share_drop_job_mismatch(j.job_id);
    printf("job %s height=%d bits=%d\n", j.job_id, j.height, j.bits);
    fflush(stdout);
}

static void flush_submits(const cfg_t *cfg) {
    if (now_ms() < g_backoff_until) return;
    inflight_release_timeouts();
    for (;;) {
        pthread_mutex_lock(&g_q_mu);
        int room = g_inflight < MAX_IN_FLIGHT;
        int have = g_q_n > 0;
        pthread_mutex_unlock(&g_q_mu);
        if (!room || !have) break;
        share_t s;
        if (!share_pop(&s)) break;
        pthread_mutex_lock(&g_job_mu);
        int live = g_job.job_id[0] && strcmp(g_job.job_id, s.job_id) == 0;
        pthread_mutex_unlock(&g_job_mu);
        if (!live) continue;
        if (send_submit(cfg, &s) != 0) {
            share_push(s.job_id, s.nonce, s.bits, s.is_fee);
            break;
        }
        inflight_add(s.is_fee, s.bits);
        __sync_fetch_and_add(&g_shares_submitted, 1);
        if (s.is_fee) __sync_fetch_and_add(&g_fee_submitted, 1);
    }
}

static int handle_line(const cfg_t *cfg, const char *line) {
    if (strstr(line, "\"method\":\"job\"") || strstr(line, "\"input\"") || strstr(line, "\"preWork\"")) {
        if (strstr(line, "\"method\":\"submit\"")) return 0;
        apply_job_line(line);
        return 0;
    }
    char why[160] = {0};
    int k = classify_reply(line, why, sizeof(why));
    if (k == 1 || k == 2 || k == 3) {
        int fee = 0, share_bits = 0;
        inflight_ack(&fee, &share_bits);
        if (k == 1 || k == 3) {
            __sync_fetch_and_add(&g_accepts, 1);
            if (!g_first_accept_ms) g_first_accept_ms = now_ms();
            if (fee) __sync_fetch_and_add(&g_fee_accepts, 1);
            if (share_bits > 0 && share_bits < 63)
                g_proven_work += (double)(1ull << share_bits);
            else if (share_bits >= 63)
                g_proven_work += ldexp(1.0, share_bits);
            if (k == 3) {
                __sync_fetch_and_add(&g_blocks, 1);
                printf("BLOCK FOUND %s\n", why);
            } else if (g_accept_print_left > 0) {
                g_accept_print_left--;
                printf("accepted share %s%s\n", why, fee ? " [fee]" : "");
            }
        } else {
            __sync_fetch_and_add(&g_rejects, 1);
            if (why[0] && strstr(why, "implausible")) {
                __sync_fetch_and_add(&g_implausible, 1);
                g_backoff_until = now_ms() + 4000;
                printf("rejected share implausible_rate — backoff 4s\n");
            } else {
                printf("rejected share %s\n", why[0] ? why : "rejected");
            }
        }
        fflush(stdout);
        flush_submits(cfg);
    } else if (k == 4) {
        printf("pool login: %s\n", why[0] ? why : line);
        fflush(stdout);
    }
    return 0;
}

static int pump_reads_fd(
#ifdef _WIN32
    SOCKET fd
#else
    int fd
#endif
, SSL *ssl, const cfg_t *cfg) {
    static char buf[16384];
    static size_t used = 0;
#ifdef _WIN32
    if (fd == INVALID_SOCKET) return -1;
#else
    if (fd < 0) return -1;
#endif
    fd_set rf;
    FD_ZERO(&rf);
    FD_SET(fd, &rf);
    struct timeval tv = {0, 50000};
    int sel = select((int)fd + 1, &rf, NULL, NULL, &tv);
    if (sel < 0) return -1;
    if (sel > 0 && FD_ISSET(fd, &rf)) {
        char tmp[4096];
        pthread_mutex_lock(&g_net_mu);
        int n = 0;
        if (ssl) n = SSL_read(ssl, tmp, (int)sizeof(tmp));
        else {
#ifdef _WIN32
            n = recv(fd, tmp, (int)sizeof(tmp), 0);
#else
            n = (int)read(fd, tmp, sizeof(tmp));
#endif
        }
        pthread_mutex_unlock(&g_net_mu);
        if (n <= 0) {
            if (ssl) {
                int e = SSL_get_error(ssl, n);
                if (e == SSL_ERROR_WANT_READ || e == SSL_ERROR_WANT_WRITE) return 0;
            } else {
#ifdef _WIN32
                if (WSAGetLastError() == WSAEWOULDBLOCK) return 0;
#else
                if (errno == EAGAIN || errno == EWOULDBLOCK) return 0;
#endif
            }
            return -1;
        }
        if (used + (size_t)n >= sizeof(buf)) used = 0;
        memcpy(buf + used, tmp, (size_t)n);
        used += (size_t)n;
        buf[used] = 0;
        char *start = buf;
        char *nl;
        while ((nl = (char*)memchr(start, '\n', (size_t)(buf + used - start))) != NULL) {
            *nl = 0;
            if (start[0]) handle_line(cfg, start);
            start = nl + 1;
        }
        size_t left = (size_t)(buf + used - start);
        memmove(buf, start, left);
        used = left;
    }
    return 0;
}

static void session_loop(const cfg_t *cfg) {
    while (g_run) {
        printf("connecting main %s://%s:%d\n", cfg->tls ? "tls" : "tcp", cfg->host, cfg->port);
        fflush(stdout);
        if (net_connect_main(cfg) != 0) {
            msleep(2000);
            continue;
        }
        if (send_login_fd(g_fd, g_ssl, cfg, cfg->user) != 0) {
            net_close_main();
            msleep(2000);
            continue;
        }

        if (net_connect_fee(cfg) == 0) {
            send_login_fd(g_fee_fd, g_fee_ssl, cfg, cfg->fee_user);
            printf("fee connection ready → %s\n", cfg->fee_user);
        }

        uint64_t last_stats = now_ms();
        uint64_t last_status = now_ms();
        while (g_run) {
            if (pump_reads_fd(g_fd, g_ssl, cfg) != 0) break;
#ifdef _WIN32
            if (g_fee_fd != INVALID_SOCKET)
#else
            if (g_fee_fd >= 0)
#endif
                pump_reads_fd(g_fee_fd, g_fee_ssl, cfg);
            flush_submits(cfg);

            uint64_t t = now_ms();
            if (t - last_stats >= STATS_MS) {
                double sec = (t > g_t0_ms ? t - g_t0_ms : 1) / 1000.0;
                char jid[128];
                int height;
                pthread_mutex_lock(&g_job_mu);
                snprintf(jid, sizeof(jid), "%s", g_job.job_id);
                height = g_job.height;
                pthread_mutex_unlock(&g_job_mu);
                send_stats(cfg, g_hash_calls / sec, g_hash_calls, jid, height);
                last_stats = t;
            }
            if (t - last_status >= STATUS_MS) {
                print_status(cfg, 1);
                last_status = t;
            }
        }
        net_close_main();
        net_close_fee();
        printf("reconnect in 2s\n");
        fflush(stdout);
        if (g_run) msleep(2000);
    }
}

/* ---------- worker (max speed, uses runtime tunables) ---------- */
static void *hash_worker(void *arg) {
    int tid = (int)(intptr_t)arg;
    uint64_t n = (uint64_t)tid;
    uint64_t local = 0;
    char nonce[NONCE_HEX + 1];
    unsigned char dig[32];
    job_t job;
    memset(&job, 0, sizeof(job));
    uint64_t last_gen = 0;

    while (g_run) {
        pthread_mutex_lock(&g_job_mu);
        if (g_job.gen != last_gen) {
            job = g_job;
            last_gen = job.gen;
            n = (uint64_t)tid;
        }
        pthread_mutex_unlock(&g_job_mu);

        if (!job.job_id[0] || !job.pre_len) {
            msleep(10);
            continue;
        }

        for (int i = 0; i < g_batch_size && g_run; i++) {
            nonce_hex16(n, nonce);
            gnfp_hash(job.pre, job.pre_len, nonce, dig);
            local++;
            if (meets_target(dig, job.bits)) {
                int is_fee = 0;
                uint64_t sc = __sync_fetch_and_add(&g_share_counter, 1);
                if ((sc % DEV_FEE_EVERY) == 0) is_fee = 1;
                share_push(job.job_id, nonce, job.bits, is_fee);
                __sync_fetch_and_add(&g_shares_found, 1);
                if (job.bits >= 16) __sync_fetch_and_add(&g_shares_found16, 1);
            }
            n += (uint64_t)g_nthreads;
        }

        if (local >= g_local_flush) {
            __sync_fetch_and_add(&g_hash_calls, local);
            local = 0;
        }
    }
    if (local) __sync_fetch_and_add(&g_hash_calls, local);
    return NULL;
}

/* ---------- inventory / fee / parse / selftest / bench / usage / main ---------- */
static int valid_addr(const char *s) {
    if (strncmp(s, "gnfp1", 5) != 0) return 0;
    size_t n = strlen(s);
    if (n < 25 || n > 85) return 0;
    for (size_t i = 5; i < n; i++) {
        char c = s[i];
        if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'z'))) return 0;
    }
    return 1;
}

static int valid_worker(const char *s) {
    size_t n = strlen(s);
    if (n < 1 || n > 32) return 0;
    for (size_t i = 0; i < n; i++) {
        char c = s[i];
        if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'z') ||
              (c >= 'A' && c <= 'Z') || c == '_' || c == '-'))
            return 0;
    }
    return 1;
}

static void inventory(cfg_t *cfg) {
#ifdef _WIN32
    SYSTEM_INFO si;
    GetSystemInfo(&si);
    cfg->cpu_threads = (int)si.dwNumberOfProcessors;
    cfg->cpu_cores = cfg->cpu_threads;
    cfg->smt = 1;
#else
    long onln = sysconf(_SC_NPROCESSORS_ONLN);
    if (onln < 1) onln = 1;
    cfg->cpu_threads = (int)onln;
    cfg->cpu_cores = cfg->cpu_threads;
    cfg->smt = 1;
#endif
    cfg->max_threads = cfg->cpu_threads > 256 ? 256 : cfg->cpu_threads;
    snprintf(cfg->platform, sizeof(cfg->platform), 
#ifdef _WIN32
             "windows"
#else
             "linux"
#endif
    );
#if defined(__x86_64__) || defined(_M_X64)
    snprintf(cfg->arch, sizeof(cfg->arch), "x64");
#else
    snprintf(cfg->arch, sizeof(cfg->arch), "unknown");
#endif
}

static void setup_fee(cfg_t *cfg) {
    size_t alen = strlen(cfg->address);
    const char *tail = cfg->address + (alen > 6 ? alen - 6 : 0);
    char wshort[9];
    size_t wl = strlen(cfg->worker);
    if (wl > 8) wl = 8;
    memcpy(wshort, cfg->worker, wl);
    wshort[wl] = 0;
    for (size_t i = 0; i < wl; i++) {
        char c = wshort[i];
        if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'z') ||
              (c >= 'A' && c <= 'Z') || c == '_' || c == '-'))
            wshort[i] = 'x';
    }
    snprintf(cfg->fee_worker, sizeof(cfg->fee_worker), "f%s_%s", tail, wshort);
    if (!valid_worker(cfg->fee_worker))
        snprintf(cfg->fee_worker, sizeof(cfg->fee_worker), "f%s", tail);
    snprintf(cfg->fee_user, sizeof(cfg->fee_user), "%s.%s", FEE_ADDR, cfg->fee_worker);
}

static int parse_user(cfg_t *cfg, const char *user) {
    char buf[320];
    snprintf(buf, sizeof(buf), "%s", user);
    char *dot = strchr(buf, '.');
    if (dot) {
        *dot = 0;
        snprintf(cfg->address, sizeof(cfg->address), "%s", buf);
        snprintf(cfg->worker, sizeof(cfg->worker), "%s", dot + 1);
    } else {
        snprintf(cfg->address, sizeof(cfg->address), "%s", buf);
        snprintf(cfg->worker, sizeof(cfg->worker), "worker");
    }
    if (!valid_addr(cfg->address) || !valid_worker(cfg->worker)) return 0;
    snprintf(cfg->user, sizeof(cfg->user), "%s.%s", cfg->address, cfg->worker);
    setup_fee(cfg);
    return 1;
}

static int selftest(void) {
    const char *pre = "test-prework";
    char nonce[17] = "0000000000000001";
    unsigned char dig[32];
    char hex[65];
    gnfp_hash(pre, strlen(pre), nonce, dig);
    hash_to_hex(dig, hex);
    const char *expect = "986437c40fee8a876e0ca3f1e58b14fa38785a179f57f98ebbb0fb03102bd4eb";
    if (strcmp(hex, expect) != 0) {
        fprintf(stderr, "selftest FAIL got %s want %s\n", hex, expect);
        return 1;
    }
    printf("selftest ok %s\n", hex);
    return 0;
}

/* ---------- config persistence ---------- */
static void save_best_config(int threads, int batch, uint64_t flush) {
    FILE *f = fopen(BEST_CFG_FILE, "w");
    if (!f) {
        fprintf(stderr, "warning: could not write %s\n", BEST_CFG_FILE);
        return;
    }
    fprintf(f,
            "# auto-generated by --bench – fastest config for this machine\n"
            "threads=%d\n"
            "batch=%d\n"
            "flush=%llu\n",
            threads, batch, (unsigned long long)flush);
    fclose(f);
    printf("best config written to %s\n", BEST_CFG_FILE);
    fflush(stdout);
}

static int load_best_config(cfg_t *cfg, int *threads_from_file) {
    FILE *f = fopen(BEST_CFG_FILE, "r");
    if (!f) return 0;
    char line[128];
    int got = 0;
    *threads_from_file = 0;
    while (fgets(line, sizeof(line), f)) {
        if (line[0] == '#' || line[0] == '\n' || line[0] == '\r') continue;
        if (strncmp(line, "threads=", 8) == 0) {
            cfg->threads = atoi(line + 8);
            *threads_from_file = 1;
            got = 1;
        } else if (strncmp(line, "batch=", 6) == 0) {
            g_batch_size = atoi(line + 6);
            if (g_batch_size < 16) g_batch_size = 16;
            if (g_batch_size > 16384) g_batch_size = 16384;
            got = 1;
        } else if (strncmp(line, "flush=", 6) == 0) {
            g_local_flush = strtoull(line + 6, NULL, 10);
            if (g_local_flush < 1024) g_local_flush = 1024;
            got = 1;
        }
    }
    fclose(f);
    return got;
}

/* ---------- single trial used by the auto-tuner ---------- */
static double run_one_trial(int threads, int batch, uint64_t flush, int seconds) {
    g_nthreads     = threads;
    g_batch_size   = batch;
    g_local_flush  = flush;
    g_run          = 1;
    g_hash_calls   = 0;
    g_shares_found = 0;
    g_shares_found16 = 0;
    g_t0_ms        = now_ms();

    pthread_mutex_lock(&g_job_mu);
    snprintf(g_job.job_id, sizeof(g_job.job_id), "bench");
    snprintf(g_job.pre, sizeof(g_job.pre), "bench-prework");
    g_job.pre_len = strlen(g_job.pre);
    g_job.bits = 14;
    g_job.gen++;
    pthread_mutex_unlock(&g_job_mu);

    pthread_t *th = (pthread_t*)calloc((size_t)threads, sizeof(*th));
    if (!th) return 0.0;
    for (int i = 0; i < threads; i++)
        pthread_create(&th[i], NULL, hash_worker, (void *)(intptr_t)i);

    uint64_t end = now_ms() + (uint64_t)seconds * 1000ull;
    while (now_ms() < end && g_run) msleep(50);

    g_run = 0;
    for (int i = 0; i < threads; i++) pthread_join(th[i], NULL);
    free(th);

    double sec = (now_ms() - g_t0_ms) / 1000.0;
    if (sec < 0.05) sec = 0.05;
    return (double)g_hash_calls / sec;
}

/* ---------- expanded --bench: hierarchical search over decade-era CPU shapes ---------- */
static int bench(int seconds, const cfg_t *cfg) {
    if (seconds < 1) seconds = 1;
    if (seconds > 30) seconds = 30;   /* keep total wall time sane */

    printf("=== GNFPHash auto-bench (%d s per trial) ===\n", seconds);
    printf("CPU: %d threads (reported cores=%d)  SHA-NI=%s\n",
           cfg->max_threads, cfg->cpu_cores,
           cpu_has_sha_ni() ? "yes" : "no");
    fflush(stdout);

    /* candidate lists covering ~2016–2026 hardware */
    int thread_cands[64];
    int n_tc = 0;
    for (int t = 1; t <= cfg->max_threads; t *= 2)
        thread_cands[n_tc++] = t;
    /* also exact core count and full hardware threads (SMT) */
    int extra[] = { cfg->cpu_cores, cfg->max_threads };
    for (int i = 0; i < 2; i++) {
        int e = extra[i];
        int already = 0;
        for (int j = 0; j < n_tc; j++) if (thread_cands[j] == e) already = 1;
        if (!already && e >= 1 && e <= cfg->max_threads)
            thread_cands[n_tc++] = e;
    }

    const int batches[]  = { 64, 128, 256, 512, 1024, 2048, 4096 };
    const int n_batches  = (int)(sizeof(batches)/sizeof(batches[0]));
    const uint64_t flushes[] = { 4096ull, 16384ull, 65536ull, 262144ull, 1048576ull };
    const int n_flushes  = (int)(sizeof(flushes)/sizeof(flushes[0]));

    double best_rate = 0.0;
    int    best_t    = cfg->threads > 0 ? cfg->threads : 8;
    int    best_b    = 512;
    uint64_t best_f  = 65536ull;

    /* Phase 1 – best thread count (fixed default batch/flush) */
    printf("\n[phase 1] searching thread counts …\n");
    for (int i = 0; i < n_tc; i++) {
        int t = thread_cands[i];
        double r = run_one_trial(t, 512, 65536ull, seconds);
        printf("  threads=%-3d  batch=512  flush=65536  → %.0f H/s\n", t, r);
        fflush(stdout);
        if (r > best_rate) {
            best_rate = r;
            best_t = t;
        }
    }
    printf("  → best threads so far: %d (%.0f H/s)\n", best_t, best_rate);

    /* Phase 2 – best batch size with the winning thread count */
    printf("\n[phase 2] searching batch sizes (threads=%d) …\n", best_t);
    for (int i = 0; i < n_batches; i++) {
        int b = batches[i];
        double r = run_one_trial(best_t, b, 65536ull, seconds);
        printf("  threads=%-3d  batch=%-4d  flush=65536  → %.0f H/s\n", best_t, b, r);
        fflush(stdout);
        if (r > best_rate) {
            best_rate = r;
            best_b = b;
        }
    }
    printf("  → best batch so far: %d (%.0f H/s)\n", best_b, best_rate);

    /* Phase 3 – best flush size */
    printf("\n[phase 3] searching flush thresholds (threads=%d batch=%d) …\n",
           best_t, best_b);
    for (int i = 0; i < n_flushes; i++) {
        uint64_t f = flushes[i];
        double r = run_one_trial(best_t, best_b, f, seconds);
        printf("  threads=%-3d  batch=%-4d  flush=%-7llu  → %.0f H/s\n",
               best_t, best_b, (unsigned long long)f, r);
        fflush(stdout);
        if (r > best_rate) {
            best_rate = r;
            best_f = f;
        }
    }

    /* final confirmation run */
    double confirm = run_one_trial(best_t, best_b, best_f, seconds + 1);
    if (confirm > best_rate) best_rate = confirm;

    printf("\n=== BEST CONFIGURATION ===\n");
    printf("threads=%d  batch=%d  flush=%llu  → %.0f H/s\n",
           best_t, best_b, (unsigned long long)best_f, best_rate);
    printf("(this config will be used automatically on the next mining run)\n");
    fflush(stdout);

    /* persist for future launches */
    save_best_config(best_t, best_b, best_f);

    /* leave the globals set so a subsequent mining session (if any) benefits */
    g_nthreads    = best_t;
    g_batch_size  = best_b;
    g_local_flush = best_f;

    return 0;
}

static void usage(void) {
    printf(
        "GNFPHash max hashrate dual-connection miner %s\n"
        "  --user gnfp1ADDR.worker   required for mining\n"
        "  --pool host:port          default %s:%d\n"
        "  --threads N               default 8 (or value from %s)\n"
        "  --notls\n"
        "  --selftest\n"
        "  --bench [SECONDS]         auto-tune threads/batch/flush on this CPU\n"
        "                            (writes %s and uses it next run)\n",
        VERSION, DEFAULT_HOST, DEFAULT_PORT, BEST_CFG_FILE, BEST_CFG_FILE);
}

int main(int argc, char **argv) {
#ifdef _WIN32
    WSADATA wsa;
    if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0) {
        fprintf(stderr, "WSAStartup failed\n");
        return 1;
    }
    SetConsoleCtrlHandler(console_handler, TRUE);
#else
    signal(SIGINT, on_sig);
    signal(SIGTERM, on_sig);
#endif

    cfg_t cfg;
    memset(&cfg, 0, sizeof(cfg));
    snprintf(cfg.host, sizeof(cfg.host), "%s", DEFAULT_HOST);
    cfg.port = DEFAULT_PORT;
    cfg.tls = 1;
    cfg.threads = 8;
    int do_self = 0, do_bench = 0, bench_s = 2;
    int user_set_threads = 0;

    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "--help") || !strcmp(argv[i], "-h")) { usage(); return 0; }
        else if (!strcmp(argv[i], "--selftest")) do_self = 1;
        else if (!strcmp(argv[i], "--bench")) {
            do_bench = 1;
            if (i + 1 < argc && isdigit((unsigned char)argv[i + 1][0]))
                bench_s = atoi(argv[++i]);
        } else if (!strcmp(argv[i], "--notls")) cfg.tls = 0;
        else if (!strcmp(argv[i], "--tls")) cfg.tls = 1;
        else if (!strcmp(argv[i], "--user") && i + 1 < argc) {
            if (!parse_user(&cfg, argv[++i])) {
                fprintf(stderr, "invalid --user\n");
                return 2;
            }
        } else if (!strcmp(argv[i], "--pool") && i + 1 < argc) {
            char *p = argv[++i];
            char *c = strrchr(p, ':');
            if (c) {
                *c = 0;
                snprintf(cfg.host, sizeof(cfg.host), "%s", p);
                cfg.port = atoi(c + 1);
                *c = ':';
            } else {
                snprintf(cfg.host, sizeof(cfg.host), "%s", p);
            }
        } else if (!strcmp(argv[i], "--threads") && i + 1 < argc) {
            cfg.threads = atoi(argv[++i]);
            user_set_threads = 1;
        }
    }

    inventory(&cfg);
    if (cfg.threads < 1) cfg.threads = 1;
    if (cfg.threads > 256) cfg.threads = 256;

    /* load previously discovered optimum (unless user forced --threads) */
    int threads_from_file = 0;
    if (!do_self && !do_bench) {
        if (load_best_config(&cfg, &threads_from_file)) {
            if (user_set_threads) {
                /* keep the command-line value */
            } else if (threads_from_file) {
                /* already applied inside load_best_config */
            }
            if (cfg.threads < 1) cfg.threads = 1;
            if (cfg.threads > cfg.max_threads) cfg.threads = cfg.max_threads;
            printf("loaded %s → threads=%d batch=%d flush=%llu\n",
                   BEST_CFG_FILE, cfg.threads, g_batch_size,
                   (unsigned long long)g_local_flush);
        }
    }

    g_nthreads = cfg.threads;

    if (do_self) return selftest();
    if (do_bench) return bench(bench_s, &cfg);

    if (!cfg.user[0]) {
        usage();
        return 2;
    }

    printf("GNFPHash %s → %s://%s:%d user=%s threads=%d batch=%d flush=%llu\n",
           VERSION, cfg.tls ? "tls" : "tcp", cfg.host, cfg.port,
           cfg.user, cfg.threads, g_batch_size, (unsigned long long)g_local_flush);
    printf("declared fee 5%% dual-connection → %s (worker %s)\n",
           FEE_ADDR, cfg.fee_worker);
    printf("device cpuCores=%d cpuThreads=%d smt=%d maxThreads=%d %s/%s\n",
           cfg.cpu_cores, cfg.cpu_threads, cfg.smt, cfg.max_threads,
           cfg.platform, cfg.arch);
    fflush(stdout);

    if (selftest() != 0) return 3;

    g_t0_ms = now_ms();
    pthread_t *th = (pthread_t*)calloc((size_t)cfg.threads, sizeof(*th));
    if (!th) return 1;
    for (int i = 0; i < cfg.threads; i++)
        pthread_create(&th[i], NULL, hash_worker, (void *)(intptr_t)i);

    session_loop(&cfg);

    g_run = 0;
    for (int i = 0; i < cfg.threads; i++) pthread_join(th[i], NULL);
    free(th);
    net_close_main();
    net_close_fee();
    if (g_ctx) SSL_CTX_free(g_ctx);

#ifdef _WIN32
    WSACleanup();
#endif
    return 0;
}