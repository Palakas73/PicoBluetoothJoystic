#pragma once
#include "core/controller_state.h"
#include <stddef.h>
#include <stdint.h>

enum { XINPUT_REPORT_BYTES = 20 };
void xinput_report_encode(const controller_state_t *state,
                          uint8_t out[XINPUT_REPORT_BYTES]);

typedef struct {
  uint8_t left_motor, right_motor, led_pattern;
  uint32_t rumble_count, led_count, malformed_count;
} xinput_feedback_t;
/* Returns true only for a recognized output report. Feedback is diagnostic-only. */
int xinput_feedback_parse(xinput_feedback_t *feedback, const uint8_t *data,
                          size_t length);
