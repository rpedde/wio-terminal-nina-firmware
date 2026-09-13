"""Stock ESP32SPI 11.1.4 verified TLS gate; run via raw REPL.
Optional TLS_TEST_HOST (default sha256.badssl.com), TLS_CYCLES (default 100).
Public negative endpoints share badssl's infrastructure with the positive test.
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
start = time.monotonic()
while True:
    try:
        stamp = esp.get_time()[0]
        assert 1700000000 < stamp < 2208988800, "Implausible SNTP timestamp"
        break
    except OSError:
        assert time.monotonic() - start < 30, "SNTP did not synchronize"
        time.sleep(0.2)
print("PHASE5 PASS SNTP Unix time:", esp.get_time())
host = os.getenv("TLS_TEST_HOST") or "sha256.badssl.com"

def capacity():
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

def https(name):
    slot = esp.get_socket()
    start = time.monotonic()
    try:
        esp.socket_open(slot, name, 443, conn_mode=esp.TLS_MODE)
        assert time.monotonic() - start < 9.5, "TLS connection exceeded response budget"
        request = ("GET / HTTP/1.0\r\nHost: " + name +
            "\r\nUser-Agent: Wio-NINA-Phase5\r\nConnection: close\r\n\r\n").encode()
        assert len(request) > 64
        assert esp.socket_write(slot, request) == len(request)
        start = time.monotonic()
        while not esp.socket_available(slot):
            assert time.monotonic() - start < 10, "TLS receive timeout"
            time.sleep(0.01)
        response = esp.socket_read(slot, 65535)
        assert response.startswith(b"HTTP/1."), response[:80]
        assert 0 < len(response) <= 4084
        total = len(response)
        while esp.socket_connected(slot) or esp.socket_available(slot):
            assert time.monotonic() - start < 15
            total += len(esp.socket_read(slot, 65535))
            time.sleep(0.01)
        return total
    finally:
        esp.socket_close(slot)

def rejected(name):
    # Establish TCP first so an unreachable endpoint cannot pass this check.
    slot = esp.get_socket()
    esp.socket_open(slot, name, 443)
    esp.socket_close(slot)
    slot = esp.get_socket()
    start = time.monotonic()
    try:
        try:
            esp.socket_open(slot, name, 443, conn_mode=esp.TLS_MODE)
        except ConnectionError:
            assert time.monotonic() - start < 9.5
        else:
            raise AssertionError("Invalid TLS certificate accepted: " + name)
        # Firmware must release failed reservations before any explicit close.
        capacity()
    finally:
        esp.socket_close(slot)
    print("PHASE5 PASS rejection:", name)

capacity()
print("PHASE5 PASS trusted HTTPS:", host, https(host), "bytes")
for name in ("self-signed.badssl.com", "wrong.host.badssl.com", "expired.badssl.com"):
    rejected(name)
    assert https(host) > 0
print("PHASE5 PASS HTTPS after each rejection")
cycles = int(os.getenv("TLS_CYCLES") or 100)
for cycle in range(cycles):
    https(host)
    capacity()
    if (cycle + 1) % 10 == 0:
        print("PHASE5 TLS cycles:", cycle + 1)
slot = esp.get_socket()
esp.socket_open(slot, host, 443, conn_mode=esp.TLS_MODE)
esp.disconnect()
# Keep the stock driver's bookkeeping consistent after firmware closes all.
esp.socket_close(slot)
capacity()
time.sleep(2)
esp.connect(os.getenv("WIFI_SSID"), os.getenv("WIFI_PASSWORD") or None, timeout=30)
assert https(host) > 0
print("PHASE5 PASS disconnect cleanup and HTTPS after reconnect")
print("PHASE5 GATE PASSED (power-cycle and runtime heap measurement separate)")
