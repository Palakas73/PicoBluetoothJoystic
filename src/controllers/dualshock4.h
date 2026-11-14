#pragma once
#include "core/controller_state.h"
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef struct {
  uint32_t received, accepted, rejected_length, rejected_id, rejected_crc;
} ds4_parser_stats_t;

typedef enum {
  DS4_REJECT_NONE = 0,
  DS4_REJECT_NULL,
  DS4_REJECT_LENGTH,
  DS4_REJECT_ID,
  DS4_REJECT_CRC
} ds4_reject_reason_t;

typedef struct {
  uint8_t report_id;
  uint8_t axis_bytes[4];
  uint8_t axes_valid;
  uint8_t crc_checked;
  uint8_t crc_valid;
  uint32_t crc_calculated;
  uint32_t crc_reported;
  ds4_reject_reason_t reject_reason;
} ds4_report_debug_t;

/* The buffer starts at the HID report ID (BTstack's HIDP A1 header removed). */
bool ds4_parse_bluetooth_report(const uint8_t *data, size_t length,
                                controller_state_t *state,
                                ds4_parser_stats_t *stats);
bool ds4_parse_bluetooth_report_debug(const uint8_t *data, size_t length,
                                      controller_state_t *state,
                                      ds4_parser_stats_t *stats,
                                      ds4_report_debug_t *debug);
uint32_t ds4_input_crc32(const uint8_t *data, size_t length);
bool ds4_report_descriptor_signature(const uint8_t *data, size_t length);
