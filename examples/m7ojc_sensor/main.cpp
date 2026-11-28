#include "../simple_sensor/SensorMesh.h"
#include "MqttTelemetry.h"
#include <helpers/BaseChatMesh.h>
#include <helpers/ntp/NtpClock.h>
#include <WiFi.h>

static MqttTelemetry mqtt;

class MyMesh : public SensorMesh {
  mesh::GroupChannel weather_channel{};

public:
  using SensorMesh::SensorMesh;

  void begin() {
    constexpr uint8_t name[] = "#m7ojc";
    mesh::Utils::sha256(weather_channel.secret, CIPHER_KEY_SIZE, name, sizeof(name) - 1);
    mesh::Utils::sha256(weather_channel.hash, sizeof(weather_channel.hash), weather_channel.secret, CIPHER_KEY_SIZE);
    SensorMesh::begin(&SPIFFS);
  }

protected:
  int searchChannelsByHash(const uint8_t* hash, mesh::GroupChannel channels[], int max_matches) override {
    if (max_matches <= 0 || hash[0] != weather_channel.hash[0]) return 0;
    channels[0] = weather_channel;
    return 1;
  }

  void onGroupDataRecv(mesh::Packet* packet, uint8_t type, const mesh::GroupChannel& channel,
                       uint8_t* data, size_t len) override {
    if (type != PAYLOAD_TYPE_GRP_TXT || len <= 5 || (data[4] >> 2) != TXT_TYPE_PLAIN) return;
    data[len] = 0;
    const char* separator = strstr(reinterpret_cast<const char*>(&data[5]), ": ");
    if (!separator) return;
    const char* cmd = separator + 2;
    if (strcasecmp(cmd, "wx") && strcasecmp(cmd, "sensor") && strcasecmp(cmd, "sensors")) return;

    sampleEnvironment();
    Weather::Readings readings;
    readings.capture(*this);
    uint8_t reply[5 + MAX_TEXT_LEN + 1];
    const uint32_t timestamp = getRTCClock()->getCurrentTimeUnique();
    memcpy(reply, &timestamp, 4);
    reply[4] = TXT_TYPE_PLAIN << 2;
    const int len_text = readings.formatChat(reinterpret_cast<char*>(&reply[5]), MAX_TEXT_LEN + 1, getNodeName());
    if (len_text < 0 || len_text > MAX_TEXT_LEN) return;
    auto response = createGroupDatagram(PAYLOAD_TYPE_GRP_TXT, channel, reply, 5 + len_text);
    if (response) sendFloodReply(response, 300, packet->getPathHashSize());
  }

  bool handleCustomCommand(uint32_t, char* command, char* reply) override {
    if (!strcmp(command, "pm")) {
      PM25_AQI_Data sample;
      if (sensors.readPMSA003I(sample)) {
        snprintf(reply, 64, "PM1.0=%u PM2.5=%u PM10=%u (µg/m³)",
                 sample.pm10_env, sample.pm25_env, sample.pm100_env);
      } else {
        strcpy(reply, "PMSA003I unavailable");
      }
      return true;
    }
    if (!ntp::handleCommand(command, reply, 157, WiFi.status() == WL_CONNECTED ? nullptr : "Wi-Fi")) return false;
    if (getNodePrefs()->isDirty()) {
      savePrefs();
      getNodePrefs()->clearDirty();
    }
    return true;
  }

  void onSensorDataRead() override {
    if (mqtt.enabled()) {
      Weather::Readings readings;
      readings.capture(*this);
      mqtt.update(readings);
    }
  }
};

StdRNG fast_rng;
SimpleMeshTables tables;
static ArduinoMillis ms;
MyMesh the_mesh(board, radio_driver, ms, fast_rng, rtc_clock, tables);

void setup() {
  Serial.begin(115200);
  delay(1000);
  board.begin();
  if (!radio_init()) {
    Serial.println("Radio init failed");
    for (;;) delay(1000);
  }
  fast_rng.begin(radio_driver.getRngSeed());
  SPIFFS.begin(true);
  IdentityStore store(SPIFFS, "/identity");
  if (!store.load("_main", the_mesh.self_id)) {
    do {
      the_mesh.self_id = radio_new_identity();
    } while (the_mesh.self_id.pub_key[0] == 0 || the_mesh.self_id.pub_key[0] == 0xFF);
    store.save("_main", the_mesh.self_id);
  }
  Serial.print("Sensor ID: ");
  mesh::Utils::printHex(Serial, the_mesh.self_id.pub_key, PUB_KEY_SIZE);
  Serial.println();

  sensors.begin();
  the_mesh.begin();
  ntp::begin(RTC_TIME_MIN);
  ntp::loadPreferences(the_mesh.getNodePrefs()->getCustom());
  mqtt.begin(the_mesh.getNodeName());
#if ENABLE_ADVERT_ON_BOOT == 1
  the_mesh.sendSelfAdvertisement(16000, false);
#endif
}

static void readSerial() {
  static char command[160];
  static size_t len = 0;
  static bool overflow = false;
  while (Serial.available()) {
    char c = Serial.read();
    if (c != '\r' && c != '\n') {
      if (len < sizeof(command) - 1) command[len++] = c;
      else overflow = true;
      continue;
    }
    if (!len) continue;
    command[len] = 0;
    char reply[160] = {};
    if (overflow) {
      strcpy(reply, "Error: command too long");
    } else if (!mqtt.handleCommand(command, reply, sizeof(reply))) {
      the_mesh.handleCommand(nullptr, 0, command, reply);
    }
    if (*reply) {
      Serial.print("  -> ");
      Serial.println(reply);
    }
    memset(command, 0, len);
    len = 0;
    overflow = false;
    break;
  }
}

void loop() {
  readSerial();
  board.loop();
  the_mesh.loop();
  sensors.loop();
  ntp::loop(rtc_clock, WiFi.status() == WL_CONNECTED);
  rtc_clock.tick();
}
