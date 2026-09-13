/* Verified NINA TLS backend, refactored from the original ssl_client adapter.
 * Original Copyright (C) 2006-2015 ARM Limited and (C) 2017 Evandro Luis
 * Copercini, Apache-2.0. No RPC objects or caller-owned certificate pointers. */
#include "ameba_soc.h"
#include "mbedtls/ssl.h"
#include "mbedtls/entropy.h"
#include "mbedtls/ctr_drbg.h"
#include "mbedtls/platform.h"
#include "mbedtls/threading.h"
#include "semphr.h"
#include "../nina/nina_sockets.h"
#include "../nina/nina_wifi.h"
#include "../nina/nina_time.h"
#include "../nina/nina_roots.h"
#include "FreeRTOS.h"
#include "task.h"
#include <string.h>
#include <stdlib.h>

typedef struct {
    mbedtls_ssl_context ssl;
    mbedtls_ssl_config config;
    mbedtls_entropy_context entropy;
    mbedtls_ctr_drbg_context random;
    mbedtls_x509_crt roots;
    int fd;
    uint32_t start;
    int64_t utc;
    bool handshake;
    size_t length, offset;
    uint8_t buffer[NINA_MAX_RESPONSE_DATA];
} nina_tls;
#define TLS_BUDGET_MS 8500u
static bool expired(nina_tls *t) {
    return t->handshake && nina_socket_millis() - t->start >= TLS_BUDGET_MS;
}
static bool retry(int n) { return n == MBEDTLS_ERR_SSL_WANT_READ || n == MBEDTLS_ERR_SSL_WANT_WRITE; }
static int send_bio(void *context, const unsigned char *data, size_t n) {
    nina_tls *t = context;
    if (expired(t)) return MBEDTLS_ERR_SSL_TIMEOUT;
    int result = nina_socket_send_backend(t->fd, data, n, NULL, 0);
    return result == -2 ? MBEDTLS_ERR_SSL_WANT_WRITE : result;
}
static int recv_bio(void *context, unsigned char *data, size_t n) {
    nina_tls *t = context;
    if (expired(t)) return MBEDTLS_ERR_SSL_TIMEOUT;
    int result = nina_socket_recv_backend(t->fd, data, n, false, NULL, NULL);
    return result == -2 ? MBEDTLS_ERR_SSL_WANT_READ : result;
}
static int64_t cert_time(const mbedtls_x509_time *time) {
    return nina_time_epoch(time->year, time->mon, time->day, time->hour, time->min, time->sec);
}
static int verify_dates(void *context, mbedtls_x509_crt *cert, int depth, uint32_t *flags) {
    (void)depth;
    nina_tls *t = context;
    /* The pinned vendor library disables MBEDTLS_HAVE_TIME_DATE. Apply dates
     * to EVERY chain member; never clear signature, trust or hostname flags. */
    if (t->utc < NINA_TIME_MIN || t->utc < cert_time(&cert->valid_from))
        *flags |= MBEDTLS_X509_BADCERT_FUTURE;
    if (!cert_time(&cert->valid_to) || t->utc > cert_time(&cert->valid_to))
        *flags |= MBEDTLS_X509_BADCERT_EXPIRED;
    if (expired(t)) return MBEDTLS_ERR_SSL_TIMEOUT;
    return 0;
}
void nina_tls_free(void *context) {
    nina_tls *t = context;
    if (!t) return;
    mbedtls_ssl_free(&t->ssl);
    mbedtls_ssl_config_free(&t->config);
    mbedtls_x509_crt_free(&t->roots);
    mbedtls_ctr_drbg_free(&t->random);
    mbedtls_entropy_free(&t->entropy);
    memset(t, 0, sizeof(*t)); vPortFree(t);
}
static void *tls_calloc(size_t count, size_t size) {
    if (size && count > SIZE_MAX / size) return NULL;
    void *p = pvPortMalloc(count * size);
    if (p) memset(p, 0, count * size);
    return p;
}
static void tls_mutex_init(mbedtls_threading_mutex_t *mutex) {
    mutex->mutex = xSemaphoreCreateMutex(); mutex->is_valid = mutex->mutex != NULL;
}
static void tls_mutex_free(mbedtls_threading_mutex_t *mutex) {
    if (mutex->is_valid) vSemaphoreDelete(mutex->mutex);
    mutex->mutex = NULL; mutex->is_valid = 0;
}
static int tls_mutex_lock(mbedtls_threading_mutex_t *mutex) {
    return mutex->is_valid && xSemaphoreTake(mutex->mutex, pdMS_TO_TICKS(100)) == pdTRUE ?
        0 : MBEDTLS_ERR_THREADING_MUTEX_ERROR;
}
static int tls_mutex_unlock(mbedtls_threading_mutex_t *mutex) {
    return mutex->is_valid && xSemaphoreGive(mutex->mutex) == pdTRUE ?
        0 : MBEDTLS_ERR_THREADING_MUTEX_ERROR;
}
static void tls_platform_init(void) {
    /* One protocol-task owner. Register before initializing any TLS object;
     * vendor defaults are dummy mutex callbacks which make entropy fail. */
    static bool initialized;
    if (!initialized) {
        mbedtls_platform_set_calloc_free(tls_calloc, vPortFree);
        mbedtls_threading_set_alt(tls_mutex_init, tls_mutex_free, tls_mutex_lock, tls_mutex_unlock);
        initialized = true;
    }
}
void *nina_tls_open(const char *hostname, uint16_t port, int *fd, uint8_t peer[4]) {
    int stage = 0, code = 0;
    *fd = -1;
    if (!hostname || !hostname[0] || strlen(hostname) > 255) return NULL;
    /* Numeric destinations are forbidden even in a hostname envelope. */
    if (strspn(hostname, "0123456789.") == strlen(hostname) || strchr(hostname, ':')) return NULL;
    nina_tls *t = pvPortMalloc(sizeof(*t));
    if (!t) return NULL;
    memset(t, 0, sizeof(*t));
    tls_platform_init();
    t->fd = -1; t->start = nina_socket_millis(); t->handshake = true;
    mbedtls_ssl_init(&t->ssl); mbedtls_ssl_config_init(&t->config);
    mbedtls_x509_crt_init(&t->roots); mbedtls_ctr_drbg_init(&t->random);
    mbedtls_entropy_init(&t->entropy);
    do {
        t->utc = nina_time_now();
        if (t->utc >= NINA_TIME_MIN) break;
        vTaskDelay(pdMS_TO_TICKS(20));
    } while (nina_socket_millis() - t->start < 5000);
    if (t->utc < NINA_TIME_MIN) goto fail;
    uint8_t ip[4];
    unsigned elapsed = nina_socket_millis() - t->start;
    if (elapsed >= TLS_BUDGET_MS) goto fail;
    unsigned remaining = TLS_BUDGET_MS - elapsed;
    if (!nina_wifi_resolve_timeout(hostname, ip, remaining > 4000 ? 4000 : remaining)) goto fail;
    elapsed = nina_socket_millis() - t->start;
    if (elapsed >= TLS_BUDGET_MS) goto fail;
    remaining = TLS_BUDGET_MS - elapsed;
    t->fd = nina_socket_open_timed(false, ip, port, false, remaining > 3000 ? 3000 : remaining);
    if (t->fd < 0 || expired(t)) goto fail;
    static const unsigned char personalization[] = "wio-nina-tls";
    stage = 1;
    if ((code = mbedtls_ctr_drbg_seed(&t->random, mbedtls_entropy_func, &t->entropy,
                              personalization, sizeof(personalization))) != 0) goto fail;
    stage = 2;
    if ((code = mbedtls_ssl_config_defaults(&t->config, MBEDTLS_SSL_IS_CLIENT,
        MBEDTLS_SSL_TRANSPORT_STREAM, MBEDTLS_SSL_PRESET_DEFAULT)) != 0) goto fail;
    /* Any skipped/unsupported root is a build/runtime error, not partial trust. */
    stage = 3;
    if ((code = mbedtls_x509_crt_parse(&t->roots, nina_roots_pem, sizeof(nina_roots_pem))) != 0) goto fail;
    mbedtls_ssl_conf_ca_chain(&t->config, &t->roots, NULL);
    mbedtls_ssl_conf_authmode(&t->config, MBEDTLS_SSL_VERIFY_REQUIRED);
    mbedtls_ssl_conf_verify(&t->config, verify_dates, t);
    mbedtls_ssl_conf_rng(&t->config, mbedtls_ctr_drbg_random, &t->random);
    mbedtls_ssl_conf_min_version(&t->config, MBEDTLS_SSL_MAJOR_VERSION_3, MBEDTLS_SSL_MINOR_VERSION_3);
    stage = 4;
    if (mbedtls_ssl_setup(&t->ssl, &t->config) != 0 ||
        mbedtls_ssl_set_hostname(&t->ssl, hostname) != 0) goto fail;
    mbedtls_ssl_set_bio(&t->ssl, t, send_bio, recv_bio, NULL);
    stage = 5;
    int result;
    while ((result = mbedtls_ssl_handshake(&t->ssl)) != 0) {
        code = result;
        if (!retry(result) || expired(t)) goto fail;
        vTaskDelay(pdMS_TO_TICKS(1));
    }
    if (expired(t) || mbedtls_ssl_get_verify_result(&t->ssl) != 0) goto fail;
    t->handshake = false; *fd = t->fd; memcpy(peer, ip, 4); return t;
fail:
#ifdef NINA_TLS_DEBUG
    printf("NINA TLS failure stage=%d code=%d flags=%u ms=%u heap=%u\n", stage, code,
        (unsigned)mbedtls_ssl_get_verify_result(&t->ssl),
        (unsigned)(nina_socket_millis() - t->start), (unsigned)xPortGetFreeHeapSize());
#else
    (void)stage; (void)code;
#endif
    if (t->fd >= 0) nina_socket_close_backend(t->fd);
    nina_tls_free(t); return NULL;
}
int nina_tls_send(void *context, const uint8_t *data, size_t n) {
    nina_tls *t = context;
    int result = mbedtls_ssl_write(&t->ssl, data, n);
    return retry(result) ? -2 : result < 0 ? -1 : result;
}
int nina_tls_recv(void *context, uint8_t *data, size_t n, bool peek) {
    nina_tls *t = context;
    if (t->offset == t->length) {
        t->offset = t->length = 0;
        int result = mbedtls_ssl_read(&t->ssl, t->buffer, sizeof(t->buffer));
        if (retry(result)) return -2;
        if (result == 0 || result == MBEDTLS_ERR_SSL_PEER_CLOSE_NOTIFY) return 0;
        if (result < 0) return -1;
        t->length = result;
    }
    if (n > t->length - t->offset) n = t->length - t->offset;
    memcpy(data, t->buffer + t->offset, n);
    if (!peek) t->offset += n;
    return (int)n;
}
