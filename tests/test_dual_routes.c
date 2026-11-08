#include "core/input_owner.h"
#include <assert.h>
#include <string.h>

static controller_state_t last_state[2];
static unsigned reports[2], disconnects[2];
void usb_xinput_submit_slot(uint8_t slot, const controller_state_t *state, uint64_t timestamp) {
  (void)timestamp;
  assert(slot < 2);
  last_state[slot] = *state;
  reports[slot]++;
}
void usb_xinput_disconnect_slot(uint8_t slot) {
  assert(slot < 2);
  disconnects[slot]++;
}
static controller_state_t state(unsigned buttons) {
  controller_state_t s;
  controller_state_neutral(&s);
  s.buttons = buttons;
  return s;
}
static void reset(void) {
  input_session_reset();
  memset(reports, 0, sizeof(reports));
  memset(disconnects, 0, sizeof(disconnects));
  memset(last_state, 0, sizeof(last_state));
}
static void both_orders(enum input_source first, enum input_source second) {
  reset();
  uint32_t a = input_connection_begin(first), b = input_connection_begin(second);
  controller_state_t one = state(1), two = state(2);
  input_release_for(first, a + 1000); /* stale callback */
  assert(input_report_for(first, a, &one, 1) == 0);
  assert(input_report_for(second, b, &two, 1) == 1); /* equal timestamp */
  assert(last_state[0].buttons == 1 && last_state[1].buttons == 2);
  input_release_for(first, a);
  assert(disconnects[0] == 1 && disconnects[1] == 0);
  assert(input_report_for(first, a, &one, 2) == -1); /* old report */
  uint32_t renewed = input_connection_begin(first);
  assert(input_report_for(first, renewed, &one, 3) == 0);
  assert(last_state[1].buttons == 2 && disconnects[1] == 0);
  input_release_for(first, a); /* delayed old disconnect */
  assert(disconnects[0] == 1);
  input_release_for(second, b);
  input_release_for(first, renewed);
  assert(disconnects[0] == 2 && disconnects[1] == 1);
  uint32_t second_reconnect = input_connection_begin(second);
  assert(input_report_for(second, second_reconnect, &two, 4) == 0);
  uint32_t first_reconnect = input_connection_begin(first);
  assert(input_report_for(first, first_reconnect, &one, 5) == 1);
  input_release_for(second, b); /* old generation cannot release reused slot */
  assert(disconnects[0] == 2 && disconnects[1] == 1);
}
static void next_free_without_moving(void) {
  reset();
  uint32_t xbox = input_connection_begin(INPUT_XBOX);
  uint32_t bitdo = input_connection_begin(INPUT_8BITDO);
  controller_state_t a = state(10), b = state(20);
  assert(input_report_for(INPUT_XBOX, xbox, &a, 1) == 0);
  assert(input_report_for(INPUT_8BITDO, bitdo, &b, 2) == 1);
  input_release_for(INPUT_XBOX, xbox);
  assert(disconnects[0] == 1 && disconnects[1] == 0);
  assert(input_report_for(INPUT_8BITDO, bitdo, &b, 3) == 1);
  xbox = input_connection_begin(INPUT_XBOX);
  assert(input_report_for(INPUT_XBOX, xbox, &a, 4) == 0);
  assert(last_state[1].buttons == 20);
}
int main(void) {
  both_orders(INPUT_XBOX, INPUT_8BITDO);
  both_orders(INPUT_8BITDO, INPUT_XBOX);
  next_free_without_moving();
  reset(); /* simulated Pico reboot */
  uint32_t b = input_connection_begin(INPUT_8BITDO);
  controller_state_t s = state(30);
  assert(input_report_for(INPUT_8BITDO, b, &s, 1) == 0);
  return 0;
}
