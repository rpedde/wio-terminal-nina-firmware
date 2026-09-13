#include "semphr.h"
#define MBEDTLS_ERR_THREADING_MUTEX_ERROR -30
typedef struct { SemaphoreHandle_t mutex; char is_valid; } mbedtls_threading_mutex_t;
void mbedtls_threading_set_alt(void (*init)(mbedtls_threading_mutex_t *),
    void (*free_mutex)(mbedtls_threading_mutex_t *), int (*lock)(mbedtls_threading_mutex_t *),
    int (*unlock)(mbedtls_threading_mutex_t *));
