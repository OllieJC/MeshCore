# Olimex ESP32-PoE with Wio-SX1262

All four targets use the external Wio-SX1262 for LoRa. Build with
`pio run -e <target>`. Add `-t upload` to flash over USB.

| PlatformIO target | Interfaces |
| --- | --- |
| `Olimex_ESP32_PoE_SX1262_repeater` | USB and Ethernet repeater CLI |
| `Olimex_ESP32_PoE_companion` | USB and Bluetooth LE |
| `Olimex_ESP32_PoE_companion_ble_ethernet` | USB, Bluetooth LE and Ethernet |
| `Olimex_ESP32_PoE_companion_ethernet` | USB and Ethernet |

## Wiring

On the standard ESP32-PoE WROOM, connect Olimex 3.3 V (UEXT pin 1) to the
standalone Wio carrier's 3V3 pin and GND (UEXT pin 2) to Wio GND. Leave Wio VIN
open. Keep the microSD slot empty because it shares the radio's SPI pins.

| Wio signal | Olimex pin |
| --- | --- |
| SCK, MISO, MOSI, NSS | 14, 15, 2, 5 |
| RST | ESP_EN (EXT1 pin 4) |
| BUSY, DIO1 | GPIO35, GPIO33 |
| RF_SW (receive enable) | 32 |

The Wio resets with the ESP32 when ESP_EN is pulled low. The firmware uses
`RADIOLIB_NC` for the radio reset pin because there is no separate GPIO reset
line. Leave any optional battery-sense jumper to GPIO35 open so the Wio can
drive BUSY. The repeater uses the UEXT UART for GPS with ESP32 RX on GPIO36
and TX on GPIO4.

When using USB and Ethernet together on the standard ESP32-PoE, use USB power
and an unpowered Ethernet port. [Olimex hardware notes](https://www.olimex.com/Products/IoT/ESP32/ESP32-POE/open-source-hardware)

## Ethernet

On the repeater, open USB serial at 115200 baud with carriage-return line
endings. Run `password <new-password>` to set an admin password, then
`eth.status` to see the DHCP address. Connect with `nc <board-ip> 23` and enter
the password. Use a trusted LAN because the text console sends it in plaintext.

The Ethernet companion targets serve the binary companion protocol on TCP port 5000.

## Fixed repeater location

Set coordinates with `set lat <degrees>` and `set lon <degrees>`. Run
`gps advert prefs` to include them in adverts. Check that `gps advert` returns
`> prefs`, then use `advert.zerohop` for a local test. Use `gps advert none`
to stop including the coordinates.

## Ethernet time sync

Ethernet builds sync by default using DHCP option 42 or `pool.ntp.org` if DHCP
supplies no NTP server. On the repeater, use `ntp status` to check, `ntp sync`
to request a sync, or `ntp off` and `ntp on` to save your preference.
