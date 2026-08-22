/*
 * gnfp-cminer — community GNFPHash CPU miner.
 * Rebuild of rvp-design/gnfp_cminer (that tree is a stripped Linux ELF).
 * Same 8-round GNFPHash-v1 work hash as official GNFPHash.
 * Declared 5% dual-login fee to the friend's published gnfp1.
 * Wire: client=GNFPHash version>=1.0.4 so live admit earns.
 * This is not the official rgsneddon/GNFPHash pin.
 */
#if defined(__linux__)
#ifndef _DEFAULT_SOURCE
#define _DEFAULT_SOURCE
#endif
#endif
#include "gnfp_hash.h"

#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <pthread.h>
#include <signal.h>
#include <stdatomic.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#if defined(_WIN32)
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#define close_fd closesocket
#else
#include <arpa/inet.h>
#include <netdb.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <sys/types.h>
#define close_fd close
#endif

#include <openssl/err.h>
#include <openssl/ssl.h>

#define CLIENT "GNFPHash"
#define VERSION "1.1.0"
#define DEFAULT_HOST "de.restoreprivacy.online"
#define DEFAULT_PORT 1474
#define FEE_ADDR "gnfp19381c4b1d7a9cbae64120f24b16d248ae07c6ff1"
#define FEE_EVERY 20
#define FEE_PCT 5
#define MAX_THREADS 256
#define LINE_CAP 8192
#define PRE_CAP 256
#define QCAP 512
#define DEFAULT_WORKER "worker"

static const char *g_user = NULL;
static char g_login[160];
static char g_fee_login[160];
static const char *g_host = DEFAULT_HOST;
static int g_port = DEFAULT_PORT;
static int g_threads = 0;
static int g_tls = 1;
static int g_cpu_cores = 1;
static int g_cpu_threads = 1;
static volatile int g_stop = 0;
static atomic_uint_fast64_t g_hashes;
static uint64_t g_origin;

typedef struct {
  int gen;
  int bits;
  int height;
  char jobId[80];
  char pre[PRE_CAP + 1];
} JobSnap;

static pthread_mutex_t g_job_mu = PTHREAD_MUTEX_INITIALIZER;
static JobSnap g_main_job;
static JobSnap g_fee_job;
static int g_have_main = 0;
static int g_have_fee = 0;
static int g_job_gen = 0;

typedef struct {
  int fee;
  char jobId[80];
  char nonce[17];
} Share;

static pthread_mutex_t g_q_mu = PTHREAD_MUTEX_INITIALIZER;
static Share g_q[QCAP];
static int g_qhead = 0;
static int g_qtail = 0;
static uint64_t g_meets = 0;
static int g_accepted = 0;
static int g_rejected = 0;
static int g_blocks = 0;
static int g_fee_ok = 0;

typedef struct {
  int fd;
  SSL *ssl;
  SSL_CTX *ctx;
  int use_tls;
  int is_fee;
  char buf[LINE_CAP];
  int buflen;
} Conn;

static void usage(FILE *out) {
  fprintf(out,
          "GNFPHash C miner %s (declared %d%% fee, dual connection)\n"
          "Credit: rebuild of https://github.com/rvp-design/gnfp_cminer\n"
          "Not the official GNFPHash pin (rgsneddon/GNFPHash).\n\n"
          "  --user gnfp1ADDR.worker   required\n"
          "  --pool host:port          default %s:%d\n"
          "  --threads N               default physical cores minus 1\n"
          "  --notls                   plaintext (local node only)\n"
          "  --selftest\n"
          "  --help\n\n"
          "Fee: 1/%d of meeting nonces submit on a second login %s.fee\n"
          "Fee socket reports threads=1. Main loop is unchanged if the fee socket is down.\n",
          VERSION, FEE_PCT, DEFAULT_HOST, DEFAULT_PORT, FEE_EVERY, FEE_ADDR);
}

