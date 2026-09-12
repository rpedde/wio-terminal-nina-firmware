#ifndef NINA_SERVER_H
#define NINA_SERVER_H
#include "nina_transport.h"
#ifdef __cplusplus
extern "C" {
#endif
bool nina_server_start(void);
size_t nina_server_reply(const uint8_t *, size_t, uint8_t *, nina_transport_counters *);
#ifdef __cplusplus
}
#endif
#endif
