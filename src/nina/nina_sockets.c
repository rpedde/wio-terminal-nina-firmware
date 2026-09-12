#include "nina_sockets.h"
#include <string.h>
/* Allocation/dispatch has one owner (the protocol task). Per-slot locks also
 * serialize explicit close-all. No backend operation runs in interrupt context. */
typedef struct {
    bool allocated, established, ever_connected, udp, bound, destination, sent;
    int fd, last_error;
    uint8_t ip[4], remote[4];
    uint16_t port, remote_port;
    size_t tx_length, rx_length, rx_offset;
    uint8_t tx[NINA_MAX_RESPONSE_DATA], rx[NINA_MAX_RESPONSE_DATA];
} socket_slot;
static socket_slot slots[NINA_MAX_SOCKETS];
static void release(socket_slot *s) {
    if (s->allocated && s->fd >= 0) nina_socket_close_backend(s->fd);
    memset(s, 0, sizeof(*s)); s->fd = -1;
}
bool nina_sockets_init(void) {
    if (!nina_socket_backend_init()) return false;
    for (unsigned i = 0; i < NINA_MAX_SOCKETS; ++i) slots[i].fd = -1;
    return true;
}
void nina_sockets_close(unsigned id) {
    if (id >= NINA_MAX_SOCKETS) return;
    nina_socket_lock(id); release(&slots[id]); nina_socket_unlock(id);
}
void nina_sockets_close_all(void) {
    for (unsigned i = 0; i < NINA_MAX_SOCKETS; ++i) nina_sockets_close(i);
}
/* Probe without consuming stream data, retaining pending bytes after FIN. */
static int probe(socket_slot *s) {
    uint8_t byte;
    int n = nina_socket_recv_backend(s->fd, &byte, 1, true, NULL, NULL);
    if (n == 0 || n == -1) { s->established = false; s->last_error = n; }
    return n;
}
uint8_t nina_sockets_allocate(void) {
    for (unsigned i = 0; i < NINA_MAX_SOCKETS; ++i) {
        nina_socket_lock(i); socket_slot *s = &slots[i];
        if (s->allocated && !s->udp && s->ever_connected && probe(s) <= 0 && !s->established) release(s);
        if (!s->allocated) {
            s->allocated = true; nina_socket_unlock(i); return i;
        }
        nina_socket_unlock(i);
    }
    return 255;
}
bool nina_sockets_connect(unsigned id, uint8_t mode, const uint8_t ip[4], uint16_t port, bool bind_only) {
    if (id >= NINA_MAX_SOCKETS) return false;
    nina_socket_lock(id); socket_slot *s = &slots[id]; bool ok = false;
    if (!s->allocated) goto done;
    if (mode > 1 || (bind_only && mode != 1)) goto fail;
    if (s->fd >= 0) {
        if (!s->udp || mode != 1) goto fail;
        if (bind_only && (s->bound || !nina_socket_bind_backend(s->fd, port))) goto fail;
    } else s->fd = nina_socket_open_backend(mode == 1, ip, port, bind_only);
    if (s->fd < 0) goto fail;
    if (bind_only) s->bound = true;
    s->udp = mode == 1; s->established = true; s->ever_connected = !s->udp;
    if (!bind_only) {
        memcpy(s->ip, ip, 4); s->port = port; s->destination = true;
        if (!s->udp) { memcpy(s->remote, ip, 4); s->remote_port = port; }
        s->tx_length = 0;
    }
    ok = true; goto done;
fail:
    release(s);
done:
    nina_socket_unlock(id); return ok;
}
size_t nina_sockets_write(unsigned id, const uint8_t *data, size_t n) {
    if (id >= NINA_MAX_SOCKETS || n > NINA_MAX_RESPONSE_DATA) return 0;
    nina_socket_lock(id); socket_slot *s = &slots[id]; size_t written = 0;
    s->sent = false;
    if (s->allocated && s->established && !s->udp) {
        uint32_t start = nina_socket_millis();
        while (written < n && nina_socket_millis() - start < 4000) {
            int result = nina_socket_send_backend(s->fd, data + written, n - written, NULL, 0);
            if (result > 0) { written += result; continue; }
            if (result == -2) { nina_socket_wait_backend(s->fd, 50); continue; }
            s->last_error = result; break;
        }
        s->sent = written == n;
        if (!s->sent) { release(s); s->last_error = -1; }
    }
    nina_socket_unlock(id); return written;
}
bool nina_sockets_insert(unsigned id, const uint8_t *data, size_t n) {
    if (id >= NINA_MAX_SOCKETS) return false;
    nina_socket_lock(id); socket_slot *s = &slots[id];
    bool ok = s->allocated && s->udp && s->destination && n <= NINA_MAX_RESPONSE_DATA - s->tx_length;
    if (ok) { memcpy(s->tx + s->tx_length, data, n); s->tx_length += n; }
    else { s->tx_length = 0; s->destination = false; } /* Never send a truncated datagram. */
    nina_socket_unlock(id); return ok;
}
bool nina_sockets_send_udp(unsigned id) {
    if (id >= NINA_MAX_SOCKETS) return false;
    nina_socket_lock(id); socket_slot *s = &slots[id]; bool ok = false;
    if (s->allocated && s->udp && s->destination) {
        int result = nina_socket_send_backend(s->fd, s->tx, s->tx_length, s->ip, s->port);
        ok = result >= 0 && (size_t)result == s->tx_length;
        s->last_error = ok ? 0 : result;
    }
    s->tx_length = 0; s->sent = ok;
    nina_socket_unlock(id); return ok;
}
static void receive_udp(socket_slot *s) {
    if (s->rx_offset != s->rx_length) return;
    s->rx_offset = s->rx_length = 0;
    int n = nina_socket_recv_backend(s->fd, s->rx, sizeof(s->rx), false, s->remote, &s->remote_port);
    if (n > 0) s->rx_length = n;
    else if (n == -1) s->last_error = n;
}
size_t nina_sockets_available(unsigned id) {
    if (id >= NINA_MAX_SOCKETS) return 0;
    nina_socket_lock(id); socket_slot *s = &slots[id]; size_t n = 0;
    if (s->allocated && s->fd >= 0) {
        if (s->udp) { receive_udp(s); n = s->rx_length - s->rx_offset; }
        else {
            int result = nina_socket_recv_backend(s->fd, s->rx, sizeof(s->rx), true, NULL, NULL);
            if (result > 0) n = result;
            else if (result != -2) s->established = false;
        }
    }
    nina_socket_unlock(id); return n;
}
size_t nina_sockets_read(unsigned id, uint8_t *data, size_t n, bool peek) {
    if (id >= NINA_MAX_SOCKETS || !n) return 0;
    if (n > NINA_MAX_RESPONSE_DATA) n = NINA_MAX_RESPONSE_DATA;
    nina_socket_lock(id); socket_slot *s = &slots[id]; size_t result = 0;
    if (s->allocated && s->fd >= 0) {
        if (s->udp) {
            receive_udp(s); result = s->rx_length - s->rx_offset;
            if (result > n) result = n;
            memcpy(data, s->rx + s->rx_offset, result);
            if (!peek) s->rx_offset += result;
        } else {
            int got = nina_socket_recv_backend(s->fd, data, n, peek, NULL, NULL);
            if (got > 0) result = got;
            else if (got != -2) s->established = false;
        }
    }
    nina_socket_unlock(id); return result;
}
uint8_t nina_sockets_state(unsigned id) {
    if (id >= NINA_MAX_SOCKETS) return 0;
    nina_socket_lock(id); socket_slot *s = &slots[id];
    if (s->allocated && !s->udp && s->fd >= 0) probe(s);
    uint8_t state = s->allocated && !s->udp && s->established ? 4 : 0;
    nina_socket_unlock(id); return state;
}
bool nina_sockets_sent(unsigned id) {
    if (id >= NINA_MAX_SOCKETS) return false;
    nina_socket_lock(id); bool sent = slots[id].sent; nina_socket_unlock(id); return sent;
}
void nina_sockets_remote(unsigned id, uint8_t ip[4], uint16_t *port) {
    memset(ip, 0, 4); *port = 0;
    if (id >= NINA_MAX_SOCKETS) return;
    nina_socket_lock(id); memcpy(ip, slots[id].remote, 4); *port = slots[id].remote_port; nina_socket_unlock(id);
}