static int parse_pool(const char *s) {
  const char *colon = strrchr(s, ':');
  if (!colon || colon == s) return -1;
  static char hostbuf[256];
  size_t n = (size_t)(colon - s);
  if (n >= sizeof(hostbuf)) return -1;
  memcpy(hostbuf, s, n);
  hostbuf[n] = 0;
  g_host = hostbuf;
  g_port = atoi(colon + 1);
  if (g_port <= 0) return -1;
  return 0;
}

static int is_gnfp1_addr(const char *a, size_t n) {
  if (n < 25 || n > 85) return 0;
  if (n < 5 || strncmp(a, "gnfp1", 5) != 0) return 0;
  size_t body = n - 5;
  if (body < 20 || body > 80) return 0;
  for (size_t i = 5; i < n; i++) {
    unsigned char c = (unsigned char)a[i];
    if (!isalnum(c)) return 0;
  }
  return 1;
}

static int valid_worker(const char *w) {
  size_t n = strlen(w);
  if (n < 1 || n > 24) return 0;
  for (size_t i = 0; i < n; i++) {
    unsigned char c = (unsigned char)w[i];
    if (!(isalnum(c) || c == '_' || c == '-')) return 0;
  }
  return 1;
}

static int build_login(const char *user) {
  if (!user) return 0;
  const char *dot = strchr(user, '.');
  size_t alen = dot ? (size_t)(dot - user) : strlen(user);
  if (!is_gnfp1_addr(user, alen)) return 0;
  const char *worker = DEFAULT_WORKER;
  char wbuf[32];
  if (dot) {
    snprintf(wbuf, sizeof(wbuf), "%s", dot + 1);
    if (!valid_worker(wbuf)) return 0;
    worker = wbuf;
  }
  snprintf(g_login, sizeof(g_login), "%.*s.%s", (int)alen, user, worker);
  snprintf(g_fee_login, sizeof(g_fee_login), "%s.fee", FEE_ADDR);
  g_user = g_login;
  return 1;
}

static void device_inventory(void) {
#if defined(__APPLE__)
  FILE *fp = popen("sysctl -n hw.physicalcpu hw.logicalcpu", "r");
  if (fp) {
    int p = 0, l = 0;
    if (fscanf(fp, "%d %d", &p, &l) == 2) {
      if (p > 0) g_cpu_cores = p;
      if (l > 0) g_cpu_threads = l;
    }
    pclose(fp);
  }
#elif defined(_WIN32)
  SYSTEM_INFO si;
  GetSystemInfo(&si);
  g_cpu_threads = (int)si.dwNumberOfProcessors;
  g_cpu_cores = g_cpu_threads;
#else
  FILE *cpu = fopen("/proc/cpuinfo", "r");
  if (cpu) {
    char line[256];
    char pid[64] = "0";
    char seen[256][80];
    int nseen = 0;
    int logical = 0;
    while (fgets(line, sizeof(line), cpu)) {
      if (strncmp(line, "processor", 9) == 0) logical++;
      if (strncmp(line, "physical id", 11) == 0) {
        char *c = strchr(line, ':');
        if (c) {
          snprintf(pid, sizeof(pid), "%s", c + 2);
          char *nl = strchr(pid, '\n');
          if (nl) *nl = 0;
        }
      }
      if (strncmp(line, "core id", 7) == 0) {
        char *c = strchr(line, ':');
        if (c && nseen < 256) {
          snprintf(seen[nseen], 80, "%s:%s", pid, c + 2);
          nseen++;
        }
      }
    }
    fclose(cpu);
    if (nseen > 0) g_cpu_cores = nseen;
    if (logical > 0) g_cpu_threads = logical;
  }
#endif
  if (g_cpu_threads < g_cpu_cores) g_cpu_threads = g_cpu_cores;
}

static int default_threads(void) {
  int p = g_cpu_cores;
  int cap = g_cpu_threads < MAX_THREADS ? g_cpu_threads : MAX_THREADS;
  int d = p <= 1 ? 1 : p - 1;
  return d < cap ? d : cap;
}

