/* lwIP BSD sockets marshal network work to tcpip_thread internally. All
 * descriptors stay nonblocking; connect and writes have wall-clock budgets. */
#include "FreeRTOS.h"
#include "task.h"
#include "semphr.h"
#include "lwip/sockets.h"
#include "nina_sockets.h"
#include <string.h>
#include <errno.h>
#ifdef NINA_HEAP_DIAGNOSTICS
#include <stdio.h>
#endif
void nina_socket_heap_trace(void) {
#ifdef NINA_HEAP_DIAGNOSTICS
    printf("NINA_HEAP free=%d minimum=%d\n",
        (int)xPortGetFreeHeapSize(), (int)xPortGetMinimumEverFreeHeapSize());
#endif
}
static SemaphoreHandle_t locks[NINA_MAX_SOCKETS];
bool nina_socket_backend_init(void) {
    for (unsigned i = 0; i < NINA_MAX_SOCKETS; ++i) {
        locks[i] = xSemaphoreCreateMutex();
        if (!locks[i]) return false;
    }
    return true;
}
void nina_socket_lock(unsigned id) { xSemaphoreTake(locks[id], portMAX_DELAY); }
void nina_socket_unlock(unsigned id) { xSemaphoreGive(locks[id]); }
uint32_t nina_socket_millis(void) { return xTaskGetTickCount() * portTICK_PERIOD_MS; }
static int result(int n) { return n >= 0 ? n : ((errno == EWOULDBLOCK || errno == EAGAIN) ? -2 : -1); }
static struct sockaddr_in address(const uint8_t ip[4], uint16_t port) {
    struct sockaddr_in a = {0}; a.sin_family = AF_INET; a.sin_port = htons(port);
    if (ip) memcpy(&a.sin_addr.s_addr, ip, 4);
    return a;
}
int nina_socket_wait_backend(int fd, unsigned ms) {
    fd_set writes; FD_ZERO(&writes); FD_SET(fd, &writes);
    struct timeval timeout = {ms / 1000, (ms % 1000) * 1000};
    return lwip_select(fd + 1, NULL, &writes, NULL, &timeout);
}
int nina_socket_open_backend(bool udp, const uint8_t ip[4], uint16_t port, bool bind_only) {
    return nina_socket_open_timed(udp, ip, port, bind_only, 4000);
}
int nina_socket_open_timed(bool udp, const uint8_t ip[4], uint16_t port, bool bind_only, unsigned timeout) {
    int fd = lwip_socket(AF_INET, udp ? SOCK_DGRAM : SOCK_STREAM, 0);
    if (fd < 0) return -1;
    unsigned long nonblocking = 1;
    if (lwip_ioctl(fd, FIONBIO, &nonblocking)) goto fail;
    struct sockaddr_in a = address(bind_only ? NULL : ip, port);
    if (bind_only) { if (lwip_bind(fd, (struct sockaddr *)&a, sizeof(a))) goto fail; }
    else if (!udp && lwip_connect(fd, (struct sockaddr *)&a, sizeof(a))) {
        if (errno != EINPROGRESS && errno != EWOULDBLOCK) goto fail;
        if (nina_socket_wait_backend(fd, timeout) <= 0) goto fail;
        int error = 0; socklen_t size = sizeof(error);
        if (lwip_getsockopt(fd, SOL_SOCKET, SO_ERROR, &error, &size) || error) goto fail;
    }
    return fd;
fail:
    lwip_close(fd); return -1;
}
bool nina_socket_bind_backend(int fd, uint16_t port) {
    struct sockaddr_in a = address(NULL, port);
    return lwip_bind(fd, (struct sockaddr *)&a, sizeof(a)) == 0;
}
void nina_socket_close_backend(int fd) { lwip_close(fd); }
int nina_socket_send_backend(int fd, const uint8_t *data, size_t n, const uint8_t *ip, uint16_t port) {
    if (!ip) return result(lwip_send(fd, data, n, 0));
    struct sockaddr_in a = address(ip, port);
    return result(lwip_sendto(fd, data, n, 0, (struct sockaddr *)&a, sizeof(a)));
}
int nina_socket_recv_backend(int fd, uint8_t *data, size_t n, bool peek, uint8_t *ip, uint16_t *port) {
    if (!ip) return result(lwip_recv(fd, data, n, peek ? MSG_PEEK : 0));
    struct sockaddr_in a; socklen_t size = sizeof(a);
    int got = lwip_recvfrom(fd, data, n, 0, (struct sockaddr *)&a, &size);
    if (got >= 0) { memcpy(ip, &a.sin_addr.s_addr, 4); *port = ntohs(a.sin_port); }
    return result(got);
}
