#include "nina_dhcp.h"
#include <string.h>
size_t nina_dhcp_hostname(uint8_t *packet, size_t length, size_t capacity, const char *name) {
    static const uint8_t cookie[] = {99, 130, 83, 99};
    if (!packet || !name || length > capacity || length < 241 || packet[0] != 1 ||
        memcmp(packet + 236, cookie, 4)) return 0;
    size_t n = strlen(name), pos = 240;
    if (!n || n > 255) return 0;
    while (pos < length) {
        uint8_t option = packet[pos];
        if (option == 255) {
            if (n + 3 > capacity - pos) return 0;
            packet[pos++] = 12; packet[pos++] = n;
            memcpy(packet + pos, name, n); pos += n; packet[pos++] = 255;
            if (pos < length) memset(packet + pos, 0, length - pos);
            return pos > length ? pos : length;
        }
        if (option == 0) { ++pos; continue; }
        if (length - pos < 2 || packet[pos + 1] > length - pos - 2) return 0;
        if (option == 12) return length;
        pos += 2 + packet[pos + 1];
    }
    return 0;
}