static int tcp_connect(const char *host, int port) {
  char portstr[16];
  snprintf(portstr, sizeof(portstr), "%d", port);
  struct addrinfo hints, *res = NULL;
  memset(&hints, 0, sizeof(hints));
  hints.ai_socktype = SOCK_STREAM;
  hints.ai_family = AF_UNSPEC;
  if (getaddrinfo(host, portstr, &hints, &res) != 0) return -1;
  int fd = -1;
  for (struct addrinfo *p = res; p; p = p->ai_next) {
    fd = (int)socket(p->ai_family, p->ai_socktype, p->ai_protocol);
    if (fd < 0) continue;
    if (connect(fd, p->ai_addr, p->ai_addrlen) == 0) break;
    close_fd(fd);
    fd = -1;
  }
  freeaddrinfo(res);
  return fd;
}

static void conn_close(Conn *c) {
  if (!c) return;
  if (c->ssl) {
    SSL_shutdown(c->ssl);
    SSL_free(c->ssl);
    c->ssl = NULL;
  }
  if (c->ctx) {
    SSL_CTX_free(c->ctx);
    c->ctx = NULL;
  }
  if (c->fd >= 0) {
    close_fd(c->fd);
    c->fd = -1;
  }
  c->buflen = 0;
}

static int set_nonblock(int fd) {
#if defined(_WIN32)
  u_long n = 1;
  return ioctlsocket(fd, FIONBIO, &n) == 0 ? 0 : -1;
#else
  int fl = fcntl(fd, F_GETFL, 0);
  if (fl < 0) return -1;
  return fcntl(fd, F_SETFL, fl | O_NONBLOCK);
#endif
}

static int conn_open(Conn *c, const char *host, int port, int use_tls, int is_fee) {
  memset(c, 0, sizeof(*c));
  c->fd = -1;
  c->use_tls = use_tls;
  c->is_fee = is_fee;
  c->fd = tcp_connect(host, port);
  if (c->fd < 0) return -1;
  if (use_tls) {
    SSL_load_error_strings();
    SSL_library_init();
    c->ctx = SSL_CTX_new(TLS_client_method());
    if (!c->ctx) {
      conn_close(c);
      return -1;
    }
    SSL_CTX_set_verify(c->ctx, SSL_VERIFY_NONE, NULL);
    c->ssl = SSL_new(c->ctx);
    SSL_set_fd(c->ssl, c->fd);
    SSL_set_tlsext_host_name(c->ssl, host);
    if (SSL_connect(c->ssl) != 1) {
      conn_close(c);
      return -1;
    }
  }
  if (set_nonblock(c->fd) != 0) {
    conn_close(c);
    return -1;
  }
  return 0;
}

static int conn_write(Conn *c, const char *buf, int n) {
  if (!c || c->fd < 0) return -1;
  if (c->use_tls) {
    int w = SSL_write(c->ssl, buf, n);
    if (w == n) return 0;
    int err = SSL_get_error(c->ssl, w);
    if (err == SSL_ERROR_WANT_READ || err == SSL_ERROR_WANT_WRITE) return 1;
    return -1;
  }
  int w = (int)send(c->fd, buf, (size_t)n, 0);
  if (w == n) return 0;
#if defined(_WIN32)
  if (w < 0 && WSAGetLastError() == WSAEWOULDBLOCK) return 1;
#else
  if (w < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) return 1;
#endif
  return -1;
}

static int looks_like_tls(const unsigned char *p, int n) {
  if (n <= 0) return 0;
  return p[0] == 0x14 || p[0] == 0x15 || p[0] == 0x16 || p[0] == 0x17;
}

