#ifndef NINA_WIFI_H
#define NINA_WIFI_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#define NINA_SCAN_MAX 60u
typedef struct {
    char ssid[33];
    uint8_t ssid_length, bssid[6], channel, encryption;
    int32_t rssi;
} nina_network;
typedef struct {
    nina_network networks[NINA_SCAN_MAX];
    size_t count;
    bool scanning;
} nina_scan;
/* Portable scan accumulator. Caller owns synchronization. */
void nina_scan_begin(nina_scan *scan);
void nina_scan_add(nina_scan *scan, const nina_network *network);
bool nina_wifi_init(void);
bool nina_wifi_connect(const char *ssid, const char *password);
bool nina_wifi_disconnect(void);
uint8_t nina_wifi_status(void);
bool nina_wifi_set_ip(const uint8_t ip[4], const uint8_t gateway[4], const uint8_t mask[4]);
bool nina_wifi_set_dns(const uint8_t primary[4], const uint8_t secondary[4]);
bool nina_wifi_set_hostname(const char *hostname);
void nina_wifi_address(uint8_t ip[4], uint8_t mask[4], uint8_t gateway[4]);
void nina_wifi_mac(uint8_t mac[6]);
void nina_wifi_current(nina_network *out);
bool nina_wifi_scan_start(void);
/* Copies an immutable generation, never the partially accumulated scan. */
void nina_wifi_scan_snapshot(nina_scan *out);
bool nina_wifi_resolve(const char *hostname, uint8_t ip[4]);
uint16_t nina_wifi_ping(const uint8_t ip[4], uint8_t ttl);
#endif
