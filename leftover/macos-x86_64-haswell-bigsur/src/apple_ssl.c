/* SecureTransport stand-in for the OpenSSL client APIs gnfp-cminer uses. */
#include "openssl/ssl.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>

#include <CoreFoundation/CoreFoundation.h>
#include <Security/SecureTransport.h>
#include <Security/Security.h>

struct ssl_ctx_st {
  int dummy;
};

struct ssl_st {
  SSLContextRef ctx;
  int fd;
  char host[256];
  int last_err;
  int last_os;
};

struct ssl_method_st {
  int dummy;
};

static const SSL_METHOD k_tls_client;

static OSStatus st_read(SSLConnectionRef connection, void *data, size_t *dataLength) {
  SSL *s = (SSL *)connection;
  size_t want = *dataLength;
  ssize_t n = recv(s->fd, data, want, 0);
  if (n > 0) {
    *dataLength = (size_t)n;
    return noErr;
  }
  if (n == 0) {
    *dataLength = 0;
    return errSSLClosedGraceful;
  }
  *dataLength = 0;
  if (errno == EAGAIN || errno == EWOULDBLOCK || errno == ETIMEDOUT) return errSSLWouldBlock;
  if (errno == ECONNRESET) return errSSLClosedAbort;
  return errSSLClosedAbort;
}

static OSStatus st_write(SSLConnectionRef connection, const void *data, size_t *dataLength) {
  SSL *s = (SSL *)connection;
  size_t want = *dataLength;
  ssize_t n = send(s->fd, data, want, 0);
  if (n > 0) {
    *dataLength = (size_t)n;
    return noErr;
  }
  *dataLength = 0;
  if (errno == EAGAIN || errno == EWOULDBLOCK || errno == ETIMEDOUT) return errSSLWouldBlock;
  return errSSLClosedAbort;
}

void SSL_load_error_strings(void) {}

int SSL_library_init(void) { return 1; }

const SSL_METHOD *TLS_client_method(void) { return &k_tls_client; }

SSL_CTX *SSL_CTX_new(const SSL_METHOD *m) {
  (void)m;
  SSL_CTX *ctx = calloc(1, sizeof(*ctx));
  return ctx;
}

void SSL_CTX_set_verify(SSL_CTX *ctx, int mode, void *cb) {
  (void)ctx;
  (void)mode;
  (void)cb;
}

void SSL_CTX_free(SSL_CTX *ctx) { free(ctx); }

SSL *SSL_new(SSL_CTX *ctx) {
  (void)ctx;
  SSL *s = calloc(1, sizeof(*s));
  if (s) s->fd = -1;
  return s;
}

int SSL_set_fd(SSL *s, int fd) {
  if (!s) return 0;
  s->fd = fd;
  return 1;
}

int SSL_set_tlsext_host_name(SSL *s, const char *name) {
  if (!s || !name) return 0;
  snprintf(s->host, sizeof(s->host), "%s", name);
  return 1;
}

int SSL_connect(SSL *s) {
  if (!s || s->fd < 0) return 0;
  s->ctx = SSLCreateContext(NULL, kSSLClientSide, kSSLStreamType);
  if (!s->ctx) return 0;
  if (SSLSetIOFuncs(s->ctx, st_read, st_write) != noErr) return 0;
  if (SSLSetConnection(s->ctx, s) != noErr) return 0;
  if (s->host[0]) SSLSetPeerDomainName(s->ctx, s->host, strlen(s->host));
  SSLSetSessionOption(s->ctx, kSSLSessionOptionBreakOnServerAuth, true);
  OSStatus err;
  do {
    err = SSLHandshake(s->ctx);
  } while (err == errSSLServerAuthCompleted);
  s->last_os = (int)err;
  if (err != noErr) {
    s->last_err = SSL_ERROR_SSL;
    return 0;
  }
  return 1;
}

int SSL_write(SSL *s, const void *buf, int num) {
  if (!s || !s->ctx || num <= 0) return -1;
  size_t n = 0;
  OSStatus err = SSLWrite(s->ctx, buf, (size_t)num, &n);
  s->last_os = (int)err;
  if (err == noErr && n > 0) return (int)n;
  if (err == errSSLWouldBlock || err == -50) {
    s->last_err = SSL_ERROR_WANT_WRITE;
    return -1;
  }
  s->last_err = SSL_ERROR_SSL;
  return -1;
}

int SSL_read(SSL *s, void *buf, int num) {
  if (!s || !s->ctx || num <= 0) return -1;
  size_t n = 0;
  OSStatus err = SSLRead(s->ctx, buf, (size_t)num, &n);
  s->last_os = (int)err;
  if (err == noErr && n > 0) return (int)n;
  if (err == errSSLWouldBlock || err == -50) {
    s->last_err = SSL_ERROR_WANT_READ;
    return -1;
  }
  if (err == errSSLClosedGraceful || err == errSSLClosedNoNotify) {
    s->last_err = SSL_ERROR_ZERO_RETURN;
    return 0;
  }
  s->last_err = SSL_ERROR_SSL;
  return -1;
}

int SSL_get_error(const SSL *s, int ret) {
  (void)ret;
  if (!s) return SSL_ERROR_SSL;
  return s->last_err;
}

int SSL_get_osstatus(const SSL *s) {
  return s ? s->last_os : 0;
}

int SSL_shutdown(SSL *s) {
  if (s && s->ctx) SSLClose(s->ctx);
  return 1;
}

void SSL_free(SSL *s) {
  if (!s) return;
  if (s->ctx) {
    CFRelease(s->ctx);
    s->ctx = NULL;
  }
  free(s);
}