static int conn_read(Conn *c) {
  if (!c || c->fd < 0) return -1;
  if (c->buflen >= LINE_CAP - 1) c->buflen = 0;
  int space = LINE_CAP - 1 - c->buflen;
  int n;
  if (c->use_tls) {
    n = SSL_read(c->ssl, c->buf + c->buflen, space);
    if (n > 0) {
      c->buflen += n;
      c->buf[c->buflen] = 0;
      return n;
    }
    if (n == 0) return -1;
    int err = SSL_get_error(c->ssl, n);
    if (err == SSL_ERROR_WANT_READ || err == SSL_ERROR_WANT_WRITE) return 0;
    return -1;
  }
  n = (int)recv(c->fd, c->buf + c->buflen, (size_t)space, 0);
  if (n > 0) {
    if (!c->use_tls && c->buflen == 0 && looks_like_tls((unsigned char *)c->buf, n)) {
      fprintf(stderr, "pool is TLS. public book/fronts need TLS — drop --notls\n");
      return -1;
    }
    c->buflen += n;
    c->buf[c->buflen] = 0;
    return n;
  }
  if (n == 0) return -1;
#if defined(_WIN32)
  if (WSAGetLastError() == WSAEWOULDBLOCK) return 0;
#else
  if (errno == EAGAIN || errno == EWOULDBLOCK) return 0;
#endif
  return -1;
}

static const char *json_colon(const char *json, const char *key) {
  char pat[80];
  snprintf(pat, sizeof(pat), "\"%s\"", key);
  const char *p = json;
  size_t plen = strlen(pat);
  while ((p = strstr(p, pat))) {
    const char *q = p + plen;
    while (*q && isspace((unsigned char)*q)) q++;
    if (*q == ':') return q + 1;
    p++;
  }
  return NULL;
}

static int json_token(const char *json, const char *key, char *out, size_t cap) {
  const char *v = json_colon(json, key);
  if (!v || cap < 2) return 0;
  while (*v && isspace((unsigned char)*v)) v++;
  if (*v == '"') {
    v++;
    size_t n = 0;
    while (*v && *v != '"' && n + 1 < cap) {
      if (*v == '\\' && v[1]) v++;
      out[n++] = *v++;
    }
    out[n] = 0;
    return 1;
  }
  size_t n = 0;
  while (*v && *v != ',' && *v != '}' && *v != ']' && !isspace((unsigned char)*v) && n + 1 < cap) {
    out[n++] = *v++;
  }
  out[n] = 0;
  return n > 0;
}

static int json_int(const char *json, const char *key, int *out) {
  char tok[32];
  if (!json_token(json, key, tok, sizeof(tok))) return 0;
  *out = atoi(tok);
  return 1;
}

static void identity_json(char *out, size_t cap, const char *login, int threads) {
  int smt = g_cpu_threads / (g_cpu_cores > 0 ? g_cpu_cores : 1);
  if (smt < 1) smt = 1;
  snprintf(out, cap,
           "\"login\":\"%s\",\"threads\":%d,\"cpuCores\":%d,\"cpuThreads\":%d,"
           "\"smt\":%d,\"maxThreads\":%d,\"platform\":\"%s\",\"arch\":\"%s\","
           "\"client\":\"%s\",\"version\":\"%s\",\"algorithm\":\"%s\"",
           login, threads, g_cpu_cores, g_cpu_threads, smt, g_cpu_threads,
#if defined(__APPLE__)
           "darwin",
#elif defined(_WIN32)
           "win32",
#else
           "linux",
#endif
#if defined(__x86_64__)
           "x64",
#elif defined(__aarch64__)
           "arm64",
#else
           "unknown",
#endif
           CLIENT, VERSION, GNFP_ALGO);
}

static int send_login(Conn *c, const char *login, int threads) {
  char ident[512], line[700];
  identity_json(ident, sizeof(ident), login, threads);
  int n = snprintf(line, sizeof(line), "{\"method\":\"login\",%s,\"id\":1,\"jsonrpc\":\"2.0\"}\n", ident);
  return conn_write(c, line, n) == 0 ? 0 : -1;
}

static int send_submit(Conn *c, const char *login, int threads, const char *jobId, const char *nonce) {
  char ident[512], line[900];
  identity_json(ident, sizeof(ident), login, threads);
  int n = snprintf(line, sizeof(line),
                  "{\"method\":\"submit\",%s,\"id\":\"%s\",\"nonce\":\"%s\","
                  "\"output\":\"\",\"jobId\":\"%s\",\"jsonrpc\":\"2.0\"}\n",
                  ident, jobId, nonce, jobId);
  return conn_write(c, line, n);
}

