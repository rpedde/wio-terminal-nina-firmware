#ifndef TEST_MBEDTLS_H
#define TEST_MBEDTLS_H
#include <stdint.h>
#include <stddef.h>
#define MBEDTLS_ERR_SSL_WANT_READ -2
#define MBEDTLS_ERR_SSL_WANT_WRITE -3
#define MBEDTLS_ERR_SSL_TIMEOUT -4
#define MBEDTLS_ERR_SSL_PEER_CLOSE_NOTIFY -5
#define MBEDTLS_X509_BADCERT_FUTURE 1
#define MBEDTLS_X509_BADCERT_EXPIRED 2
#define MBEDTLS_SSL_IS_CLIENT 0
#define MBEDTLS_SSL_TRANSPORT_STREAM 0
#define MBEDTLS_SSL_PRESET_DEFAULT 0
#define MBEDTLS_SSL_VERIFY_REQUIRED 2
#define MBEDTLS_SSL_MAJOR_VERSION_3 3
#define MBEDTLS_SSL_MINOR_VERSION_3 3
/* Behavior mock only; real verification is covered on hardware. */
typedef struct { int year, mon, day, hour, min, sec; } mbedtls_x509_time;
typedef struct { void *memory; mbedtls_x509_time valid_from, valid_to; } mbedtls_x509_crt;
typedef struct { void *memory; } mbedtls_ssl_context;
typedef struct { void *memory; } mbedtls_ssl_config;
typedef struct { void *memory; } mbedtls_entropy_context;
typedef struct { void *memory; } mbedtls_ctr_drbg_context;
void mbedtls_ssl_init(mbedtls_ssl_context *p);
void mbedtls_ssl_config_init(mbedtls_ssl_config *p);
void mbedtls_x509_crt_init(mbedtls_x509_crt *p);
void mbedtls_entropy_init(mbedtls_entropy_context *p);
void mbedtls_ctr_drbg_init(mbedtls_ctr_drbg_context *p);
void mbedtls_ssl_free(mbedtls_ssl_context *p);
void mbedtls_ssl_config_free(mbedtls_ssl_config *p);
void mbedtls_x509_crt_free(mbedtls_x509_crt *p);
void mbedtls_entropy_free(mbedtls_entropy_context *p);
void mbedtls_ctr_drbg_free(mbedtls_ctr_drbg_context *p);
int mbedtls_entropy_func(void *p, unsigned char *out, size_t n);
int mbedtls_ctr_drbg_random(void *p, unsigned char *out, size_t n);
int mbedtls_ctr_drbg_seed(mbedtls_ctr_drbg_context *p, int (*rng)(void *, unsigned char *, size_t), void *ctx, const unsigned char *data, size_t n);
int mbedtls_ssl_config_defaults(mbedtls_ssl_config *p, int a, int b, int c);
int mbedtls_x509_crt_parse(mbedtls_x509_crt *p, const unsigned char *data, size_t n);
void mbedtls_ssl_conf_ca_chain(mbedtls_ssl_config *p, mbedtls_x509_crt *roots, void *crl);
void mbedtls_ssl_conf_authmode(mbedtls_ssl_config *p, int mode);
void mbedtls_ssl_conf_verify(mbedtls_ssl_config *p, int (*verify)(void *, mbedtls_x509_crt *, int, uint32_t *), void *ctx);
void mbedtls_ssl_conf_rng(mbedtls_ssl_config *p, int (*rng)(void *, unsigned char *, size_t), void *ctx);
void mbedtls_ssl_conf_min_version(mbedtls_ssl_config *p, int major, int minor);
int mbedtls_ssl_setup(mbedtls_ssl_context *p, const mbedtls_ssl_config *conf);
int mbedtls_ssl_set_hostname(mbedtls_ssl_context *p, const char *name);
void mbedtls_ssl_set_bio(mbedtls_ssl_context *p, void *ctx, int (*send)(void *, const unsigned char *, size_t), int (*recv)(void *, unsigned char *, size_t), void *timeout);
int mbedtls_ssl_handshake(mbedtls_ssl_context *p);
uint32_t mbedtls_ssl_get_verify_result(mbedtls_ssl_context *p);
int mbedtls_ssl_read(mbedtls_ssl_context *p, unsigned char *out, size_t n);
int mbedtls_ssl_write(mbedtls_ssl_context *p, const unsigned char *out, size_t n);
void mbedtls_platform_set_calloc_free(void *(*alloc)(size_t, size_t), void (*release)(void *));
#endif
