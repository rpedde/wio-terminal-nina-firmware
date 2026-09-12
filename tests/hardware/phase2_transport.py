"""Run as code.py on stock Wio Terminal CircuitPython with ESP32SPI 11.1.4.

Capture USB console output and READY/CS/CLK/MOSI/MISO with a logic analyzer.
There are no retries: any timeout, wrong byte or failed reset fails the run.
"""
import board
import busio
import digitalio
import sys
import time
from adafruit_esp32spi import adafruit_esp32spi

EXPECTED = "3.3.0+rtl8720.1"
print("PHASE2 environment:", sys.implementation)
print("ESP32SPI:", adafruit_esp32spi.__version__)
if adafruit_esp32spi.__version__ != "11.1.4":
    raise RuntimeError("Install the released ESP32SPI 11.1.4 library")

spi = busio.SPI(board.RTL_CLK, MOSI=board.RTL_MOSI, MISO=board.RTL_MISO)
cs = digitalio.DigitalInOut(board.RTL_CS)
ready = digitalio.DigitalInOut(board.RTL_READY)
reset = digitalio.DigitalInOut(board.RTL_PWR)
# Leave board.RTL_DIR/SYNC untouched. The stock driver configures 8 MHz.
esp = adafruit_esp32spi.ESP_SPIcontrol(spi, cs, ready, reset)

started = time.monotonic()
for i in range(100):
    assert esp.firmware_version == EXPECTED, ("version", i)
print("PHASE2 PASS version: 100/100")

for i in range(20):
    esp.reset()
    assert esp.status == 0, ("reset status", i)
print("PHASE2 PASS reset/status: 20/20")

for i in range(10000):
    if i & 1:
        assert esp.status == 0, ("mixed status", i)
    else:
        assert esp.firmware_version == EXPECTED, ("mixed version", i)
    if (i + 1) % 1000 == 0:
        print("PHASE2 mixed progress:", i + 1)
print("PHASE2 PASS mixed: 10000/10000")
print("PHASE2 automated checks passed in", time.monotonic() - started, "seconds")
print("Logic-analyzer capture remains deferred by user decision; no trace is claimed.")
