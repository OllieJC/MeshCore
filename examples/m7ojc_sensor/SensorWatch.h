#pragma once

#include <Mesh.h>
#include <helpers/KeyValueStore.h>

class SensorWatch {
public:
  void begin(KeyValueStore& settings);
  bool handleCommand(const char* command, char* reply);
  int searchChannels(const uint8_t* hash, mesh::GroupChannel out[], int limit) const;
  bool matches(uint8_t type, const mesh::GroupChannel& channel, const uint8_t* data, size_t len) const;

private:
  static constexpr int MaxRules = 8;
  struct Rule {
    char spec[128] = {};  // Canonical CLI argument, also the persisted value.
    uint8_t patterns_at = 0;
    mesh::GroupChannel channel{};

    const char* parse(char* args);
  };

  Rule rules[MaxRules];
  KeyValueStore* settings = nullptr;
  bool enabled = false;

  int collectChannels(const uint8_t* hash, mesh::GroupChannel out[], int limit) const;
  void add(char* args, char* reply);
  void list(int start, char* reply) const;
  void save(int slot, const Rule& rule, char* reply);
};
