#include "nina_sockets.h"
#include <assert.h>
#include <string.h>
static int incoming = -2, partial = 7, failure;
static unsigned opened, closed, sent_bytes;
static uint32_t now;
static bool held[4];
void nina_socket_heap_trace(void) {}
bool nina_socket_backend_init(void) { return true; }
void nina_socket_lock(unsigned id) { assert(!held[id]); held[id] = true; }
void nina_socket_unlock(unsigned id) { assert(held[id]); held[id] = false; }
uint32_t nina_socket_millis(void) { return now++; }
int nina_socket_open_backend(bool udp, const uint8_t ip[4], uint16_t port, bool bind_only) {
    (void)udp; (void)ip; (void)port; (void)bind_only;
    if (failure) return -1;
    ++opened; return 10;
}
bool nina_socket_bind_backend(int fd, uint16_t port) { (void)port; assert(fd == 10); return !failure; }
void nina_socket_close_backend(int fd) { assert(fd == 10); ++closed; }
int nina_socket_send_backend(int fd, const uint8_t *data, size_t n, const uint8_t *ip, uint16_t port) {
    (void)data; (void)port; assert(fd == 10);
    if (failure) return failure;
    int result = !ip && n > (size_t)partial ? partial : (int)n;
    sent_bytes += result; return result;
}
int nina_socket_wait_backend(int fd, unsigned ms) { (void)fd; now += ms; return 1; }
int nina_socket_recv_backend(int fd, uint8_t *data, size_t n, bool peek, uint8_t *ip, uint16_t *port) {
    assert(fd == 10);
    if (incoming < 0) return incoming;
    size_t got = (size_t)incoming < n ? (size_t)incoming : n;
    memset(data, 'x', got);
    if (!peek) incoming = ip ? -2 : incoming - (int)got;
    if (ip) { memcpy(ip, "\1\2\3\4", 4); *port = 123; }
    return got;
}
void socket_tests(void) {
    uint8_t ip[4] = {127, 0, 0, 1}, data[4084] = {0};
    assert(nina_sockets_init());
    for (unsigned i = 0; i < 4; ++i) assert(nina_sockets_allocate() == i);
    assert(nina_sockets_allocate() == 255);
    nina_sockets_close(2); nina_sockets_close(2); assert(nina_sockets_allocate() == 2);
    assert(nina_sockets_connect(0, 0, ip, 80, false));
    assert(nina_sockets_state(0) == 4);
    assert(nina_sockets_write(0, data, 4084) == 4084 && sent_bytes == 4084);
    assert(nina_sockets_sent(0)); assert(nina_sockets_read(0, data, 65535, false) == 0);
    incoming = 5000; assert(nina_sockets_available(0) == 4084);
    assert(nina_sockets_read(0, data, 1, true) == 1 && incoming == 5000);
    assert(nina_sockets_read(0, data, 65535, false) == 4084);
    assert(nina_sockets_read(0, data, 65535, false) == 916);
    assert(nina_sockets_state(0) == 0); assert(nina_sockets_allocate() == 0);
    incoming = -2;
    failure = -1; assert(!nina_sockets_connect(0, 0, ip, 80, false));
    failure = 0; assert(nina_sockets_allocate() == 0);
    assert(!nina_sockets_connect(0, 2, ip, 443, false)); assert(nina_sockets_allocate() == 0);
    assert(nina_sockets_connect(0, 1, ip, 123, true));
    assert(nina_sockets_connect(0, 1, ip, 123, false));
    assert(nina_sockets_insert(0, data, 4000)); assert(nina_sockets_insert(0, data, 84));
    assert(nina_sockets_insert(0, data, 0)); assert(nina_sockets_send_udp(0));
    assert(nina_sockets_insert(0, data, 4084)); assert(!nina_sockets_insert(0, data, 1));
    assert(!nina_sockets_send_udp(0));
    assert(nina_sockets_connect(0, 1, ip, 123, false));
    incoming = 48; assert(nina_sockets_available(0) == 48);
    assert(nina_sockets_read(0, data, 10, false) == 10);
    assert(nina_sockets_available(0) == 38);
    uint16_t port; nina_sockets_remote(0, ip, &port); assert(port == 123 && ip[3] == 4);
    assert(nina_sockets_read(0, data, 4084, false) == 38);
    assert(nina_sockets_available(0) == 0);
    assert(nina_sockets_connect(1, 0, ip, 80, false));
    failure = -2; uint32_t start = now;
    assert(nina_sockets_write(1, data, 20) == 0 && now - start < 4100);
    failure = 0; assert(nina_sockets_state(0) == 0); /* UDP has no TCP state. */
    nina_sockets_close_all(); assert(opened == closed);
    for (unsigned cycle = 0; cycle < 100; ++cycle) {
        assert(nina_sockets_allocate() == 0);
        assert(nina_sockets_connect(0, 0, ip, 80, false));
        nina_sockets_close(0);
    }
    assert(opened == closed);
    incoming = -2;
    assert(nina_sockets_allocate() == 0);
    assert(nina_sockets_connect_tls(0, "example.com", 443));
    assert(nina_sockets_state(0) == 4);
    nina_sockets_remote(0, ip, &port); assert(port == 443 && ip[3] == 4);
    assert(nina_sockets_allocate() == 1);
    assert(!nina_sockets_connect_tls(1, "example.com", 443));
    assert(nina_sockets_allocate() == 1);
    assert(nina_sockets_write(0, data, 100) == 100);
    nina_sockets_close_all(); assert(opened == closed);
    failure = -1;
    assert(nina_sockets_allocate() == 0);
    assert(!nina_sockets_connect_tls(0, "example.com", 443));
    assert(nina_sockets_allocate() == 0); failure = 0;
    assert(nina_sockets_connect_tls(0, "example.com", 443));
    nina_sockets_close_all(); assert(opened == closed);
}

int64_t nina_time_now(void) { return 1789257600LL; }
void *nina_tls_open(const char *name, uint16_t port, int *fd, uint8_t peer[4]) {
    (void)name; (void)port; memcpy(peer, "\1\2\3\4", 4);
    if (failure) return NULL;
    *fd = 10; ++opened; return (void *)1;
}
void nina_tls_free(void *t) { assert(t == (void *)1); }
int nina_tls_send(void *t, const uint8_t *data, size_t n) {
    assert(t); return nina_socket_send_backend(10, data, n, NULL, 0);
}
int nina_tls_recv(void *t, uint8_t *data, size_t n, bool peek) {
    assert(t); return nina_socket_recv_backend(10, data, n, peek, NULL, NULL);
}
