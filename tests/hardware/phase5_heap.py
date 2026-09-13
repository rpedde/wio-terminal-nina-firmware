"""RTL heap measurements via dedicated UART; run via raw REPL.
Requires NINA_HEAP_DIAGNOSTICS=1 firmware and stock ESP32SPI 11.1.4.
Fixed 10 warm-up/100 measured HTTPS cycles and 30 certificate rejections.
TLS_TEST_HOST is optional; credentials remain in local settings.toml.
"""
import os
import time
import board
import busio
import digitalio
from adafruit_esp32spi import adafruit_esp32spi
# RTL dedicated LOG UART, separate from the SPI signals.
uart = busio.UART(board.RTL_RXD, board.RTL_TXD, baudrate=115200,
                  receiver_buffer_size=8192, timeout=0.01)
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


def heap_sample(label):
    # Drain older snapshots emitted by driver status queries; request one now.
    while uart.read(8192):
        pass
    assert esp.status == 3
    deadline = time.monotonic() + 2
    pending = b""
    while time.monotonic() < deadline:
        pending += uart.read(8192) or b""
        while b"\n" in pending:
            line, pending = pending.split(b"\n", 1)
            marker = line.find(b"NINA_HEAP ")
            if marker >= 0:
                fields = line[marker:].strip().split()
                free = int(fields[1].split(b"=")[1])
                minimum = int(fields[2].split(b"=")[1])
                assert 0 < minimum <= free
                print("HEAP_SAMPLE", label, free, minimum)
                return free
        time.sleep(0.01)
    raise AssertionError("Missing RTL heap telemetry; flash NINA_HEAP_DIAGNOSTICS=1")

try:
    heap_sample("before_tls")
    slot = esp.get_socket()
    try:
        esp.socket_open(slot, host, 443, conn_mode=esp.TLS_MODE)
        heap_sample("tls_open")
    finally:
        esp.socket_close(slot)
    heap_sample("first_tls_closed")
    for cycle in range(10):
        https(host)
        capacity()
        time.sleep(0.2)
        heap_sample("warmup_" + str(cycle + 1))
    baseline = heap_sample("baseline")
    for cycle in range(100):
        https(host)
        capacity()
        time.sleep(0.2)
        heap_sample("https_" + str(cycle + 1))
    for cycle in range(10):
        for name in ("self-signed.badssl.com", "wrong.host.badssl.com", "expired.badssl.com"):
            rejected(name)
        time.sleep(0.2)
        heap_sample("rejection_batch_" + str(cycle + 1))
    assert https(host) > 0
    capacity()
    heap_sample("after_recovery")
    # Let TCP close-state and other asynchronous network allocations settle.
    for elapsed in range(10, 131, 10):
        time.sleep(10)
        final = heap_sample("settled_" + str(elapsed))
    print("HEAP_RESULT baseline", baseline, "final", final, "loss", baseline - final)
    assert baseline - final <= 1024, "Post-warmup heap loss exceeds 1 KiB allowance"
    print("PHASE5 RTL HEAP GATE PASSED: 100 HTTPS cycles, 30 certificate failures, recovery")
finally:
    uart.deinit()
