# Supported commands and limits

| Commands | Behavior |
| --- | --- |
| `10`, `11` | Open and WPA/WPA2 station connection |
| `14`–`16` | Static IPv4, DNS servers, DHCP hostname |
| `20`–`27` | Connection, addressing, MAC, SSID, BSSID, RSSI, security, scan results |
| `28` | UDP bind; TCP server mode returns failure |
| `2a`–`2f` | Send status, available/read, TCP/UDP/TLS connect, close, client state |
| `30` | Disconnect and release all sockets |
| `32`, `33`, `36`, `3c`, `3d` | Scan RSSI, security, start, BSSID, channel |
| `34`, `35` | DNS lookup and cached IPv4 result |
| `37` | Firmware version |
| `39`, `3a` | Send accumulated UDP datagram and remote endpoint |
| `3b`, `3e`, `3f` | Unix time, ping, allocate socket |
| `44`–`46` | Stream write, buffered read, UDP accumulation |

Command IDs are hexadecimal. Unsupported commands return a NINA error frame.

There are four socket slots, with at most one TLS context. SPI frames are
limited to 4092 bytes, response data and accumulated UDP datagrams to 4084
bytes. Larger stream transfers require multiple requests. TLS requires a
hostname, synchronized time and a chain to one of the four checked-in roots;
IP-only TLS is rejected. The root selection is deliberately smaller than a
browser trust store. See [certificate details](certificates/README.md).

AP mode, TCP servers, enterprise Wi-Fi, BLE/HCI, mDNS RPC, GPIO proxy,
filesystem commands, OTA, client certificates and low-level BSD commands
`70`–`7f` are not implemented. Open-network support exists but separate
open-AP hardware acceptance remains unrun. Direct DHCP hostname-option
inspection and logic-analyzer timing capture also remain unrun; the latter
was explicitly deferred during hardware acceptance.

