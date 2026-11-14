#include "usb_gamepad.h"
#include "gamepad_output.h"
#include "btstack_run_loop.h"
#include "led/status_led.h"
#include "pico/stdio_usb.h"
#include "pico/time.h"
#include "tusb.h"
#include <stdio.h>
#include <string.h>

static gamepad_output_t output;
static gamepad_profile_t profile; /* deadzone disabled by default */
static btstack_timer_source_t usb_timer;
static bool mounted, suspended;
static uint8_t usb_events;
static uint32_t queued, completed, endpoint_busy, neutral_queued;
static uint64_t latest_ble_us, prepare_total_us, queue_total_us;
static uint32_t prepare_last_us, prepare_max_us, queue_last_us, queue_max_us;
static uint32_t prepared_count, queued_from_ble;
static bool first_completed_log, neutral_log;

enum { USB_EVENT_MOUNT = 1, USB_EVENT_UNMOUNT = 2,
       USB_EVENT_SUSPEND = 4, USB_EVENT_RESUME = 8 };

static void try_send(bool input_arrival) {
  if (!mounted || suspended) return;
  uint8_t report[USB_GAMEPAD_REPORT_BYTES];
  gamepad_output_kind_t kind = gamepad_output_peek(&output, report);
  if (kind == GAMEPAD_OUTPUT_NONE) return;
  if (!tud_hid_ready()) {
    if (input_arrival) endpoint_busy++;
    return;
  }
  if (!tud_hid_report(USB_GAMEPAD_REPORT_ID, report, sizeof(report))) {
    if (input_arrival) endpoint_busy++;
    return;
  }
  gamepad_output_mark_queued(&output, kind, report);
  queued++;
  if (kind == GAMEPAD_OUTPUT_NEUTRAL) {
    neutral_queued++;
    neutral_log = true;
  } else if (latest_ble_us) {
    uint32_t elapsed = (uint32_t)(time_us_64() - latest_ble_us);
    queue_last_us = elapsed;
    if (elapsed > queue_max_us) queue_max_us = elapsed;
    queue_total_us += elapsed;
    queued_from_ble++;
  }
}

static void usb_timeout(btstack_timer_source_t *ts) {
  (void)ts;
  tud_task();
  try_send(false);
  /* Defer messages until CDC is open; suspend/unmount cannot transmit them. */
  if (stdio_usb_connected()) {
    uint8_t events = usb_events;
    usb_events = 0;
    if (events & USB_EVENT_UNMOUNT) printf("[USB] unmounted\r\n");
    if (events & USB_EVENT_SUSPEND) printf("[USB] suspended\r\n");
    if (events & USB_EVENT_MOUNT) printf("[USB] mounted\r\n");
    if (events & USB_EVENT_RESUME) printf("[USB] resumed\r\n");
    if (first_completed_log) {
      printf("[GAMEPAD] first report sent\r\n");
      first_completed_log = false;
    }
    if (neutral_log) {
      printf("[GAMEPAD] neutral state queued\r\n");
      neutral_log = false;
    }
  }
  btstack_run_loop_set_timer(&usb_timer, 1);
  btstack_run_loop_add_timer(&usb_timer);
}

void usb_gamepad_init(void) {
  gamepad_output_init(&output);
  profile.stick_deadzone = 0;
}
void usb_gamepad_start(void) {
  btstack_run_loop_set_timer_handler(&usb_timer, usb_timeout);
  btstack_run_loop_set_timer(&usb_timer, 1);
  btstack_run_loop_add_timer(&usb_timer);
}
void usb_gamepad_submit(const controller_state_t *state, uint64_t ble_received_us) {
  if (gamepad_output_submit(&output, state, &profile)) {
    latest_ble_us = ble_received_us;
    uint32_t elapsed = (uint32_t)(time_us_64() - ble_received_us);
    prepare_last_us = elapsed;
    if (elapsed > prepare_max_us) prepare_max_us = elapsed;
    prepare_total_us += elapsed;
    prepared_count++;
    try_send(true);
  }
}
void usb_gamepad_disconnect(void) {
  gamepad_output_disconnect(&output);
  latest_ble_us = 0;
  try_send(false);
}
bool usb_gamepad_mounted(void) { return mounted && !suspended; }
void usb_gamepad_set_deadzone(uint16_t normalized_units) {
  profile.stick_deadzone = normalized_units < 32767 ? normalized_units : 0;
}
void usb_gamepad_print_stats(void) {
  printf("[STATS-USB] mounted=%u suspended=%u queued=%lu sent=%lu "
         "busy=%lu stale=%lu neutral=%lu prep_us=%lu/%lu/%lu "
         "queue_us=%lu/%lu/%lu\r\n",
         mounted, suspended, (unsigned long)queued, (unsigned long)completed,
         (unsigned long)endpoint_busy, (unsigned long)output.stale_replaced,
         (unsigned long)neutral_queued,
         (unsigned long)prepare_last_us,
         (unsigned long)(prepared_count ? prepare_total_us / prepared_count : 0),
         (unsigned long)prepare_max_us,
         (unsigned long)queue_last_us,
         (unsigned long)(queued_from_ble ? queue_total_us / queued_from_ble : 0),
         (unsigned long)queue_max_us);
}

void tud_mount_cb(void) {
  mounted = true;
  suspended = false;
  usb_events |= USB_EVENT_MOUNT;
  gamepad_output_usb_reset(&output);
  status_led_usb_mounted(true);
}
void tud_umount_cb(void) {
  mounted = false;
  usb_events |= USB_EVENT_UNMOUNT;
  gamepad_output_usb_reset(&output);
  status_led_usb_mounted(false);
}
void tud_suspend_cb(bool remote_wakeup_en) {
  (void)remote_wakeup_en;
  suspended = true;
  usb_events |= USB_EVENT_SUSPEND;
  status_led_usb_mounted(false);
}
void tud_resume_cb(void) {
  suspended = false;
  usb_events |= USB_EVENT_RESUME;
  gamepad_output_usb_reset(&output);
  status_led_usb_mounted(true);
}
void tud_hid_report_complete_cb(uint8_t instance, uint8_t const *report,
                                uint16_t len) {
  (void)instance; (void)report; (void)len;
  completed++;
  if (completed == 1) first_completed_log = true;
}
uint16_t tud_hid_get_report_cb(uint8_t instance, uint8_t report_id,
    hid_report_type_t report_type, uint8_t *buffer, uint16_t reqlen) {
  if (instance != 0 || report_id != 0 || report_type != HID_REPORT_TYPE_INPUT ||
      reqlen < USB_GAMEPAD_REPORT_BYTES) return 0;
  memcpy(buffer, output.pending_neutral || !output.have_last ? output.neutral
                                                          : output.last_queued,
         USB_GAMEPAD_REPORT_BYTES);
  return USB_GAMEPAD_REPORT_BYTES;
}
void tud_hid_set_report_cb(uint8_t instance, uint8_t report_id,
    hid_report_type_t report_type, uint8_t const *buffer, uint16_t len) {
  (void)instance; (void)report_id; (void)report_type;
  (void)buffer; (void)len;
}
