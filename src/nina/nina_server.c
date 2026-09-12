#include "nina_server.h"
#include "nina_protocol.h"
#include "nina_wifi.h"
#include "nina_sockets.h"
#include <string.h>

/* One transport task owns dispatch and this fixed scratch storage. */
static nina_scan snapshot;
static uint8_t resolved[4];
static uint8_t socket_data[NINA_MAX_RESPONSE_DATA];
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
    case 0x30: nina_sockets_close_all(); value[0] = nina_wifi_disconnect(); memset(resolved, 0, 4); break;
    case 0x28: case 0x2d: {
        unsigned base = r.count - (r.command == 0x2d ? 4 : 3);
        uint8_t ip[4] = {0}; bool ok = true;
        if (r.command == 0x2d) {
            memcpy(ip, r.params[base].data, 4);
            if (base && r.params[4].data[0] <= 1) {
                text_param(name, &r.params[0]); ok = nina_wifi_resolve(name, ip);
            }
            ++base;
        }
        unsigned id = r.params[base + 1].data[0];
        uint16_t port = ((uint16_t)r.params[base].data[0] << 8) | r.params[base].data[1];
        if (ok) value[0] = nina_sockets_connect(id, r.params[base + 2].data[0], ip, port, r.command == 0x28);
        else nina_sockets_close(id);
        break;
    }
    case 0x2a: value[0] = nina_sockets_sent(r.params[0].data[0]); break;
    case 0x2b:
        nina_write_le16(value, nina_sockets_available(r.params[0].data[0])); p[0].length = 2; break;
    case 0x2c:
        value[0] = 255; nina_sockets_read(r.params[0].data[0], value, 1, r.params[1].data[0] != 0); break;
    case 0x2e: nina_sockets_close(r.params[0].data[0]); value[0] = 1; break;
    case 0x2f: value[0] = nina_sockets_state(r.params[0].data[0]); break;
    case 0x39: value[0] = nina_sockets_send_udp(r.params[0].data[0]); break;
    case 0x3a: {
        uint16_t port;
        nina_sockets_remote(r.params[0].data[0], value, &port);
        /* Stock ESP32SPI 11.1.4 unpacks the remote port as little endian. */
        nina_write_le16(value + 4, port);
        p[0].length = 4; p[1] = (nina_param){value + 4, 2}; count = 2; break;
    }
    case 0x3f: value[0] = nina_sockets_allocate(); break;
    case 0x44:
        nina_write_le16(value, nina_sockets_write(r.params[0].data[0], r.params[1].data, r.params[1].length));
        p[0].length = 2; break;
    case 0x45:
        p[0] = (nina_param){socket_data, nina_sockets_read(r.params[0].data[0], socket_data, nina_read_le16(r.params[1].data), false)}; break;
    case 0x46: value[0] = nina_sockets_insert(r.params[0].data[0], r.params[1].data, r.params[1].length); break;
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
    return nina_wifi_init() && nina_sockets_init() && nina_transport_start(nina_server_reply);
}
