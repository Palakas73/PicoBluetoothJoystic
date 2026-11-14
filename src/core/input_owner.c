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
static uint8_t source_slots[INPUT_SOURCE_COUNT];
static uint32_t next_generation[INPUT_SOURCE_COUNT], active_generation[INPUT_SOURCE_COUNT];
static autofire_t autofire[INPUT_SOURCE_COUNT];
enum { AUTOFIRE_IDENTITY_CAPACITY = 4 };
typedef struct { uint8_t address[6], hz[AUTOFIRE_TARGET_COUNT]; bool used; } autofire_identity_t;
static autofire_identity_t classic_autofire[AUTOFIRE_IDENTITY_CAPACITY];
static int8_t active_identity[INPUT_SOURCE_COUNT];
static controller_state_t last_sent[INPUT_SOURCE_COUNT];
static bool have_last_sent[INPUT_SOURCE_COUNT];
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
  memset(classic_autofire, 0, sizeof(classic_autofire));
  for (unsigned source = INPUT_XBOX; source < INPUT_SOURCE_COUNT; source++) {
    source_slots[source] = UNASSIGNED;
    active_generation[source] = 0;
    active_identity[source] = -1;
    autofire_init(&autofire[source]);
    have_last_sent[source] = false;
  }
}
static void save_identity_settings(enum input_source source) {
  int i = active_identity[source];
  if (i >= 0 && i < AUTOFIRE_IDENTITY_CAPACITY)
    memcpy(classic_autofire[i].hz, autofire[source].hz, AUTOFIRE_TARGET_COUNT);
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
  for (unsigned source = INPUT_XBOX; source < INPUT_SOURCE_COUNT; source++) {
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
  if (source <= INPUT_IDLE || source >= INPUT_SOURCE_COUNT) return 0;
  if (autofire_configuring(&autofire[source])) feedback(AF_EVENT_CANCELLED);
  save_identity_settings(source);
  active_identity[source] = -1;
  if (source_slots[source] < 2) usb_xinput_disconnect_slot(source_slots[source]);
  source_slots[source] = UNASSIGNED;
  uint32_t generation = ++next_generation[source];
  if (!generation) generation = ++next_generation[source];
  active_generation[source] = generation;
  autofire_connect(&autofire[source]);
  have_last_sent[source] = false;
  return generation;
}
uint32_t input_classic_connection_begin(enum input_source source,
                                       const uint8_t identity[6]) {
  if (!identity || source < INPUT_CLASSIC_0 || source >= INPUT_SOURCE_COUNT)
    return 0;
  save_identity_settings(source);
  uint32_t generation = input_connection_begin(source);
  int selected = -1, free_entry = -1;
  for (int i = 0; i < AUTOFIRE_IDENTITY_CAPACITY; ++i) {
    if (classic_autofire[i].used &&
        memcmp(classic_autofire[i].address, identity, 6) == 0) selected = i;
    if (!classic_autofire[i].used && free_entry < 0) free_entry = i;
  }
  if (selected < 0) selected = free_entry;
  memset(autofire[source].hz, 0, sizeof(autofire[source].hz));
  if (selected >= 0) {
    autofire_identity_t *entry = &classic_autofire[selected];
    if (!entry->used) { memcpy(entry->address, identity, 6); entry->used = true; }
    memcpy(autofire[source].hz, entry->hz, sizeof(entry->hz));
    active_identity[source] = (int8_t)selected;
  } else {
    printf("[AUTOFIRE] identity registry full; settings disabled for Classic peer\r\n");
  }
  return generation;
}
uint8_t input_classic_autofire_target_hz(const uint8_t identity[6],
                                         unsigned target) {
  if (!identity || target >= AUTOFIRE_TARGET_COUNT) return 0;
  for (int i = 0; i < AUTOFIRE_IDENTITY_CAPACITY; ++i)
    if (classic_autofire[i].used &&
        memcmp(classic_autofire[i].address, identity, 6) == 0)
      return classic_autofire[i].hz[target];
  return 0;
}
void input_classic_forget_autofire_identity(const uint8_t identity[6]) {
  if (!identity) return;
  for (int i = 0; i < AUTOFIRE_IDENTITY_CAPACITY; ++i) {
    if (classic_autofire[i].used &&
        memcmp(classic_autofire[i].address, identity, 6) == 0) {
      memset(&classic_autofire[i], 0, sizeof(classic_autofire[i]));
      if (active_identity[INPUT_CLASSIC_0] == i) {
        active_identity[INPUT_CLASSIC_0] = -1;
        memset(autofire[INPUT_CLASSIC_0].hz, 0,
               sizeof(autofire[INPUT_CLASSIC_0].hz));
      }
      return;
    }
  }
}
void input_classic_bind_autofire_identity(const uint8_t identity[6]) {
  if (!identity) return;
  int selected = -1, free_entry = -1;
  for (int i = 0; i < AUTOFIRE_IDENTITY_CAPACITY; ++i) {
    if (classic_autofire[i].used &&
        memcmp(classic_autofire[i].address, identity, 6) == 0) selected = i;
    if (!classic_autofire[i].used && free_entry < 0) free_entry = i;
  }
  if (selected < 0) selected = free_entry;
  if (selected < 0) {
    printf("[AUTOFIRE] identity registry full; candidate stays neutral until a context is available\r\n");
    return;
  }
  if (!classic_autofire[selected].used) {
    memcpy(classic_autofire[selected].address, identity, 6);
    classic_autofire[selected].used = true;
    memset(classic_autofire[selected].hz, 0, sizeof(classic_autofire[selected].hz));
  }
  active_identity[INPUT_CLASSIC_0] = (int8_t)selected;
  memcpy(autofire[INPUT_CLASSIC_0].hz, classic_autofire[selected].hz,
         sizeof(autofire[INPUT_CLASSIC_0].hz));
}
uint8_t input_usb_slots_used(void) {
  uint8_t used = 0;
  for (unsigned source = INPUT_XBOX; source < INPUT_SOURCE_COUNT; ++source)
    if (source_slots[source] < 2) ++used;
  return used;
}
static uint8_t slot_for(enum input_source source) {
  if (source <= INPUT_IDLE || source >= INPUT_SOURCE_COUNT) return UNASSIGNED;
  if (source_slots[source] < 2) return source_slots[source];
  bool used[2] = {false, false};
  for (unsigned other = INPUT_XBOX; other < INPUT_SOURCE_COUNT; other++) {
    if (other != (unsigned)source && source_slots[other] < 2)
      used[source_slots[other]] = true;
  }
  uint8_t slot = !used[0] ? 0 : (!used[1] ? 1 : UNASSIGNED);
  if (slot == UNASSIGNED) return UNASSIGNED;
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
  if (source <= INPUT_IDLE || source >= INPUT_SOURCE_COUNT) return -1;
  if (!generation || generation != active_generation[source] || !state) return -1;
  uint8_t slot = slot_for(source);
  if (slot >= 2) return -1;
  controller_state_t transformed;
  autofire_event_t event = autofire_physical(&autofire[source], state,
                                              timestamp, &transformed);
  feedback(event);
  submit_transformed(source, &transformed, timestamp);
  return slot;
}
void input_release_for(enum input_source source, uint32_t generation) {
  if (source <= INPUT_IDLE || source >= INPUT_SOURCE_COUNT) return;
  if (!generation || generation != active_generation[source]) return;
  if (autofire_configuring(&autofire[source])) feedback(AF_EVENT_CANCELLED);
  save_identity_settings(source);
  if (source_slots[source] < 2) usb_xinput_disconnect_slot(source_slots[source]);
  autofire_disconnect(&autofire[source]);
  have_last_sent[source] = false;
  source_slots[source] = UNASSIGNED;
  active_generation[source] = 0;
  active_identity[source] = -1;
}
void input_quiesce_for(enum input_source source, uint32_t generation) {
  if (source <= INPUT_IDLE || source >= INPUT_SOURCE_COUNT) return;
  if (!generation || generation != active_generation[source]) return;
  if (autofire_configuring(&autofire[source])) feedback(AF_EVENT_CANCELLED);
  save_identity_settings(source);
  if (source_slots[source] < 2) usb_xinput_disconnect_slot(source_slots[source]);
  autofire_disconnect(&autofire[source]);
  have_last_sent[source] = false;
}
void input_release(enum input_source source) {
  if (source <= INPUT_IDLE || source >= INPUT_SOURCE_COUNT) return;
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
uint32_t input_classic_connection_begin(enum input_source source,
                                       const uint8_t identity[6]) {
  (void)identity;
  return input_connection_begin(source);
}
uint8_t input_classic_autofire_target_hz(const uint8_t identity[6],
                                         unsigned target) {
  (void)identity; (void)target; return 0;
}
void input_classic_forget_autofire_identity(const uint8_t identity[6]) { (void)identity; }
void input_classic_bind_autofire_identity(const uint8_t identity[6]) { (void)identity; }
uint8_t input_usb_slots_used(void) { return active == INPUT_IDLE ? 0 : 1; }
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
