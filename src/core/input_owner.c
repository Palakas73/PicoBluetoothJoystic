#include "input_owner.h"
#if BRIDGE_USB_XINPUT
#include "usb/usb_xinput.h"
#if BRIDGE_XINPUT_SLOTS == 2
#include "autofire.h"
#include "led/status_led.h"
#ifndef BRIDGE_HOST_TEST
#include "btstack_run_loop.h"
#include "pico/time.h"
#endif
#include <stdio.h>
#include <string.h>
enum { UNASSIGNED = 0xff };
/* Player slots are session state. Bluetooth identities and keys are stored by
 * the respective hosts; the former slot TLV record is deliberately ignored. */
static uint8_t source_slots[3] = {UNASSIGNED, UNASSIGNED, UNASSIGNED};
static uint32_t next_generation[3], active_generation[3];
static autofire_t autofire[3];
static controller_state_t last_sent[3];
static bool have_last_sent[3];
#ifndef BRIDGE_HOST_TEST
static btstack_timer_source_t processing_timer;
#endif
static void feedback(autofire_event_t event) {
#ifndef BRIDGE_HOST_TEST
  status_led_autofire_feedback(event);
#else
  (void)event;
#endif
}
void input_session_reset(void) {
  source_slots[INPUT_XBOX] = UNASSIGNED;
  source_slots[INPUT_8BITDO] = UNASSIGNED;
  active_generation[INPUT_XBOX] = 0;
  active_generation[INPUT_8BITDO] = 0;
  for (unsigned source = INPUT_XBOX; source <= INPUT_8BITDO; source++) {
    autofire_init(&autofire[source]);
    have_last_sent[source] = false;
  }
}
static void submit_transformed(enum input_source source,
                               const controller_state_t *state,
                               uint64_t timestamp) {
  uint8_t slot = source_slots[source];
  if (slot >= 2) return;
  if (have_last_sent[source] &&
      memcmp(&last_sent[source], state, sizeof(*state)) == 0) return;
  last_sent[source] = *state;
  have_last_sent[source] = true;
  usb_xinput_submit_slot(slot, state, timestamp);
}
#ifndef BRIDGE_HOST_TEST
static void processing_tick(btstack_timer_source_t *ts) {
  (void)ts;
  uint64_t now = time_us_64();
  for (unsigned source = INPUT_XBOX; source <= INPUT_8BITDO; source++) {
    if (!active_generation[source] || source_slots[source] >= 2) continue;
    controller_state_t transformed;
    autofire_event_t event = autofire_tick(&autofire[source], now, &transformed);
    feedback(event);
    submit_transformed((enum input_source)source, &transformed, now);
  }
  btstack_run_loop_set_timer(&processing_timer, 1);
  btstack_run_loop_add_timer(&processing_timer);
}
void input_processing_start(void) {
  btstack_run_loop_set_timer_handler(&processing_timer, processing_tick);
  btstack_run_loop_set_timer(&processing_timer, 1);
  btstack_run_loop_add_timer(&processing_timer);
}
#else
void input_processing_start(void) {}
#endif
uint32_t input_connection_begin(enum input_source source) {
  if (source != INPUT_XBOX && source != INPUT_8BITDO) return 0;
  if (autofire_configuring(&autofire[source])) feedback(AF_EVENT_CANCELLED);
  if (source_slots[source] < 2) usb_xinput_disconnect_slot(source_slots[source]);
  source_slots[source] = UNASSIGNED;
  uint32_t generation = ++next_generation[source];
  if (!generation) generation = ++next_generation[source];
  active_generation[source] = generation;
  autofire_connect(&autofire[source]);
  have_last_sent[source] = false;
  return generation;
}
static uint8_t slot_for(enum input_source source) {
  if (source != INPUT_XBOX && source != INPUT_8BITDO) return UNASSIGNED;
  if (source_slots[source] < 2) return source_slots[source];
  uint8_t other = source == INPUT_XBOX ? INPUT_8BITDO : INPUT_XBOX;
  uint8_t slot = source_slots[other] == 0 ? 1 : 0;
  source_slots[source] = slot;
  printf("[SLOTS] INPUT_READY source=%u logical_player=%u\r\n", source, slot + 1);
  return slot;
}
void input_report(enum input_source source, const controller_state_t *state, uint64_t timestamp) {
  if (!state) return;
  input_report_for(source, active_generation[source], state, timestamp);
}
int input_report_for(enum input_source source, uint32_t generation,
                     const controller_state_t *state, uint64_t timestamp) {
  if (source != INPUT_XBOX && source != INPUT_8BITDO) return -1;
  if (!generation || generation != active_generation[source] || !state) return -1;
  uint8_t slot = slot_for(source);
  controller_state_t transformed;
  autofire_event_t event = autofire_physical(&autofire[source], state,
                                              timestamp, &transformed);
  feedback(event);
  submit_transformed(source, &transformed, timestamp);
  return slot;
}
void input_release_for(enum input_source source, uint32_t generation) {
  if (source != INPUT_XBOX && source != INPUT_8BITDO) return;
  if (!generation || generation != active_generation[source]) return;
  if (autofire_configuring(&autofire[source])) feedback(AF_EVENT_CANCELLED);
  if (source_slots[source] < 2) usb_xinput_disconnect_slot(source_slots[source]);
  autofire_disconnect(&autofire[source]);
  have_last_sent[source] = false;
  source_slots[source] = UNASSIGNED;
  active_generation[source] = 0;
}
void input_quiesce_for(enum input_source source, uint32_t generation) {
  if (source != INPUT_XBOX && source != INPUT_8BITDO) return;
  if (!generation || generation != active_generation[source]) return;
  if (autofire_configuring(&autofire[source])) feedback(AF_EVENT_CANCELLED);
  if (source_slots[source] < 2) usb_xinput_disconnect_slot(source_slots[source]);
  autofire_disconnect(&autofire[source]);
  have_last_sent[source] = false;
}
void input_release(enum input_source source) {
  if (source != INPUT_XBOX && source != INPUT_8BITDO) return;
  input_release_for(source, active_generation[source]);
}
#else
#define output_submit usb_xinput_submit
#define output_disconnect usb_xinput_disconnect
#endif
#else
#include "usb/usb_gamepad.h"
#define output_submit usb_gamepad_submit
#define output_disconnect usb_gamepad_disconnect
#endif
#if !(BRIDGE_USB_XINPUT && BRIDGE_XINPUT_SLOTS == 2)
static enum input_source active;
void input_session_reset(void) { active = INPUT_IDLE; }
void input_processing_start(void) {}
uint32_t input_connection_begin(enum input_source source) { (void)source; return 1; }
int input_report_for(enum input_source source, uint32_t generation,
                     const controller_state_t *state, uint64_t timestamp) {
  if (!generation || !state) return -1;
  input_report(source, state, timestamp);
  return 0;
}
void input_release_for(enum input_source source, uint32_t generation) {
  if (generation) input_release(source);
}
void input_quiesce_for(enum input_source source, uint32_t generation) {
  if (generation && active == source) output_disconnect();
}
void input_report(enum input_source source, const controller_state_t *state, uint64_t timestamp) {
  if (active && source != active) return;
  active = source;
  output_submit(state, timestamp);
}
void input_release(enum input_source source) {
  if (active != source) return;
  output_disconnect();
  active = INPUT_IDLE;
}
#endif
