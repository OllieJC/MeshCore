#pragma once

#include <ETH.h>
#include <WiFi.h>
#include "EthernetCLIInput.h"

// Serve one text console on the Ethernet DHCP address.
class ESP32EthernetCLI {
  const uint16_t port;
  IPAddress address;
  WiFiServer server;
  WiFiClient client;
  EthernetCLIInput input;
  uint32_t nextListen = 0;

public:
  explicit ESP32EthernetCLI(uint16_t port) : port(port), server(port) {}

  void begin() {
    if (!ETH.begin()) Serial.println("Ethernet driver failed to start");
  }

  bool readCommand(char* command, size_t capacity, const char* password) {
    IPAddress ip = ETH.linkUp() ? ETH.localIP() : IPAddress();
    if (ip != address) {
      client.stop();
      server.end();
      address = ip;
      nextListen = millis();
      if (ip != IPAddress()) server = WiFiServer(ip, port, 1);
    }
    if (ip == IPAddress()) return false;
    if (!server) {
      if ((int32_t)(millis() - nextListen) < 0) return false;
      server.begin();
      if (!server) {
        server.end();
        nextListen = millis() + 1000;
        return false;
      }
    }

    WiFiClient incoming = server.available();
    if (incoming) {
      if (!EthernetCLIInput::passwordConfigured(password)) {
        incoming.println("Set an admin password over USB first.");
        incoming.stop();
      } else if (client.connected()) {
        incoming.stop();
      } else {
        client = incoming;
        input = EthernetCLIInput();
        client.println("MeshCore Repeater CLI");
        client.println("Password:");
      }
    }

    // Bound work per loop so a busy console cannot starve the mesh.
    for (size_t n = 0; n < capacity && client.available(); ++n) {
      int c = client.read();
      if (c < 0) break;
      switch (input.consume((char)c, command, capacity, password)) {
        case EthernetCLIInput::Command: return true;
        case EthernetCLIInput::LoggedIn: client.println("OK"); break;
        case EthernetCLIInput::InvalidLine: client.println("Err - invalid line"); break;
        case EthernetCLIInput::Denied:
          client.println("Access denied");
          client.stop();
          return false;
        case EthernetCLIInput::Pending: break;
      }
    }
    return false;
  }

  void sendReply(const char* reply) {
    if (*reply) {
      client.print("  -> ");
      client.println(reply);
    }
  }
};
