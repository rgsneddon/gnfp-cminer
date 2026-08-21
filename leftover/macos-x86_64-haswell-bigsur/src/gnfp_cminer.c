/*
 * gnfp_cminer — native macOS port of rvp-design/gnfp_cminer for this
 * MacBook Air (MacBookAir6,1, Haswell i5-4250U, AVX2).
 * Same 8-round GNFPHash-v1 as the Linux ELF, 5% dual-login fee,
 * wire client=GNFPHash version=1.0.5. Hash path is AVX2 8-way SHA-256
 * (the ELF used OpenSSL SHA256; this Mac has AVX2 and no SHA-NI).
 */
#include "gnfphash.h"
#include "sha256.h"

#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <math.h>
#include <pthread.h>
#include <signal.h>
#include <stdatomic.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#ifdef __APPLE__
#include <mach/thread_act.h>
#include <mach/thread_policy.h>
#include <sys/sysctl.h>
#endif

#if defined(_WIN32)
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#define close_fd closesocket
#else
#include <arpa/inet.h>
#include <netdb.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <sys/types.h>
#define close_fd close
#endif

#include <openssl/err.h>
#include <openssl/ssl.h>

#define CLIENT "GNFPHash"
#define VERSION "1.0.5"
#define GNFP_ALGO "GNFPHash"
#define GNFP_SELFTEST_PRE "test-prework"
#define GNFP_SELFTEST_NONCE "0000000000000001"
#define GNFP_SELFTEST_HASH "986437c40fee8a876e0ca3f1e58b14fa38785a179f57f98ebbb0fb03102bd4eb"
#define DEFAULT_HOST "de.restoreprivacy.online"
#define DEFAULT_PORT 1474
#define FEE_ADDR "gnfp19381c4b1d7a9cbae64120f24b16d248ae07c6ff1"
#define FEE_EVERY 20
#define FEE_PCT 5
#define MAX_THREADS 256
#define LINE_CAP 8192
#define PRE_CAP 256
#define QCAP 64
#define IN_FLIGHT_MAX 24
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
static atomic_uint_fast64_t g_origin;

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
static atomic_int g_inflight;
static uint64_t g_dropped = 0;
static uint64_t g_submitted = 0;
static char g_seen[32][17];
static int g_seen_n = 0;
static char g_last_ack[160] = "-";
static char g_close_why[160] = "";
static pthread_mutex_t g_io_mu = PTHREAD_MUTEX_INITIALIZER;

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
          "Native macOS port of https://github.com/rvp-design/gnfp_cminer\n"
          "This build: MacBookAir6,1 Haswell AVX2 8-way SHA-256.\n\n"
          "  --user gnfp1ADDR.worker   required\n"
          "  --pool host:port          default %s:%d\n"
          "  --threads N               default physical cores (this Air: 2)\n"
          "  --notls                   plaintext (local node only)\n"
          "  --bench [SECONDS]         local hashrate, no pool (default 3s)\n"
          "  --selftest\n"
          "  --help\n\n"
          "Display: wire log for login/job/submit/reject; accepts are counted on the stats line.\n\n"
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
  int p = 0, l = 0;
  size_t psz = sizeof(p), lsz = sizeof(l);
  if (sysctlbyname("hw.physicalcpu", &p, &psz, NULL, 0) == 0 && p > 0) g_cpu_cores = p;
  if (sysctlbyname("hw.logicalcpu", &l, &lsz, NULL, 0) == 0 && l > 0) g_cpu_threads = l;
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
  int cap = g_cpu_threads < MAX_THREADS ? g_cpu_threads : MAX_THREADS;
  int d = g_cpu_cores > 0 ? g_cpu_cores : 1;
  return d < cap ? d : cap;
}

static int tcp_try(struct addrinfo *p) {
  int fd = (int)socket(p->ai_family, p->ai_socktype, p->ai_protocol);
  if (fd < 0) return -1;
  int one = 1;
  setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, &one, sizeof(one));
  struct linger lin = { .l_onoff = 1, .l_linger = 0 };
  setsockopt(fd, SOL_SOCKET, SO_LINGER, &lin, sizeof(lin));
  if (connect(fd, p->ai_addr, p->ai_addrlen) != 0) {
    close_fd(fd);
    return -1;
  }
  return fd;
}

