#ifndef TEST_TLS_SEMPHR_H
#define TEST_TLS_SEMPHR_H
#define pdTRUE 1
typedef void *SemaphoreHandle_t;
SemaphoreHandle_t xSemaphoreCreateMutex(void);
void vSemaphoreDelete(SemaphoreHandle_t p);
int xSemaphoreTake(SemaphoreHandle_t p, unsigned timeout);
int xSemaphoreGive(SemaphoreHandle_t p);
#endif
