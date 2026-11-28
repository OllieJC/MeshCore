#include "NodeDiagnostics.h"
#include <Arduino.h>
#include <WiFi.h>
#include <esp_freertos_hooks.h>
#include <esp_heap_caps.h>

namespace {
TaskHandle_t idle_tasks[portNUM_PROCESSORS];
portMUX_TYPE cpu_lock = portMUX_INITIALIZER_UNLOCKED;
uint32_t busy_samples = 0, core_samples = 0;

// Sample the tasks interrupted by the tick. A polling task would count itself as busy.
void IRAM_ATTR sampleCpu() {
  static unsigned ticks = 0;
  if (++ticks < pdMS_TO_TICKS(10)) return;
  ticks = 0;
  unsigned busy = 0;
  for (unsigned core = 0; core < portNUM_PROCESSORS; core++) {
    if (xTaskGetCurrentTaskHandleForCPU(core) != idle_tasks[core]) busy++;
  }
  portENTER_CRITICAL_ISR(&cpu_lock);
  busy_samples += busy;
  core_samples += portNUM_PROCESSORS;
  portEXIT_CRITICAL_ISR(&cpu_lock);
}
}

void NodeDiagnostics::begin() {
  for (unsigned core = 0; core < portNUM_PROCESSORS; core++) {
    idle_tasks[core] = xTaskGetIdleTaskHandleForCPU(core);
  }
  esp_register_freertos_tick_hook_for_cpu(sampleCpu, 0);
}

void NodeDiagnostics::sample() {
  portENTER_CRITICAL(&cpu_lock);
  const uint32_t busy = busy_samples, total = core_samples;
  busy_samples = core_samples = 0;
  portEXIT_CRITICAL(&cpu_lock);
  cpu_load = total ? round(1000.0 * busy / total) / 10.0 : NAN;

  multi_heap_info_t heap;
  heap_caps_get_info(&heap, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
  free_heap_bytes = heap.total_free_bytes;
  const size_t heap_bytes = heap.total_allocated_bytes + heap.total_free_bytes;
  heap_used_pct = heap_bytes ? round(1000.0 * heap.total_allocated_bytes / heap_bytes) / 10.0 : NAN;
  wifi_rssi = WiFi.status() == WL_CONNECTED ? WiFi.RSSI() : 0;
}
