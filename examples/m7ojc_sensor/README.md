# M7OJC sensor

Build and flash the XIAO ESP32S3 with the Wio-SX1262 using
`pio run -e Xiao_S3_WIO_m7ojc_sensor -t upload`. The BME680, SCD41, Pimoroni
MiCS-6814 and PMSA003I share D4 (SDA) and D5 (SCL). The fitted MiCS-6814 uses address 0x18.

Send `wx`, `sensor` or `sensors` in the MeshCore channel `#m7ojc`. Case is
ignored. A companion that has advertised can also request telemetry without
logging in. The gas channels report resistance in kΩ. The SCD41 reports CO₂ in
ppm once its first measurement is ready. The PMSA003I at 0x12 supplies atmospheric
PM1.0, PM2.5 and PM10 in µg/m³. The fourth chat line gives the three readings in
that order. The MeshCore telemetry reply uses the feature branch's eight-byte
LPP record (type 228). The USB command `pm` prints a fresh reading.

## Channel watch

Use the USB CLI or the node's admin CLI in MeshCore. Changes take effect immediately
and survive a reboot. The node starts with watch on and rule 1 set to
`#m7ojc wx,sensor,sensors`. You can delete this rule like any other.

```text
sensor.watch
sensor.watch add #m7ojc wx,sensor
sensor.watch add #weather wx da7*,wx bexley*,wx m7ojc
sensor.watch list
sensor.watch del 2
sensor.watch off
sensor.watch on
```

Each `add` creates one numbered rule. The CLI, hashtag names and request patterns
accept any mix of ASCII upper and lower case. Commas separate alternatives. Each
pattern matches a whole single-line chat message, ignoring surrounding spaces.
`*` matches zero or more characters. On `#weather`, the rule above accepts
`WX DA7`, `wx Bexleyheath` and `Wx M7OJC`. A match sends one weather report back
to the same channel, even if several rules match.

`sensor.watch` shows the on/off state and rule count. Turning it off keeps the
rules. Up to eight rules fit, each with up to 127 bytes for the hashtag, space
and patterns together. IDs stay fixed until deletion. When a list ends with
`next=4`, use `sensor.watch list 4` for the next page.

## Home Assistant

Open the node's USB serial console at 115200 baud and enter one command per line.
Use your own values. Spaces inside values are preserved.

```text
set wifi.ssid Your Wi-Fi
set wifi.password Your Wi-Fi password
set mqtt.host 192.168.1.10
set mqtt.port 1883
set mqtt.user Your MQTT username
set mqtt.password Your MQTT password
set mqtt.enabled on
reboot
```

Use a 2.4 GHz network and a local MQTT broker that accepts plain MQTT over TCP.
Port 1883 is the default. Omit the user and password for an anonymous broker.
Enter a string setting with no value to clear it. Settings are saved on the node
and take effect on reboot. Use `mqtt status` for the live connection state,
`get wifi.ip` for the node’s current address and `get mqtt.host` for the saved broker. Password queries show only whether a
password is set. To turn off MQTT, run `set mqtt.enabled off` and reboot.
Clearing `wifi.ssid` and rebooting also turns off the Wi-Fi station.

With MQTT discovery enabled, open **Settings > Devices & services > MQTT > Devices**
and select the node by its MeshCore name. The twelve environmental sensors and
six diagnostics update every minute. CPU load estimates the percentage of time
the two cores run non-idle tasks over the sampling interval. The RAM readings
show used internal heap (%) and free byte-addressable internal heap (B). Wi-Fi
signal, uptime and the MQTT reconnect count help distinguish resets from link drops.

Home Assistant receives fresh samples on `meshcore/m7ojc_<mac>/live` and marks
the entities unavailable after 300 seconds without a new sample. The broker
retains the most recent snapshot on `meshcore/m7ojc_<mac>/state` and the
connection's online/offline status on `meshcore/m7ojc_<mac>/availability`.
The MAC is the station's twelve lowercase hex digits. Missing readings are JSON `null`.

The node groups all entities in one retained discovery message on
`homeassistant/device/m7ojc_<mac>/config`. When Home Assistant sends `online`
on `homeassistant/status`, the node reannounces discovery and connection status
and replays the cached snapshot on `/state`. The next measurement updates `/live`.

## Clock

NTP starts when the configured Wi-Fi network has an IP address, even with MQTT
disabled. It uses the DHCP time server when supplied and otherwise resolves
`pool.ntp.org`. Use `ntp status` for the last successful reply and `clock` for
the current UTC time. `ntp sync` requests a fresh reply. `ntp off` and `ntp on`
take effect immediately and survive a reboot.
