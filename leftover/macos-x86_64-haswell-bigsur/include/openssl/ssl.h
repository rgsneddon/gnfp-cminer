#ifndef OPENSSL_SSL_H
#define OPENSSL_SSL_H

#include <stddef.h>

#define SSL_VERIFY_NONE 0
#define SSL_ERROR_NONE 0
#define SSL_ERROR_SSL 1
#define SSL_ERROR_WANT_READ 2
#define SSL_ERROR_WANT_WRITE 3
#define SSL_ERROR_SYSCALL 5
#define SSL_ERROR_ZERO_RETURN 6

typedef struct ssl_st SSL;
typedef struct ssl_ctx_st SSL_CTX;
typedef struct ssl_method_st SSL_METHOD;

const SSL_METHOD *TLS_client_method(void);
SSL_CTX *SSL_CTX_new(const SSL_METHOD *m);
void SSL_CTX_set_verify(SSL_CTX *ctx, int mode, void *cb);
void SSL_CTX_free(SSL_CTX *ctx);
SSL *SSL_new(SSL_CTX *ctx);
int SSL_set_fd(SSL *s, int fd);
int SSL_set_tlsext_host_name(SSL *s, const char *name);
int SSL_connect(SSL *s);
int SSL_write(SSL *s, const void *buf, int num);
int SSL_read(SSL *s, void *buf, int num);
int SSL_get_error(const SSL *s, int ret);
int SSL_get_osstatus(const SSL *s);
int SSL_shutdown(SSL *s);
void SSL_free(SSL *s);
void SSL_load_error_strings(void);
int SSL_library_init(void);

#endif
