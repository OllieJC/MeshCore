#include "SensorMesh.h"

#ifdef SENSOR_WEATHER_CHANNEL
  #include <helpers/BaseChatMesh.h>
  #include <helpers/sensors/LPPDataHelpers.h>
#endif

#ifdef DISPLAY_CLASS
  #include "UITask.h"
  static UITask ui_task(display);
#endif

class MyMesh : public SensorMesh {
public:
  MyMesh(mesh::MainBoard& board, mesh::Radio& radio, mesh::MillisecondClock& ms, mesh::RNG& rng, mesh::RTCClock& rtc, mesh::MeshTables& tables)
     : SensorMesh(board, radio, ms, rng, rtc, tables), 
       battery_data(12*24, 5*60)    // 24 hours worth of battery data, every 5 minutes
  {
#ifdef SENSOR_WEATHER_CHANNEL
    constexpr uint8_t channel_name[] = SENSOR_WEATHER_CHANNEL;
    mesh::Utils::sha256(weather_channel.secret, CIPHER_KEY_SIZE, channel_name, sizeof(channel_name) - 1);
    mesh::Utils::sha256(weather_channel.hash, sizeof(weather_channel.hash), weather_channel.secret, CIPHER_KEY_SIZE);
#endif
  }

protected:
  /* ========================== custom logic here ========================== */
#ifdef SENSOR_WEATHER_CHANNEL
  mesh::GroupChannel weather_channel{};

  int searchChannelsByHash(const uint8_t* hash, mesh::GroupChannel channels[], int max_matches) override {
    if (max_matches <= 0 || hash[0] != weather_channel.hash[0]) return 0;
    channels[0] = weather_channel;
    return 1;
  }

  void onGroupDataRecv(mesh::Packet* packet, uint8_t type, const mesh::GroupChannel& channel, uint8_t* data, size_t len) override {
    if (type != PAYLOAD_TYPE_GRP_TXT || len <= 5 || (data[4] >> 2) != TXT_TYPE_PLAIN) return;

    data[len] = 0;  // Decrypted group text is zero-padded to a cipher block.
    const char* separator = strstr(reinterpret_cast<const char*>(&data[5]), ": ");
    if (!separator) return;
    const char* command = separator + 2;
    if (strcasecmp(command, "wx") != 0 && strcasecmp(command, "sensor") != 0 && strcasecmp(command, "sensors") != 0) return;

    sampleEnvironment();
    const uint8_t bme = sensors.getSensorChannel("BME680");
    const uint8_t scd = sensors.getSensorChannel("SCD41");
    const struct {
      uint8_t channel;
      uint8_t type;
      int decimals;
      float scale;
    } readings[] = {
      {bme, LPP_TEMPERATURE, 1, 1.0f},
      {bme, LPP_RELATIVE_HUMIDITY, 1, 1.0f},
      {bme, LPP_BAROMETRIC_PRESSURE, 1, 1.0f},
      {scd, LPP_CONCENTRATION, 0, 1.0f},
      {sensors.getSensorChannel("MiCS-6814", 0), LPP_GENERIC_SENSOR, 1, 0.001f},
      {sensors.getSensorChannel("MiCS-6814", 1), LPP_GENERIC_SENSOR, 1, 0.001f},
      {sensors.getSensorChannel("MiCS-6814", 2), LPP_GENERIC_SENSOR, 1, 0.001f},
    };
    char values[7][12];
    for (size_t i = 0; i < sizeof(readings) / sizeof(readings[0]); i++) {
      const auto& reading = readings[i];
      const float value = getTelemValue(reading.channel, reading.type, NAN);
      if (isnan(value)) {
        strcpy(values[i], "n/a");
      } else {
        snprintf(values[i], sizeof(values[i]), "%.*f", reading.decimals, value * reading.scale);
      }
    }

    uint8_t reply[5 + MAX_TEXT_LEN + 1];
    const uint32_t timestamp = getRTCClock()->getCurrentTimeUnique();
    memcpy(reply, &timestamp, 4);
    reply[4] = TXT_TYPE_PLAIN << 2;
    const int text_len = snprintf(reinterpret_cast<char*>(&reply[5]), MAX_TEXT_LEN + 1,
        "%s: %s °C | %s%% RH | %s hPa\nCO₂ %s ppm\nMiCS kΩ: Ox %s | Red %s | NH₃ %s",
        getNodeName(), values[0], values[1], values[2], values[3], values[4], values[5], values[6]);
    if (text_len < 0 || text_len > MAX_TEXT_LEN) return;  // Keep the message in one packet.

    auto response = createGroupDatagram(PAYLOAD_TYPE_GRP_TXT, channel, reply, 5 + text_len);
    if (response) sendFloodReply(response, 300, packet->getPathHashSize());
  }
#endif

#ifndef SENSOR_DISABLE_BATTERY_ALERTS
  Trigger low_batt, critical_batt;
#endif
  TimeSeriesData  battery_data;

