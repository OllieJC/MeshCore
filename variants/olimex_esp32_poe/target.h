#pragma once

#include <helpers/ESP32Board.h>
#include <helpers/esp32/ESPNOWRadio.h>
#include <helpers/sensors/EnvironmentSensorManager.h>

class OlimexPoEBoard : public ESP32Board {
public:
  const char* getManufacturerName() const override { return "Olimex ESP32-PoE (ESP-NOW)"; }
  bool handleCommand(const char* command, uint32_t timestamp, char* reply) override;
};

extern OlimexPoEBoard board;
extern ESPNOWRadio radio_driver;
extern ESP32RTCClock rtc_clock;
extern EnvironmentSensorManager sensors;

bool radio_init();
mesh::LocalIdentity radio_new_identity();