static void conn_close(Conn *c) {
  if (!c) return;
  if (c->ssl) {
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

static int conn_open(Conn *c, const char *host, int port, int use_tls, int is_fee) {
  char portstr[16];
  snprintf(portstr, sizeof(portstr), "%d", port);
  struct addrinfo hints, *res = NULL;
  memset(&hints, 0, sizeof(hints));
  hints.ai_socktype = SOCK_STREAM;
  hints.ai_family = AF_UNSPEC;
  if (getaddrinfo(host, portstr, &hints, &res) != 0) return -1;
  SSL_load_error_strings();
  SSL_library_init();
  for (struct addrinfo *p = res; p; p = p->ai_next) {
    memset(c, 0, sizeof(*c));
    c->fd = -1;
    c->use_tls = use_tls;
    c->is_fee = is_fee;
    c->fd = tcp_try(p);
    if (c->fd < 0) continue;
    if (use_tls) {
      c->ctx = SSL_CTX_new(TLS_client_method());
      if (!c->ctx) {
        conn_close(c);
        continue;
      }
      SSL_CTX_set_verify(c->ctx, SSL_VERIFY_NONE, NULL);
      c->ssl = SSL_new(c->ctx);
      SSL_set_fd(c->ssl, c->fd);
      SSL_set_tlsext_host_name(c->ssl, host);
      if (SSL_connect(c->ssl) != 1) {
        fprintf(stderr, "tls handshake failed osstatus=%d, trying next address\n",
                c->ssl ? SSL_get_osstatus(c->ssl) : 0);
        conn_close(c);
        continue;
      }
    }
    {
      struct timeval short_tv = { .tv_sec = 0, .tv_usec = 50000 };
      struct timeval send_tv = { .tv_sec = 5, .tv_usec = 0 };
      setsockopt(c->fd, SOL_SOCKET, SO_RCVTIMEO, &short_tv, sizeof(short_tv));
      setsockopt(c->fd, SOL_SOCKET, SO_SNDTIMEO, &send_tv, sizeof(send_tv));
    }
    freeaddrinfo(res);
    return 0;
  }
  freeaddrinfo(res);
  memset(c, 0, sizeof(*c));
  c->fd = -1;
  return -1;
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
    if (n == 0) {
      snprintf(g_close_why, sizeof(g_close_why), "TLS close_notify (peer finished)");
      return -1;
    }
    int err = SSL_get_error(c->ssl, n);
    if (err == SSL_ERROR_WANT_READ || err == SSL_ERROR_WANT_WRITE) return 0;
    int os = SSL_get_osstatus(c->ssl);
    if (os == -9806) {
      snprintf(g_close_why, sizeof(g_close_why), "peer aborted TLS (osstatus=-9806)");
    } else if (os == -9805) {
      snprintf(g_close_why, sizeof(g_close_why), "peer closed TLS (osstatus=-9805)");
    } else {
      snprintf(g_close_why, sizeof(g_close_why), "TLS read err=%d osstatus=%d errno=%d", err, os, errno);
    }
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
  if (n == 0) {
    snprintf(g_close_why, sizeof(g_close_why), "TCP EOF (peer closed)");
    return -1;
  }
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
           "x86_64",
#elif defined(__aarch64__)
           "arm64",
#else
           "unknown",
#endif
           CLIENT, VERSION, GNFP_ALGO);
}

static double now_s(void) {
  struct timeval tv;
  gettimeofday(&tv, NULL);
  return (double)tv.tv_sec + (double)tv.tv_usec / 1e6;
}

static double share_work(int bits) {
  if (bits <= 0) return 1;
  if (bits >= 63) return ldexp(1.0, bits);
  return (double)(1ull << bits);
}

static void io_stamp(char *out, size_t cap) {
  struct timeval tv;
  gettimeofday(&tv, NULL);
  time_t sec = tv.tv_sec;
  struct tm tm;
  localtime_r(&sec, &tm);
  snprintf(out, cap, "%02d:%02d:%02d", tm.tm_hour, tm.tm_min, tm.tm_sec);
}

static void io_log(const char *dir, int is_fee, const char *line) {
  if (!line || !line[0]) return;
  char t[16];
  io_stamp(t, sizeof(t));
  const char *sock = is_fee ? "fee " : "main";
  pthread_mutex_lock(&g_io_mu);
  fprintf(stdout, "%s %s %s  %s\n", t, dir, sock, line);
  fflush(stdout);
  pthread_mutex_unlock(&g_io_mu);
}

static void io_note(const char *msg) {
  char t[16];
  io_stamp(t, sizeof(t));
  pthread_mutex_lock(&g_io_mu);
  fprintf(stdout, "%s --     %s\n", t, msg);
  fflush(stdout);
  pthread_mutex_unlock(&g_io_mu);
}

static int send_all(Conn *c, const char *buf, int n) {
  for (int i = 0; i < 50; i++) {
    int w = conn_write(c, buf, n);
    if (w == 0) return 0;
    if (w < 0) return -1;
    usleep(20000);
  }
  return -1;
}

static int send_login(Conn *c, const char *login, int threads) {
  char ident[512], line[700];
  identity_json(ident, sizeof(ident), login, threads);
  int n = snprintf(line, sizeof(line), "{\"method\":\"login\",%s,\"id\":1,\"jsonrpc\":\"2.0\"}\n", ident);
  if (n > 0 && line[n - 1] == '\n') line[n - 1] = 0;
  io_log(">>", c->is_fee, line);
  line[n - 1] = '\n';
  return send_all(c, line, n);
}

static int send_submit(Conn *c, const char *login, int threads, const char *jobId, const char *nonce) {
  char ident[512], line[900], brief[240];
  identity_json(ident, sizeof(ident), login, threads);
  int n = snprintf(line, sizeof(line),
                  "{\"method\":\"submit\",%s,\"id\":\"%s\",\"nonce\":\"%s\","
                  "\"output\":\"\",\"jobId\":\"%s\",\"jsonrpc\":\"2.0\"}\n",
                  ident, jobId, nonce, jobId);
  snprintf(brief, sizeof(brief), "submit job=%s nonce=%s threads=%d", jobId, nonce, threads);
  io_log(">>", c->is_fee, brief);
  return send_all(c, line, n);
}

static int send_stats(Conn *c, double call_hs, double proven_hs, uint64_t hashes, const char *jobId,
                      int height) {
  char ident[512], line[900], brief[280];
  identity_json(ident, sizeof(ident), g_login, g_threads);
  /* Pool board stores this hashrate field. Send proven (accepted × 2^diff / s)
   * so the panel matches share math. Call rate stays on the miner STATS line. */
  int n = snprintf(line, sizeof(line),
                  "{\"method\":\"stats\",%s,\"hashes\":%llu,\"hashrate\":%.3f,"
                  "\"callrate\":%.3f,\"jobId\":\"%s\",\"height\":%d,\"jsonrpc\":\"2.0\"}\n",
                  ident, (unsigned long long)hashes, proven_hs, call_hs, jobId ? jobId : "", height);
  snprintf(brief, sizeof(brief), "stats proven=%.0f call=%.0f hashes=%llu job=%s height=%d",
           proven_hs, call_hs, (unsigned long long)hashes, jobId ? jobId : "", height);
  io_log(">>", c->is_fee, brief);
  return send_all(c, line, n);
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
    pthread_mutex_lock(&g_q_mu);
    g_qhead = g_qtail = 0;
    pthread_mutex_unlock(&g_q_mu);
  }
  pthread_mutex_unlock(&g_job_mu);
}

