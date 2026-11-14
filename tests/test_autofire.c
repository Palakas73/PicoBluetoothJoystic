#include "core/autofire.h"
#include <assert.h>
#include <string.h>

static controller_state_t neutral(void) {
  controller_state_t s;
  controller_state_neutral(&s);
  return s;
}
static autofire_event_t report(autofire_t *a, controller_state_t *p,
                               uint64_t us, controller_state_t *o) {
  return autofire_physical(a, p, us, o);
}
static void arm(autofire_t *a, uint64_t base) {
  controller_state_t p = neutral(), o;
  report(a, &p, base, &o); /* release-after-connect */
  p.buttons = CTRL_VIEW | CTRL_MENU;
  assert(report(a, &p, base + 1, &o) == AF_EVENT_NONE);
  assert(autofire_tick(a, base + 1999999, &o) == AF_EVENT_NONE);
  assert(autofire_tick(a, base + 2000001, &o) == AF_EVENT_ARMED);
  assert((o.buttons & (CTRL_VIEW | CTRL_MENU)) == 0);
  assert(autofire_tick(a, base + 2500000, &o) == AF_EVENT_NONE); /* once */
  p.buttons = 0;
  report(a, &p, base + 2500001, &o);
  assert(a->mode == AF_MODE_ARMED);
}
static void configure(autofire_t *a, uint16_t button, uint8_t dpad,
                      uint64_t base, autofire_event_t expected) {
  controller_state_t p = neutral(), o;
  p.buttons = button; p.dpad = dpad;
  assert(report(a, &p, base, &o) == expected);
  assert((o.buttons & button) == 0 && o.dpad == 8);
  p.buttons = 0; p.dpad = 8;
  report(a, &p, base + 1, &o);
  assert(a->mode == AF_MODE_NORMAL);
}
static void test_combo_and_toggle(void) {
  autofire_t a; controller_state_t p = neutral(), o;
  autofire_init(&a); autofire_connect(&a);
  arm(&a, 0);
  configure(&a, CTRL_A, 8, 3000000, AF_EVENT_ENABLED_10);
  assert(autofire_target_hz(&a, AF_A) == 10);
  p.buttons = CTRL_A;
  report(&a, &p, 4000000, &o);
  assert(o.buttons & CTRL_A); /* immediate ON */
  autofire_tick(&a, 4049999, &o); assert(o.buttons & CTRL_A);
  autofire_tick(&a, 4050000, &o); assert(!(o.buttons & CTRL_A));
  autofire_tick(&a, 4100000, &o); assert(o.buttons & CTRL_A);
  p.buttons = 0; report(&a, &p, 4100001, &o); assert(!(o.buttons & CTRL_A));
  arm(&a, 5000000);
  configure(&a, CTRL_A, 8, 8000000, AF_EVENT_DISABLED);
  assert(!autofire_target_enabled(&a, AF_A));
}
static void test_speeds_and_no_drift(void) {
  const uint8_t dpads[] = {0,2,4,6};
  const uint8_t hz[] = {5,10,15,20};
  const autofire_event_t ev[] = {AF_EVENT_SPEED_5,AF_EVENT_SPEED_10,
    AF_EVENT_SPEED_15,AF_EVENT_SPEED_20};
  for (unsigned n = 0; n < 4; n++) {
    autofire_t a; controller_state_t p = neutral(), o;
    autofire_init(&a); autofire_connect(&a); arm(&a, 0);
    configure(&a, CTRL_B, dpads[n], 3000000, ev[n]);
    assert(autofire_target_hz(&a, AF_B) == hz[n]);
    p.buttons = CTRL_B; report(&a, &p, 4000000, &o);
    assert(o.buttons & CTRL_B);
    uint64_t half_floor = 1000000u / (2u * hz[n]);
    autofire_tick(&a, 4000000 + half_floor - 1, &o);
    assert(o.buttons & CTRL_B);
  }
  autofire_t a; controller_state_t p = neutral(), o;
  autofire_init(&a); autofire_connect(&a); arm(&a, 0);
  configure(&a, CTRL_X, 4, 3000000, AF_EVENT_SPEED_15);
  p.buttons = CTRL_X; report(&a, &p, 4000000, &o);
  autofire_tick(&a, 4033333, &o); assert(o.buttons & CTRL_X);
  autofire_tick(&a, 4033334, &o); assert(!(o.buttons & CTRL_X));
  autofire_tick(&a, 4066667, &o); assert(o.buttons & CTRL_X);
  autofire_tick(&a, 14000000, &o); assert(o.buttons & CTRL_X); /* exact 150 cycles */
}
static void test_reject_timeout_cancel(void) {
  autofire_t a; controller_state_t p = neutral(), o;
  autofire_init(&a); autofire_connect(&a); arm(&a, 0);
  p.buttons = CTRL_A | CTRL_B;
  report(&a, &p, 3000000, &o);
  assert(a.mode == AF_MODE_REJECT_UNTIL_RELEASE && !a.hz[AF_A] && !a.hz[AF_B]);
  p.buttons = 0; report(&a, &p, 3000001, &o); assert(a.mode == AF_MODE_ARMED);
  /* A target held when setup began plus a new target is still a multiple selection. */
  a.await_release_mask = (1u << AF_A); p.buttons = CTRL_A | CTRL_B;
  report(&a, &p, 3000001, &o);
  assert(a.mode == AF_MODE_REJECT_UNTIL_RELEASE && !a.hz[AF_A] && !a.hz[AF_B]);
  p.buttons = 0; report(&a, &p, 3000001, &o); assert(a.mode == AF_MODE_ARMED);
  p.buttons = CTRL_A; p.dpad = 1; report(&a, &p, 3000002, &o);
  assert(a.mode == AF_MODE_REJECT_UNTIL_RELEASE && !a.hz[AF_A]);
  p.buttons = 0; p.dpad = 8; report(&a, &p, 3000003, &o);
  assert(autofire_tick(&a, 12000002, &o) == AF_EVENT_CANCELLED);
  assert(a.mode == AF_MODE_CONSUME_UNTIL_RELEASE);
  report(&a, &p, 12000003, &o); assert(a.mode == AF_MODE_NORMAL);
  arm(&a, 13000000);
  p.buttons = CTRL_VIEW | CTRL_MENU; report(&a, &p, 16000000, &o);
  assert(autofire_tick(&a, 18000000, &o) == AF_EVENT_CANCELLED);
}
static void test_trigger_hysteresis_and_reconnect(void) {
  autofire_t a; controller_state_t p = neutral(), o;
  autofire_init(&a); autofire_connect(&a); arm(&a, 0);
  p.left_trigger = 40000; p.dpad = 2;
  assert(report(&a, &p, 3000000, &o) == AF_EVENT_SPEED_10);
  p.left_trigger = 0; p.dpad = 8; report(&a, &p, 3000001, &o);
  p.left_trigger = 32768; report(&a, &p, 4000000, &o);
  assert(o.left_trigger == 65535);
  p.left_trigger = 31000; report(&a, &p, 4000001, &o);
  assert(o.left_trigger == 65535); /* hysteresis holds */
  p.left_trigger = 29490; report(&a, &p, 4000002, &o);
  assert(o.left_trigger == 0);
  autofire_disconnect(&a); autofire_connect(&a);
  p.left_trigger = 50000; report(&a, &p, 5000000, &o);
  assert(o.left_trigger == 50000); /* held across reconnect is gated, analog preserved */
  p.left_trigger = 0; report(&a, &p, 5000001, &o);
  p.left_trigger = 50000; report(&a, &p, 5000002, &o);
  assert(o.left_trigger == 65535);
}
static void test_two_independent(void) {
  autofire_t x, b; controller_state_t px = neutral(), pb = neutral(), ox, ob;
  autofire_init(&x); autofire_init(&b); autofire_connect(&x); autofire_connect(&b);
  x.hz[AF_A] = 5; b.hz[AF_B] = 20;
  report(&x, &px, 0, &ox); report(&b, &pb, 0, &ob);
  px.buttons = CTRL_A; pb.buttons = CTRL_B;
  report(&x, &px, 100, &ox); report(&b, &pb, 100, &ob);
  autofire_tick(&x, 25100, &ox); autofire_tick(&b, 25100, &ob);
  assert(ox.buttons & CTRL_A);
  assert(!(ob.buttons & CTRL_B));
}
int main(void) {
  test_combo_and_toggle();
  test_speeds_and_no_drift();
  test_reject_timeout_cancel();
  test_trigger_hysteresis_and_reconnect();
  test_two_independent();
  return 0;
}
