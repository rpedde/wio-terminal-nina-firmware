#ifndef NINA_TRANSPORT_H
#define NINA_TRANSPORT_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define NINA_SPI_BUFFER_SIZE 4092u
#define NINA_MAX_RESPONSE_DATA 4084u
#define NINA_TRANSACTION_TIMEOUT_MS 2000u

#ifdef __cplusplus
extern "C" {
#endif
typedef struct {
    uint32_t rx_frames, tx_frames, parse_failures, unknown_commands;
    uint32_t overruns, underruns, transport_resets, timeouts, unexpected_edges;
} nina_transport_counters;

/* Return wire length through END_CMD, excluding alignment padding. */
typedef size_t (*nina_response_fn)(const uint8_t *, size_t, uint8_t *,
                                 nina_transport_counters *);
bool nina_transport_start(nina_response_fn respond);
void nina_transport_get_counters(nina_transport_counters *out);

/* Platform-independent engine. Platform serializes poll/edge/snapshot calls.
 * The handler runs only from dispatch (task context), never from the CS ISR. */
void nina_transport_init(nina_response_fn respond);
void nina_transport_poll(uint32_t now_ms);
/* begin/finish run under the platform's CS critical section; dispatch runs
 * outside it, with scheduling enabled. No poll/rearm until finish returns. */
bool nina_transport_begin_request(void);
size_t nina_transport_dispatch(void);
void nina_transport_finish_request(size_t length, uint32_t now_ms);
void nina_transport_cs_edge(bool selected, uint32_t now_ms);

/* Backend contract. stop freezes DMA and returns actual clocks received,
 * including on TX transactions. An arm failure must leave hardware stopped.
 * READY is driven separately and may go low only after arm succeeds. */
enum { NINA_HW_OVERRUN = 1, NINA_HW_UNDERRUN = 2, NINA_HW_ERROR = 4 };
bool nina_hw_selected(void);
void nina_hw_ready(bool high);
bool nina_hw_arm(uint8_t *rx, const uint8_t *tx, size_t capacity);
size_t nina_hw_stop(unsigned *faults);
void nina_hw_reset(void);
void nina_transport_snapshot(nina_transport_counters *out);
#ifdef __cplusplus
}
#endif
#endif
