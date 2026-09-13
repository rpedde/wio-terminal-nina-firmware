#include "nina_sockets.h"
#include <sys/socket.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <assert.h>
#include <string.h>
#include <stdio.h>
static int listener(bool udp, uint16_t *port) {
    int fd = socket(AF_INET, udp ? SOCK_DGRAM : SOCK_STREAM, 0); assert(fd >= 0);
    struct sockaddr_in a = {0}; a.sin_family = AF_INET; a.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    assert(!bind(fd, (struct sockaddr *)&a, sizeof(a)));
    socklen_t size = sizeof(a); assert(!getsockname(fd, (struct sockaddr *)&a, &size));
    *port = ntohs(a.sin_port); if (!udp) assert(!listen(fd, 1)); return fd;
}
static void wait_data(unsigned id) {
    uint32_t start = nina_socket_millis();
    while (!nina_sockets_available(id)) { assert(nina_socket_millis() - start < 1000); usleep(1000); }
}
int main(void) {
    assert(nina_sockets_init()); uint8_t ip[4] = {127, 0, 0, 1}, data[4084]; uint16_t port;
    int server = listener(false, &port);
    assert(nina_sockets_allocate() == 0); assert(nina_sockets_connect(0, 0, ip, port, false));
    int peer = accept(server, NULL, NULL); assert(peer >= 0);
    const char request[] = "GET / HTTP/1.0\r\nHost: localhost\r\n\r\n";
    assert(nina_sockets_write(0, (const uint8_t *)request, sizeof(request)) == sizeof(request));
    assert(recv(peer, data, sizeof(data), 0) == sizeof(request) && !memcmp(data, request, sizeof(request)));
    memset(data, 'a', sizeof(data)); assert(send(peer, data, sizeof(data), 0) == sizeof(data));
    assert(send(peer, data, 1000, 0) == 1000); shutdown(peer, SHUT_WR);
    size_t total = 0;
    while (total < 5084) { wait_data(0); size_t n = nina_sockets_read(0, data, 65535, false); assert(n > 0 && n <= 4084); total += n; }
    assert(nina_sockets_read(0, data, 20, false) == 0); assert(nina_sockets_state(0) == 0);
    nina_sockets_close_all(); close(peer); close(server);
    server = listener(true, &port); assert(nina_sockets_allocate() == 0);
    assert(nina_sockets_connect(0, 1, ip, port, false));
    assert(nina_sockets_connect(0, 1, ip, 0, true));
    uint8_t ntp[48] = {0x1b}; assert(nina_sockets_insert(0, ntp, 48)); assert(nina_sockets_send_udp(0));
    struct sockaddr_in from; socklen_t size = sizeof(from);
    assert(recvfrom(server, data, sizeof(data), 0, (struct sockaddr *)&from, &size) == 48 && data[0] == 0x1b);
    ntp[0] = 0x24; assert(sendto(server, ntp, 48, 0, (struct sockaddr *)&from, size) == 48);
    wait_data(0); assert(nina_sockets_available(0) == 48);
    assert(nina_sockets_read(0, data, 1, true) == 1 && data[0] == 0x24);
    assert(nina_sockets_read(0, data, 12, false) == 12 && data[0] == 0x24);
    assert(nina_sockets_read(0, data, 100, false) == 36);
    uint16_t remote; nina_sockets_remote(0, data, &remote); assert(remote == port && !memcmp(data, ip, 4));
    nina_sockets_close_all(); close(server);
    /* Refused connection must release the reservation. */
    assert(nina_sockets_allocate() == 0); assert(!nina_sockets_connect(0, 0, ip, port, false));
    assert(nina_sockets_allocate() == 0); nina_sockets_close_all();
    puts("Production socket backend: live TCP stream/EOF and UDP datagram round trips passed");
}

void *nina_tls_open(const char *name, uint16_t port, int *fd, uint8_t peer[4]) {
    (void)name; (void)port; (void)fd; (void)peer; return NULL;
}
void nina_tls_free(void *t) { (void)t; }
int nina_tls_send(void *t, const uint8_t *data, size_t n) { (void)t; (void)data; (void)n; return -1; }
int nina_tls_recv(void *t, uint8_t *data, size_t n, bool peek) { (void)t; (void)data; (void)n; (void)peek; return -1; }