static void remember_ack(const char *msg) {
  if (!msg || !msg[0]) msg = "-";
  snprintf(g_last_ack, sizeof(g_last_ack), "%s", msg);
}

static void apply_ack(const char *line, int is_fee) {
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
    remember_ack(desc[0] ? desc : "login");
    return;
  }
  if (strstr(low, "old_miner") || strstr(low, "client_required") || strstr(low, "miner_update")) {
    remember_ack("client refused");
    io_note("pool refused this client — need GNFPHash 1.0.4+");
    return;
  }
  int is_block = strcmp(formed, "true") == 0 || strstr(low, "block found") || strstr(low, "block");
  if (is_block && strstr(low, "login") == NULL) {
    g_blocks++;
    g_accepted++;
    if (atomic_load(&g_inflight) > 0) atomic_fetch_sub(&g_inflight, 1);
    remember_ack("BLOCK");
    io_note("BLOCK FOUND");
    return;
  }
  if (strcmp(low, "accepted") == 0 || code == 1) {
    g_accepted++;
    if (atomic_load(&g_inflight) > 0) atomic_fetch_sub(&g_inflight, 1);
    remember_ack("accepted");
    return;
  }
  if (strstr(low, "rejected") || strstr(low, "error") || code < 0) {
    g_rejected++;
    if (atomic_load(&g_inflight) > 0) atomic_fetch_sub(&g_inflight, 1);
    remember_ack(desc[0] ? desc : "rejected");
    (void)is_fee;
  }
}

