#pragma once
#include "core/controller_state.h"
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef struct {
  uint32_t reports_received, reports_processed, reports_rejected;
} switch_parser_stats_t;

/* Parses Switch-compatible 0x30 and observed Ultimate 2C simple 0x3f.
 * Input includes the report ID byte. No Bluetooth or BTstack types leak here. */
bool switch_8bitdo_parse_report(const uint8_t *data, size_t length,
                                controller_state_t *state,
                                switch_parser_stats_t *stats);
bool switch_parse_simple_3f(const uint8_t *data, size_t length,
                            controller_state_t *state);
bool switch_pro_parse_full_30(const uint8_t *data, size_t length,
                              controller_state_t *state);
