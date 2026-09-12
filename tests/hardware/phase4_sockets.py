"""Phase 4 gate: stock ESP32SPI 11.1.4 TCP/UDP, exhaustion and reuse.

Run through raw REPL or as code.py with local settings.toml credentials.
Optional HTTP_TEST_HOST, NTP_TEST_HOST, SOCKET_CYCLES (default 100).
"""
import os
import time
import board
import busio
import digitalio
from adafruit_esp32spi import adafruit_esp32spi

assert adafruit_esp32spi.__version__ == "11.1.4"
spi = busio.SPI(board.RTL_CLK, MOSI=board.RTL_MOSI, MISO=board.RTL_MISO)
cs = digitalio.DigitalInOut(board.RTL_CS)
ready = digitalio.DigitalInOut(board.RTL_READY)
reset = digitalio.DigitalInOut(board.RTL_PWR)
esp = adafruit_esp32spi.ESP_SPIcontrol(spi, cs, ready, reset)
esp.connect(os.getenv("WIFI_SSID"), os.getenv("WIFI_PASSWORD") or None, timeout=30)
http_host = os.getenv("HTTP_TEST_HOST") or "example.com"
ntp_host = os.getenv("NTP_TEST_HOST") or "pool.ntp.org"


def exhaustion():
    slots = [esp.get_socket() for _ in range(4)]
    assert len(set(slots)) == 4
    try:
        esp.get_socket()
    except OSError as error:
        assert error.args[0] == 23
    else:
        raise AssertionError("Fifth allocation succeeded")
    for slot in slots:
        esp.socket_close(slot)
        esp.socket_close(slot)
    print("PHASE4 PASS four reserved slots, exhaustion and repeated close")


def wait_data(slot):
    start = time.monotonic()
    while not esp.socket_available(slot):
        assert time.monotonic() - start < 10, "Receive timeout"
        time.sleep(0.01)


exhaustion()
slot = esp.get_socket()
try:
    esp.socket_connect(slot, http_host, 80)
    # More than one driver's 64-byte write chunk, including a zero final
    # chunk when the request length happens to be a multiple of 64.
    request = ("GET / HTTP/1.0\r\nHost: " + http_host +
               "\r\nUser-Agent: Wio-NINA-Phase4\r\nConnection: close\r\n\r\n").encode()
    assert len(request) > 64
    assert esp.socket_write(slot, request) == len(request)
    wait_data(slot)
    response = esp.socket_read(slot, 65535)
    assert response.startswith(b"HTTP/1."), response[:80]
    assert len(response) <= 4084
    total = len(response)
    start = time.monotonic()
    while esp.socket_connected(slot) or esp.socket_available(slot):
        assert time.monotonic() - start < 15
        total += len(esp.socket_read(slot, 65535))
        time.sleep(0.01)
    print("PHASE4 PASS HTTP and oversized receive request:", total, "bytes")
finally:
    esp.socket_close(slot)

slot = esp.get_socket()
try:
    esp.start_server(0, slot, conn_mode=esp.UDP_MODE)
    esp.socket_open(slot, ntp_host, 123, conn_mode=esp.UDP_MODE)
    packet = bytearray(48)
    packet[0] = 0x1b
    esp.socket_write(slot, packet, conn_mode=esp.UDP_MODE)
    wait_data(slot)
    assert esp.socket_available(slot) >= 48
    first = esp.socket_read(slot, 12)
    assert len(first) == 12 and first[0] & 7 == 4
    assert esp.socket_available(slot) >= 36
    rest = esp.socket_read(slot, 4084)
    assert len(rest) >= 36 and any(rest[28:36])
    remote = esp.get_remote_data(slot)
    assert remote["port"] == 123 and any(remote["ip_addr"])
    print("PHASE4 PASS UDP NTP, bind, partial datagram reads and remote port")
finally:
    esp.socket_close(slot)

ip = esp.get_host_by_name(http_host)
cycles = int(os.getenv("SOCKET_CYCLES") or 100)
for cycle in range(cycles):
    slot = esp.get_socket()
    try:
        esp.socket_connect(slot, ip, 80)
        esp.socket_write(slot, request)
        wait_data(slot)
        assert esp.socket_read(slot, 16).startswith(b"HTTP/1.")
    finally:
        esp.socket_close(slot)
print("PHASE4 PASS TCP connect/transfer/close cycles:", cycles)
exhaustion()
slots = [esp.get_socket() for _ in range(4)]
esp.disconnect()
exhaustion()
time.sleep(2)  # Let the asynchronous Wi-Fi disconnect worker finish.
esp.connect(os.getenv("WIFI_SSID"), os.getenv("WIFI_PASSWORD") or None, timeout=30)
print("PHASE4 PASS disconnect releases all slots and Wi-Fi reconnects")
print("PHASE4 GATE PASSED (runtime free-heap measurement not included)")

spi.deinit()
cs.deinit()
ready.deinit()
reset.deinit()
