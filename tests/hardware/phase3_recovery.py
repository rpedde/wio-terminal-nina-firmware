"""Phase 3 connection failures, cancellation, static IPv4 and recovery."""
import os
import time
import board
import busio
import digitalio
from adafruit_esp32spi import adafruit_esp32spi

ssid = os.getenv("WIFI_SSID")
password = os.getenv("WIFI_PASSWORD") or None
assert ssid
spi = busio.SPI(board.RTL_CLK, MOSI=board.RTL_MOSI, MISO=board.RTL_MISO)
cs = digitalio.DigitalInOut(board.RTL_CS)
ready = digitalio.DigitalInOut(board.RTL_READY)
reset = digitalio.DigitalInOut(board.RTL_PWR)
esp = adafruit_esp32spi.ESP_SPIcontrol(spi, cs, ready, reset)


def final_status():
    start = time.monotonic()
    while time.monotonic() - start < 35:
        status = esp.status
        if status != 0:
            return status
        time.sleep(0.1)
    raise AssertionError("Connection worker did not finish")


try:
    start = time.monotonic()
    esp.wifi_set_network(b"nina-phase3-absent-8720dn")
    assert time.monotonic() - start < 1, "Connection acknowledgement blocked"
    assert final_status() == 1
    # Allow the worker to consume the vendor completion following its event.
    time.sleep(1)
    print("PHASE3 PASS asynchronous acknowledgement and no-SSID status")
    if password:
        wrong = ("x" if password[0] != "x" else "y") + password[1:]
        esp.wifi_set_passphrase(ssid.encode(), wrong.encode())
        assert final_status() == 4
        print("PHASE3 PASS authentication failure status")

    esp.disconnect()
    time.sleep(2)
    if password:
        esp.wifi_set_passphrase(ssid.encode(), password.encode())
    else:
        esp.wifi_set_network(ssid.encode())
    esp.disconnect()
    assert esp.status == 6
    time.sleep(8)
    assert esp.status == 6, "Late completion undid explicit disconnect"
    print("PHASE3 PASS cancellation and late-completion protection")

    esp.connect(ssid, password, timeout=30)
    addresses = esp.network_data
    # Reuse this client's just-assigned lease rather than inventing an IP.
    result = esp.set_ip_config(esp.pretty_ip(addresses["ip_addr"]),
                               esp.pretty_ip(addresses["gateway"]),
                               esp.pretty_ip(addresses["netmask"]))
    assert result[0][0] == 1
    assert esp.network_data == addresses
    assert esp.ping(addresses["gateway"]) != 65535
    esp.disconnect()
    time.sleep(2)
    esp.connect(ssid, password, timeout=30)
    assert esp.network_data == addresses
    assert esp.ping(addresses["gateway"]) != 65535
    print("PHASE3 PASS static IPv4 and static reconnect")

    esp.set_dns_config("1.1.1.1", "1.0.0.1")
    address = esp.get_host_by_name("www.iana.org")
    assert any(address)
    print("PHASE3 PASS explicit DNS configuration and lookup")
    esp.reset()
    assert esp.status == 0
    print("PHASE3 recovery checks passed; reset to default DHCP configuration")
finally:
    spi.deinit()
    cs.deinit()
    ready.deinit()
    reset.deinit()
