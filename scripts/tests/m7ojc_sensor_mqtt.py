"""Verify the flashed M7OJC sensor's retained discovery, availability and live samples.

Usage: python m7ojc_sensor_mqtt.py HOST STATION_MAC [--user USER] [--port PORT]
Requires paho-mqtt 2.x. When --user is given, the broker password is prompted.
"""

import argparse
import getpass
import json
import math
import queue
import re
import time

import paho.mqtt.client as mqtt


UNITS = {
    "temperature": "°C", "humidity": "%", "pressure": "hPa", "co2": "ppm",
    "no2": "kΩ", "co": "kΩ", "nh3": "kΩ", "bme_gas": "kΩ", "altitude": "m",
    "pm1": "µg/m³", "pm25": "µg/m³", "pm10": "µg/m³",
}


DIAGNOSTICS = {
    "cpu_load": "%", "heap_used_pct": "%", "free_heap_bytes": "B", "wifi_rssi": "dBm",
    "uptime_s": "s", "mqtt_reconnects": None,
}


def check(host, port, mac, user, password, require_pm):
    mac = mac.replace(":", "").lower()
    if not re.fullmatch(r"[0-9a-f]{12}", mac):
        raise ValueError("Expected the XIAO station MAC")
    device = "m7ojc_" + mac
    config = f"homeassistant/device/{device}/config"
    base = f"meshcore/{device}/"
    events = queue.Queue()
    client = mqtt.Client(mqtt.CallbackAPIVersion.VERSION2)
    if user:
        client.username_pw_set(user, password)

    def connected(client, userdata, flags, reason, properties):
        if reason.is_failure:
            events.put(RuntimeError(str(reason)))
        else:
            client.subscribe([(config, 1), (base + "+", 1)])

    client.on_connect = connected
    client.on_message = lambda client, userdata, msg: events.put(msg)
    client.connect(host, port, keepalive=15)
    client.loop_start()
    expected = {**UNITS, **DIAGNOSTICS}
    seen = set()
    retained_state = live_state = online = False
    deadline = time.monotonic() + 135
    try:
        while time.monotonic() < deadline:
            try:
                msg = events.get(timeout=max(0.1, deadline - time.monotonic()))
            except queue.Empty:
                break
            if isinstance(msg, Exception):
                raise msg
            if msg.topic == config:
                discovery = json.loads(msg.payload)
                assert discovery["origin"]["name"] and discovery["device"]["name"]
                assert discovery["device"]["identifiers"] == [device]
                assert discovery["state_topic"] == base + "live"
                assert "availability" not in discovery and "availability_topic" not in discovery
                assert set(discovery["components"]) == set(expected)
                for key, entry in discovery["components"].items():
                    assert entry["platform"] == "sensor" and entry.get("unit_of_measurement") == expected[key]
                    assert entry["unique_id"] == f"{device}_{key}"
                    assert entry["value_template"] == "{{ value_json." + key + " }}"
                    assert entry["state_class"] == "measurement"
                    assert entry["expire_after"] == 300
                    assert "availability" not in entry and "availability_topic" not in entry
                    assert (entry.get("entity_category") == "diagnostic") == (key in DIAGNOSTICS)
                    if key in ("pm1", "pm25", "pm10"):
                        assert entry["device_class"] == key
                    if msg.retain:
                        seen.add(key)
            elif msg.topic == base + "availability":
                online = msg.payload == b"online"
            elif msg.topic == base + "state":
                if msg.retain:
                    retained_state = set(json.loads(msg.payload)) == set(expected)
            elif msg.topic == base + "live":
                assert not msg.retain, "A cached sample would restart Home Assistant's expiry timer"
                state = json.loads(msg.payload)
                assert set(state) == set(expected), state
                assert all(v is None or math.isfinite(v) for v in state.values()), state
                pm = [state[k] for k in ("pm1", "pm25", "pm10")]
                assert all(v is None for v in pm) or all(isinstance(v, int) and 0 <= v <= 65535 for v in pm)
                required = UNITS if require_pm else (k for k in UNITS if k not in ("pm1", "pm25", "pm10"))
                if all(state[k] is not None for k in required):
                    assert 0 <= state["cpu_load"] <= 100 and 0 < state["heap_used_pct"] < 100, state
                    assert state["free_heap_bytes"] > 0 and -127 <= state["wifi_rssi"] < 0, state
                    assert state["uptime_s"] > 0 and state["mqtt_reconnects"] >= 0, state
                    assert state["co2"] > 0 and 0 <= state["humidity"] <= 100, state
                    live_state = True
                    print("Fresh sensor state", state)
            if seen == set(expected) and online and retained_state and live_state:
                print(f"Verified {len(seen)} entities with five-minute expiry, connection status, retained snapshot and fresh telemetry.")
                return
        raise TimeoutError(f"Discovery {seen}, online {online}, retained {retained_state}, live {live_state}")
    finally:
        client.disconnect()
        client.loop_stop()


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("host")
    parser.add_argument("mac")
    parser.add_argument("--port", type=int, default=1883)
    parser.add_argument("--user")
    parser.add_argument("--require-pm", action="store_true", help="require a live PMSA003I sample")
    args = parser.parse_args()
    check(args.host, args.port, args.mac, args.user, getpass.getpass() if args.user else None, args.require_pm)