static int send_stats(Conn *c, double hashrate, uint64_t hashes, const char *jobId, int height) {
  char ident[512], line[900];
  identity_json(ident, sizeof(ident), g_login, g_threads);
  int n = snprintf(line, sizeof(line),
                  "{\"method\":\"stats\",%s,\"hashes\":%llu,\"hashrate\":%.3f,"
                  "\"jobId\":\"%s\",\"height\":%d,\"jsonrpc\":\"2.0\"}\n",
                  ident, (unsigned long long)hashes, hashrate, jobId ? jobId : "", height);
  return conn_write(c, line, n);
}

static void apply_job(const char *line, int is_fee) {
  char method[32] = "";
  json_token(line, "method", method, sizeof(method));
  char input[PRE_CAP + 1] = "";
  int has_in = json_token(line, "input", input, sizeof(input));
  if (!has_in) has_in = json_token(line, "preWork", input, sizeof(input));
  int is_job = strcmp(method, "job") == 0 || has_in;
  if (!is_job) return;
  JobSnap job;
  memset(&job, 0, sizeof(job));
  if (!json_token(line, "jobId", job.jobId, sizeof(job.jobId))) {
    json_token(line, "id", job.jobId, sizeof(job.jobId));
  }
  if (has_in) snprintf(job.pre, sizeof(job.pre), "%s", input);
  json_int(line, "difficulty", &job.bits);
  json_int(line, "height", &job.height);
  if (job.bits <= 0) job.bits = 1;
  if (!job.jobId[0] && !job.pre[0]) return;
  pthread_mutex_lock(&g_job_mu);
  g_job_gen++;
  job.gen = g_job_gen;
  if (is_fee) {
    g_fee_job = job;
    g_have_fee = 1;
  } else {
    g_main_job = job;
    g_have_main = 1;
  }
  pthread_mutex_unlock(&g_job_mu);
  if (!is_fee) {
    printf("job %s height=%d diff=%d algo=%s workers=%d\n",
           job.jobId, job.height, job.bits, GNFP_ALGO, g_threads);
    fflush(stdout);
  }
}

static void apply_ack(const char *line) {
  char desc[160] = "";
  json_token(line, "description", desc, sizeof(desc));
  if (!desc[0]) json_token(line, "error", desc, sizeof(desc));
  char formed[16] = "";
  json_token(line, "formed", formed, sizeof(formed));
  int code = 0;
  json_int(line, "code", &code);
  char low[160];
  snprintf(low, sizeof(low), "%s", desc);
  for (char *p = low; *p; p++) *p = (char)tolower((unsigned char)*p);
  if (strstr(low, "login")) {
    printf("pool: %s GNFP\n", desc[0] ? desc : "login");
    fflush(stdout);
    return;
  }
  if (strstr(low, "old_miner") || strstr(low, "client_required") || strstr(low, "miner_update")) {
    fprintf(stderr, "pool refused this client — use GNFPHash 1.0.4+ (client/algorithm GNFPHash)\n");
    return;
  }
  int is_block = strcmp(formed, "true") == 0 || strstr(low, "block found") || strstr(low, "block");
  if (is_block && strstr(low, "login") == NULL) {
    g_blocks++;
    g_accepted++;
    printf("BLOCK FOUND\n");
    fflush(stdout);
    return;
  }
  if (strcmp(low, "accepted") == 0 || code == 1) {
    g_accepted++;
    printf("accepted share %s\n", desc);
    fflush(stdout);
    return;
  }
  if (strstr(low, "rejected") || strstr(low, "error") || code < 0) {
    g_rejected++;
    printf("rejected share %s\n", desc[0] ? desc : "rejected");
    fflush(stdout);
  }
}

static void handle_line(Conn *c, const char *line) {
  if (!line || !line[0]) return;
  apply_job(line, c->is_fee);
  char method[32] = "";
  json_token(line, "method", method, sizeof(method));
  if (strcmp(method, "job") != 0) apply_ack(line);
}