static int rx_is_accept(const char *line) {
  char desc[160] = "";
  json_token(line, "description", desc, sizeof(desc));
  char low[160];
  snprintf(low, sizeof(low), "%s", desc);
  for (char *p = low; *p; p++) *p = (char)tolower((unsigned char)*p);
  int code = 0;
  json_int(line, "code", &code);
  return strcmp(low, "accepted") == 0 || code == 1;
}

static void handle_line(Conn *c, const char *line) {
  if (!line || !line[0]) return;
  char method[32] = "";
  json_token(line, "method", method, sizeof(method));
  int is_job = strcmp(method, "job") == 0;
  int quiet = !is_job && rx_is_accept(line);
  if (!quiet) io_log("<<", c->is_fee, line);
  apply_job(line, c->is_fee);
  if (!is_job) apply_ack(line, c->is_fee);
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

static int fee_same_job(const char *jobId) {
  pthread_mutex_lock(&g_job_mu);
  int ok = g_have_fee && jobId && strcmp(g_fee_job.jobId, jobId) == 0;
  pthread_mutex_unlock(&g_job_mu);
  return ok;
}

static int nonce_seen(const char nonce[16]) {
  for (int i = 0; i < 32; i++) {
    if (g_seen[i][0] && memcmp(g_seen[i], nonce, 16) == 0) return 1;
  }
  return 0;
}

static void nonce_remember(const char nonce[16]) {
  int i = g_seen_n % 32;
  memcpy(g_seen[i], nonce, 16);
  g_seen[i][16] = 0;
  g_seen_n++;
}

static int enqueue_share(const char *jobId, const char nonce[16]) {
  pthread_mutex_lock(&g_q_mu);
  int queued = (g_qtail - g_qhead + QCAP) % QCAP;
  if (queued >= QCAP - 1 || nonce_seen(nonce)) {
    if (queued >= QCAP - 1) g_dropped++;
    pthread_mutex_unlock(&g_q_mu);
    return 0;
  }
  int next = (g_qtail + 1) % QCAP;
  if (next == g_qhead) {
    g_dropped++;
    pthread_mutex_unlock(&g_q_mu);
    return 0;
  }
  g_meets++;
  Share *s = &g_q[g_qtail];
  s->fee = (g_meets % FEE_EVERY) == 0;
  snprintf(s->jobId, sizeof(s->jobId), "%s", jobId);
  memcpy(s->nonce, nonce, 16);
  s->nonce[16] = 0;
  nonce_remember(nonce);
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

static void pin_worker(int id) {
#ifdef __APPLE__
  thread_affinity_policy_data_t pol = { id + 1 };
  thread_policy_set(pthread_mach_thread_np(pthread_self()), THREAD_AFFINITY_POLICY,
                    (thread_policy_t)&pol, THREAD_AFFINITY_POLICY_COUNT);
#else
  (void)id;
#endif
}

static void *hash_worker(void *arg) {
  int tid = (int)(intptr_t)arg;
  pin_worker(tid);
  uint64_t n = atomic_load_explicit(&g_origin, memory_order_relaxed) + ((uint64_t)(unsigned)tid << 40);
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
      n = atomic_load_explicit(&g_origin, memory_order_relaxed) + ((uint64_t)(unsigned)tid << 40);
    }
    char nonces[GNFP_X8][GNFP_NONCE_LEN];
    uint8_t hashes[GNFP_X8][GNFP_HASH_LEN];
    for (int k = 0; k < GNFP_X8; k++) gnfp_nonce_hex(n + (uint64_t)k, nonces[k]);
    gnfp_hash_x8((const uint8_t *)job.pre, strlen(job.pre), nonces, hashes);
    atomic_fetch_add_explicit(&g_hashes, (uint64_t)GNFP_X8, memory_order_relaxed);
    JobSnap live;
    memset(&live, 0, sizeof(live));
    if (copy_main_job(&live) && live.gen == job.gen) {
      for (int k = 0; k < GNFP_X8; k++) {
        if (gnfp_meets_target(hashes[k], job.bits)) enqueue_share(job.jobId, nonces[k]);
      }
    } else if (live.gen != last_gen) {
      job = live;
      last_gen = job.gen;
    }
    n += GNFP_X8;
  }
  return NULL;
}

