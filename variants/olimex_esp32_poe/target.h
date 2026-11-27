#pragma once

#include <helpers/ESP32Board.h>
#include <helpers/radiolib/CustomSX1262Wrapper.h>
#include <helpers/sensors/EnvironmentSensorManager.h>

class OlimexPoEBoard : public ESP32Board {
public:
  const char* getManufacturerName() const override { return "Olimex ESP32-PoE (Wio-SX1262)"; }
#ifdef ETHERNET_ENABLED
  void begin();
  void attachDynamicPrefs(KeyValueStore* prefs);
  void loop() override;
  bool handleCommand(const char* command, uint32_t timestamp, char* reply) override;
#endif
};

extern OlimexPoEBoard board;
extern CustomSX1262Wrapper radio_driver;
extern ESP32RTCClock rtc_clock;
extern EnvironmentSensorManager sensors;

bool radio_init();
mesh::LocalIdentity radio_new_identity();
