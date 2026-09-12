/* Typed station backend; no eRPC allocation rules or RPC callbacks. */
#include "ameba_soc.h"
#include "FreeRTOS.h"
#include "task.h"
#include "queue.h"
#include "semphr.h"
#include "wifi_conf.h"
#include "wifi_ind.h"
#include "lwip_netconf.h"
#include "lwip/tcpip.h"
#include "lwip/dns.h"
#include "lwip/dhcp.h"
#include "lwip/sockets.h"
#include "nina_wifi.h"
#include <string.h>

#ifndef WIFI_COUNTRY
#define WIFI_COUNTRY RTW_COUNTRY_US
#endif
extern struct netif xnetif[NET_IF_NUM];
static SemaphoreHandle_t mutex, ip_done, dns_done, worker_done;
static QueueHandle_t jobs;
static uint8_t status;
static bool busy, requested_disconnect, static_ip;
static uint8_t static_address[12];
static nina_scan accumulating, completed;
static char hostname[256] = "wio-terminal";
typedef struct { bool disconnect; char ssid[33], password[64]; bool secured; } wifi_job;
static void lock(void) { xSemaphoreTake(mutex, portMAX_DELAY); }
static void unlock(void) { xSemaphoreGive(mutex); }
static uint8_t encryption(rtw_security_t security) {
    if (security == RTW_SECURITY_OPEN) return 7;
    if (security & WEP_ENABLED) return 5;
    if (security & WPA2_SECURITY) return 4;
    if (security & WPA_SECURITY) return 2;
    return 255;
}
static void disconnected(char *data, int length, int flags, void *arg) {
    (void)data; (void)length; (void)flags; (void)arg;
    lock();
    if (!requested_disconnect && status == 3) status = 5;
    unlock();
}
static void no_network(char *data, int length, int flags, void *arg) {
    (void)data; (void)length; (void)flags; (void)arg;
    lock(); if (busy && !requested_disconnect) status = 1; unlock();
}

/* TCP/IP callbacks own all direct netif/DNS mutations. Storage survives a
 * response timeout; a late callback cannot access a departed task stack. */
static struct {
    unsigned operation;
    bool pending, success;
    uint8_t bytes[12];
    char hostname[256];
} ip_work;
enum { IP_SET, DNS_SET, HOST_SET, IP_GET, IP_CLEAR };
static void ip_callback(void *unused) {
    (void)unused;
    ip_addr_t a, b, c;
    memcpy(&a.addr, ip_work.bytes, 4);
    memcpy(&b.addr, ip_work.bytes + 4, 4);
    memcpy(&c.addr, ip_work.bytes + 8, 4);
    ip_work.success = true;
    switch (ip_work.operation) {
    case IP_SET:
        dhcp_stop(&xnetif[0]); netif_set_addr(&xnetif[0], &a, &c, &b); break;
    case DNS_SET: dns_setserver(0, &a); dns_setserver(1, &b); break;
    case HOST_SET: memcpy(hostname, ip_work.hostname, sizeof(hostname)); break;
    case IP_GET:
        memcpy(ip_work.bytes, &xnetif[0].ip_addr.addr, 4);
        memcpy(ip_work.bytes + 4, &xnetif[0].netmask.addr, 4);
        memcpy(ip_work.bytes + 8, &xnetif[0].gw.addr, 4); break;
    case IP_CLEAR:
        dhcp_stop(&xnetif[0]);
        memset(&a, 0, sizeof(a)); netif_set_addr(&xnetif[0], &a, &a, &a); break;
    }
    xSemaphoreGive(ip_done);
}
/* Called only by the SPI task. Worker DHCP setup uses the SDK's adapter. */
static bool ip_call(unsigned operation, uint8_t bytes[12], const char *name) {
    if (ip_work.pending) {
        if (xSemaphoreTake(ip_done, 0) != pdTRUE) return false;
        ip_work.pending = false;
    }
    ip_work.operation = operation;
    if (bytes) memcpy(ip_work.bytes, bytes, 12);
    if (name) { memset(ip_work.hostname, 0, 256); memcpy(ip_work.hostname, name, strlen(name)); }
    ip_work.pending = true;
    if (tcpip_callback_with_block(ip_callback, NULL, 0) != ERR_OK) {
        ip_work.pending = false; return false;
    }
    if (xSemaphoreTake(ip_done, pdMS_TO_TICKS(1000)) != pdTRUE) return false;
    ip_work.pending = false;
    if (bytes) memcpy(bytes, ip_work.bytes, 12);
    return ip_work.success;
}
/* Used by the DHCP packet hook, in TCP/IP thread context. */
const char *nina_wifi_dhcp_hostname(void) { return hostname; }