static void seed_origin(void) {
  uint64_t o = 0;
#if defined(__APPLE__)
  arc4random_buf(&o, sizeof(o));
#else
  FILE *ur = fopen("/dev/urandom", "rb");
  if (!ur || fread(&o, sizeof(o), 1, ur) != 1) {
    o = ((uint64_t)time(NULL) << 16) ^ (uint64_t)getpid();
  }
  if (ur) fclose(ur);
#endif
  atomic_store_explicit(&g_origin, o, memory_order_relaxed);
}

static void clear_jobs(void) {
  pthread_mutex_lock(&g_job_mu);
  g_have_main = 0;
  g_have_fee = 0;
  g_job_gen++;
  pthread_mutex_unlock(&g_job_mu);
  pthread_mutex_lock(&g_q_mu);
  g_qhead = g_qtail = 0;
  memset(g_seen, 0, sizeof(g_seen));
  g_seen_n = 0;
  pthread_mutex_unlock(&g_q_mu);
  g_fee_ok = 0;
  atomic_store_explicit(&g_inflight, 0, memory_order_relaxed);
  atomic_store_explicit(&g_hashes, 0, memory_order_relaxed);
  seed_origin();
}

static void flush_shares(Conn *mainc, Conn *feec) {
  Share s;
  while (atomic_load_explicit(&g_inflight, memory_order_relaxed) < IN_FLIGHT_MAX && dequeue_share(&s)) {
    int use_fee = s.fee && g_fee_ok && feec && feec->fd >= 0 && fee_same_job(s.jobId);
    int rc;
    if (use_fee) {
      rc = send_submit(feec, g_fee_login, 1, s.jobId, s.nonce);
      if (rc != 0) {
        g_fee_ok = 0;
        rc = send_submit(mainc, g_login, g_threads, s.jobId, s.nonce);
      }
    } else {
      rc = send_submit(mainc, g_login, g_threads, s.jobId, s.nonce);
    }
    if (rc == 0) {
      atomic_fetch_add_explicit(&g_inflight, 1, memory_order_relaxed);
      g_submitted++;
    }
  }
}

static void paint_live(double elapsed, const char *jobId, int height, int bits, double call_hs,
                       double proven_hs) {
  double acc_s = elapsed > 0.001 ? (double)g_accepted / elapsed : 0;
  int inflight = atomic_load_explicit(&g_inflight, memory_order_relaxed);
  int q = 0;
  pthread_mutex_lock(&g_q_mu);
  q = (g_qtail - g_qhead + QCAP) % QCAP;
  pthread_mutex_unlock(&g_q_mu);
  int hh = (int)(elapsed / 3600);
  int mm = ((int)elapsed % 3600) / 60;
  int ss = (int)elapsed % 60;
  char line[480];
  snprintf(line, sizeof(line),
           "STATS proven=%.0f H/s  call=%.0f H/s  acc=%d rej=%d blk=%d  %.2f acc/s  "
           "sub=%llu drop=%llu q=%d  inflight=%d/%d  fee=%s  job=%s h=%d d=%d  %d:%02d:%02d  last=%s",
           proven_hs, call_hs, g_accepted, g_rejected, g_blocks, acc_s,
           (unsigned long long)g_submitted, (unsigned long long)g_dropped, q, inflight, IN_FLIGHT_MAX,
           g_fee_ok ? "up" : "down", jobId && jobId[0] ? jobId : "-", height, bits, hh, mm, ss,
           g_last_ack);
  io_note(line);
}

