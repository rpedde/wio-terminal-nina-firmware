"""Verified HTTPS on stock Wio CircuitPython and ESP32SPI 11.1.4."""
import os
import time
import board
import busio
import digitalio
from adafruit_esp32spi import adafruit_esp32spi

spi = busio.SPI(board.RTL_CLK, MOSI=board.RTL_MOSI, MISO=board.RTL_MISO)
cs = digitalio.DigitalInOut(board.RTL_CS)
ready = digitalio.DigitalInOut(board.RTL_READY)
reset = digitalio.DigitalInOut(board.RTL_PWR)
esp = adafruit_esp32spi.ESP_SPIcontrol(spi, cs, ready, reset)
esp.connect(os.getenv("WIFI_SSID"), os.getenv("WIFI_PASSWORD") or None, timeout=30)

deadline = time.monotonic() + 30
while True:
    try:
        if esp.get_time()[0] >= 946684800:
            break
    except OSError:
        pass
    if time.monotonic() >= deadline:
        raise RuntimeError("SNTP did not synchronize")
    time.sleep(0.2)

host = "sha256.badssl.com"
sock = esp.get_socket()
try:
    esp.socket_open(sock, host, 443, conn_mode=esp.TLS_MODE)
    request = ("GET / HTTP/1.0\r\nHost: " + host +
               "\r\nConnection: close\r\n\r\n").encode()
    if esp.socket_write(sock, request) != len(request):
        raise RuntimeError("Incomplete HTTP request")
    deadline = time.monotonic() + 20
    while esp.socket_connected(sock) or esp.socket_available(sock):
        if time.monotonic() >= deadline:
            raise RuntimeError("HTTP response timeout")
        data = esp.socket_read(sock, 1024)
        if data:
            print(data.decode("utf-8"), end="")
        else:
            time.sleep(0.01)
finally:
    esp.socket_close(sock)
