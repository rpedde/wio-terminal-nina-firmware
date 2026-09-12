#ifndef NINA_DHCP_H
#define NINA_DHCP_H
#include <stddef.h>
#include <stdint.h>
/* Inserts DHCP option 12 before END, preserving other options and minimum
 * packet size. Returns the new size or zero for malformed/oversized input. */
size_t nina_dhcp_hostname(uint8_t *packet, size_t length, size_t capacity, const char *name);
#endif