static int mine_once(void) {
  Conn mainc, feec;
  memset(&mainc, 0, sizeof(mainc));
  memset(&feec, 0, sizeof(feec));
  mainc.fd = -1;
  feec.fd = -1;
  clear_jobs();
  if (conn_open(&mainc, g_host, g_port, g_tls, 0) != 0) {
    io_note("connect failed");
    return -1;
  }
  {
    char msg[160];
    snprintf(msg, sizeof(msg), "connected %s://%s:%d socket=main", g_tls ? "tls" : "tcp", g_host, g_port);
    io_note(msg);
  }
  if (send_login(&mainc, g_login, g_threads) != 0) {
    io_note("pool login send failed");
    conn_close(&mainc);
    return -1;
  }
  usleep(400000);
  if (conn_open(&feec, g_host, g_port, g_tls, 1) == 0) {
    if (send_login(&feec, g_fee_login, 1) != 0) {
      io_note("fee socket dropped — main keeps mining");
      conn_close(&feec);
    } else {
      g_fee_ok = 1;
      io_note("fee socket up");
    }
  } else {
    io_note("fee socket dropped — main keeps mining");
  }

  double started = now_s();
  double last_stats = started - 1;
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
        {
          char msg[200];
          snprintf(msg, sizeof(msg), "main socket closed — %s",
                   g_close_why[0] ? g_close_why : "read failed");
          io_note(msg);
        }
        break;
      }
      if (r > 0) drain_lines(&mainc);
    }
    if (feec.fd >= 0) {
      int r = conn_read(&feec);
      if (r < 0) {
        io_note("fee socket dropped — main keeps mining");
        conn_close(&feec);
        g_fee_ok = 0;
      } else if (r > 0) {
        drain_lines(&feec);
      }
    }
    flush_shares(&mainc, feec.fd >= 0 ? &feec : NULL);
    double now = now_s();
    if (now - last_stats >= 2) {
      last_stats = now;
      JobSnap job;
      memset(&job, 0, sizeof(job));
      copy_main_job(&job);
      double elapsed = now - started;
      if (elapsed < 1) elapsed = 1;
      uint64_t h = atomic_load_explicit(&g_hashes, memory_order_relaxed);
      double call_hs = (double)h / elapsed;
      double proven_hs = (double)g_accepted * share_work(job.bits) / elapsed;
      uint64_t proven_hashes = (uint64_t)((double)g_accepted * share_work(job.bits));
      send_stats(&mainc, call_hs, proven_hs, proven_hashes, job.jobId, job.height);
      paint_live(elapsed, job.jobId, job.height, job.bits, call_hs, proven_hs);
    }
    if (g_stop) break;
  }
  conn_close(&mainc);
  conn_close(&feec);
  return 0;
}

static void hash_hex(const uint8_t h[32], char out[65]) {
  static const char *d = "0123456789abcdef";
  for (int i = 0; i < 32; i++) {
    out[i * 2] = d[h[i] >> 4];
    out[i * 2 + 1] = d[h[i] & 15];
  }
  out[64] = 0;
}

