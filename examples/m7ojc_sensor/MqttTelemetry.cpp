#include "MqttTelemetry.h"
#include "NodeDiagnostics.h"
#include <ArduinoJson.h>
#include <PubSubClient.h>
#include <WiFi.h>
#include <esp_timer.h>

namespace {

struct Setting {
  const char* key;
  uint8_t max_length;
};

constexpr Setting settings[] = {
  {"wifi.ssid", 32}, {"wifi.password", 64}, {"mqtt.host", 96}, {"mqtt.port", 5},
  {"mqtt.user", 64}, {"mqtt.password", 64}, {"mqtt.enabled", 3},
};

struct TimedReadings {
  Weather::Readings readings;
  int64_t captured_us;
};

bool encodeJson(const JsonDocument& json, String& body) {
  body = "";
  if (json.overflowed()) return false;
  const size_t len = measureJson(json);
  return body.reserve(len) && serializeJson(json, body) == len;
}

bool publishBody(PubSubClient& client, const char* topic, const String& body, bool retain) {
  const size_t len = body.length();
  return client.beginPublish(topic, len, retain) &&
         client.write(reinterpret_cast<const uint8_t*>(body.c_str()), len) == len &&
         client.endPublish();
}

constexpr char ha_status[] = "homeassistant/status";

void addSensor(JsonDocument& json, const char* id, const char* key, const char* name,
               const char* unit, const char* device_class, bool diagnostic = false) {
  JsonObject component = json["components"][key].to<JsonObject>();
  char unique_id[48], value_template[48];
  snprintf(unique_id, sizeof(unique_id), "%s_%s", id, key);
  snprintf(value_template, sizeof(value_template), "{{ value_json.%s }}", key);
  component["platform"] = "sensor";
  component["name"] = name;
  component["unique_id"] = unique_id;
  component["state_class"] = "measurement";
  component["value_template"] = value_template;
  component["expire_after"] = 300;
  if (unit) component["unit_of_measurement"] = unit;
  if (device_class) component["device_class"] = device_class;
  if (diagnostic) component["entity_category"] = "diagnostic";
}

bool publishDiscovery(PubSubClient& client, const char* id, const char* name, const char* live_topic) {
  JsonDocument json;
  json["device"]["identifiers"][0] = id;
  json["device"]["name"] = name;
  json["origin"]["name"] = "MeshCore M7OJC sensor";
  json["state_topic"] = live_topic;
  for (const auto& field : Weather::fields) {
    addSensor(json, id, field.key, field.name, field.unit, field.device_class);
  }
  addSensor(json, id, "cpu_load", "CPU load", "%", nullptr, true);
  addSensor(json, id, "heap_used_pct", "Internal heap used", "%", nullptr, true);
  addSensor(json, id, "free_heap_bytes", "Free internal heap", "B", "data_size", true);
  addSensor(json, id, "wifi_rssi", "Wi-Fi signal", "dBm", "signal_strength", true);
  addSensor(json, id, "uptime_s", "Uptime", "s", "duration", true);
  addSensor(json, id, "mqtt_reconnects", "MQTT reconnects since boot", nullptr, nullptr, true);
  char topic[64];
  snprintf(topic, sizeof(topic), "homeassistant/device/%s/config", id);
  String body;
  return encodeJson(json, body) && publishBody(client, topic, body, true);
}

bool encodeState(const TimedReadings& sample, uint32_t reconnects, String& body) {
  JsonDocument json;
  for (size_t i = 0; i < Weather::Count; i++) {
    const auto& field = Weather::fields[i];
    if (isfinite(sample.readings.values[i])) {
      const float scale = field.decimals ? 10.0f : 1.0f;
      json[field.key] = static_cast<double>(roundf(sample.readings.values[i] * scale)) / scale;
    } else {
      json[field.key] = nullptr;
    }
  }
  NodeDiagnostics diag;
  diag.sample();
  json["cpu_load"] = diag.cpu_load;
  json["heap_used_pct"] = diag.heap_used_pct;
  json["free_heap_bytes"] = diag.free_heap_bytes;
  if (diag.wifi_rssi) json["wifi_rssi"] = diag.wifi_rssi;
  else json["wifi_rssi"] = nullptr;
  json["uptime_s"] = static_cast<uint64_t>(sample.captured_us / 1000000);
  json["mqtt_reconnects"] = reconnects;
  return encodeJson(json, body);
}

}

String MqttTelemetry::getSetting(const char* key) {
  if (prefs.isKey(key)) return prefs.getString(key);
  if (!strcmp(key, "mqtt.enabled")) return "off";
  if (!strcmp(key, "mqtt.port")) return "1883";
  return "";
}

void MqttTelemetry::begin(const char* name) {
  if (!prefs.begin("m7ojc-sensor", false)) {
    status = "settings unavailable";
    return;
  }
  const String ssid = getSetting("wifi.ssid");
  if (ssid.length()) {
    WiFi.persistent(false);
    WiFi.mode(WIFI_STA);
    WiFi.setAutoReconnect(true);
    WiFi.begin(ssid.c_str(), getSetting("wifi.password").c_str());
  }
  if (getSetting("mqtt.enabled") != "on") return;
  host = getSetting("mqtt.host");
  if (!ssid.length() || !host.length()) {
    status = "missing Wi-Fi SSID or MQTT host";
    return;
  }
  user = getSetting("mqtt.user");
  password = getSetting("mqtt.password");
  port = getSetting("mqtt.port").toInt();
  strlcpy(node_name, name, sizeof(node_name));

  // Only the newest sample matters after a network outage. Network I/O stays off the mesh loop.
  samples = xQueueCreate(1, sizeof(TimedReadings));
  status = "connecting Wi-Fi";
  if (!samples || xTaskCreate([](void* arg) {
        static_cast<MqttTelemetry*>(arg)->run();
        vTaskDelete(nullptr);
      }, "sensor-mqtt", 6144, this, 1, nullptr) != pdPASS) {
    if (samples) vQueueDelete(samples);
    samples = nullptr;
    status = "could not start MQTT";
  }
}

