#ifndef NINA_PROTOCOL_H
#define NINA_PROTOCOL_H
#include "nina_transport.h"
#define NINA_MAX_PARAMS 60u
#define NINA_MAX_SOCKETS 4u
#define NINA_FIRMWARE_VERSION "3.3.0+rtl8720.1"
typedef struct { const uint8_t *data; size_t length; } nina_param;
typedef struct {
    uint8_t command, count;
    nina_param params[NINA_MAX_PARAMS];
} nina_request;
typedef enum { NINA_PARSE_OK, NINA_PARSE_INVALID, NINA_PARSE_UNKNOWN } nina_parse_result;
nina_parse_result nina_protocol_parse(const uint8_t *data, size_t length, nina_request *out);
/* Returns bytes through END_CMD; zero pads the full aligned wire frame. */
size_t nina_protocol_reply(uint8_t command, const nina_param *params, size_t count,
                           uint8_t *out, size_t capacity);
size_t nina_protocol_error(uint8_t *out);
uint16_t nina_read_le16(const uint8_t *p);
void nina_write_le16(uint8_t *p, uint16_t value);
void nina_write_le32(uint8_t *p, uint32_t value);
#endif
