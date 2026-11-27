#pragma once

#include <ETH.h>
#include <helpers/wifi/SerialWifiInterface.h>

#ifndef ETHERNET_TCP_PORT
  #define ETHERNET_TCP_PORT 5000
#endif

// ESP32 Ethernet and Wi-Fi share the same TCP server and client implementation.
class ESP32EthernetInterface : public SerialWifiInterface {
public:
  void begin() {
    if (ETH.begin()) {
      SerialWifiInterface::begin(ETHERNET_TCP_PORT);
    }
  }
};
