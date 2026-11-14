#include "xinput_output.h"
#include <string.h>

void xinput_output_init(xinput_output_t *o) {
  memset(o, 0, sizeof(*o));
  controller_state_t neutral;
  controller_state_neutral(&neutral);
  xinput_report_encode(&neutral, o->neutral);
  memcpy(o->latest, o->neutral, XINPUT_REPORT_BYTES);
  o->pending_neutral = true;
}
bool xinput_output_submit(xinput_output_t *o, const controller_state_t *state) {
  uint8_t next[XINPUT_REPORT_BYTES];
  xinput_report_encode(state, next);
  if (o->pending_latest && !memcmp(next, o->latest, sizeof(next))) return false;
  if (!o->pending_latest && !o->pending_neutral && o->have_last &&
      !memcmp(next, o->last_queued, sizeof(next))) return false;
  if (o->pending_latest) o->stale_replaced++;
  memcpy(o->latest, next, sizeof(next));
  o->pending_latest = true;
  return true;
}
void xinput_output_disconnect(xinput_output_t *o) {
  o->pending_latest = false;
  if (o->pending_neutral || (o->have_last &&
      !memcmp(o->last_queued, o->neutral, XINPUT_REPORT_BYTES))) return;
  o->pending_neutral = true;
}
void xinput_output_usb_reset(xinput_output_t *o) {
  o->have_last = false;
  o->pending_neutral = true;
}
xinput_output_kind_t xinput_output_peek(const xinput_output_t *o,
    uint8_t report[XINPUT_REPORT_BYTES]) {
  if (o->pending_neutral) {
    memcpy(report, o->neutral, XINPUT_REPORT_BYTES);
    return XINPUT_OUTPUT_NEUTRAL;
  }
  if (o->pending_latest) {
    memcpy(report, o->latest, XINPUT_REPORT_BYTES);
    return XINPUT_OUTPUT_LATEST;
  }
  return XINPUT_OUTPUT_NONE;
}
void xinput_output_mark_queued(xinput_output_t *o, xinput_output_kind_t kind,
    const uint8_t report[XINPUT_REPORT_BYTES]) {
  if (kind == XINPUT_OUTPUT_NONE) return;
  memcpy(o->last_queued, report, XINPUT_REPORT_BYTES);
  o->have_last = true;
  if (kind == XINPUT_OUTPUT_NEUTRAL) o->pending_neutral = false;
  else o->pending_latest = false;
}
