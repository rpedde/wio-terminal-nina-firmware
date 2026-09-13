#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../src/wifi/wifi_ssl_client.c"
static uint8_t peer[4];
static unsigned allocations, opened, closed, tick, step, fail_step;
static int handshake_result, read_result = -2, write_result;
static uint32_t verification_flags;
static bool no_time, no_dns, no_connect, no_memory;
static int (*verify_callback)(void *, mbedtls_x509_crt *, int, uint32_t *);
static void *verify_context;
void *pvPortMalloc(size_t n) { if (no_memory) return NULL; void *p = malloc(n); if (p) ++allocations; return p; }
void vPortFree(void *p) { if (p) { assert(allocations); --allocations; free(p); } }
void vTaskDelay(unsigned ms) { tick += ms; }
uint32_t nina_socket_millis(void) { return tick++; }
int64_t nina_time_now(void) { return no_time ? 0 : 1789257600LL; }
bool nina_wifi_resolve_timeout(const char *name, uint8_t ip[4], unsigned timeout) { assert(timeout <= 4000); assert(!strcmp(name, "example.com")); memset(ip, 1, 4); return !no_dns; }
int nina_socket_open_timed(bool udp, const uint8_t ip[4], uint16_t port, bool bind_only, unsigned ms) {
    assert(!udp && ip && port == 443 && !bind_only && ms <= 3000);
    if (no_connect) return -1;
    ++opened; return 10;
}
void nina_socket_close_backend(int fd) { assert(fd == 10); ++closed; }
int nina_socket_send_backend(int fd, const uint8_t *data, size_t n, const uint8_t *ip, uint16_t port) {
    (void)fd; (void)data; (void)n; (void)ip; (void)port; return -2;
}
int nina_socket_recv_backend(int fd, uint8_t *data, size_t n, bool peek, uint8_t *ip, uint16_t *port) {
    (void)fd; (void)data; (void)n; (void)peek; (void)ip; (void)port; return -2;
}
#define LIFECYCLE(type, name) \
void name##_init(type *p) { memset(p, 0, sizeof(*p)); } \
void name##_free(type *p) { vPortFree(p->memory); p->memory = NULL; }
LIFECYCLE(mbedtls_ssl_context, mbedtls_ssl)
LIFECYCLE(mbedtls_ssl_config, mbedtls_ssl_config)
LIFECYCLE(mbedtls_x509_crt, mbedtls_x509_crt)
LIFECYCLE(mbedtls_entropy_context, mbedtls_entropy)
LIFECYCLE(mbedtls_ctr_drbg_context, mbedtls_ctr_drbg)
static int allocate_step(void **p) { *p = pvPortMalloc(16); return ++step == fail_step ? -1 : 0; }
int mbedtls_entropy_func(void *p, unsigned char *out, size_t n) { (void)p; memset(out, 0, n); return 0; }
int mbedtls_ctr_drbg_random(void *p, unsigned char *out, size_t n) { return mbedtls_entropy_func(p, out, n); }
int mbedtls_ctr_drbg_seed(mbedtls_ctr_drbg_context *p, int (*rng)(void *, unsigned char *, size_t), void *ctx, const unsigned char *data, size_t n) {
    (void)rng; (void)data; (void)n; ((mbedtls_entropy_context *)ctx)->memory = pvPortMalloc(16); return allocate_step(&p->memory);
}
int mbedtls_ssl_config_defaults(mbedtls_ssl_config *p, int a, int b, int c) { (void)a; (void)b; (void)c; return allocate_step(&p->memory); }
int mbedtls_x509_crt_parse(mbedtls_x509_crt *p, const unsigned char *data, size_t n) { assert(n > 4000 && data[n-1] == 0); return allocate_step(&p->memory); }
void mbedtls_ssl_conf_ca_chain(mbedtls_ssl_config *p, mbedtls_x509_crt *roots, void *crl) { (void)p; assert(roots->memory && !crl); }
void mbedtls_ssl_conf_authmode(mbedtls_ssl_config *p, int mode) { (void)p; assert(mode == MBEDTLS_SSL_VERIFY_REQUIRED); }
void mbedtls_ssl_conf_verify(mbedtls_ssl_config *p, int (*verify)(void *, mbedtls_x509_crt *, int, uint32_t *), void *ctx) { (void)p; verify_callback = verify; verify_context = ctx; }
void mbedtls_ssl_conf_rng(mbedtls_ssl_config *p, int (*rng)(void *, unsigned char *, size_t), void *ctx) { (void)p; assert(rng == mbedtls_ctr_drbg_random && ctx); }
void mbedtls_ssl_conf_min_version(mbedtls_ssl_config *p, int major, int minor) { (void)p; assert(major == 3 && minor == 3); }
int mbedtls_ssl_setup(mbedtls_ssl_context *p, const mbedtls_ssl_config *conf) { assert(conf->memory); return allocate_step(&p->memory); }
int mbedtls_ssl_set_hostname(mbedtls_ssl_context *p, const char *name) { (void)p; assert(!strcmp(name, "example.com")); return ++step == fail_step ? -1 : 0; }
void mbedtls_ssl_set_bio(mbedtls_ssl_context *p, void *ctx, int (*send)(void *, const unsigned char *, size_t), int (*recv)(void *, unsigned char *, size_t), void *timeout) { (void)p; assert(ctx && send && recv && !timeout); }
int mbedtls_ssl_handshake(mbedtls_ssl_context *p) { (void)p; return handshake_result; }
uint32_t mbedtls_ssl_get_verify_result(mbedtls_ssl_context *p) { (void)p; return verification_flags; }
int mbedtls_ssl_read(mbedtls_ssl_context *p, unsigned char *out, size_t n) { (void)p; if (read_result > 0) { assert(n >= (size_t)read_result); memset(out, 'x', read_result); } int r = read_result; read_result = -2; return r; }
int mbedtls_ssl_write(mbedtls_ssl_context *p, const unsigned char *out, size_t n) { (void)p; (void)out; return write_result ? write_result : (int)n; }
void mbedtls_platform_set_calloc_free(void *(*alloc)(size_t, size_t), void (*release)(void *)) { assert(alloc && release); }
static void failure(void) { int fd = 99; assert(!nina_tls_open("example.com", 443, &fd, peer)); assert(fd == -1 && !allocations && opened == closed); }
int main(void) {
    assert(nina_time_sample(0, 0, 10000) == 0);
    assert(nina_time_sample(1789257600LL, 900000, 1200) == 1789257602LL);
    assert(nina_time_sample(1789257600LL, -1, 0) == 0);
    assert(nina_time_epoch(2000, 1, 1, 0, 0, 0) == NINA_TIME_MIN);
    assert(nina_time_epoch(2024, 3, 1, 0, 0, 0) - nina_time_epoch(2024, 2, 28, 0, 0, 0) == 172800);
    for (fail_step = 1; fail_step <= 5; ++fail_step) { step = 0; failure(); }
    fail_step = 0; no_memory = true; failure(); no_memory = false;
    no_time = true; unsigned start = tick; failure(); assert(tick - start < 5100); no_time = false;
    no_dns = true; failure(); no_dns = false; no_connect = true; failure(); no_connect = false;
    handshake_result = -100; failure(); handshake_result = -2; start = tick; failure(); assert(tick - start < 8600); handshake_result = 0;
    verification_flags = 16; failure(); verification_flags = 0;
    int fd; assert(!nina_tls_open("1.2.3.4", 443, &fd, peer)); assert(!nina_tls_open("", 443, &fd, peer));
    void *t = nina_tls_open("example.com", 443, &fd, peer); assert(t && fd == 10);
    mbedtls_x509_crt cert = {0}; cert.valid_from = (mbedtls_x509_time){2020,1,1,0,0,0}; cert.valid_to = (mbedtls_x509_time){2030,1,1,0,0,0};
    uint32_t flags = 16; assert(!verify_callback(verify_context, &cert, 0, &flags) && flags == 16);
    cert.valid_to.year = 2025; flags = 16; verify_callback(verify_context, &cert, 1, &flags); assert(flags == (16 | MBEDTLS_X509_BADCERT_EXPIRED));
    cert.valid_from.year = 2027; flags = 0; verify_callback(verify_context, &cert, 2, &flags); assert(flags & MBEDTLS_X509_BADCERT_FUTURE);
    uint8_t data[4084]; read_result = 100;
    assert(nina_tls_recv(t, data, 20, true) == 20); assert(nina_tls_recv(t, data, 100, false) == 100);
    assert(nina_tls_recv(t, data, 100, false) == -2);
    read_result = MBEDTLS_ERR_SSL_PEER_CLOSE_NOTIFY; assert(nina_tls_recv(t, data, 100, false) == 0);
    write_result = 7; assert(nina_tls_send(t, data, 100) == 7);
    write_result = MBEDTLS_ERR_SSL_WANT_WRITE; assert(nina_tls_send(t, data, 100) == -2);
    nina_tls_free(t); nina_socket_close_backend(fd); assert(!allocations && opened == closed);
    puts("TLS backend mock: verification policy, chain dates, failures, deadlines, cleanup and buffered reads passed");
}

unsigned xPortGetFreeHeapSize(void) { return 100000; }
SemaphoreHandle_t xSemaphoreCreateMutex(void) { return pvPortMalloc(1); }
void vSemaphoreDelete(SemaphoreHandle_t p) { vPortFree(p); }
int xSemaphoreTake(SemaphoreHandle_t p, unsigned timeout) { assert(p && timeout == 100); return 1; }
int xSemaphoreGive(SemaphoreHandle_t p) { assert(p); return 1; }
void mbedtls_threading_set_alt(void (*init)(mbedtls_threading_mutex_t *),
    void (*free_mutex)(mbedtls_threading_mutex_t *), int (*lock)(mbedtls_threading_mutex_t *),
    int (*unlock)(mbedtls_threading_mutex_t *)) {
    mbedtls_threading_mutex_t mutex;
    init(&mutex); assert(!lock(&mutex)); assert(!unlock(&mutex)); free_mutex(&mutex);
    assert(lock(&mutex) == MBEDTLS_ERR_THREADING_MUTEX_ERROR);
    no_memory = true; init(&mutex); assert(lock(&mutex) == MBEDTLS_ERR_THREADING_MUTEX_ERROR);
    free_mutex(&mutex); no_memory = false;
}
