#pragma once
#include "core/controller_state.h"
#include <stdbool.h>
#include <stdint.h>

#define HID_GAMEPAD_MAX_FIELDS 40
typedef struct {
  uint16_t usage_page, usage;
  uint16_t bit_offset;
  uint8_t bit_size;
  uint8_t report_id;
  int32_t logical_min, logical_max;
  bool variable, signed_value;
} hid_gamepad_field_t;
typedef struct {
  hid_gamepad_field_t fields[HID_GAMEPAD_MAX_FIELDS];
  uint8_t field_count;
  uint16_t input_bits[256];
  bool has_report_ids;
  uint32_t reports;
} hid_gamepad_parser_t;

/* Parses only standard short HID items needed for a Gamepad Report Map.
 * Unsupported long items or overflow reject the map instead of guessing
 * offsets. */
bool hid_gamepad_parser_reset(hid_gamepad_parser_t *parser,
                              const uint8_t *descriptor,
                              uint16_t descriptor_len);
bool hid_gamepad_parser_decode(hid_gamepad_parser_t *parser, uint8_t report_id,
                               const uint8_t *report, uint16_t report_len,
                               controller_state_t *out);
