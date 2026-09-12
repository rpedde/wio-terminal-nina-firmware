"""Additional 8 MHz CS-boundary test; run separately after the stock-driver gate."""
import board
import busio
import digitalio
import time

spi = busio.SPI(board.RTL_CLK, MOSI=board.RTL_MOSI, MISO=board.RTL_MISO)
cs = digitalio.DigitalInOut(board.RTL_CS)
cs.switch_to_output(value=True)
ready = digitalio.DigitalInOut(board.RTL_READY)
reset = digitalio.DigitalInOut(board.RTL_PWR)
reset.switch_to_output(value=False)
time.sleep(0.1)
reset.value = True
time.sleep(1)


def wait_ready(value):
    deadline = time.monotonic() + 1
    while ready.value != value:
        if time.monotonic() > deadline:
            raise RuntimeError("READY timeout", value)


def select():
    wait_ready(False)
    cs.value = False
    wait_ready(True)


while not spi.try_lock():
    pass
spi.configure(baudrate=8000000, polarity=0, phase=0, bits=8)
select()
spi.write(b"\xe0\x37")
time.sleep(0.05)
assert ready.value, "READY changed during request pause"
spi.write(b"\x00\xee")
cs.value = True
select()
header = bytearray(4)
spi.readinto(header)
time.sleep(0.05)
assert ready.value, "READY changed during response pause"
payload = bytearray(17)
spi.readinto(payload)
cs.value = True
wait_ready(False)
assert header == b"\xe0\xb7\x01\x10", header
assert payload == b"3.3.0+rtl8720.1\x00\xee", payload
spi.unlock()
spi.deinit()
cs.deinit()
ready.deinit()
reset.deinit()
print("PHASE2 PASS CS boundaries: 50 ms pauses in request and response at 8 MHz")
