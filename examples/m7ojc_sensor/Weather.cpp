#include "Weather.h"
#include "../simple_sensor/SensorMesh.h"

namespace Weather {

const Field fields[Count] = {
  {"temperature", "Temperature", "°C", "temperature", "BME680", 0, LPP_TEMPERATURE, 1, 1},
  {"humidity", "Humidity", "%", "humidity", "BME680", 0, LPP_RELATIVE_HUMIDITY, 1, 1},
  {"pressure", "Pressure", "hPa", "atmospheric_pressure", "BME680", 0, LPP_BAROMETRIC_PRESSURE, 1, 1},
  {"co2", "Carbon dioxide", "ppm", "carbon_dioxide", "SCD41", 0, LPP_CONCENTRATION, 0, 1},
  {"no2", "NO₂ resistance", "kΩ", nullptr, "MiCS-6814", 0, LPP_GENERIC_SENSOR, 1, 0.001f},
  {"co", "CO resistance", "kΩ", nullptr, "MiCS-6814", 1, LPP_GENERIC_SENSOR, 1, 0.001f},
  {"nh3", "NH₃ resistance", "kΩ", nullptr, "MiCS-6814", 2, LPP_GENERIC_SENSOR, 1, 0.001f},
  {"bme_gas", "BME680 gas resistance", "kΩ", nullptr, "BME680", 0, LPP_GENERIC_SENSOR, 1, 0.001f},
  {"altitude", "Pressure altitude", "m", "distance", "BME680", 0, LPP_ALTITUDE, 0, 1},
  {"pm1", "PM1", "µg/m³", "pm1"},
  {"pm25", "PM2.5", "µg/m³", "pm25"},
  {"pm10", "PM10", "µg/m³", "pm10"},
};

void Readings::capture(SensorMesh& mesh) {
  for (size_t i = 0; i < PM1; i++) {
    const Field& field = fields[i];
    const uint8_t ch = sensors.getSensorChannel(field.sensor, field.sub_channel);
    values[i] = ch ? mesh.getTelemValue(ch, field.lpp_type, NAN) * field.scale : NAN;
  }
  uint16_t pm[3];
  const bool have_pm = mesh.getParticulateMatter(pm[0], pm[1], pm[2]);
  for (size_t i = 0; i < 3; i++) values[PM1 + i] = have_pm ? pm[i] : NAN;
}

int Readings::formatChat(char* dest, size_t size, const char* node_name) const {
  char text[Count][12];
  for (size_t i = Temperature; i < Count; i++) {
    if (isfinite(values[i])) {
      snprintf(text[i], sizeof(text[i]), "%.*f", fields[i].decimals, values[i]);
    } else {
      strcpy(text[i], "n/a");
    }
  }
  int n = snprintf(dest, size,
      "%s: %s °C | %s%% RH | %s hPa\nCO₂ %s ppm\nGas kΩ: NO₂ %s | CO %s | NH₃ %s"
      "\nPM1/2.5/10: %s/%s/%s µg/m³",
      node_name, text[Temperature], text[Humidity], text[Pressure], text[CO2],
      text[NO2], text[CO], text[NH3], text[PM1], text[PM25], text[PM10]);
  if (n < 0 || static_cast<size_t>(n) < size) return n;
  // Preserve all four lines in one mesh packet even with a full-length name and large readings.
  return snprintf(dest, size, "%s: %s°C %s%% %shPa\nCO₂ %sppm\nNO₂/CO/NH₃ %s/%s/%skΩ"
                  "\nPM1/2.5/10 %s/%s/%sµg/m³", node_name, text[Temperature], text[Humidity],
                  text[Pressure], text[CO2], text[NO2], text[CO], text[NH3], text[PM1], text[PM25], text[PM10]);
}

}
