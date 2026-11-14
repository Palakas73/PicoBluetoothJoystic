#pragma once
#include "gamepad_report.h"
#include <stdbool.h>
#include <stdint.h>

typedef enum { GAMEPAD_OUTPUT_NONE, GAMEPAD_OUTPUT_NEUTRAL,
               GAMEPAD_OUTPUT_LATEST } gamepad_output_kind_t;
typedef struct {
  uint8_t latest[USB_GAMEPAD_REPORT_BYTES];
  uint8_t neutral[USB_GAMEPAD_REPORT_BYTES];
  uint8_t last_queued[USB_GAMEPAD_REPORT_BYTES];
  bool pending_latest, pending_neutral, have_last;
  uint32_t stale_replaced;
} gamepad_output_t;

void gamepad_output_init(gamepad_output_t *output);
bool gamepad_output_submit(gamepad_output_t *output,
                           const controller_state_t *state,
                           const gamepad_profile_t *profile);
void gamepad_output_disconnect(gamepad_output_t *output);
void gamepad_output_usb_reset(gamepad_output_t *output);
gamepad_output_kind_t gamepad_output_peek(const gamepad_output_t *output,
    uint8_t report[USB_GAMEPAD_REPORT_BYTES]);
void gamepad_output_mark_queued(gamepad_output_t *output,
    gamepad_output_kind_t kind, const uint8_t report[USB_GAMEPAD_REPORT_BYTES]);
