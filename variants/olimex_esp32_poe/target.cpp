#include "target.h"
#ifdef ETHERNET_ENABLED
#include <helpers/ethernet/ESP32EthernetInterface.h>
#endif
#ifdef BLE_PIN_CODE
#include <esp_bt.h>
#endif

OlimexPoEBoard board;
ESPNOWRadio radio_driver;
ESP32RTCClock rtc_clock;
EnvironmentSensorManager sensors;

#ifdef ETHERNET_ENABLED
bool OlimexPoEBoard::handleCommand(const char* command, uint32_t timestamp, char* reply) {
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
  radio_driver.init();
  return true;
}

mesh::LocalIdentity radio_new_identity() {
  class HardwareRNG : public mesh::RNG {
    void random(uint8_t* dest, size_t size) override { esp_fill_random(dest, size); }
  } rng;
  return mesh::LocalIdentity(&rng);
}
