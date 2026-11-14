#include "usb/xinput_report.h"
#include "usb/xinput_output.h"
#include "usb/xinput_protocol.h"
#include <assert.h>
#include <string.h>
#include <stdio.h>

static unsigned get16(const uint8_t *p) { return p[0] | ((unsigned)p[1] << 8); }
static void test_report(void) {
  controller_state_t s;
  controller_state_neutral(&s);
  uint8_t r[XINPUT_REPORT_BYTES];
  xinput_report_encode(&s, r);
  assert(r[0] == 0 && r[1] == 20);
  for (int i = 2; i < 20; i++) assert(r[i] == 0);
  s.buttons = CTRL_A|CTRL_B|CTRL_X|CTRL_Y|CTRL_LB|CTRL_RB|
              CTRL_VIEW|CTRL_MENU|CTRL_L3|CTRL_R3|CTRL_GUIDE;
  s.dpad = 1; s.left_x = -32768; s.left_y = -32768;
  s.right_x = 32767; s.right_y = 32767;
  s.left_trigger = 65535; s.right_trigger = 32768; s.share = 1;
  xinput_report_encode(&s, r);
  assert(get16(r+2) == 0xf7f9);
  assert(r[4] == 255 && r[5] == 128);
  assert(get16(r+6) == 0x8000 && get16(r+8) == 0x7fff);
  assert(get16(r+10) == 0x7fff && get16(r+12) == 0x8001);
  for (int i = 14; i < 20; i++) assert(r[i] == 0);
  s.buttons = 0; s.share = 0;
  for (int d = 0; d < 9; d++) {
    s.dpad = (uint8_t)d; xinput_report_encode(&s, r);
    static const unsigned hats[] = {1,9,8,10,2,6,4,5,0};
    assert(get16(r+2) == hats[d]);
  }
  s.dpad = 8; s.left_trigger = -9; s.right_trigger = 99999;
  s.left_x = -99999; s.left_y = 99999;
  xinput_report_encode(&s, r);
  assert(r[4] == 0 && r[5] == 255);
  assert(get16(r+6) == 0x8000 && get16(r+8) == 0x8001);
}
static void test_queue(void) {
  xinput_output_t q;
  controller_state_t s;
  controller_state_neutral(&s);
  xinput_output_init(&q);
  uint8_t report[XINPUT_REPORT_BYTES];
  assert(xinput_output_peek(&q, report) == XINPUT_OUTPUT_NEUTRAL);
  s.buttons = CTRL_A;
  assert(xinput_output_submit(&q, &s));
  s.buttons = CTRL_B;
  assert(xinput_output_submit(&q, &s));
  assert(q.stale_replaced == 1);
  /* Busy endpoint: no mark_queued, latest state remains pending. */
  assert(xinput_output_peek(&q, report) == XINPUT_OUTPUT_NEUTRAL);
  xinput_output_mark_queued(&q, XINPUT_OUTPUT_NEUTRAL, report);
  assert(xinput_output_peek(&q, report) == XINPUT_OUTPUT_LATEST);
  assert(get16(report+2) == 0x2000);
  xinput_output_mark_queued(&q, XINPUT_OUTPUT_LATEST, report);
  assert(xinput_output_peek(&q, report) == XINPUT_OUTPUT_NONE);
  assert(!xinput_output_submit(&q, &s));
  s.buttons = CTRL_A; assert(xinput_output_submit(&q, &s));
  xinput_output_disconnect(&q);
  assert(xinput_output_peek(&q, report) == XINPUT_OUTPUT_NEUTRAL);
  xinput_output_mark_queued(&q, XINPUT_OUTPUT_NEUTRAL, report);
  assert(xinput_output_peek(&q, report) == XINPUT_OUTPUT_NONE);
  assert(xinput_output_submit(&q, &s)); /* reconnection */
  xinput_output_usb_reset(&q);
  assert(xinput_output_peek(&q, report) == XINPUT_OUTPUT_NEUTRAL);
}
static void test_descriptors_requests(void) {
  assert(xinput_device_descriptor[0] == 18);
  assert(get16(xinput_device_descriptor+8) != 0x045e);
  assert(get16(xinput_device_descriptor+12) == BRIDGE_XINPUT_BCD_DEVICE);
  assert(get16(xinput_config_descriptor+2) == XINPUT_CONFIG_BYTES);
  assert(xinput_config_descriptor[13] == 2);
  assert(xinput_config_descriptor[14] == 0xff &&
         xinput_config_descriptor[15] == 0x5d);
  assert(xinput_config_descriptor[18] == 17 &&
         xinput_config_descriptor[19] == 0x21);
  assert(xinput_config_descriptor[35] == 7 &&
         xinput_config_descriptor[37] == XINPUT_EP_IN);
  assert(xinput_config_descriptor[42] == 7 &&
         xinput_config_descriptor[44] == XINPUT_EP_OUT);
  assert(get16(xinput_os_compat_descriptor) == XINPUT_OS_COMPAT_BYTES);
  assert(!memcmp(xinput_os_compat_descriptor+18, "XUSB10", 6));
  assert(xinput_os_string_descriptor[0] == 0x0312);
  assert(xinput_os_string_descriptor[1] == 'M' &&
         xinput_os_string_descriptor[7] == '0' &&
         xinput_os_string_descriptor[8] == XINPUT_OS_VENDOR_CODE);
  assert(xinput_classify_request(0xc0,0x20,0,4,40) == XINPUT_REQUEST_OS_COMPAT);
  assert(xinput_classify_request(0xa1,1,0x0100,0,20) == XINPUT_REQUEST_GET_REPORT);
  assert(xinput_classify_request(0x21,9,0x0200,0,8) == XINPUT_REQUEST_SET_REPORT);
  assert(xinput_classify_request(0xc0,1,0,0,4) == XINPUT_REQUEST_GET_DEVICE_ID);
  assert(xinput_classify_request(0xc0,0x20,0,5,40) == XINPUT_REQUEST_UNSUPPORTED);
  assert(xinput_classify_request(0x21,9,0x0200,1,8) == XINPUT_REQUEST_UNSUPPORTED);
  assert(xinput_classify_request(0x21,9,0x0200,0,33) == XINPUT_REQUEST_UNSUPPORTED);
}
static void test_feedback(void) {
  xinput_feedback_t f = {0};
  const uint8_t rumble[] = {0,8,0,0x80,0x40,0,0,0};
  const uint8_t led[] = {1,3,0x06};
  const uint8_t invalid[] = {0,8,0};
  assert(xinput_feedback_parse(&f, rumble, sizeof rumble));
  assert(f.left_motor == 0x80 && f.right_motor == 0x40 && f.rumble_count == 1);
  assert(xinput_feedback_parse(&f, led, sizeof led));
  assert(f.led_pattern == 6 && f.led_count == 1);
  assert(!xinput_feedback_parse(&f, invalid, sizeof invalid));
  assert(f.malformed_count == 1);
}
static void test_two_independent_outputs(void) {
  xinput_output_t slots[2];
  controller_state_t xbox, bitdo;
  uint8_t report[XINPUT_REPORT_BYTES];
  controller_state_neutral(&xbox);
  controller_state_neutral(&bitdo);
  xinput_output_init(&slots[0]);
  xinput_output_init(&slots[1]);
  for (int i = 0; i < 2; i++) {
    assert(xinput_output_peek(&slots[i], report) == XINPUT_OUTPUT_NEUTRAL);
    xinput_output_mark_queued(&slots[i], XINPUT_OUTPUT_NEUTRAL, report);
  }
  xbox.buttons = CTRL_A;
  bitdo.buttons = CTRL_B;
  assert(xinput_output_submit(&slots[0], &xbox));
  assert(xinput_output_submit(&slots[1], &bitdo));
  assert(xinput_output_peek(&slots[0], report) == XINPUT_OUTPUT_LATEST);
  assert(get16(report+2) == 0x1000);
  xinput_output_mark_queued(&slots[0], XINPUT_OUTPUT_LATEST, report);
  /* Slot 0 has its own prior report while slot 1 is sent. */
  assert(xinput_output_peek(&slots[1], report) == XINPUT_OUTPUT_LATEST);
  assert(get16(report+2) == 0x2000);
  xinput_output_mark_queued(&slots[1], XINPUT_OUTPUT_LATEST, report);
  xinput_output_disconnect(&slots[0]);
  assert(xinput_output_peek(&slots[0], report) == XINPUT_OUTPUT_NEUTRAL);
  assert(xinput_output_peek(&slots[1], report) == XINPUT_OUTPUT_NONE);
  assert(xinput_output_submit(&slots[0], &xbox));
  assert(xinput_output_peek(&slots[0], report) == XINPUT_OUTPUT_NEUTRAL);
}
int main(void) {
  test_report(); test_queue(); test_descriptors_requests(); test_feedback();
  test_two_independent_outputs();
  puts("XInput report, queue, descriptors, requests, feedback: OK");
  return 0;
}
