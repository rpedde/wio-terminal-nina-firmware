#ifndef NINA_SOCKETS_H
#define NINA_SOCKETS_H
#include "nina_protocol.h"
/* Backend results: -2 means would-block, -1 fatal, zero TCP EOF. */
bool nina_socket_backend_init(void);
/* Optional dedicated-UART heap snapshot; no extra wire protocol command. */
void nina_socket_heap_trace(void);
void nina_socket_lock(unsigned id);
void nina_socket_unlock(unsigned id);
uint32_t nina_socket_millis(void);
int nina_socket_open_backend(bool udp, const uint8_t ip[4], uint16_t port, bool bind_only);
void nina_socket_close_backend(int fd);
bool nina_socket_bind_backend(int fd, uint16_t port);
int nina_socket_send_backend(int fd, const uint8_t *data, size_t n, const uint8_t *ip, uint16_t port);
int nina_socket_recv_backend(int fd, uint8_t *data, size_t n, bool peek, uint8_t *ip, uint16_t *port);
int nina_socket_wait_backend(int fd, unsigned ms);
#define NINA_MAX_TLS_SOCKETS 1
/* TLS owns only its cryptographic state; the slot owns the descriptor. */
void *nina_tls_open(const char *hostname, uint16_t port, int *fd, uint8_t peer[4]);
void nina_tls_free(void *tls);
int nina_tls_send(void *tls, const uint8_t *data, size_t n);
int nina_tls_recv(void *tls, uint8_t *data, size_t n, bool peek);
int nina_socket_open_timed(bool udp, const uint8_t ip[4], uint16_t port, bool bind_only, unsigned timeout);
bool nina_sockets_connect_tls(unsigned id, const char *hostname, uint16_t port);
bool nina_sockets_init(void);
void nina_sockets_close_all(void);
uint8_t nina_sockets_allocate(void);
void nina_sockets_close(unsigned id);
bool nina_sockets_connect(unsigned id, uint8_t mode, const uint8_t ip[4], uint16_t port, bool bind_only);
size_t nina_sockets_write(unsigned id, const uint8_t *data, size_t n);
bool nina_sockets_insert(unsigned id, const uint8_t *data, size_t n);
bool nina_sockets_send_udp(unsigned id);
size_t nina_sockets_available(unsigned id);
size_t nina_sockets_read(unsigned id, uint8_t *data, size_t n, bool peek);
uint8_t nina_sockets_state(unsigned id);
bool nina_sockets_sent(unsigned id);
void nina_sockets_remote(unsigned id, uint8_t ip[4], uint16_t *port);
#endif
