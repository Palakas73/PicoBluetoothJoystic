#pragma once
#include <stdbool.h>
#include <stdint.h>
typedef enum {
  DIAG_OFF,
  DIAG_SCANNING,
  DIAG_FOUND,
  DIAG_CONNECTING,
  DIAG_PAIRING,
  DIAG_DISCOVERING_GATT,
  DIAG_HID_READY,
  DIAG_DISCONNECTED,
  DIAG_ERROR
} diag_state_t;
void diag_state(diag_state_t, const char *);
void diag_device(const char *, const char *, uint8_t, int);
void diag_gatt_service(uint16_t, uint16_t, uint16_t);
void diag_gatt_characteristic(uint16_t, uint16_t, uint16_t);
void diag_report_map(const uint8_t *, uint16_t);
void diag_report_raw(const uint8_t *, uint16_t);
void diag_error(const char *, uint8_t);
void diag_controller_report_count(uint32_t, uint32_t);
bool diag_verbose_enabled(void);
