#pragma once

#include <stddef.h>
#include <stdint.h>

class SensorMesh;

namespace Weather {

enum Metric { Temperature, Humidity, Pressure, CO2, NO2, CO, NH3, BmeGas, Altitude, PM1, PM25, PM10, Count };

struct Field {
  const char* key;
  const char* name;
  const char* unit;
  const char* device_class;
  const char* sensor;
  uint8_t sub_channel, lpp_type, decimals;
  float scale;
};
extern const Field fields[Count];

struct Readings {
  float values[Count];
  void capture(SensorMesh& mesh);
  int formatChat(char* dest, size_t size, const char* node_name) const;
};

}
