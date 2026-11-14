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
static void two_classic_contexts_are_independent(void) {
  reset();
  uint32_t a = input_connection_begin(INPUT_CLASSIC_0);
  uint32_t b = input_connection_begin(INPUT_CLASSIC_1);
  controller_state_t one = state(0x11), two = state(0x22);
  assert(input_report_for(INPUT_CLASSIC_0, a, &one, 10) == 0);
  assert(input_report_for(INPUT_CLASSIC_1, b, &two, 11) == 1);
  input_release_for(INPUT_CLASSIC_0, a);
  assert(disconnects[0] == 1 && disconnects[1] == 0);
  assert(input_report_for(INPUT_CLASSIC_1, b, &two, 12) == 1);
  uint32_t again = input_connection_begin(INPUT_CLASSIC_0);
  assert(input_report_for(INPUT_CLASSIC_0, again, &one, 13) == 0);
  input_release_for(INPUT_CLASSIC_0, a); /* stale generation */
  assert(disconnects[0] == 1);
}
static void autofire_follows_bluetooth_identity(void) {
  reset();
  const uint8_t bitdo[6]={1,2,3,4,5,6}, ds4[6]={7,8,9,10,11,12};
  const uint8_t swpro[6]={13,14,15,16,17,18};
  const uint8_t fourth[6]={19,20,21,22,23,24};
  uint32_t g = input_classic_connection_begin(INPUT_CLASSIC_0, bitdo);
  controller_state_t p = state(CTRL_VIEW|CTRL_MENU); p.dpad=8;
  input_report_for(INPUT_CLASSIC_0,g,&p,1);
  input_report_for(INPUT_CLASSIC_0,g,&p,2000001);
  p=state(0); p.dpad=8; input_report_for(INPUT_CLASSIC_0,g,&p,2000002);
  p=state(CTRL_A); p.dpad=0; input_report_for(INPUT_CLASSIC_0,g,&p,2000003);
  input_release_for(INPUT_CLASSIC_0,g);
  assert(input_classic_autofire_target_hz(bitdo,0)==5);

  /* Alternate through other Classic identities on the same source. */
  g=input_classic_connection_begin(INPUT_CLASSIC_0,ds4);
  assert(input_classic_autofire_target_hz(ds4,0)==0);
  p=state(CTRL_VIEW|CTRL_MENU); p.dpad=8;
  input_report_for(INPUT_CLASSIC_0,g,&p,3000000);
  input_report_for(INPUT_CLASSIC_0,g,&p,5000001);
  p=state(0); p.dpad=8; input_report_for(INPUT_CLASSIC_0,g,&p,5000002);
  p=state(CTRL_Y); p.dpad=6; input_report_for(INPUT_CLASSIC_0,g,&p,5000003);
  input_release_for(INPUT_CLASSIC_0,g);
  assert(input_classic_autofire_target_hz(ds4,3)==20);
  g=input_classic_connection_begin(INPUT_CLASSIC_0,ds4);
  p=state(0); p.dpad=8; input_report_for(INPUT_CLASSIC_0,g,&p,5100000);
  p=state(CTRL_A); p.dpad=8; input_report_for(INPUT_CLASSIC_0,g,&p,5100001);
  input_report_for(INPUT_CLASSIC_0,g,&p,5200001);
  assert(last_state[0].buttons & CTRL_A); /* Must not inherit Bitdo's 5 Hz rate. */
  input_release_for(INPUT_CLASSIC_0,g);

  g=input_classic_connection_begin(INPUT_CLASSIC_0,swpro);
  assert(input_classic_autofire_target_hz(swpro,3)==0);
  input_release_for(INPUT_CLASSIC_0,g);
  g=input_classic_connection_begin(INPUT_CLASSIC_0,fourth);
  assert(input_classic_autofire_target_hz(fourth,0)==0);
  p=state(CTRL_VIEW|CTRL_MENU); p.dpad=8;
  input_report_for(INPUT_CLASSIC_0,g,&p,6000000);
  input_report_for(INPUT_CLASSIC_0,g,&p,8000001);
  p=state(0); p.dpad=8; input_report_for(INPUT_CLASSIC_0,g,&p,8000002);
  p=state(CTRL_B); p.dpad=4; input_report_for(INPUT_CLASSIC_0,g,&p,8000003);
  input_release_for(INPUT_CLASSIC_0,g);
  assert(input_classic_autofire_target_hz(fourth,1)==15);
  g=input_classic_connection_begin(INPUT_CLASSIC_0,bitdo);
  assert(input_classic_autofire_target_hz(bitdo,0)==5);
  assert(input_classic_autofire_target_hz(bitdo,3)==0);
  input_release_for(INPUT_CLASSIC_0,g);
  /* A fifth identity cannot inherit or evict one of the four RAM contexts. */
  const uint8_t fifth[6]={25,26,27,28,29,30};
  g=input_classic_connection_begin(INPUT_CLASSIC_0,fifth);
  assert(input_classic_autofire_target_hz(fifth,0)==0);
  assert(input_classic_autofire_target_hz(bitdo,0)==5);
  assert(input_classic_autofire_target_hz(fourth,1)==15);
  /* Successful replacement clears the victim context and binds the new
   * identity into that freed RAM context without inheriting its old rate. */
  input_classic_forget_autofire_identity(ds4);
  input_classic_bind_autofire_identity(fifth);
  assert(input_classic_autofire_target_hz(ds4,3)==0);
  assert(input_classic_autofire_target_hz(fifth,3)==0);
  p=state(CTRL_VIEW|CTRL_MENU); p.dpad=8;
  input_report_for(INPUT_CLASSIC_0,g,&p,9000000);
  input_report_for(INPUT_CLASSIC_0,g,&p,11000001);
  p=state(0); p.dpad=8; input_report_for(INPUT_CLASSIC_0,g,&p,11000002);
  p=state(CTRL_A); p.dpad=8; input_report_for(INPUT_CLASSIC_0,g,&p,11000003);
  input_release_for(INPUT_CLASSIC_0,g);
  assert(input_classic_autofire_target_hz(fifth,0)==10);
  assert(input_classic_autofire_target_hz(bitdo,0)==5);
  assert(input_classic_autofire_target_hz(fourth,1)==15);
}
int main(void) {
  both_orders(INPUT_XBOX, INPUT_8BITDO);
  both_orders(INPUT_8BITDO, INPUT_XBOX);
  next_free_without_moving();
  two_classic_contexts_are_independent();
  autofire_follows_bluetooth_identity();
  reset(); /* simulated Pico reboot */
  uint32_t b = input_connection_begin(INPUT_8BITDO);
  controller_state_t s = state(30);
  assert(input_report_for(INPUT_8BITDO, b, &s, 1) == 0);
  return 0;
}
