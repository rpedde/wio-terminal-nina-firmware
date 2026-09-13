#include "nina_protocol.h"
#include <string.h>

uint16_t nina_read_le16(const uint8_t *p) { return (uint16_t)p[0] | (uint16_t)p[1] << 8; }
void nina_write_le16(uint8_t *p, uint16_t v) { p[0] = v; p[1] = v >> 8; }
void nina_write_le32(uint8_t *p, uint32_t v) {
    for (unsigned i = 0; i < 4; ++i) p[i] = v >> (8 * i);
}
static bool wide(uint8_t c) { return c >= 0x44 && c <= 0x46; }
static bool sized(const nina_request *r, unsigned i, size_t n) { return r->params[i].length == n; }
static bool string(const nina_request *r, unsigned i, size_t max) {
    return r->params[i].length <= max &&
        !memchr(r->params[i].data, 0, r->params[i].length);
}
static int count(uint8_t c) {
    switch (c) {
    case 0x10: case 0x16: case 0x21: case 0x22: case 0x32: case 0x33:
    case 0x34: case 0x3c: case 0x3d: case 0x23: case 0x24: case 0x25: case 0x26: return 1;
    case 0x2c: case 0x11: case 0x3e: case 0x44: case 0x45: case 0x46: return 2;
    case 0x14: case 0x2d: return 4;
    case 0x28: return 3;
    case 0x2a: case 0x2b: case 0x2e: case 0x2f: case 0x39: case 0x3a: return 1;
    case 0x15: return 3;
    case 0x3b: case 0x3f: case 0x20: case 0x27:
    case 0x30: case 0x35: case 0x36: case 0x37: return 0;
    default: return -1;
    }
}
nina_parse_result nina_protocol_parse(const uint8_t *p, size_t n, nina_request *r) {
    if (!p || !r || n < 4 || n > NINA_SPI_BUFFER_SIZE || (n & 3) ||
        p[0] != 0xe0 || (p[1] & 0x80)) return NINA_PARSE_INVALID;
    int expected = count(p[1]);
    if (expected < 0) return NINA_PARSE_UNKNOWN;
    if (p[1] == 0x2d && p[2] == 5) expected = 5;
    if (p[1] == 0x28 && p[2] == 4) expected = 4;
    if (p[2] != expected) return NINA_PARSE_INVALID;
    memset(r, 0, sizeof(*r));
    r->command = p[1]; r->count = p[2];
    size_t pos = 3, width = wide(p[1]) ? 2 : 1;
    for (unsigned i = 0; i < r->count; ++i) {
        if (width > n - pos) return NINA_PARSE_INVALID;
        size_t len = p[pos++];
        if (width == 2) len = (len << 8) | p[pos++];
        if (len > n - pos) return NINA_PARSE_INVALID;
        r->params[i].data = p + pos; r->params[i].length = len; pos += len;
    }
    if (pos >= n || p[pos++] != 0xee || ((pos + 3) & ~(size_t)3) != n)
        return NINA_PARSE_INVALID;
    /* ESP32SPI 11.1.4 reuses _sendbuf without clearing alignment bytes.
     * Ignore up to three bytes after END, but reject extra whole words. */
    bool valid = true;
    switch (r->command) {
    case 0x10: valid = string(r, 0, 32) && r->params[0].length; break;
    case 0x11: valid = string(r, 0, 32) && r->params[0].length && string(r, 1, 63); break;
    case 0x16: case 0x34: valid = string(r, 0, 255) && r->params[0].length; break;
    case 0x14: case 0x15:
        valid = sized(r, 0, 1);
        for (unsigned i = 1; i < r->count; ++i) valid = valid && sized(r, i, 4);
        break;
    case 0x21: case 0x22: case 0x23: case 0x24: case 0x25: case 0x26:
    case 0x32: case 0x33: case 0x3c: case 0x3d:
        valid = sized(r, 0, 1); break;
    case 0x28: case 0x2d: {
        unsigned base = r->count - (r->command == 0x2d ? 4 : 3);
        valid = true;
        if (base) valid = r->command == 0x2d ? (string(r, 0, 255) && r->params[0].length) : sized(r, 0, 4);
        if (r->command == 0x2d) { valid = valid && sized(r, base, 4); ++base; }
        valid = valid && sized(r, base, 2) && sized(r, base + 1, 1) &&
            r->params[base + 1].data[0] < NINA_MAX_SOCKETS && sized(r, base + 2, 1);
        break;
    }
    case 0x2a: case 0x2b: case 0x2c: case 0x2e: case 0x2f: case 0x39: case 0x3a:
        valid = sized(r, 0, 1) && r->params[0].data[0] < NINA_MAX_SOCKETS &&
            (r->command != 0x2c || sized(r, 1, 1)); break;
    case 0x3e: valid = sized(r, 0, 4) && sized(r, 1, 1); break;
    case 0x44: case 0x45: case 0x46:
        valid = sized(r, 0, 1) && r->params[0].data[0] < NINA_MAX_SOCKETS &&
            (r->command == 0x45 ? sized(r, 1, 2) : r->params[1].length <= NINA_MAX_RESPONSE_DATA);
        break;
    default: break;
    }
    return valid ? NINA_PARSE_OK : NINA_PARSE_INVALID;
}
size_t nina_protocol_error(uint8_t *out) {
    out[0] = 0xef; out[1] = 0; out[2] = 0xee; out[3] = 0; return 3;
}
size_t nina_protocol_reply(uint8_t command, const nina_param *params, size_t count,
                           uint8_t *out, size_t capacity) {
    size_t size = 4, width = command == 0x45 ? 2 : 1;
    if (!out || count > NINA_MAX_PARAMS || (count && !params) || (command & 0x80)) return 0;
    if (capacity > NINA_SPI_BUFFER_SIZE) capacity = NINA_SPI_BUFFER_SIZE;
    for (size_t i = 0; i < count; ++i) {
        if (params[i].length > (width == 2 ? NINA_MAX_RESPONSE_DATA : 255) ||
            (params[i].length && !params[i].data)) return 0;
        size += width + params[i].length;
    }
    size_t aligned = (size + 3) & ~(size_t)3;
    if (aligned > capacity) return 0;
    out[0] = 0xe0; out[1] = command | 0x80; out[2] = count;
    size_t pos = 3;
    for (size_t i = 0; i < count; ++i) {
        if (width == 2) out[pos++] = params[i].length >> 8;
        out[pos++] = params[i].length;
        if (params[i].length) memcpy(out + pos, params[i].data, params[i].length);
        pos += params[i].length;
    }
    out[pos++] = 0xee;
    memset(out + pos, 0, aligned - pos);
    return pos;
}
