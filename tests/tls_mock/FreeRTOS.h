#include <stddef.h>
#define pdMS_TO_TICKS(ms) (ms)
void *pvPortMalloc(size_t n);
void vPortFree(void *p);
unsigned xPortGetFreeHeapSize(void);
