#include "target.h"
#if ENV_INCLUDE_GPS
#include <helpers/sensors/MicroNMEALocationProvider.h>
#endif
#ifdef ETHERNET_ENABLED
#include <helpers/ethernet/ESP32EthernetInterface.h>
#include <helpers/ntp/NtpClock.h>
#endif
#ifdef BLE_PIN_CODE
#include <esp_bt.h>
#endif

OlimexPoEBoard board;
static SPIClass radio_spi(HSPI);
static Module radio_module(P_LORA_NSS, P_LORA_DIO_1, P_LORA_RESET, P_LORA_BUSY,
                           radio_spi, SPISettings(1000000, MSBFIRST, SPI_MODE0));
static CustomSX1262 radio(&radio_module);
CustomSX1262Wrapper radio_driver(radio, board);
ESP32RTCClock rtc_clock;
#if ENV_INCLUDE_GPS
#if __has_include(<helpers/sensors/NmeaGpsDriver.h>)
static NmeaGpsDriver gps_driver(Serial1);
static MicroNMEALocationProvider gps(gps_driver, &rtc_clock);
#else
static MicroNMEALocationProvider gps(Serial1, &rtc_clock);
#endif
EnvironmentSensorManager sensors(gps);
#else
EnvironmentSensorManager sensors;
#endif

#ifdef ETHERNET_ENABLED
static bool ntpNetworkReady() {
  return ETH.linkUp() && ETH.localIP() != IPAddress();
}

void OlimexPoEBoard::begin() {
  ESP32Board::begin();
  ntp::begin(RTC_TIME_MIN);
}

void OlimexPoEBoard::attachDynamicPrefs(KeyValueStore* prefs) {
  ntp::loadPreferences(prefs);
}

void OlimexPoEBoard::loop() {
  ntp::loop(rtc_clock, ntpNetworkReady());
}

bool OlimexPoEBoard::handleCommand(const char* command, uint32_t timestamp, char* reply) {
  if (ntp::handleCommand(command, reply, 157, ntpNetworkReady() ? nullptr : "Ethernet")) return true;
  if (strcmp(command, "eth.status") != 0) return false;

  if (!(WiFi.getStatusBits() & ETH_STARTED_BIT)) {
    strcpy(reply, "Ethernet driver stopped");
  } else if (!ETH.linkUp()) {
    strcpy(reply, "Ethernet started, link down");
  } else {
    snprintf(reply, 64, "%u Mbps, %s:%u", ETH.linkSpeed(), ETH.localIP().toString().c_str(), ETHERNET_TCP_PORT);
  }
  return true;
}
#endif

bool radio_init() {
  // Keep the PHY off until an Ethernet build starts the RMII clock.
  pinMode(ETH_PHY_POWER, OUTPUT);
  digitalWrite(ETH_PHY_POWER, LOW);
  rtc_clock.begin();
#ifdef BLE_PIN_CODE
  // One phone connection leaves RAM for the mesh and Ethernet.
  esp_bt_controller_config_t bt = BT_CONTROLLER_INIT_CONFIG_DEFAULT();
  bt.mode = ESP_BT_MODE_BLE;
  bt.ble_max_conn = 1;
  if (esp_bt_controller_mem_release(ESP_BT_MODE_CLASSIC_BT) != ESP_OK ||
      esp_bt_controller_init(&bt) != ESP_OK ||
      esp_bt_controller_enable(ESP_BT_MODE_BLE) != ESP_OK) return false;
#endif
  return radio.std_init(&radio_spi);
}

mesh::LocalIdentity radio_new_identity() {
  RadioNoiseListener rng(radio);
  return mesh::LocalIdentity(&rng);
}