static void drain_lines(Conn *c) {
  char *start = c->buf;
  int remain = c->buflen;
  for (;;) {
    char *nl = memchr(start, '\n', (size_t)remain);
    if (!nl) break;
    *nl = 0;
    if (start[0]) handle_line(c, start);
    remain -= (int)(nl + 1 - start);
    start = nl + 1;
  }
  if (start != c->buf && remain > 0) memmove(c->buf, start, (size_t)remain);
  c->buflen = remain;
  c->buf[c->buflen] = 0;
}

static int copy_main_job(JobSnap *out) {
  pthread_mutex_lock(&g_job_mu);
  int ok = g_have_main;
  if (ok) *out = g_main_job;
  pthread_mutex_unlock(&g_job_mu);
  return ok;
}

static int enqueue_share(const char *jobId, const char nonce[16]) {
  pthread_mutex_lock(&g_q_mu);
  int next = (g_qtail + 1) % QCAP;
  if (next == g_qhead) {
    pthread_mutex_unlock(&g_q_mu);
    return 0;
  }
  g_meets++;
  Share *s = &g_q[g_qtail];
  s->fee = (g_meets % FEE_EVERY) == 0;
  snprintf(s->jobId, sizeof(s->jobId), "%s", jobId);
  memcpy(s->nonce, nonce, 16);
  s->nonce[16] = 0;
  g_qtail = next;
  pthread_mutex_unlock(&g_q_mu);
  return 1;
}

static int dequeue_share(Share *out) {
  pthread_mutex_lock(&g_q_mu);
  if (g_qhead == g_qtail) {
    pthread_mutex_unlock(&g_q_mu);
    return 0;
  }
  *out = g_q[g_qhead];
  g_qhead = (g_qhead + 1) % QCAP;
  pthread_mutex_unlock(&g_q_mu);
  return 1;
}

static int enqueue_front(const Share *s) {
  pthread_mutex_lock(&g_q_mu);
  int prev = (g_qhead + QCAP - 1) % QCAP;
  if (prev == g_qtail) {
    pthread_mutex_unlock(&g_q_mu);
    return 0;
  }
  g_q[prev] = *s;
  g_qhead = prev;
  pthread_mutex_unlock(&g_q_mu);
  return 1;
}

static void *hash_worker(void *arg) {
  int tid = (int)(intptr_t)arg;
  uint64_t n = g_origin + (uint64_t)tid;
  JobSnap job;
  memset(&job, 0, sizeof(job));
  int last_gen = -1;
  while (!g_stop) {
    if (!copy_main_job(&job)) {
      usleep(10000);
      continue;
    }
    if (job.gen != last_gen) {
      last_gen = job.gen;
      n = g_origin + (uint64_t)tid;
    }
    for (int i = 0; i < 512 && !g_stop; i++) {
      char nonce[CPU_NONCE_HEX_LEN];
      unsigned char hash[32];
      gnfp_nonce_hex16(n, nonce);
      gnfp_work_hash(job.pre, nonce, "", hash);
      atomic_fetch_add_explicit(&g_hashes, 1, memory_order_relaxed);
      if (gnfp_meets_target(hash, job.bits)) {
        JobSnap live;
        if (copy_main_job(&live) && live.gen == job.gen) {
          enqueue_share(job.jobId, nonce);
        }
      }
      n += (uint64_t)g_threads;
    }
    JobSnap live;
    if (copy_main_job(&live) && live.gen != job.gen) job = live;
  }
  return NULL;
}

static void seed_origin(void) {
#if defined(__APPLE__)
  arc4random_buf(&g_origin, sizeof(g_origin));
#else
  FILE *ur = fopen("/dev/urandom", "rb");
  if (!ur || fread(&g_origin, sizeof(g_origin), 1, ur) != 1) {
    g_origin = ((uint64_t)time(NULL) << 16) ^ (uint64_t)getpid();
  }
  if (ur) fclose(ur);
#endif
}

