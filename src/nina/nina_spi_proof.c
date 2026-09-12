#include "nina_spi_proof.h"
#include <string.h>

/* Deliberately just two literal zero-parameter commands. The general command
 * parser, server and networking backends belong to later phases. */
size_t nina_spi_proof_reply(const uint8_t *request, size_t length,
                            uint8_t *reply, nina_transport_counters *counters)
{
    static const uint8_t version[] = NINA_PROOF_VERSION;
    if (length != 4 || request[0] != 0xe0 || request[2] != 0 ||
        request[3] != 0xee || (request[1] & 0x80)) {
        ++counters->parse_failures;
    } else if (request[1] == 0x37 || request[1] == 0x20) {
        reply[0] = 0xe0;
        reply[1] = request[1] | 0x80;
        reply[2] = 1;
        if (request[1] == 0x37) {
            reply[3] = sizeof(version);
            memcpy(reply + 4, version, sizeof(version));
            reply[4 + sizeof(version)] = 0xee;
            return 5 + sizeof(version);
        }
        reply[3] = 1;
        reply[4] = 0; /* Fixed WL_IDLE_STATUS; Wi-Fi is not connected. */
        reply[5] = 0xee;
        return 6;
    } else {
        ++counters->unknown_commands;
    }
    reply[0] = 0xef;
    reply[1] = 0;
    reply[2] = 0xee;
    return 3;
}
