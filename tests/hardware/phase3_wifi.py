"""Stock Wio CircuitPython + ESP32SPI 11.1.4 phase 3 acceptance.

Configure CIRCUITPY/settings.toml using settings.toml.example. Credentials
remain on the board and are never printed. Run via raw REPL or as code.py.
"""
import os
import time
import board
import busio
import digitalio
from adafruit_esp32spi import adafruit_esp32spi

assert adafruit_esp32spi.__version__ == "11.1.4"
ssid = os.getenv("WIFI_SSID")
password = os.getenv("WIFI_PASSWORD") or None
open_ssid = os.getenv("OPEN_WIFI_SSID")
dns_host = os.getenv("DNS_TEST_HOST") or "example.com"
assert ssid, "Fill in WIFI_SSID in CIRCUITPY/settings.toml"

spi = busio.SPI(board.RTL_CLK, MOSI=board.RTL_MOSI, MISO=board.RTL_MISO)
cs = digitalio.DigitalInOut(board.RTL_CS)
ready = digitalio.DigitalInOut(board.RTL_READY)
reset = digitalio.DigitalInOut(board.RTL_PWR)
esp = adafruit_esp32spi.ESP_SPIcontrol(spi, cs, ready, reset)


def connect_and_check(name, key, label):
    esp.set_hostname("wio-nina-test")
    start = time.monotonic()
    esp.connect(name, key, timeout=30)
    assert esp.status == 3
    print("PHASE3 PASS", label, "connection in", time.monotonic() - start, "seconds")
    addresses = esp.network_data
    for field in ("ip_addr", "netmask", "gateway"):
        assert len(addresses[field]) == 4 and any(addresses[field]), field
    print("PHASE3 PASS addressing:", {k: esp.pretty_ip(v) for k, v in addresses.items()})
    mac = esp.MAC_address_actual
    assert len(mac) == 6 and any(mac)
    info = esp.ap_info
    assert info.ssid == name
    assert len(info.bssid) == 6 and any(info.bssid)
    assert -127 <= info.rssi <= 0
    assert info.authmode == ("WPA2" if key else "OPEN"), info.authmode
    print("PHASE3 PASS MAC/SSID/BSSID/RSSI/security:", bytes(mac).hex(), info.rssi, info.authmode)
    address = esp.get_host_by_name(dns_host)
    assert len(address) == 4 and any(address)
    print("PHASE3 PASS DNS:", dns_host, esp.pretty_ip(address))
    elapsed = esp.ping(addresses["gateway"])
    assert elapsed != 65535, "Gateway did not respond to ICMP"
    print("PHASE3 PASS ping gateway:", elapsed, "ms")
    # Cached numeric DNS and failure must not leak the previous success.
    assert esp.get_host_by_name("192.0.2.80") == bytes([192, 0, 2, 80])
    start = time.monotonic()
    try:
        esp.get_host_by_name("nina-phase3-test.invalid")
    except ConnectionError:
        assert time.monotonic() - start < 6
    else:
        raise AssertionError("Invalid DNS hostname unexpectedly resolved")
    assert esp._send_command_get_response(0x35)[0] == bytes(4)
    print("PHASE3 PASS DNS failure deadline and cleared cache")
    esp.disconnect()
    assert esp.status == 6
    time.sleep(2)


try:
    assert esp.firmware_version == "3.3.0+rtl8720.1"
    assert esp.status == 0
    for _ in range(100):
        assert esp.firmware_version == "3.3.0+rtl8720.1"
    print("PHASE3 PASS version/status")
    networks = esp.scan_networks()
    assert networks, "No access points found"
    bssids = set()
    for network in networks:
        # Driver Network.ssid falls back to live AP queries for empty SSIDs.
        assert len(network._raw_ssid) <= 32
        assert len(network.bssid) == 6
        assert network.bssid not in bssids
        bssids.add(network.bssid)
        assert -127 <= network.rssi <= 0
        assert network.channel > 0
        assert network.authmode in ("OPEN", "WEP", "PSK", "WPA2", "UNKNOWN")
    print("PHASE3 PASS scan:", len(networks), "unique APs with indexed metadata")
    connect_and_check(ssid, password, "configured AP")
    connect_and_check(ssid, password, "reconnect")
    if open_ssid:
        connect_and_check(open_ssid, None, "open AP")
    else:
        print("PHASE3 NOT RUN: separate open AP (OPEN_WIFI_SSID unset)")
    print("PHASE3 configured network checks passed")
finally:
    spi.deinit()
    cs.deinit()
    ready.deinit()
    reset.deinit()
