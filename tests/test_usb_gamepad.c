#include "controllers/hid_gamepad.h"
#include "led/status_led.h"
#include "usb/gamepad_output.h"
#include <stdio.h>
#include <string.h>

static int failures;
static void check(bool condition, const char *name) {
  if (!condition) { printf("FAIL %s\n", name); failures++; }
  else printf("PASS %s\n", name);
}
static uint16_t get16(const uint8_t *p) {
  return (uint16_t)p[0] | ((uint16_t)p[1] << 8);
}

int main(void) {
  uint8_t bytes[USB_GAMEPAD_REPORT_BYTES];
  controller_state_t state;
  controller_state_neutral(&state);
  gamepad_report_encode(&state, NULL, bytes);
  check(sizeof(bytes) == 15 && bytes[12] == 8 && get16(bytes + 13) == 0,
        "neutral report releases hat and buttons");
  for (int i = 0; i < 12; i += 2) check(get16(bytes + i) == 0,
                                  "neutral axis or trigger");

  state.left_x = -32768; state.left_y = 32767;
  state.right_x = 100; state.right_y = -100;
  state.left_trigger = 65535; state.right_trigger = 1024;
  state.dpad = 3;
  state.buttons = CTRL_A | CTRL_B | CTRL_X | CTRL_Y | CTRL_LB |
      CTRL_RB | CTRL_VIEW | CTRL_MENU | CTRL_GUIDE | CTRL_L3 | CTRL_R3;
  state.share = 1;
  gamepad_report_encode(&state, NULL, bytes);
  check(get16(bytes) == 0x8000 && get16(bytes + 2) == 0x7fff &&
        get16(bytes + 4) == 100 && get16(bytes + 6) == (uint16_t)-100,
        "four independent full precision stick axes");
  check(get16(bytes + 8) == 65535 && get16(bytes + 10) == 1024,
        "independent unsigned 16-bit triggers");
  check(bytes[12] == 3 && get16(bytes + 13) == 0x0fff,
        "hat and twelve digital buttons");
  bool hats_ok = true;
  for (uint8_t direction = 0; direction <= 8; direction++) {
    state.dpad = direction;
    gamepad_report_encode(&state, NULL, bytes);
    hats_ok &= bytes[12] == direction;
  }
  check(hats_ok, "all eight hat directions and neutral");

  state.left_x = -50000; state.right_trigger = 70000; state.dpad = 99;
  gamepad_report_encode(&state, NULL, bytes);
  check(get16(bytes) == 0x8000 && get16(bytes + 10) == 65535 &&
        bytes[12] == 8, "out of range values clamp safely");
  gamepad_profile_t profile = {.stick_deadzone = 300};
  state.left_x = 100; state.left_y = -100;
  state.right_x = 32767; state.right_y = -32768;
  gamepad_report_encode(&state, &profile, bytes);
  check(get16(bytes) == 0 && get16(bytes + 2) == 0 &&
        get16(bytes + 4) == 32767 && get16(bytes + 6) == 0x8000,
        "optional deadzone centers small drift and preserves endpoints");

  hid_gamepad_parser_t parser;
  check(usb_gamepad_report_descriptor_len == USB_GAMEPAD_DESCRIPTOR_BYTES &&
        hid_gamepad_parser_reset(&parser, usb_gamepad_report_descriptor,
                                 USB_GAMEPAD_DESCRIPTOR_BYTES) &&
        !parser.has_report_ids && parser.input_bits[0] == 120,
        "USB descriptor parses to one 120-bit input report without Report ID");

  gamepad_output_t output;
  gamepad_output_init(&output);
  gamepad_output_kind_t kind = gamepad_output_peek(&output, bytes);
  check(kind == GAMEPAD_OUTPUT_NEUTRAL && bytes[12] == 8,
        "USB mount starts neutral");
  gamepad_output_mark_queued(&output, kind, bytes);
  controller_state_neutral(&state);
  state.buttons = CTRL_A;
  check(gamepad_output_submit(&output, &state, NULL), "first input queued");
  state.buttons = CTRL_B;
  check(gamepad_output_submit(&output, &state, NULL) &&
        output.stale_replaced == 1, "latest state replaces stale pending state");
  kind = gamepad_output_peek(&output, bytes);
  check(kind == GAMEPAD_OUTPUT_LATEST && get16(bytes + 13) == CTRL_B,
        "endpoint busy retains newest state");
  gamepad_output_mark_queued(&output, kind, bytes);
  gamepad_output_disconnect(&output);
  state.buttons = CTRL_X;
  gamepad_output_submit(&output, &state, NULL);
  kind = gamepad_output_peek(&output, bytes);
  check(kind == GAMEPAD_OUTPUT_NEUTRAL && get16(bytes + 13) == 0,
        "disconnect neutral takes priority over reconnection input");
  gamepad_output_mark_queued(&output, kind, bytes);
  kind = gamepad_output_peek(&output, bytes);
  check(kind == GAMEPAD_OUTPUT_LATEST && get16(bytes + 13) == CTRL_X,
        "reconnected latest input follows neutral");
  gamepad_output_mark_queued(&output, kind, bytes);
  check(!gamepad_output_submit(&output, &state, NULL) &&
        gamepad_output_peek(&output, bytes) == GAMEPAD_OUTPUT_NONE,
        "unchanged HID reports are not sent redundantly");
  gamepad_output_usb_reset(&output);
  check(gamepad_output_peek(&output, bytes) == GAMEPAD_OUTPUT_NEUTRAL,
        "USB remount or resume schedules neutral first");

  check(status_led_pattern_output(LED_BOOT, 0) &&
        !status_led_pattern_output(LED_BOOT, 100) &&
        status_led_pattern_output(LED_BOOT, 160) &&
        !status_led_pattern_output(LED_BOOT, 250), "boot double flash");
  check(status_led_pattern_output(LED_SCANNING, 0) &&
        !status_led_pattern_output(LED_SCANNING, 500) &&
        status_led_pattern_output(LED_PAIRING, 210) &&
        !status_led_pattern_output(LED_PAIRING, 100),
        "scan and pairing blink without delays");
  check(status_led_pattern_output(LED_READY, 1599) &&
        status_led_pattern_output(LED_RECOVERABLE_ERROR, 450) &&
        !status_led_pattern_output(LED_RECOVERABLE_ERROR, 700) &&
        status_led_pattern_output(LED_CRITICAL_ERROR, 900),
        "ready and error patterns differ");
  check(status_led_effective(LED_READY, false) == LED_RECOVERABLE_ERROR &&
        status_led_effective(LED_READY, true) == LED_READY &&
        status_led_effective(LED_SCANNING, false) == LED_SCANNING,
        "LED ready requires both BLE input and USB mount");
  return failures ? 1 : 0;
}
