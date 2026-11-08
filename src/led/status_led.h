#pragma once
#include <stdbool.h>
#include <stdint.h>
#include "core/autofire.h"

typedef enum {
  LED_BOOT, LED_SCANNING, LED_PAIRING, LED_READY,
  LED_RECOVERABLE_ERROR, LED_CRITICAL_ERROR
} status_led_state_t;

/* Pure pattern calculation for host tests. */
bool status_led_pattern_output(status_led_state_t state, uint32_t elapsed_ms);
status_led_state_t status_led_effective(status_led_state_t ble_state,
                                       bool usb_mounted);
void status_led_init(void);
void status_led_set(status_led_state_t state);
void status_led_usb_mounted(bool mounted);
/* Bounded, non-blocking shared feedback queue. */
void status_led_autofire_feedback(autofire_event_t event);
uint32_t status_led_feedback_dropped(void);