void MqttTelemetry::update(const Weather::Readings& readings) {
  const TimedReadings sample{readings, esp_timer_get_time()};
  xQueueOverwrite(samples, &sample);
}

void MqttTelemetry::run() {
  NodeDiagnostics::begin();
  char id[24], state_topic[48], live_topic[48], availability_topic[56];
  uint8_t mac[6];
  WiFi.macAddress(mac);
  snprintf(id, sizeof(id), "m7ojc_%02x%02x%02x%02x%02x%02x",
           mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
  snprintf(state_topic, sizeof(state_topic), "meshcore/%s/state", id);
  snprintf(live_topic, sizeof(live_topic), "meshcore/%s/live", id);
  snprintf(availability_topic, sizeof(availability_topic), "meshcore/%s/availability", id);

  WiFiClient socket;
  PubSubClient client(host.c_str(), port, socket);
  bool announce = false;
  client.setCallback([&announce](char* topic, uint8_t* data, unsigned int len) {
    if (!strcmp(topic, ha_status) && len == 6 && !memcmp(data, "online", 6)) announce = true;
  });
  client.setSocketTimeout(3);
  String snapshot;
  uint32_t connections = 0;
  int64_t connected_us = 0;
  for (;;) {
    TimedReadings sample;
    bool sampled = xQueueReceive(samples, &sample, pdMS_TO_TICKS(250)) == pdTRUE;
    if (sampled && !encodeState(sample, connections ? connections - 1 : 0, snapshot)) {
      snapshot = "";
      sampled = false;
    }
    if (WiFi.status() != WL_CONNECTED) {
      status = "connecting Wi-Fi";
      socket.stop();
      continue;
    }

    if (!client.connected()) {
      status = "connecting MQTT";
      if (!client.connect(id, user.length() ? user.c_str() : nullptr, password.c_str(),
                          availability_topic, 1, true, "offline")) {
        status = client.state() == MQTT_CONNECT_BAD_CREDENTIALS || client.state() == MQTT_CONNECT_UNAUTHORIZED
                     ? "broker rejected credentials" : "broker unavailable";
        vTaskDelay(pdMS_TO_TICKS(5000));
        continue;
      }
      connections++;
      connected_us = esp_timer_get_time();
      if (!client.subscribe(ha_status, 1)) {
        socket.stop();
        continue;
      }
      announce = true;
    }
    if (!client.loop()) continue;
    const bool replay = announce;
    if (announce) {
      if (!publishDiscovery(client, id, node_name, live_topic) ||
          !client.publish(availability_topic, "online", true)) {
        socket.stop();
        vTaskDelay(pdMS_TO_TICKS(5000));
        continue;
      }
      announce = false;
      status = "online";
    }
    if ((sampled || replay) && snapshot.length()) {
      // Cached snapshots can be replayed on /state. Only a new sample can refresh HA's expiry.
      const bool fresh = sampled && sample.captured_us >= connected_us;
      if (!publishBody(client, state_topic, snapshot, true) ||
          (fresh && !publishBody(client, live_topic, snapshot, false))) socket.stop();
    }
  }
}

bool MqttTelemetry::handleCommand(const char* cmd, char* reply, size_t size) {
  if (strcmp(cmd, "get wifi.ip") == 0) {
    if (WiFi.status() == WL_CONNECTED) {
      snprintf(reply, size, "%s", WiFi.localIP().toString().c_str());
    } else {
      snprintf(reply, size, "disconnected");
    }
    return true;
  }
  if (strcmp(cmd, "mqtt status") == 0) {
    snprintf(reply, size, "MQTT: %s", status.load());
    return true;
  }
  const bool set = strncmp(cmd, "set ", 4) == 0;
  if (!set && strncmp(cmd, "get ", 4) != 0) return false;
  const char* key = cmd + 4;
  const size_t key_len = strcspn(key, " ");
  const char* value = &key[key_len];
  if (*value) value++;  // Keep spaces inside SSIDs and passwords intact.
  for (const auto& setting : settings) {
    if (strlen(setting.key) != key_len || strncmp(key, setting.key, key_len)) continue;
    const bool enabled_key = !strcmp(setting.key, "mqtt.enabled");
    const bool port_key = !strcmp(setting.key, "mqtt.port");
    if (!set) {
      String saved = getSetting(setting.key);
      if (strstr(setting.key, "password")) saved = saved.length() ? "(set)" : "(empty)";
      snprintf(reply, size, "%s", saved.length() ? saved.c_str() : "(empty)");
    } else if (strlen(value) > setting.max_length ||
               (enabled_key && strcmp(value, "on") && strcmp(value, "off")) ||
               (port_key && (!*value || value[strspn(value, "0123456789")] || !atoi(value) || atoi(value) > 65535)) ||
               (!strcmp(setting.key, "mqtt.host") && strpbrk(value, " /:@?#\t"))) {
      snprintf(reply, size, "Error: invalid %s", setting.key);
    } else {
      bool ok = *value ? prefs.putString(setting.key, value) == strlen(value)
                       : !prefs.isKey(setting.key) || prefs.remove(setting.key);
      snprintf(reply, size, "%s", ok ? "Saved. Reboot to apply." : "Error: could not save");
    }
    return true;
  }
  return false;
}