  void onSensorDataRead() override {
    float batt_voltage = getVoltage(TELEM_CHANNEL_SELF);

    battery_data.recordData(getRTCClock(), batt_voltage);   // record battery
#ifndef SENSOR_DISABLE_BATTERY_ALERTS
    alertIf(batt_voltage < 3.4f, critical_batt, HIGH_PRI_ALERT, "Battery is critical!");
    alertIf(batt_voltage < 3.6f, low_batt, LOW_PRI_ALERT, "Battery is low");
#endif
  }

  int querySeriesData(uint32_t start_secs_ago, uint32_t end_secs_ago, MinMaxAvg dest[], int max_num) override {
    battery_data.calcMinMaxAvg(getRTCClock(), start_secs_ago, end_secs_ago, &dest[0], TELEM_CHANNEL_SELF, LPP_VOLTAGE);
    return 1;
  }

  bool handleCustomCommand(uint32_t sender_timestamp, char* command, char* reply) override {
    if (strcmp(command, "magic") == 0) {    // example 'custom' command handling
      strcpy(reply, "**Magic now done**");
      return true;   // handled
    }
    return false;  // not handled
  }
  /* ======================================================================= */
};

StdRNG fast_rng;
SimpleMeshTables tables;

MyMesh the_mesh(board, radio_driver, *new ArduinoMillis(), fast_rng, rtc_clock, tables);

void halt() {
  while (1) ;
}

static char command[160];

void setup() {
  Serial.begin(115200);
  delay(1000);

  board.begin();

#ifdef HAS_EXTERNAL_WATCHDOG
  external_watchdog.begin();
#endif

#ifdef DISPLAY_CLASS
  if (display.begin()) {
    display.startFrame();
    display.print("Please wait...");
    display.endFrame();
  }
#endif

  if (!radio_init()) { halt(); }

  fast_rng.begin(radio_driver.getRngSeed());

  FILESYSTEM* fs;
#if defined(NRF52_PLATFORM) || defined(STM32_PLATFORM)
  InternalFS.begin();
  fs = &InternalFS;
  IdentityStore store(InternalFS, "");
#elif defined(ESP32)
  SPIFFS.begin(true);
  fs = &SPIFFS;
  IdentityStore store(SPIFFS, "/identity");
#elif defined(RP2040_PLATFORM)
  LittleFS.begin();
  fs = &LittleFS;
  IdentityStore store(LittleFS, "/identity");
  store.begin();
#else
  #error "need to define filesystem"
#endif
  if (!store.load("_main", the_mesh.self_id)) {
    MESH_DEBUG_PRINTLN("Generating new keypair");
    the_mesh.self_id = radio_new_identity();   // create new random identity
    int count = 0;
    while (count < 10 && (the_mesh.self_id.pub_key[0] == 0x00 || the_mesh.self_id.pub_key[0] == 0xFF)) {  // reserved id hashes
      the_mesh.self_id = radio_new_identity(); count++;
    }
    store.save("_main", the_mesh.self_id);
  }

  Serial.print("Sensor ID: ");
  mesh::Utils::printHex(Serial, the_mesh.self_id.pub_key, PUB_KEY_SIZE); Serial.println();

  command[0] = 0;

  sensors.begin();

  the_mesh.begin(fs);

#ifdef DISPLAY_CLASS
  ui_task.begin(the_mesh.getNodePrefs(), FIRMWARE_BUILD_DATE, FIRMWARE_VERSION);
#endif

  // send out initial zero hop Advertisement to the mesh
#if ENABLE_ADVERT_ON_BOOT == 1
  the_mesh.sendSelfAdvertisement(16000, false);
#endif
}

void loop() {
  int len = strlen(command);
  // `command` must stay NUL-terminated within its bounds. If it ever isn't,
  // strlen() above can return >= sizeof(command) and the loop below would then
  // index past the buffer, so clamp defensively.
  if (len >= (int)sizeof(command)) {
    command[0] = 0;
    len = 0;
  }
  while (Serial.available() && len < sizeof(command)-1) {
    char c = Serial.read();
    if (c != '\n') {
      command[len++] = c;
      command[len] = 0;
    }
    Serial.print(c);
  }
  if (len == sizeof(command)-1) {  // buffer full: treat as a completed line
    command[sizeof(command)-2] = '\r';  // place end-of-line marker inside the buffer
    command[sizeof(command)-1] = 0;     // keep the buffer NUL-terminated
  }

  if (len > 0 && command[len - 1] == '\r') {  // received complete line
    command[len - 1] = 0;  // replace newline with C string null terminator
    char reply[160];
    the_mesh.handleCommand(NULL, 0, command, reply);  // NOTE: there is no sender_timestamp via serial!
    if (reply[0]) {
      Serial.print("  -> "); Serial.println(reply);
    }

    command[0] = 0;  // reset command buffer
  }

  board.loop();   // let the board feed its watchdog, run periodic housekeeping

  the_mesh.loop();
  sensors.loop();
#ifdef DISPLAY_CLASS
  ui_task.loop();
#endif
  rtc_clock.tick();
#ifdef HAS_EXTERNAL_WATCHDOG
  external_watchdog.loop();
#endif
}