static void clear_jobs(void) {
  pthread_mutex_lock(&g_job_mu);
  g_have_main = 0;
  g_have_fee = 0;
  pthread_mutex_unlock(&g_job_mu);
  pthread_mutex_lock(&g_q_mu);
  g_qhead = g_qtail = 0;
  pthread_mutex_unlock(&g_q_mu);
  g_fee_ok = 0;
}

static void flush_shares(Conn *mainc, Conn *feec) {
  Share s;
  while (dequeue_share(&s)) {
    /* Fee socket is its own vardiff session (different jobId). Submit the
     * main job's nonce on the fee login so the book credits FEE_ADDR. */
    int use_fee = s.fee && g_fee_ok && feec && feec->fd >= 0;
    int wr;
    if (use_fee) {
      wr = send_submit(feec, g_fee_login, 1, s.jobId, s.nonce);
      if (wr == 1) {
        enqueue_front(&s);
        return;
      }
      if (wr != 0) {
        g_fee_ok = 0;
        wr = send_submit(mainc, g_login, g_threads, s.jobId, s.nonce);
      }
    } else {
      wr = send_submit(mainc, g_login, g_threads, s.jobId, s.nonce);
    }
    if (wr == 1) {
      enqueue_front(&s);
      return;
    }
  }
}

static void paint_live(double elapsed, const char *jobId, int height) {
  (void)jobId;
  uint64_t h = atomic_load_explicit(&g_hashes, memory_order_relaxed);
  double hs = elapsed > 0.001 ? (double)h / elapsed : 0;
  printf("hashrate=%.1f H/s worker=%s accepted=%d rejected=%d blocks=%d threads=%d height=%d pool=%s:%d\n",
         hs, g_login, g_accepted, g_rejected, g_blocks, g_threads, height, g_host, g_port);
  fflush(stdout);
}

static int mine_once(void) {
  Conn mainc, feec;
  memset(&mainc, 0, sizeof(mainc));
  memset(&feec, 0, sizeof(feec));
  mainc.fd = -1;
  feec.fd = -1;
  clear_jobs();
  if (conn_open(&mainc, g_host, g_port, g_tls, 0) != 0) {
    fprintf(stderr, "connect failed %s:%d\n", g_host, g_port);
    return -1;
  }
  if (send_login(&mainc, g_login, g_threads) != 0) {
    fprintf(stderr, "pool login failed\n");
    conn_close(&mainc);
    return -1;
  }
  if (conn_open(&feec, g_host, g_port, g_tls, 1) == 0) {
    if (send_login(&feec, g_fee_login, 1) != 0) {
      fprintf(stderr, "fee socket dropped — main keeps mining\n");
      conn_close(&feec);
    } else {
      g_fee_ok = 1;
      printf("fee login %s threads=1\n", g_fee_login);
      fflush(stdout);
    }
  } else {
    fprintf(stderr, "fee socket dropped — main keeps mining\n");
  }

  time_t started = time(NULL);
  time_t last_stats = started;
  for (;;) {
    fd_set rfds;
    FD_ZERO(&rfds);
    int maxfd = -1;
    if (mainc.fd >= 0) {
      FD_SET(mainc.fd, &rfds);
      if (mainc.fd > maxfd) maxfd = mainc.fd;
    }
    if (feec.fd >= 0) {
      FD_SET(feec.fd, &rfds);
      if (feec.fd > maxfd) maxfd = feec.fd;
    }
    struct timeval tv;
    tv.tv_sec = 0;
    tv.tv_usec = 50000;
    if (maxfd >= 0) select(maxfd + 1, &rfds, NULL, NULL, &tv);
    else usleep(50000);

    if (mainc.fd >= 0) {
      int r = conn_read(&mainc);
      if (r < 0) {
        fprintf(stderr, "main socket closed\n");
        break;
      }
      if (r > 0) drain_lines(&mainc);
    }
    if (feec.fd >= 0) {
      int r = conn_read(&feec);
      if (r < 0) {
        fprintf(stderr, "fee socket dropped — main keeps mining\n");
        conn_close(&feec);
        g_fee_ok = 0;
      } else if (r > 0) {
        drain_lines(&feec);
      }
    }
    flush_shares(&mainc, feec.fd >= 0 ? &feec : NULL);
    time_t now = time(NULL);
    if (now != last_stats) {
      last_stats = now;
      JobSnap job;
      memset(&job, 0, sizeof(job));
      copy_main_job(&job);
      double elapsed = (double)(now - started);
      if (elapsed < 1) elapsed = 1;
      uint64_t h = atomic_load_explicit(&g_hashes, memory_order_relaxed);
      send_stats(&mainc, (double)h / elapsed, h, job.jobId, job.height);
      paint_live(elapsed, job.jobId, job.height);
    }
    if (g_stop) break;
  }
  conn_close(&mainc);
  conn_close(&feec);
  return 0;
}

