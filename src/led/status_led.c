#include "status_led.h"
#include "btstack_run_loop.h"
#include "pico/cyw43_arch.h"
#include "pico/time.h"
#include <stdio.h>

static btstack_timer_source_t led_timer;
static status_led_state_t current = LED_BOOT, requested = LED_SCANNING;
static status_led_state_t ble_state = LED_SCANNING;
static uint32_t state_start_ms;
static bool output, initialized, usb_attached, critical_latched;
typedef struct { uint8_t count; uint16_t on_ms, off_ms; } feedback_t;
enum { FEEDBACK_QUEUE_SIZE = 8 };
static feedback_t feedback_queue[FEEDBACK_QUEUE_SIZE], active_feedback;
static uint8_t feedback_read, feedback_write, feedback_count;
static uint32_t feedback_start_ms, feedback_dropped;
static bool feedback_active;

static const char *name(status_led_state_t s) {
  static const char *const names[] = {"boot", "scan", "pairing", "ready",
                                     "recoverable-error", "critical-error"};
  return (unsigned)s < sizeof(names) / sizeof(names[0]) ? names[s] : "?";
}

static void led_timeout(btstack_timer_source_t *ts) {
  (void)ts;
  uint32_t now = to_ms_since_boot(get_absolute_time());
  if ((current == LED_BOOT && now - state_start_ms >= 320) ||
      (current != LED_BOOT && current != requested)) {
    current = requested;
    state_start_ms = now;
    printf("[LED] state changed: %s\r\n", name(current));
  }
  if (!critical_latched && !feedback_active && feedback_count) {
    active_feedback = feedback_queue[feedback_read];
    feedback_read = (feedback_read + 1u) % FEEDBACK_QUEUE_SIZE;
    feedback_count--;
    feedback_start_ms = now;
    feedback_active = true;
  }
  bool next;
  if (!critical_latched && feedback_active) {
    uint32_t unit = active_feedback.on_ms + active_feedback.off_ms;
    uint32_t elapsed = now - feedback_start_ms;
    uint32_t total = unit * active_feedback.count;
    if (elapsed >= total) {
      feedback_active = false;
      next = status_led_pattern_output(current, now - state_start_ms);
    } else next = elapsed % unit < active_feedback.on_ms;
  } else next = status_led_pattern_output(current, now - state_start_ms);
  if (next != output) {
    cyw43_arch_gpio_put(CYW43_WL_GPIO_LED_PIN, next);
    output = next;
  }
  btstack_run_loop_set_timer(&led_timer, 25);
  btstack_run_loop_add_timer(&led_timer);
}

void status_led_init(void) {
  initialized = true;
  state_start_ms = to_ms_since_boot(get_absolute_time());
  output = false;
  current = LED_BOOT;
  requested = LED_SCANNING;
  ble_state = LED_SCANNING;
  btstack_run_loop_set_timer_handler(&led_timer, led_timeout);
  btstack_run_loop_set_timer(&led_timer, 1);
  btstack_run_loop_add_timer(&led_timer);
  printf("[LED] state changed: boot\r\n");
}

void status_led_set(status_led_state_t state) {
  if (!initialized || critical_latched) return;
  if (state == LED_CRITICAL_ERROR) critical_latched = true;
  ble_state = state;
  requested = status_led_effective(ble_state, usb_attached);
}

void status_led_usb_mounted(bool mounted) {
  usb_attached = mounted;
  if (critical_latched) return;
  requested = status_led_effective(ble_state, usb_attached);
}

void status_led_autofire_feedback(autofire_event_t event) {
  if (!initialized || critical_latched || event == AF_EVENT_NONE) return;
  feedback_t item = {0, 90, 90};
  switch (event) {
  case AF_EVENT_ARMED: item.count = 3; item.on_ms = item.off_ms = 70; break;
  case AF_EVENT_ENABLED_10: item.count = 2; break;
  case AF_EVENT_DISABLED: item.count = 1; item.on_ms = 500; item.off_ms = 100; break;
  case AF_EVENT_SPEED_5: item.count = 1; break;
  case AF_EVENT_SPEED_10: item.count = 2; break;
  case AF_EVENT_SPEED_15: item.count = 3; break;
  case AF_EVENT_SPEED_20: item.count = 4; break;
  case AF_EVENT_CANCELLED: item.count = 4; item.on_ms = item.off_ms = 65; break;
  default: return;
  }
  if (feedback_count == FEEDBACK_QUEUE_SIZE) { feedback_dropped++; return; }
  feedback_queue[feedback_write] = item;
  feedback_write = (feedback_write + 1u) % FEEDBACK_QUEUE_SIZE;
  feedback_count++;
}

uint32_t status_led_feedback_dropped(void) { return feedback_dropped; }
