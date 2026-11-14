#include "diag.h"
#include <stdio.h>
#ifndef BRIDGE_DIAG_VERBOSE
#define BRIDGE_DIAG_VERBOSE 0
#endif
bool diag_verbose_enabled(void) { return BRIDGE_DIAG_VERBOSE != 0; }
static const char *names[] = {"OFF",        "SCANNING",     "FOUND",
                              "CONNECTING", "PAIRING",      "DISCOVERING_GATT",
                              "HID_READY",  "DISCONNECTED", "ERROR"};
void diag_state(diag_state_t s, const char *d) {
  printf("[STATE] %s %s\r\n",
         (unsigned)s < sizeof(names) / sizeof(names[0]) ? names[s] : "?",
         d ? d : "");
}
void diag_device(const char *n, const char *a, uint8_t t, int r) {
  printf("[DEVICE] name=%s addr=%s type=%u rssi=%d\r\n", n ? n : "?",
         a ? a : "?", t, r);
}
void diag_gatt_service(uint16_t u, uint16_t s, uint16_t e) {
  printf("[GATT] service uuid=0x%04x handles=%u..%u\r\n", u, s, e);
}
void diag_gatt_characteristic(uint16_t u, uint16_t h, uint16_t p) {
  printf("[GATT] characteristic uuid=0x%04x value=%u props=0x%04x\r\n", u, h,
         p);
}
void diag_report_map(const uint8_t *d, uint16_t n) {
  printf("[HID] report-map bytes=%u\r\n", n);
  if (!diag_verbose_enabled() || !d) return;
  for (uint16_t i = 0; i < n; i++) {
    printf("%02x%s", d[i], (i % 16 == 15 || i + 1 == n) ? "\r\n" : " ");
  }
}
void diag_report_raw(const uint8_t *d, uint16_t n) {
  printf("[REPORT] len=%u:", n);
  for (uint16_t i = 0; i < n; i++)
    printf(" %02x", d[i]);
  printf("\r\n");
}
void diag_error(const char *w, uint8_t s) {
  printf("[ERROR] %s status=0x%02x\r\n", w, s);
}
void diag_controller_report_count(uint32_t n, uint32_t ms) {
  printf("[RATE] reports=%lu elapsed_ms=%lu\r\n", (unsigned long)n,
         (unsigned long)ms);
}
