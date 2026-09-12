#include "nina_transport.h"
#include <string.h>

enum state { RECOVER, WAIT_RELEASE, RX_ARMED, RX_ACTIVE, RX_DONE, PROCESSING,
             TX_ARMED, TX_ACTIVE, TX_DONE };
static volatile enum state state;
/* Whole cache lines prevent DMA invalidation from discarding adjacent state. */
#define STORAGE_SIZE ((NINA_SPI_BUFFER_SIZE + 31u) & ~31u)
static uint8_t rx[STORAGE_SIZE] __attribute__((aligned(32)));
static uint8_t tx[STORAGE_SIZE] __attribute__((aligned(32)));
static size_t received, response_length;
static uint32_t started;
static nina_response_fn responder;
static nina_transport_counters counters;

static void abort_transfer(void)
{
    unsigned ignored = 0;
    nina_hw_ready(true);
    nina_hw_stop(&ignored);
    state = RECOVER;
}

void nina_transport_init(nina_response_fn respond)
{
    memset(&counters, 0, sizeof(counters));
    responder = respond;
    state = RECOVER;
    nina_hw_ready(true);
}

void nina_transport_cs_edge(bool selected, uint32_t now_ms)
{
    nina_hw_ready(true);
    if (!selected && state == WAIT_RELEASE) return;
    if (selected && (state == RX_ARMED || state == TX_ARMED)) {
        state = state == RX_ARMED ? RX_ACTIVE : TX_ACTIVE;
        started = now_ms;
        return;
    }
    if (!selected && (state == RX_ACTIVE || state == TX_ACTIVE)) {
        unsigned faults = 0;
        received = nina_hw_stop(&faults);
        if (received > NINA_SPI_BUFFER_SIZE) faults |= NINA_HW_OVERRUN;
        if (faults & NINA_HW_OVERRUN) ++counters.overruns;
        if (faults & NINA_HW_UNDERRUN) ++counters.underruns;
        if (!received || faults) {
            state = RECOVER;
            return;
        }
        if (state == TX_ACTIVE) {
            /* ESP32SPI reads through END_CMD; other hosts may read padding.
             * It stops immediately on ERR_CMD, which is also a consumed reply. */
            size_t minimum = tx[0] == 0xef ? 1 : response_length;
            size_t padded = (response_length + 3u) & ~3u;
            if (received < minimum || received > padded) {
                if (received < minimum) ++counters.underruns;
                else ++counters.overruns;
                state = RECOVER;
                return;
            }
            ++counters.tx_frames;
            state = TX_DONE;
        } else {
            ++counters.rx_frames;
            state = RX_DONE;
        }
        return;
    }
    ++counters.unexpected_edges;
    abort_transfer();
}

static void arm(bool response, uint32_t now_ms)
{
    nina_hw_ready(true);
    memset(rx, 0, sizeof(rx));
    if (!response) memset(tx, 0, sizeof(tx));
    if (nina_hw_selected() || !nina_hw_arm(rx, tx, NINA_SPI_BUFFER_SIZE)) {
        abort_transfer();
        return;
    }
    state = response ? TX_ARMED : RX_ARMED;
    started = now_ms;
    nina_hw_ready(false);
}

void nina_transport_poll(uint32_t now_ms)
{
    if ((state == RX_ACTIVE || state == TX_ACTIVE || state == TX_ARMED) &&
        (uint32_t)(now_ms - started) >= NINA_TRANSACTION_TIMEOUT_MS) {
        ++counters.timeouts;
        abort_transfer();
    }
    if (state == RECOVER) {
        nina_hw_ready(true);
        nina_hw_reset();
        ++counters.transport_resets;
        /* A stuck-low host must release CS before we advertise readiness. */
        state = WAIT_RELEASE;
        if (!nina_hw_selected()) arm(false, now_ms);
    } else if (state == WAIT_RELEASE) {
        if (!nina_hw_selected()) arm(false, now_ms);
    } else if (state == TX_DONE) {
        arm(false, now_ms);
    }
}

bool nina_transport_begin_request(void)
{
    if (state != RX_DONE) return false;
    state = PROCESSING;
    return true;
}

size_t nina_transport_dispatch(void)
{
    memset(tx, 0, sizeof(tx));
    return responder(rx, received, tx, &counters);
}

void nina_transport_finish_request(size_t length, uint32_t now_ms)
{
    /* Unexpected CS edges during a slow command invalidate its response.
     * The ISR stops hardware but cannot rearm/overwrite the request buffer. */
    if (state != PROCESSING) return;
    response_length = length;
    if (!length || length > NINA_SPI_BUFFER_SIZE) abort_transfer();
    else arm(true, now_ms);
}

void nina_transport_snapshot(nina_transport_counters *out)
{
    *out = counters;
}
