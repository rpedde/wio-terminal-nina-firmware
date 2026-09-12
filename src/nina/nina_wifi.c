#include "nina_wifi.h"
#include <string.h>
void nina_scan_begin(nina_scan *scan) {
    memset(scan, 0, sizeof(*scan));
    scan->scanning = true;
}
void nina_scan_add(nina_scan *scan, const nina_network *network) {
    if (!scan->scanning) return;
    size_t i;
    for (i = 0; i < scan->count; ++i)
        if (!memcmp(scan->networks[i].bssid, network->bssid, 6)) break;
    if (i == NINA_SCAN_MAX) return;
    if (i == scan->count) ++scan->count;
    scan->networks[i] = *network;
    nina_network *n = &scan->networks[i];
    if (n->ssid_length > 32) n->ssid_length = 32;
    n->ssid[n->ssid_length] = 0;
}
