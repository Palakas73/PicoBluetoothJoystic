#pragma once
#include "usb/xinput_report.h"
#include <stdbool.h>
#include <stdint.h>

typedef enum { XINPUT_OUTPUT_NONE, XINPUT_OUTPUT_NEUTRAL,
               XINPUT_OUTPUT_LATEST } xinput_output_kind_t;
typedef struct {
  uint8_t latest[XINPUT_REPORT_BYTES], neutral[XINPUT_REPORT_BYTES];
  uint8_t last_queued[XINPUT_REPORT_BYTES];
  bool pending_latest, pending_neutral, have_last;
  uint32_t stale_replaced;
} xinput_output_t;
void xinput_output_init(xinput_output_t *o);
bool xinput_output_submit(xinput_output_t *o, const controller_state_t *state);
void xinput_output_disconnect(xinput_output_t *o);
void xinput_output_usb_reset(xinput_output_t *o);
xinput_output_kind_t xinput_output_peek(const xinput_output_t *o,
    uint8_t report[XINPUT_REPORT_BYTES]);
void xinput_output_mark_queued(xinput_output_t *o, xinput_output_kind_t kind,
    const uint8_t report[XINPUT_REPORT_BYTES]);