static int run_selftest(void) {
  uint8_t h[32];
  char got[65];
  gnfp_hash((const uint8_t *)GNFP_SELFTEST_PRE, strlen(GNFP_SELFTEST_PRE), GNFP_SELFTEST_NONCE, h);
  hash_hex(h, got);
  if (strcmp(got, GNFP_SELFTEST_HASH) != 0) {
    fprintf(stderr, "selftest FAIL got %s want %s\n", got, GNFP_SELFTEST_HASH);
    return 1;
  }
  char n8[GNFP_X8][GNFP_NONCE_LEN];
  uint8_t o8[GNFP_X8][GNFP_HASH_LEN];
  uint8_t ref[GNFP_HASH_LEN];
  const char *pre = "pre-x8-check";
  for (int i = 0; i < GNFP_X8; i++) gnfp_nonce_hex((uint64_t)(1000 + i), n8[i]);
  gnfp_hash_x8((const uint8_t *)pre, strlen(pre), n8, o8);
  for (int i = 0; i < GNFP_X8; i++) {
    gnfp_hash((const uint8_t *)pre, strlen(pre), n8[i], ref);
    if (memcmp(ref, o8[i], 32) != 0) {
      fprintf(stderr, "selftest FAIL avx2 lane %d\n", i);
      return 1;
    }
  }
  printf("selftest ok %s backend=%s avx2=%s\n", got, gnfp_hash_backend(),
         sha256_have_avx2() ? "yes" : "no");
  return 0;
}

static int run_bench(int seconds) {
  if (seconds < 1) seconds = 3;
  JobSnap job;
  memset(&job, 0, sizeof(job));
  snprintf(job.jobId, sizeof(job.jobId), "bench");
  snprintf(job.pre, sizeof(job.pre), "bench-prework");
  job.bits = 32;
  job.height = 0;
  job.gen = 1;
  pthread_mutex_lock(&g_job_mu);
  g_main_job = job;
  g_have_main = 1;
  g_job_gen = 1;
  pthread_mutex_unlock(&g_job_mu);
  seed_origin();
  atomic_store_explicit(&g_hashes, 0, memory_order_relaxed);
  int n = g_threads > 0 ? g_threads : default_threads();
  g_threads = n;
  pthread_t th[MAX_THREADS];
  g_stop = 0;
  for (int i = 0; i < n; i++) {
    if (pthread_create(&th[i], NULL, hash_worker, (void *)(intptr_t)i) != 0) {
      fprintf(stderr, "thread start failed\n");
      g_stop = 1;
      n = i;
      break;
    }
  }
  sleep((unsigned)seconds);
  g_stop = 1;
  for (int i = 0; i < n; i++) pthread_join(th[i], NULL);
  uint64_t h = atomic_load_explicit(&g_hashes, memory_order_relaxed);
  double hs = (double)h / (double)seconds;
  printf("bench threads=%d hashes=%llu rate=%.0f H/s backend=%s (%.0fs)\n", n,
         (unsigned long long)h, hs, gnfp_hash_backend(), (double)seconds);
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
      return run_selftest();
    }
    if (strcmp(argv[i], "--bench") == 0) {
      int secs = 3;
      if (i + 1 < argc && argv[i + 1][0] != '-') secs = atoi(argv[++i]);
      return run_bench(secs);
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
  {
    char msg[320];
    snprintf(msg, sizeof(msg),
             "gnfp_cminer %s  %s://%s:%d  user=%s  threads=%d  backend=%s  fee=%d%%",
             VERSION, g_tls ? "tls" : "tcp", g_host, g_port, g_login, g_threads,
             gnfp_hash_backend(), FEE_PCT);
    io_note(msg);
    snprintf(msg, sizeof(msg), "device cores=%d threads=%d  fee login %s  accepts folded into STATS",
             g_cpu_cores, g_cpu_threads, g_fee_login);
    io_note(msg);
  }
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
  int backoff = 3;
  for (;;) {
    int rc = mine_once();
    if (g_stop) break;
    int wait = rc != 0 ? backoff : 3;
    {
      char msg[160];
      snprintf(msg, sizeof(msg), "reconnect in %ds %s:%d", wait, g_host, g_port);
      io_note(msg);
    }
    sleep((unsigned)wait);
    if (rc != 0) {
      if (backoff < 16) backoff *= 2;
    } else {
      backoff = 3;
    }
  }
  g_stop = 1;
  for (int i = 0; i < n; i++) pthread_join(th[i], NULL);
  return 0;
}
