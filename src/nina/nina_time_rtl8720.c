#include "FreeRTOS.h"
#include "task.h"
#include "semphr.h"
#include "lwip/tcpip.h"
#include "sntp.h"
#include "nina_time.h"
#include "nina_wifi.h"
static SemaphoreHandle_t done;
static bool pending, running;
static int64_t value;
bool nina_time_init(void) { done = xSemaphoreCreateBinary(); return done != NULL; }
static void link_callback(void *unused) {
    (void)unused;
    bool connected = nina_wifi_status() == 3;
    if (connected && !running) { sntp_init(); running = true; }
    if (!connected && running) { sntp_stop(); running = false; }
}
void nina_time_link_changed(void) { tcpip_callback_with_block(link_callback, NULL, 0); }
static void read_callback(void *unused) {
    (void)unused;
    link_callback(NULL);
    value = 0;
    if (running) {
        long seconds, micros; unsigned int tick;
        /* Pinned sntp.o stores Unix seconds and the FreeRTOS receive tick.
         * Its calendar helper adds 1900/1 itself; avoid that nonstandard tm. */
        sntp_get_lasttime(&seconds, &micros, &tick);
        value = nina_time_sample(seconds, micros,
            (uint32_t)(xTaskGetTickCount() - tick) * portTICK_PERIOD_MS);
    }
    xSemaphoreGive(done);
}
int64_t nina_time_now(void) {
    /* One protocol-task caller; callback storage remains alive on timeout. */
    if (pending) {
        if (xSemaphoreTake(done, 0) != pdTRUE) return 0;
        pending = false;
    }
    pending = true;
    if (tcpip_callback_with_block(read_callback, NULL, 0) != ERR_OK) { pending = false; return 0; }
    if (xSemaphoreTake(done, pdMS_TO_TICKS(100)) != pdTRUE) return 0;
    pending = false; return value;
}
