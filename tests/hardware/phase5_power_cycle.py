"""Run via raw REPL after a user-confirmed physical power removal of >=10 s.
Uses stock ESP32SPI 11.1.4 and production firmware. TLS_TEST_HOST is optional.
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


capacity()
print("PHASE5 POWER-CYCLE HTTPS PASS:", host, https(host), "bytes")
capacity()
