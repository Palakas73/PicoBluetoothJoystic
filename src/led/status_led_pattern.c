#include "status_led.h"

status_led_state_t status_led_effective(status_led_state_t ble_state,
                                       bool usb_mounted) {
  if (ble_state == LED_READY && !usb_mounted) return LED_RECOVERABLE_ERROR;
  return ble_state;
}

bool status_led_pattern_output(status_led_state_t state, uint32_t elapsed_ms) {
  uint32_t phase;
  switch (state) {
  case LED_BOOT:
    return elapsed_ms < 70 || (elapsed_ms >= 140 && elapsed_ms < 210);
  case LED_SCANNING:
    return elapsed_ms % 1000 < 120;
  case LED_PAIRING:
    return elapsed_ms % 200 < 90;
  case LED_READY:
    return true;
  case LED_RECOVERABLE_ERROR:
    phase = elapsed_ms % 1600;
    return phase < 100 || (phase >= 200 && phase < 300) ||
           (phase >= 400 && phase < 500);
  case LED_CRITICAL_ERROR:
    phase = elapsed_ms % 1600;
    return phase < 700 || (phase >= 850 && phase < 950);
  default:
    return false;
  }
}
