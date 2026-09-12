#include "nina_protocol.h"
#include "nina_server.h"
#include "nina_wifi.h"
#include "nina_dhcp.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "protocol_vectors.h"

static bool backend_ok = true;
static uint8_t state = 3;
static nina_scan scan;
static unsigned effects;
static const uint8_t ip[] = {192, 0, 2, 10}, mask[] = {255, 255, 255, 0}, gateway[] = {192, 0, 2, 1};
bool nina_wifi_init(void) { return true; }
bool nina_transport_start(nina_response_fn fn) { (void)fn; return true; }
bool nina_wifi_connect(const char *ssid, const char *pass) {
    assert(!strcmp(ssid, "test")); assert(!pass || !strcmp(pass, "password")); ++effects; return backend_ok;
}
bool nina_wifi_disconnect(void) { ++effects; state = 6; return backend_ok; }
uint8_t nina_wifi_status(void) { return state; }
bool nina_wifi_set_ip(const uint8_t a[4], const uint8_t b[4], const uint8_t c[4]) {
    assert(!memcmp(a, ip, 4) && !memcmp(b, gateway, 4) && !memcmp(c, mask, 4)); ++effects; return backend_ok;
}
bool nina_wifi_set_dns(const uint8_t a[4], const uint8_t b[4]) {
    assert(!memcmp(a, gateway, 4) && !memcmp(b, "\1\1\1\1", 4)); ++effects; return backend_ok;
}
bool nina_wifi_set_hostname(const char *name) { assert(!strcmp(name, "wio-test")); ++effects; return backend_ok; }
void nina_wifi_address(uint8_t a[4], uint8_t b[4], uint8_t c[4]) { memcpy(a, ip, 4); memcpy(b, mask, 4); memcpy(c, gateway, 4); }
void nina_wifi_mac(uint8_t mac[6]) { for (unsigned i = 0; i < 6; ++i) mac[i] = 5 - i; }
void nina_wifi_current(nina_network *out) { *out = scan.networks[0]; }
bool nina_wifi_scan_start(void) { ++effects; return backend_ok; }
void nina_wifi_scan_snapshot(nina_scan *out) { *out = scan; }
bool nina_wifi_resolve(const char *name, uint8_t address[4]) {
    assert(!strcmp(name, "example.com")); ++effects;
    if (backend_ok) memcpy(address, "\xc0\0\2\x50", 4);
    return backend_ok;
}
uint16_t nina_wifi_ping(const uint8_t address[4], uint8_t ttl) {
    assert(!memcmp(address, gateway, 4) && ttl == 250); ++effects; return backend_ok ? 0x1234 : 0xffff;
}
static size_t unhex(const char *hex, uint8_t *out) {
    size_t length = strlen(hex) / 2;
    for (size_t i = 0; i < length; ++i) { unsigned value; assert(sscanf(hex + i * 2, "%2x", &value) == 1); out[i] = value; }
    return length;
}
static void initialize(void) {
    memset(&scan, 0, sizeof(scan)); scan.count = 2;
    nina_network *n = &scan.networks[0];
    memcpy(n->ssid, "test", 5); n->ssid_length = 4; n->rssi = -42; n->channel = 6; n->encryption = 4;
    for (unsigned i = 0; i < 6; ++i) n->bssid[i] = i;
}
static void vectors(void) {
    initialize();
    uint8_t request[4092], response[4092], expected[4092];
    nina_transport_counters counters = {0};
    for (size_t i = 0; i < sizeof(golden) / sizeof(golden[0]); ++i) {
        size_t n = unhex(golden[i].request, request), want = unhex(golden[i].response, expected);
        memset(response, 0xa5, sizeof(response));
        size_t got = nina_server_reply(request, n, response, &counters);
        if (((got + 3) & ~(size_t)3) != want || memcmp(response, expected, want)) {
            fprintf(stderr, "Golden failure: %s\n", golden[i].name); assert(0);
        }
        assert(response[want] == 0xa5); /* No writes past aligned frame. */
    }
    assert(!counters.parse_failures && !counters.unknown_commands);
    /* Failure acknowledgements and failed DNS never reveal a stale address. */
    backend_ok = false;
    for (unsigned i = 0; i < sizeof(golden) / sizeof(golden[0]); ++i) {
        uint8_t cmd;
        size_t n = unhex(golden[i].request, request); cmd = request[1];
        if (cmd == 0x10 || cmd == 0x11 || cmd == 0x14 || cmd == 0x15 || cmd == 0x16 ||
            cmd == 0x30 || cmd == 0x34 || cmd == 0x36) {
            nina_server_reply(request, n, response, &counters); assert(response[4] == 0);
        }
    }
    nina_server_reply((const uint8_t *)"\xe0\x35\0\xee", 4, response, &counters);
    assert(!memcmp(response + 4, "\0\0\0\0", 4));
    for (state = 0; state <= 6; ++state) {
        nina_server_reply((const uint8_t *)"\xe0\x20\0\xee", 4, response, &counters); assert(response[4] == state);
    }
    size_t n = unhex(golden[21].request, request);
    nina_server_reply(request, n, response, &counters); assert(response[4] == 255 && response[5] == 255);
    backend_ok = true;
    /* Indexed queries keep the generation previously exposed by 0x27. */
    nina_server_reply((const uint8_t *)"\xe0\x27\0\xee", 4, response, &counters);
    scan.networks[0].channel = 11;
    nina_server_reply((const uint8_t *)"\xe0\x3d\1\1\0\xee\0\0", 8, response, &counters);
    assert(response[4] == 6);
    nina_server_reply((const uint8_t *)"\xe0\x3d\1\1\x3c\xee\0\0", 8, response, &counters);
    assert(response[0] == 0xef);
    scan.count = 0;
    assert(nina_server_reply((const uint8_t *)"\xe0\x27\0\xee", 4, response, &counters) == 4);
    assert(response[2] == 0 && response[3] == 0xee);
}
static void parser_bounds(void) {
    nina_request parsed;
    uint8_t frame[4100], response[4100], payload[4085] = {0};
    for (size_t i = 0; i < sizeof(golden) / sizeof(golden[0]); ++i) {
        size_t length = unhex(golden[i].request, frame);
        assert(nina_protocol_parse(frame, length, &parsed) == NINA_PARSE_OK);
        for (size_t j = 0; j < length; ++j) assert(nina_protocol_parse(frame, j, &parsed) != NINA_PARSE_OK);
        frame[2] ^= 1; assert(nina_protocol_parse(frame, length, &parsed) != NINA_PARSE_OK); frame[2] ^= 1;
        frame[1] |= 128; assert(nina_protocol_parse(frame, length, &parsed) != NINA_PARSE_OK); frame[1] &= 127;
        frame[0] = 0; assert(nina_protocol_parse(frame, length, &parsed) != NINA_PARSE_OK);
    }
    assert(nina_protocol_parse(NULL, 4, &parsed) == NINA_PARSE_INVALID);
    assert(nina_protocol_parse(frame, sizeof(frame), &parsed) == NINA_PARSE_INVALID);
    size_t n = unhex("e016010161ee0000", frame);
    assert(nina_protocol_parse(frame, n, &parsed) == NINA_PARSE_OK);
    frame[4] = 0; assert(nina_protocol_parse(frame, n, &parsed) == NINA_PARSE_INVALID);
    frame[4] = 'a'; frame[5] = 0; assert(nina_protocol_parse(frame, n, &parsed) == NINA_PARSE_INVALID);
    frame[5] = 0xee; assert(nina_protocol_parse(frame, n + 4, &parsed) == NINA_PARSE_INVALID);
    n = unhex("e045020001030002f40fee00", frame);
    assert(nina_protocol_parse(frame, n, &parsed) == NINA_PARSE_OK);
    assert(nina_read_le16(parsed.params[1].data) == 4084);
    frame[5] = 4; assert(nina_protocol_parse(frame, n, &parsed) == NINA_PARSE_INVALID);
    frame[5] = 3; frame[3] = 255; assert(nina_protocol_parse(frame, n, &parsed) == NINA_PARSE_INVALID);
    for (unsigned cmd = 0x44; cmd <= 0x46; ++cmd) {
        if (cmd == 0x45) continue;
        n = unhex("e044020001000000ee000000", frame); frame[1] = cmd;
        assert(nina_protocol_parse(frame, n, &parsed) == NINA_PARSE_OK);
        assert(parsed.params[1].length == 0);
    }
    /* Every padding remainder, zero length, 8/16-bit length boundaries. */
    for (size_t length = 0; length <= 4085; ++length) {
        nina_param p = {payload, length};
        memset(response, 0xa5, sizeof(response));
        size_t got = nina_protocol_reply(0x45, &p, 1, response, 4092);
        if (length > 4084) { assert(got == 0); continue; }
        assert(got == length + 6 && response[3] == length >> 8 && response[4] == (length & 255));
        size_t padded = (got + 3) & ~(size_t)3;
        for (size_t i = got; i < padded; ++i) assert(!response[i]);
        assert(response[padded] == 0xa5);
        assert(!nina_protocol_reply(0x45, &p, 1, response, padded - 1));
        if (length <= 255) assert(nina_protocol_reply(0x23, &p, 1, response, 4092) == length + 5);
        else assert(!nina_protocol_reply(0x23, &p, 1, response, 4092));
    }
    uint8_t endian[4]; nina_write_le32(endian, (uint32_t)-1234);
    assert(!memcmp(endian, "\x2e\xfb\xff\xff", 4));
    nina_write_le16(endian, 0xabcd); assert(nina_read_le16(endian) == 0xabcd);
}
static void scans(void) {
    nina_scan s; nina_scan_begin(&s);
    nina_network n = {0}; memset(n.ssid, 'x', sizeof(n.ssid)); n.ssid_length = 255;
    for (unsigned i = 0; i < 65; ++i) { n.bssid[5] = i; nina_scan_add(&s, &n); }
    assert(s.count == 60 && s.networks[0].ssid_length == 32 && s.networks[0].ssid[32] == 0);
    n.bssid[5] = 0; n.rssi = -70; nina_scan_add(&s, &n);
    assert(s.count == 60 && s.networks[0].rssi == -70);
    s.scanning = false; n.rssi = -80; nina_scan_add(&s, &n); assert(s.networks[0].rssi == -70);
    nina_scan_begin(&s); assert(s.count == 0 && s.scanning);
}
static void dhcp(void) {
    uint8_t packet[600] = {1}; memcpy(packet + 236, "\x63\x82\x53\x63", 4);
    memcpy(packet + 240, "\x35\1\1\xff", 4);
    assert(nina_dhcp_hostname(packet, 300, sizeof(packet), "wio-test") == 300);
    assert(!memcmp(packet + 243, "\x0c\x08wio-test\xff", 11));
    assert(nina_dhcp_hostname(packet, 300, sizeof(packet), "wio-test") == 300);
    packet[243] = 255;
    assert(!nina_dhcp_hostname(packet, 244, 245, "wio-test"));
    packet[243] = 12; packet[244] = 255;
    assert(!nina_dhcp_hostname(packet, 245, sizeof(packet), "wio-test"));
}
static void fuzz(void) {
    uint32_t random = 0x8720; uint8_t data[4100], response[4092]; nina_request parsed;
    for (unsigned i = 0; i < 30000; ++i) {
        random = random * 1664525u + 1013904223u;
        size_t n = random % sizeof(data);
        for (size_t j = 0; j < n; ++j) { random = random * 1664525u + 1013904223u; data[j] = random >> 24; }
        if (n > 2 && (i & 1)) { data[0] = 0xe0; data[1] = 0x44 + i % 3; data[2] = 2; }
        nina_protocol_parse(data, n, &parsed);
        nina_dhcp_hostname(data, n, sizeof(data), "fuzz-host");
        nina_param p = {data, n}; nina_protocol_reply(0x45, &p, 1, response, sizeof(response));
    }
    /* Mutate every byte of each valid golden request to exercise deep paths.
     * Only malformed cases enter dispatch: backend assertions stay meaningful. */
    nina_transport_counters counters = {0};
    for (size_t i = 0; i < sizeof(golden) / sizeof(golden[0]); ++i) {
        size_t n = unhex(golden[i].request, data);
        for (size_t j = 0; j < n; ++j) {
            uint8_t saved = data[j];
            for (unsigned v = 0; v < 256; ++v) {
                data[j] = v;
                if (nina_protocol_parse(data, n, &parsed) != NINA_PARSE_OK) {
                    unsigned before = effects;
                    assert(nina_server_reply(data, n, response, &counters) == 3);
                    assert(response[0] == 0xef && effects == before);
                }
            }
            data[j] = saved;
        }
    }
}
int main(void) {
    vectors(); parser_bounds(); scans(); dhcp(); fuzz();
    puts("23 stock-driver golden vectors, bounds, backend failures, scan generations, DHCP and fuzz passed");
    return 0;
}
