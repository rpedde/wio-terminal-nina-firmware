#include "nina_server.h"
#include "nina_protocol.h"
#include "nina_wifi.h"
#include <string.h>

/* One transport task owns dispatch and this fixed scratch storage. */
static nina_scan snapshot;
static uint8_t resolved[4];
static void text_param(char *out, const nina_param *p) {
    memcpy(out, p->data, p->length); out[p->length] = 0;
}
size_t nina_server_reply(const uint8_t *data, size_t length, uint8_t *out,
                         nina_transport_counters *counters) {
    nina_request r;
    nina_parse_result result = nina_protocol_parse(data, length, &r);
    if (result != NINA_PARSE_OK) {
        if (result == NINA_PARSE_UNKNOWN) ++counters->unknown_commands;
        else ++counters->parse_failures;
        return nina_protocol_error(out);
    }
    uint8_t value[16] = {0};
    nina_param p[NINA_MAX_PARAMS] = {{value, 1}};
    size_t count = 1;
    char name[256], password[64];
    nina_network network;
    memset(&network, 0, sizeof(network));
    switch (r.command) {
    case 0x10: case 0x11:
        text_param(name, &r.params[0]);
        password[0] = 0;
        if (r.command == 0x11) text_param(password, &r.params[1]);
        value[0] = nina_wifi_connect(name, r.command == 0x11 ? password : NULL);
        memset(password, 0, sizeof(password));
        break;
    case 0x14: value[0] = nina_wifi_set_ip(r.params[1].data, r.params[2].data, r.params[3].data); break;
    case 0x15: value[0] = nina_wifi_set_dns(r.params[1].data, r.params[2].data); break;
    case 0x16:
        text_param(name, &r.params[0]); value[0] = nina_wifi_set_hostname(name); break;
    case 0x20: value[0] = nina_wifi_status(); break;
    case 0x21:
        nina_wifi_address(value, value + 4, value + 8);
        count = 3;
        for (unsigned i = 0; i < 3; ++i) p[i] = (nina_param){value + i * 4, 4};
        break;
    case 0x22: nina_wifi_mac(value); p[0].length = 6; break;
    case 0x23: case 0x24: case 0x25: case 0x26:
        nina_wifi_current(&network);
        if (r.command == 0x23) p[0] = (nina_param){(uint8_t *)network.ssid, network.ssid_length};
        if (r.command == 0x24) p[0] = (nina_param){network.bssid, 6};
        if (r.command == 0x25) { nina_write_le32(value, (uint32_t)network.rssi); p[0].length = 4; }
        if (r.command == 0x26) value[0] = network.encryption;
        break;
    case 0x27:
        nina_wifi_scan_snapshot(&snapshot);
        count = snapshot.count;
        for (size_t i = 0; i < count; ++i)
            p[i] = (nina_param){(uint8_t *)snapshot.networks[i].ssid, snapshot.networks[i].ssid_length};
        break;
    case 0x30: value[0] = nina_wifi_disconnect(); memset(resolved, 0, 4); break;
    case 0x32: case 0x33: case 0x3c: case 0x3d:
        /* Freeze the generation returned by 0x27 through its indexed reads. */
        if (r.params[0].data[0] >= snapshot.count) return nina_protocol_error(out);
        network = snapshot.networks[r.params[0].data[0]];
        if (r.command == 0x32) { nina_write_le32(value, (uint32_t)network.rssi); p[0].length = 4; }
        if (r.command == 0x33) value[0] = network.encryption;
        if (r.command == 0x3c) p[0] = (nina_param){network.bssid, 6};
        if (r.command == 0x3d) value[0] = network.channel;
        break;
    case 0x34:
        memset(resolved, 0, 4); text_param(name, &r.params[0]);
        value[0] = nina_wifi_resolve(name, resolved); break;
    case 0x35: p[0] = (nina_param){resolved, 4}; break;
    case 0x36:
        value[0] = nina_wifi_scan_start();
        if (value[0]) memset(&snapshot, 0, sizeof(snapshot));
        break;
    case 0x37: p[0] = (nina_param){(const uint8_t *)NINA_FIRMWARE_VERSION, sizeof(NINA_FIRMWARE_VERSION)}; break;
    case 0x3e:
        nina_write_le16(value, nina_wifi_ping(r.params[0].data, r.params[1].data[0])); p[0].length = 2; break;
    default: ++counters->unknown_commands; return nina_protocol_error(out);
    }
    size_t encoded = nina_protocol_reply(r.command, p, count, out, NINA_SPI_BUFFER_SIZE);
    return encoded ? encoded : nina_protocol_error(out);
}
bool nina_server_start(void) {
    memset(&snapshot, 0, sizeof(snapshot)); memset(resolved, 0, sizeof(resolved));
    return nina_wifi_init() && nina_transport_start(nina_server_reply);
}
