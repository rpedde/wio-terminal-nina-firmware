#ifndef NINA_SPI_PROOF_H
#define NINA_SPI_PROOF_H
#include "nina_transport.h"
#ifdef __cplusplus
extern "C" {
#endif
#define NINA_PROOF_VERSION "3.3.0+rtl8720.1"
size_t nina_spi_proof_reply(const uint8_t *request, size_t length,
                            uint8_t *reply, nina_transport_counters *counters);
#ifdef __cplusplus
}
#endif
#endif