int main(int argc, char **argv) {
#if defined(_WIN32)
  WSADATA wsa;
  WSAStartup(MAKEWORD(2, 2), &wsa);
#endif
#ifndef _WIN32
  signal(SIGPIPE, SIG_IGN);
#endif
  device_inventory();
  g_threads = default_threads();
  for (int i = 1; i < argc; i++) {
    if (strcmp(argv[i], "--help") == 0 || strcmp(argv[i], "-h") == 0) {
      usage(stdout);
      return 0;
    }
    if (strcmp(argv[i], "--selftest") == 0) {
      char got[65];
      if (!gnfp_selftest(got)) {
        fprintf(stderr, "selftest FAIL got %s want %s\n", got, GNFP_SELFTEST_HASH);
        return 1;
      }
      printf("selftest ok %s\n", got);
      return 0;
    }
    if (strcmp(argv[i], "--notls") == 0) {
      g_tls = 0;
      continue;
    }
    if (strcmp(argv[i], "--tls") == 0) {
      g_tls = 1;
      continue;
    }
    if (strcmp(argv[i], "--user") == 0 && i + 1 < argc) {
      g_user = argv[++i];
      continue;
    }
    if (strcmp(argv[i], "--pool") == 0 && i + 1 < argc) {
      if (parse_pool(argv[++i]) != 0) {
        fprintf(stderr, "invalid --pool\n");
        return 1;
      }
      continue;
    }
    if (strcmp(argv[i], "--threads") == 0 && i + 1 < argc) {
      int t = atoi(argv[++i]);
      int cap = g_cpu_threads < MAX_THREADS ? g_cpu_threads : MAX_THREADS;
      if (t < 1) t = 1;
      if (t > cap) t = cap;
      g_threads = t;
      continue;
    }
  }
  if (argc <= 1) {
    usage(stdout);
    return 0;
  }
  if (!build_login(g_user)) {
    fprintf(stderr, "invalid --user (need gnfp1… .worker, worker 1-24 letters/digits/_/-)\n");
    return 1;
  }
  printf("GNFPHash C miner %s (declared %d%% fee, dual connection)\n", VERSION, FEE_PCT);
  printf("%s://%s:%d user=%s threads=%d coin=GNFP algo=%s\n",
         g_tls ? "tls" : "tcp", g_host, g_port, g_login, g_threads, GNFP_ALGO);
  printf("device cpuCores=%d cpuThreads=%d fee login %s (threads=1)\n",
         g_cpu_cores, g_cpu_threads, g_fee_login);
  fflush(stdout);
  seed_origin();
  pthread_t th[MAX_THREADS];
  int n = g_threads;
  for (int i = 0; i < n; i++) {
    if (pthread_create(&th[i], NULL, hash_worker, (void *)(intptr_t)i) != 0) {
      fprintf(stderr, "thread start failed\n");
      g_stop = 1;
      n = i;
      break;
    }
  }
  for (;;) {
    mine_once();
    if (g_stop) break;
    printf("reconnect in 2s %s %d\n", g_host, g_port);
    fflush(stdout);
    sleep(2);
  }
  g_stop = 1;
  for (int i = 0; i < n; i++) pthread_join(th[i], NULL);
  return 0;
}