static void static_callback(void *unused) {
    (void)unused;
    ip_addr_t ip, gateway, mask;
    memcpy(&ip.addr, static_address, 4);
    memcpy(&gateway.addr, static_address + 4, 4);
    memcpy(&mask.addr, static_address + 8, 4);
    dhcp_stop(&xnetif[0]); netif_set_addr(&xnetif[0], &ip, &mask, &gateway);
    xSemaphoreGive(worker_done);
}

static void worker(void *unused) {
    (void)unused;
    wifi_job job;
    for (;;) {
        xQueueReceive(jobs, &job, portMAX_DELAY);
        if (job.disconnect) {
            wifi_disconnect(); LwIP_DHCP(0, DHCP_STOP);
            lock(); status = 6; busy = false; unlock();
        } else {
            if (wifi_is_connected_to_ap() == RTW_SUCCESS) {
                wifi_disconnect(); LwIP_DHCP(0, DHCP_STOP);
            }
            int result = wifi_connect(job.ssid,
                job.secured ? RTW_SECURITY_WPA2_AES_PSK : RTW_SECURITY_OPEN,
                job.secured ? job.password : NULL, strlen(job.ssid),
                strlen(job.password), -1, NULL);
            lock(); bool cancelled = requested_disconnect; bool configured = static_ip; unlock();
            bool addressed = result == RTW_SUCCESS && !cancelled;
            if (addressed) {
                if (configured) {
                    addressed = tcpip_callback_with_block(static_callback, NULL, 0) == ERR_OK;
                    /* Worker-only wait; never delays a SPI acknowledgement.
                     * Static configuration cannot be replaced while busy. */
                    if (addressed) xSemaphoreTake(worker_done, portMAX_DELAY);
                } else addressed = LwIP_DHCP(0, DHCP_START) == DHCP_ADDRESS_ASSIGNED;
            }
            lock();
            if (requested_disconnect) status = 6;
            else if (addressed && wifi_is_connected_to_ap() == RTW_SUCCESS) status = 3;
            else if (status != 1) status = wifi_get_last_error() == RTW_NONE_NETWORK ? 1 : 4;
            busy = requested_disconnect;
            unlock();
        }
        memset(&job, 0, sizeof(job));
    }
}
bool nina_wifi_init(void) {
    /* Advertise BOOT while network initialization runs. */
    GPIO_InitTypeDef pin = {0};
    pin.GPIO_Pin = _PA_12; pin.GPIO_Mode = GPIO_Mode_OUT;
    GPIO_WriteBit(_PA_12, 1); GPIO_Init(&pin);
    pin.GPIO_Pin = _PA_13; pin.GPIO_Mode = GPIO_Mode_IN; GPIO_Init(&pin);
    mutex = xSemaphoreCreateMutex(); ip_done = xSemaphoreCreateBinary(); dns_done = xSemaphoreCreateBinary();
    worker_done = xSemaphoreCreateBinary();
    jobs = xQueueCreate(2, sizeof(wifi_job));
    if (!mutex || !ip_done || !dns_done || !worker_done || !jobs) return false;
    LwIP_Init();
    if (wifi_on(RTW_MODE_STA) != RTW_SUCCESS) return false;
    wifi_set_autoreconnect(0);
    if (wifi_set_country(WIFI_COUNTRY) != RTW_SUCCESS) return false;
    wifi_reg_event_handler(WIFI_EVENT_DISCONNECT, disconnected, NULL);
    wifi_reg_event_handler(WIFI_EVENT_NO_NETWORK, no_network, NULL);
    return xTaskCreate(worker, "nina_wifi", 1536, NULL, tskIDLE_PRIORITY + 2, NULL) == pdPASS;
}
bool nina_wifi_connect(const char *ssid, const char *password) {
    wifi_job job = {0};
    if (strlen(ssid) > 32 || (password && strlen(password) > 63)) return false;
    strcpy(job.ssid, ssid); job.secured = password != NULL;
    if (password) strcpy(job.password, password);
    lock();
    if (busy || accumulating.scanning) { unlock(); return false; }
    busy = true; requested_disconnect = false; status = 0;
    bool ok = xQueueSend(jobs, &job, 0) == pdTRUE;
    if (!ok) { busy = false; status = 4; }
    unlock(); memset(&job, 0, sizeof(job)); return ok;
}
bool nina_wifi_disconnect(void) {
    wifi_job job = {0}; job.disconnect = true;
    lock();
    if (requested_disconnect) { unlock(); return true; }
    bool ok = xQueueSend(jobs, &job, 0) == pdTRUE;
    if (ok) { requested_disconnect = true; status = 6; busy = true; }
    unlock(); return ok;
}
uint8_t nina_wifi_status(void) { lock(); uint8_t value = status; unlock(); return value; }
bool nina_wifi_set_ip(const uint8_t ip[4], const uint8_t gateway[4], const uint8_t mask[4]) {
    lock(); bool active = busy; unlock(); if (active) return false;
    uint8_t bytes[12]; memcpy(bytes, ip, 4); memcpy(bytes + 4, gateway, 4); memcpy(bytes + 8, mask, 4);
    bool ok = ip_call(IP_SET, bytes, NULL);
    if (ok) { lock(); memcpy(static_address, bytes, 12); static_ip = true; unlock(); } return ok;
}
bool nina_wifi_set_dns(const uint8_t primary[4], const uint8_t secondary[4]) {
    uint8_t bytes[12] = {0}; memcpy(bytes, primary, 4); memcpy(bytes + 4, secondary, 4);
    return ip_call(DNS_SET, bytes, NULL);
}
bool nina_wifi_set_hostname(const char *name) { return ip_call(HOST_SET, NULL, name); }
void nina_wifi_address(uint8_t ip[4], uint8_t mask[4], uint8_t gateway[4]) {
    uint8_t bytes[12] = {0};
    if (nina_wifi_status() == 3 && !ip_call(IP_GET, bytes, NULL)) memset(bytes, 0, 12);
    memcpy(ip, bytes, 4); memcpy(mask, bytes + 4, 4); memcpy(gateway, bytes + 8, 4);
}
void nina_wifi_mac(uint8_t mac[6]) {
    /* NINA's legacy MAC property is reversed by MAC_address_actual. */
    for (unsigned i = 0; i < 6; ++i) mac[i] = xnetif[0].hwaddr[5 - i];
}
void nina_wifi_current(nina_network *out) {
    memset(out, 0, sizeof(*out)); out->encryption = 255;
    if (nina_wifi_status() != 3) return;
    rtw_wifi_setting_t setting;
    memset(&setting, 0, sizeof(setting));
    if (wifi_get_setting(WLAN0_NAME, &setting) == RTW_SUCCESS) {
        out->ssid_length = strnlen((char *)setting.ssid, 32);
        memcpy(out->ssid, setting.ssid, out->ssid_length);
        out->channel = setting.channel; out->encryption = encryption(setting.security_type);
    }
    int rssi = 0;
    wifi_get_ap_bssid(out->bssid);
    if (wifi_get_rssi(&rssi) == RTW_SUCCESS) out->rssi = rssi;
}
static rtw_result_t scan_result(rtw_scan_handler_result_t *result) {
    lock();
    if (result->scan_complete) {
        accumulating.scanning = false; completed = accumulating;
    } else {
        nina_network network = {0};
        rtw_scan_result_t *ap = &result->ap_details;
        network.ssid_length = ap->SSID.len > 32 ? 32 : ap->SSID.len;
        memcpy(network.ssid, ap->SSID.val, network.ssid_length);
        memcpy(network.bssid, ap->BSSID.octet, 6);
        network.channel = ap->channel; network.rssi = ap->signal_strength;
        network.encryption = encryption(ap->security);
        nina_scan_add(&accumulating, &network);
    }
    unlock(); return RTW_SUCCESS;
}
bool nina_wifi_scan_start(void) {
    lock();
    if (busy || accumulating.scanning) { unlock(); return false; }
    nina_scan_begin(&accumulating); memset(&completed, 0, sizeof(completed)); unlock();
    if (wifi_scan_networks(scan_result, NULL) == RTW_SUCCESS) return true;
    lock(); accumulating.scanning = false; unlock(); return false;
}
void nina_wifi_scan_snapshot(nina_scan *out) { lock(); *out = completed; unlock(); }

