/* The pinned SDK ships a precompiled DHCP client with LWIP_NETIF_HOSTNAME=0.
 * Link-wrap its two outgoing UDP paths to add option 12. Do not change the
 * netif ABI or replace the vendor DHCP implementation. TCP/IP task only. */
#include "lwip/udp.h"
#include "nina_dhcp.h"
extern const char *nina_wifi_dhcp_hostname(void);
extern err_t __real_udp_sendto_if(struct udp_pcb *, struct pbuf *, const ip_addr_t *, u16_t, struct netif *);
extern err_t __real_udp_sendto_if_src(struct udp_pcb *, struct pbuf *, const ip_addr_t *, u16_t, struct netif *, const ip_addr_t *);
static struct pbuf *with_hostname(struct pbuf *p) {
    /* Bound allocations even if a broken caller presents a jumbo datagram. */
    if (p->tot_len < 241 || p->tot_len > 1200) return NULL;
    struct pbuf *copy = pbuf_alloc(PBUF_TRANSPORT, p->tot_len + 258, PBUF_RAM);
    if (!copy) return NULL;
    if (copy->len != copy->tot_len || pbuf_copy_partial(p, copy->payload, p->tot_len, 0) != p->tot_len) {
        pbuf_free(copy); return NULL;
    }
    size_t length = nina_dhcp_hostname(copy->payload, p->tot_len, copy->len, nina_wifi_dhcp_hostname());
    if (!length) { pbuf_free(copy); return NULL; }
    pbuf_realloc(copy, length); return copy;
}
err_t __wrap_udp_sendto_if(struct udp_pcb *pcb, struct pbuf *p, const ip_addr_t *ip,
                           u16_t port, struct netif *netif) {
    if (pcb->local_port != 68 || port != 67) return __real_udp_sendto_if(pcb, p, ip, port, netif);
    struct pbuf *copy = with_hostname(p);
    if (!copy) return ERR_MEM;
    err_t result = __real_udp_sendto_if(pcb, copy, ip, port, netif);
    pbuf_free(copy); return result;
}
err_t __wrap_udp_sendto_if_src(struct udp_pcb *pcb, struct pbuf *p, const ip_addr_t *ip,
                               u16_t port, struct netif *netif, const ip_addr_t *source) {
    if (pcb->local_port != 68 || port != 67) return __real_udp_sendto_if_src(pcb, p, ip, port, netif, source);
    struct pbuf *copy = with_hostname(p);
    if (!copy) return ERR_MEM;
    err_t result = __real_udp_sendto_if_src(pcb, copy, ip, port, netif, source);
    pbuf_free(copy); return result;
}
