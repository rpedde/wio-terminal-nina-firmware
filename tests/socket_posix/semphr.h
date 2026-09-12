typedef pthread_mutex_t *SemaphoreHandle_t;
static pthread_mutex_t test_locks[4];
static inline SemaphoreHandle_t xSemaphoreCreateMutex(void) {
    static unsigned next; pthread_mutex_t *p = &test_locks[next++];
    pthread_mutex_init(p, NULL); return p;
}
#define xSemaphoreTake(p, wait) pthread_mutex_lock(p)
#define xSemaphoreGive(p) pthread_mutex_unlock(p)
