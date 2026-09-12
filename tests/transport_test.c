#include "nina_spi_proof.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

static bool selected, ready, armed, fail_arm, in_isr;
static unsigned faults, calls, resets;
static size_t clocks;
static uint8_t *rx;
static const uint8_t *tx;
static uint32_t now;
bool nina_hw_selected(void) { return selected; }
void nina_hw_ready(bool high) { assert(high || armed); ready = high; }
bool nina_hw_arm(uint8_t *r, const uint8_t *t, size_t capacity)
{
    assert(ready && !selected && !armed);
    assert(capacity == NINA_SPI_BUFFER_SIZE);
    assert(((uintptr_t)r % 32) == 0 && ((uintptr_t)t % 32) == 0);
    rx = r; tx = t; clocks = 0;
    armed = !fail_arm;
    return armed;
}
size_t nina_hw_stop(unsigned *errors)
{
    *errors |= faults; faults = 0;
    if (!armed) return 0;
    armed = false;
    return clocks;
}
void nina_hw_reset(void) { assert(ready); armed = false; ++resets; }
static size_t reply(const uint8_t *r, size_t n, uint8_t *t, nina_transport_counters *c)
{
    assert(!in_isr && ready && !armed);
    ++calls;
    return nina_spi_proof_reply(r, n, t, c);
}
static void poll(void) {
    nina_transport_poll(now++);
    if (nina_transport_begin_request())
        nina_transport_finish_request(nina_transport_dispatch(), now++);
}
static void edge(bool low)
{
    selected = low;
    in_isr = true;
    nina_transport_cs_edge(low, now);
    in_isr = false;
    assert(ready);
}
static nina_transport_counters stats(void)
{
    nina_transport_counters c;
    nina_transport_snapshot(&c);
    return c;
}
static void init(void)
{
    selected = armed = fail_arm = in_isr = false;
    clocks = faults = calls = resets = now = 0;
    nina_transport_init(reply);
    assert(ready);
    poll();
    assert(!ready && armed);
}
static void command(const uint8_t *data, size_t length)
{
    assert(!ready && armed);
    edge(true);
    assert(length <= NINA_SPI_BUFFER_SIZE);
    memcpy(rx, data, length);
    clocks = length;
    unsigned before = calls;
    edge(false);
    assert(calls == before); /* Never dispatch from ISR. */
    poll();
}
static void consume(size_t length)
{
    assert(!ready && armed);
    edge(true);
    clocks = length;
    edge(false);
    poll();
    assert(!ready && armed);
}
static void check_status(void)
{
    static const uint8_t request[] = {0xe0, 0x20, 0, 0xee};
    static const uint8_t expected[] = {0xe0, 0xa0, 1, 1, 0, 0xee, 0, 0};
    command(request, sizeof(request));
    assert(memcmp(tx, expected, sizeof(expected)) == 0);
    consume(6); /* Stock driver leaves padding unread. */
}
static void vectors(void)
{
    init();
    check_status();
    const uint8_t version[] = {0xe0, 0x37, 0, 0xee};
    const uint8_t expected[] = {0xe0, 0xb7, 1, 16,
        '3','.','3','.','0','+','r','t','l','8','7','2','0','.','1',0,0xee,0,0,0};
    assert(sizeof(NINA_PROOF_VERSION) == 16);
    command(version, 4);
    assert(memcmp(tx, expected, sizeof(expected)) == 0);
    consume(21);
    assert(stats().rx_frames == 2 && stats().tx_frames == 2);
    const uint8_t bad[][4] = {{0,0x20,0,0xee}, {0xe0,0xa0,0,0xee},
        {0xe0,0x20,1,0xee}, {0xe0,0x20,0,0}, {0xe0,0x21,0,0xee}};
    for (size_t i = 0; i < sizeof(bad)/sizeof(bad[0]); ++i) {
        command(bad[i], 4);
        assert(memcmp(tx, "\xef\x00\xee\x00", 4) == 0);
        consume(i % 2 ? 4 : 1); /* ERR_CMD short read is normal. */
    }
    assert(stats().parse_failures == 4 && stats().unknown_commands == 1);
    command(version, 3);
    assert(tx[0] == 0xef);
    consume(1);
    check_status();
}
static void recovery(void)
{
    const uint8_t request[] = {0xe0, 0x20, 0, 0xee};
    for (unsigned fault = 1; fault <= NINA_HW_ERROR; fault <<= 1) {
        init(); edge(true); clocks = 4; faults = fault; edge(false); poll();
        assert(calls == 0 && stats().transport_resets == 2);
        if (fault == NINA_HW_OVERRUN) assert(stats().overruns == 1);
        if (fault == NINA_HW_UNDERRUN) assert(stats().underruns == 1);
        check_status();
    }
    init(); edge(true); edge(false); poll(); /* Zero clocks. */
    assert(calls == 0 && stats().transport_resets == 2); check_status();
    init(); edge(false); poll(); /* Unexpected rising edge. */
    assert(stats().unexpected_edges == 1); check_status();
    init(); edge(true); clocks = NINA_SPI_BUFFER_SIZE + 1; edge(false); poll();
    assert(stats().overruns == 1 && calls == 0); check_status();
    init(); edge(true); now += NINA_TRANSACTION_TIMEOUT_MS; poll();
    assert(ready && !armed && stats().timeouts == 1);
    for (int i = 0; i < 100; ++i) poll();
    assert(stats().transport_resets == 2); /* No reset storm on stuck CS. */
    edge(false); poll(); check_status();
    init(); now = UINT32_MAX - 10; edge(true); now += NINA_TRANSACTION_TIMEOUT_MS;
    poll(); assert(stats().timeouts == 1); edge(false); poll(); check_status();
    init(); command(request, 4); now += NINA_TRANSACTION_TIMEOUT_MS; poll();
    assert(stats().timeouts == 1 && stats().tx_frames == 0); check_status();
    init(); command(request, 4); consume(5);
    assert(stats().underruns == 1 && stats().tx_frames == 0); check_status();
    init(); command(request, 4); consume(9);
    assert(stats().overruns == 1 && stats().tx_frames == 0); check_status();
    init(); command(request, 4); consume(8); /* Padded read is also valid. */
    assert(stats().tx_frames == 1);
    init(); edge(true); edge(false); fail_arm = true; poll();
    assert(ready && !armed); fail_arm = false; poll(); check_status();
    init(); now += 1000000; poll(); /* Indefinite idle command wait is normal. */
    assert(stats().timeouts == 0 && !ready);
}
static void stress(void)
{
    init();
    for (int i = 0; i < 10000; ++i) check_status();
    assert(stats().rx_frames == 10000 && stats().tx_frames == 10000);
    assert(stats().transport_resets == 1);
    uint32_t random = 42;
    uint8_t data[NINA_SPI_BUFFER_SIZE];
    for (int i = 0; i < 20000; ++i) {
        random = random * 1664525u + 1013904223u;
        size_t n = 1 + random % sizeof(data);
        for (size_t j = 0; j < n; ++j) {
            random = random * 1664525u + 1013904223u;
            data[j] = random >> 24;
        }
        command(data, n);
        assert(tx[0] == 0xef);
        consume(1);
    }
    check_status();
}
static void slow_response(void)
{
    init(); edge(true); memcpy(rx, "\xe0\x20\0\xee", 4); clocks = 4; edge(false);
    nina_transport_poll(now++); assert(nina_transport_begin_request());
    now += 4500; /* A DNS operation longer than the transport timeout. */
    size_t length = nina_transport_dispatch();
    nina_transport_finish_request(length, now);
    nina_transport_poll(++now); assert(!ready && armed && !stats().timeouts);
    consume(6); check_status();

    init(); edge(true); memcpy(rx, "\xe0\x20\0\xee", 4); clocks = 4; edge(false);
    nina_transport_poll(now++); assert(nina_transport_begin_request());
    edge(true); /* Host violates READY while the task is processing. */
    length = nina_transport_dispatch();
    nina_transport_finish_request(length, now);
    assert(ready && !armed); poll(); assert(ready && !armed);
    edge(false); poll(); check_status();
}
int main(void)
{
    vectors(); recovery(); stress(); slow_response();
    puts("SPI proof vectors, handshake, fault recovery and stress passed");
    return 0;
}
