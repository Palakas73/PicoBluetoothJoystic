#include "gamepad_output.h"
#include <string.h>

void gamepad_output_init(gamepad_output_t *o) {
  memset(o, 0, sizeof(*o));
  controller_state_t neutral;
  controller_state_neutral(&neutral);
  gamepad_report_encode(&neutral, NULL, o->neutral);
  memcpy(o->latest, o->neutral, USB_GAMEPAD_REPORT_BYTES);
  o->pending_neutral = true;
}

bool gamepad_output_submit(gamepad_output_t *o, const controller_state_t *state,
                           const gamepad_profile_t *profile) {
  uint8_t next[USB_GAMEPAD_REPORT_BYTES];
  gamepad_report_encode(state, profile, next);
  if (o->pending_latest && !memcmp(next, o->latest, sizeof(next)))
    return false;
  if (!o->pending_latest && !o->pending_neutral && o->have_last &&
      !memcmp(next, o->last_queued, sizeof(next)))
    return false;
  if (o->pending_latest)
    o->stale_replaced++;
  memcpy(o->latest, next, sizeof(next));
  o->pending_latest = true;
  return true;
}

void gamepad_output_disconnect(gamepad_output_t *o) {
  o->pending_latest = false;
  if (o->pending_neutral ||
      (o->have_last && !memcmp(o->last_queued, o->neutral,
                               USB_GAMEPAD_REPORT_BYTES))) return;
  o->pending_neutral = true;
}

void gamepad_output_usb_reset(gamepad_output_t *o) {
  o->have_last = false;
  o->pending_neutral = true;
}

gamepad_output_kind_t gamepad_output_peek(const gamepad_output_t *o,
    uint8_t report[USB_GAMEPAD_REPORT_BYTES]) {
  if (o->pending_neutral) {
    memcpy(report, o->neutral, USB_GAMEPAD_REPORT_BYTES);
    return GAMEPAD_OUTPUT_NEUTRAL;
  }
  if (o->pending_latest) {
    memcpy(report, o->latest, USB_GAMEPAD_REPORT_BYTES);
    return GAMEPAD_OUTPUT_LATEST;
  }
  return GAMEPAD_OUTPUT_NONE;
}

void gamepad_output_mark_queued(gamepad_output_t *o,
    gamepad_output_kind_t kind, const uint8_t report[USB_GAMEPAD_REPORT_BYTES]) {
  if (kind == GAMEPAD_OUTPUT_NONE) return;
  memcpy(o->last_queued, report, USB_GAMEPAD_REPORT_BYTES);
  o->have_last = true;
  if (kind == GAMEPAD_OUTPUT_NEUTRAL)
    o->pending_neutral = false;
  else
    o->pending_latest = false;
}