static struct { bool pending, success; char name[256]; ip_addr_t address; } dns_work;
static void dns_result(const char *name, const ip_addr_t *address, void *arg) {
    (void)name; (void)arg;
    dns_work.success = address != NULL;
    if (address) dns_work.address = *address;
    xSemaphoreGive(dns_done);
}
static void dns_callback(void *unused) {
    (void)unused;
    err_t result = dns_gethostbyname(dns_work.name, &dns_work.address, dns_result, NULL);
    if (result != ERR_INPROGRESS) dns_result(NULL, result == ERR_OK ? &dns_work.address : NULL, NULL);
}
bool nina_wifi_resolve(const char *name, uint8_t ip[4]) {
    memset(ip, 0, 4);
    if (dns_work.pending) {
        if (xSemaphoreTake(dns_done, 0) != pdTRUE) return false;
        dns_work.pending = false;
    }
    if (nina_wifi_status() != 3 || strlen(name) > 255) return false;
    strcpy(dns_work.name, name); dns_work.pending = true;
    if (tcpip_callback_with_block(dns_callback, NULL, 0) != ERR_OK) {
        dns_work.pending = false; return false;
    }
    if (xSemaphoreTake(dns_done, pdMS_TO_TICKS(4000)) != pdTRUE) return false;
    dns_work.pending = false;
    if (dns_work.success) memcpy(ip, &dns_work.address.addr, 4);
    return dns_work.success;
}
uint16_t nina_wifi_ping(const uint8_t ip[4], uint8_t ttl) {
    static uint16_t sequence;
    if (nina_wifi_status() != 3) return 0xffff;
    int fd = lwip_socket(AF_INET, SOCK_RAW, IPPROTO_ICMP);
    if (fd < 0) return 0xffff;
    int timeout = 1000, hops = ttl;
    uint16_t result = 0xffff;
    if (lwip_setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout)) ||
        lwip_setsockopt(fd, IPPROTO_IP, IP_TTL, &hops, sizeof(hops))) goto done;
    struct sockaddr_in destination = {0};
    destination.sin_family = AF_INET; memcpy(&destination.sin_addr.s_addr, ip, 4);
    uint8_t request[16] = {8, 0, 0, 0, 0x4e, 0x49, 0, 0, 'N', 'I', 'N', 'A', 'P', 'I', 'N', 'G'};
    ++sequence; request[6] = sequence >> 8; request[7] = sequence;
    uint32_t sum = 0;
    for (unsigned i = 0; i < sizeof(request); i += 2) sum += ((uint16_t)request[i] << 8) | request[i + 1];
    while (sum >> 16) sum = (sum & 0xffff) + (sum >> 16);
    uint16_t checksum = ~sum; request[2] = checksum >> 8; request[3] = checksum;
    TickType_t start = xTaskGetTickCount();
    if (lwip_sendto(fd, request, sizeof(request), 0, (struct sockaddr *)&destination, sizeof(destination)) != sizeof(request)) goto done;
    while ((xTaskGetTickCount() - start) * portTICK_PERIOD_MS < 1000) {
        uint8_t response[128]; struct sockaddr_in from; socklen_t from_size = sizeof(from);
        int length = lwip_recvfrom(fd, response, sizeof(response), 0, (struct sockaddr *)&from, &from_size);
        if (length < 20) break;
        size_t header = (response[0] & 15) * 4;
        if (header < 20 || header + sizeof(request) > (size_t)length || (response[0] >> 4) != 4 ||
            memcmp(&from.sin_addr.s_addr, ip, 4)) continue;
        uint8_t *icmp = response + header;
        if (icmp[0] || icmp[1] || memcmp(icmp + 4, request + 4, sizeof(request) - 4)) continue;
        result = (xTaskGetTickCount() - start) * portTICK_PERIOD_MS; break;
    }
done:
    lwip_close(fd); return result;
}
