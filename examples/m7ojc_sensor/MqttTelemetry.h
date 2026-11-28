#pragma once

#include "Weather.h"
#include <Preferences.h>
#include <atomic>

class MqttTelemetry {
public:
  void begin(const char* node_name);
  bool enabled() const { return samples != nullptr; }
  void update(const Weather::Readings& readings);
  bool handleCommand(const char* command, char* reply, size_t size);

private:
  void run();
  String getSetting(const char* key);
  Preferences prefs;
  QueueHandle_t samples = nullptr;
  char node_name[32] = {};
  String host, user, password;
  uint16_t port = 1883;
  std::atomic<const char*> status{"off"};
};
