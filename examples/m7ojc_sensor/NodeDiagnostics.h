#pragma once

#include <stdint.h>

struct NodeDiagnostics {
  double cpu_load;
  double heap_used_pct;
  uint32_t free_heap_bytes;
  int32_t wifi_rssi;

  static void begin();
  void sample();
};
