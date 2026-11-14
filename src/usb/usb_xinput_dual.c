#include "usb_xinput.h"
#include "xinput_output.h"
#include "xinput_protocol.h"
#include "xinput_dual_protocol.h"
#include "btstack_run_loop.h"
#include "led/status_led.h"
#include "pico/time.h"
#include "pico/unique_id.h"
#include "tusb.h"
#include "device/usbd_pvt.h"
#if BRIDGE_GUIDE_MSC
#include "guide_msc.h"
#endif
#include <stdio.h>
#include <string.h>

enum { SLOT_COUNT = 2 };
static const uint8_t ep_in[SLOT_COUNT] = {0x81, 0x83};
static const uint8_t ep_out[SLOT_COUNT] = {0x02, 0x04};
typedef struct {
  xinput_output_t output;
  xinput_feedback_t feedback;
  uint8_t in_buffer[XINPUT_REPORT_BYTES], out_buffer[32];
  bool endpoints_open, in_flight;
  uint32_t queued, completed, busy, errors, neutral;
  uint64_t received_us;
} slot_t;
static slot_t slots[SLOT_COUNT];
static btstack_timer_source_t usb_timer;
static bool mounted, suspended;
static uint8_t control_buffer[32], usb_events;
enum { EV_MOUNT=1, EV_UNMOUNT=2, EV_SUSPEND=4, EV_RESUME=8 };

static void try_send(uint8_t slot, bool arrival) {
  slot_t *s = &slots[slot];
  if (!mounted || suspended || !s->endpoints_open) return;
  uint8_t report[XINPUT_REPORT_BYTES];
  xinput_output_kind_t kind = xinput_output_peek(&s->output, report);
  if (kind == XINPUT_OUTPUT_NONE) return;
  if (s->in_flight || !usbd_edpt_claim(0, ep_in[slot])) {
    if (arrival) s->busy++;
    return;
  }
  memcpy(s->in_buffer, report, sizeof(report));
  if (!usbd_edpt_xfer(0, ep_in[slot], s->in_buffer, sizeof(s->in_buffer))) {
    usbd_edpt_release(0, ep_in[slot]);
    if (arrival) s->busy++;
    return;
  }
  s->in_flight = true;
  xinput_output_mark_queued(&s->output, kind, report);
  s->queued++;
  if (kind == XINPUT_OUTPUT_NEUTRAL) s->neutral++;
}
static bool arm_out(uint8_t rhport, uint8_t slot) {
  slot_t *s = &slots[slot];
  if (!usbd_edpt_claim(rhport, ep_out[slot])) return false;
  if (usbd_edpt_xfer(rhport, ep_out[slot], s->out_buffer,
                    sizeof(s->out_buffer))) return true;
  usbd_edpt_release(rhport, ep_out[slot]);
  s->errors++;
  return false;
}
static void timer_cb(btstack_timer_source_t *ts) {
  (void)ts;
  tud_task();
  for (uint8_t i = 0; i < SLOT_COUNT; i++) try_send(i, false);
  uint8_t events = usb_events;
  usb_events = 0;
  if (events) printf("[XUSB-DUAL] USB events=0x%02x mounted=%u\r\n", events, mounted);
  btstack_run_loop_set_timer(&usb_timer, 1);
  btstack_run_loop_add_timer(&usb_timer);
}
void usb_xinput_init(void) {
  memset(slots, 0, sizeof(slots));
  for (uint8_t i = 0; i < SLOT_COUNT; i++) xinput_output_init(&slots[i].output);
}
void usb_xinput_start(void) {
  btstack_run_loop_set_timer_handler(&usb_timer, timer_cb);
  btstack_run_loop_set_timer(&usb_timer, 1);
  btstack_run_loop_add_timer(&usb_timer);
}
void usb_xinput_submit_slot(uint8_t slot, const controller_state_t *state,
                            uint64_t received_us) {
  if (slot >= SLOT_COUNT || !state) return;
  slot_t *s = &slots[slot];
  if (xinput_output_submit(&s->output, state)) {
    s->received_us = received_us;
    try_send(slot, true);
  }
}
void usb_xinput_disconnect_slot(uint8_t slot) {
  if (slot >= SLOT_COUNT) return;
  xinput_output_disconnect(&slots[slot].output);
  slots[slot].received_us = 0;
  try_send(slot, false);
}
void usb_xinput_submit(const controller_state_t *s, uint64_t t) {
  usb_xinput_submit_slot(0, s, t);
}
void usb_xinput_disconnect(void) { usb_xinput_disconnect_slot(0); }
bool usb_xinput_mounted(void) { return mounted && !suspended; }
void usb_xinput_print_stats(void) {
  for (uint8_t i = 0; i < SLOT_COUNT; i++) {
    slot_t *s = &slots[i];
    printf("[STATS-XUSB] slot=%u mounted=%u open=%u queued=%lu sent=%lu busy=%lu stale=%lu neutral=%lu errors=%lu rumble=%lu\r\n",
           i, mounted, s->endpoints_open, (unsigned long)s->queued,
           (unsigned long)s->completed, (unsigned long)s->busy,
           (unsigned long)s->output.stale_replaced, (unsigned long)s->neutral,
           (unsigned long)s->errors, (unsigned long)s->feedback.rumble_count);
  }
#if BRIDGE_GUIDE_MSC
  uint32_t calls,bytes,errors,writes,total_us,max_us;
  guide_msc_get_stats(&calls,&bytes,&errors,&writes,&total_us,&max_us);
  printf("[STATS-MSC] reads=%lu bytes=%lu errors=%lu writes_rejected=%lu read_us_total=%lu read_us_max=%lu\r\n",
         (unsigned long)calls,(unsigned long)bytes,(unsigned long)errors,
         (unsigned long)writes,(unsigned long)total_us,(unsigned long)max_us);
#endif
}
void tud_mount_cb(void) {
  mounted = true; suspended = false; usb_events |= EV_MOUNT;
  for (uint8_t i = 0; i < SLOT_COUNT; i++) xinput_output_usb_reset(&slots[i].output);
  status_led_usb_mounted(true);
}
void tud_umount_cb(void) {
  mounted = false; usb_events |= EV_UNMOUNT;
  for (uint8_t i = 0; i < SLOT_COUNT; i++) {
    slots[i].endpoints_open = false; slots[i].in_flight = false;
    xinput_output_usb_reset(&slots[i].output);
  }
  status_led_usb_mounted(false);
}
void tud_suspend_cb(bool remote_wakeup_en) {
  (void)remote_wakeup_en;
  suspended = true; usb_events |= EV_SUSPEND; status_led_usb_mounted(false);
}
void tud_resume_cb(void) {
  suspended = false; usb_events |= EV_RESUME;
  for (uint8_t i = 0; i < SLOT_COUNT; i++) xinput_output_usb_reset(&slots[i].output);
  status_led_usb_mounted(true);
}
uint8_t const *tud_descriptor_device_cb(void) { return xinput_dual_device_descriptor; }
uint8_t const *tud_descriptor_configuration_cb(uint8_t index) {
  return index == 0 ? xinput_dual_config_descriptor : NULL;
}
uint16_t const *tud_descriptor_string_cb(uint8_t index, uint16_t langid) {
  (void)langid;
  static uint16_t desc[32];
  static char serial[PICO_UNIQUE_BOARD_ID_SIZE_BYTES * 2 + 1];
  if (!serial[0]) pico_get_unique_board_id_string(serial, sizeof(serial));
  if (index == 0) { desc[0] = 0x0304; desc[1] = 0x0409; return desc; }
  if (index == 0xee) return xinput_os_string_descriptor;
  const char *str = index == 1 ? "PicoBluetoothJoystic" :
                    index == 2 ? "Pico Dual XInput + Guide" :
                    index == 3 ? serial :
                    index == 4 ? "PICO_GUIDE Read Only" : NULL;
  if (!str) return NULL;
  uint8_t len = (uint8_t)strlen(str);
  if (len > 31) len = 31;
  desc[0] = (uint16_t)(0x0300 | (2u * len + 2u));
  for (uint8_t i = 0; i < len; i++) desc[i+1] = (uint8_t)str[i];
  return desc;
}
bool tud_vendor_control_xfer_cb(uint8_t rhport, uint8_t stage,
    tusb_control_request_t const *request) {
  if (stage != CONTROL_STAGE_SETUP) return true;
  if (request->bmRequestType == 0xc0 && request->bRequest == XINPUT_OS_VENDOR_CODE &&
      request->wValue == 0 && request->wIndex == 4 && request->wLength)
    return tud_control_xfer(rhport, request, (void *)xinput_dual_os_compat_descriptor,
                            XINPUT_DUAL_OS_COMPAT_BYTES);
  if (request->bmRequestType == 0xc0 && request->bRequest == 1 &&
      request->wValue == 0 && request->wIndex == 0 && request->wLength) {
    static uint8_t id[4]; pico_unique_board_id_t uid;
    pico_get_unique_board_id(&uid); memcpy(id, uid.id, sizeof(id));
    return tud_control_xfer(rhport, request, id, sizeof(id));
  }
  return false;
}
static void driver_init(void) {
  for (uint8_t i = 0; i < SLOT_COUNT; i++) slots[i].endpoints_open = false;
}
static bool driver_deinit(void) { driver_init(); return true; }
static void driver_reset(uint8_t rhport) {
  (void)rhport;
  for (uint8_t i = 0; i < SLOT_COUNT; i++) {
    slots[i].endpoints_open = false; slots[i].in_flight = false;
    xinput_output_usb_reset(&slots[i].output);
  }
}
static uint16_t driver_open(uint8_t rhport, tusb_desc_interface_t const *itf,
                            uint16_t max_len) {
  if (max_len < XINPUT_DUAL_INTERFACE_BYTES || itf->bInterfaceNumber >= SLOT_COUNT ||
      itf->bInterfaceClass != 0xff || itf->bInterfaceSubClass != 0x5d ||
      itf->bInterfaceProtocol != 1 || itf->bNumEndpoints != 2) return 0;
  uint8_t slot = itf->bInterfaceNumber;
  const uint8_t *p = (const uint8_t *)itf;
  if (p[9] != 17 || p[10] != 0x21) return 0;
  tusb_desc_endpoint_t const *in = (tusb_desc_endpoint_t const *)(p + 26);
  tusb_desc_endpoint_t const *out = (tusb_desc_endpoint_t const *)(p + 33);
  if (in->bEndpointAddress != ep_in[slot] || out->bEndpointAddress != ep_out[slot] ||
      !usbd_edpt_open(rhport, in) || !usbd_edpt_open(rhport, out)) return 0;
  slots[slot].endpoints_open = true;
  if (!arm_out(rhport, slot)) return 0;
  return XINPUT_DUAL_INTERFACE_BYTES;
}
static bool driver_control(uint8_t rhport, uint8_t stage,
                           tusb_control_request_t const *request) {
  uint8_t slot = (uint8_t)request->wIndex;
  if (request->wIndex >= SLOT_COUNT) return false;
  if (request->bmRequestType == 0xa1 && request->bRequest == 1 &&
      request->wValue == 0x0100 && request->wLength) {
    if (stage != CONTROL_STAGE_SETUP) return true;
    xinput_output_t *o = &slots[slot].output;
    const uint8_t *report = o->pending_neutral || !o->have_last ? o->neutral : o->last_queued;
    memcpy(control_buffer, report, XINPUT_REPORT_BYTES);
    return tud_control_xfer(rhport, request, control_buffer, XINPUT_REPORT_BYTES);
  }
  if (request->bmRequestType == 0x21 && request->bRequest == 9 &&
      request->wValue == 0x0200 && request->wLength && request->wLength <= sizeof(control_buffer)) {
    if (stage == CONTROL_STAGE_SETUP)
      return tud_control_xfer(rhport, request, control_buffer, request->wLength);
    if (stage == CONTROL_STAGE_DATA)
      xinput_feedback_parse(&slots[slot].feedback, control_buffer, request->wLength);
    return true;
  }
  return false;
}
static bool driver_xfer(uint8_t rhport, uint8_t ep_addr,
                        xfer_result_t result, uint32_t n) {
  for (uint8_t i = 0; i < SLOT_COUNT; i++) {
    slot_t *s = &slots[i];
    if (ep_addr == ep_in[i]) {
      s->in_flight = false;
      if (result == XFER_RESULT_SUCCESS && n == XINPUT_REPORT_BYTES) s->completed++;
      else s->errors++;
      try_send(i, false);
      return true;
    }
    if (ep_addr == ep_out[i]) {
      if (result == XFER_RESULT_SUCCESS)
        xinput_feedback_parse(&s->feedback, s->out_buffer, n);
      else s->errors++;
      return arm_out(rhport, i);
    }
  }
  return false;
}
usbd_class_driver_t const *usbd_app_driver_get_cb(uint8_t *count) {
  static const usbd_class_driver_t driver = {
    .name = "Pico Dual XUSB", .init = driver_init, .deinit = driver_deinit,
    .reset = driver_reset, .open = driver_open,
    .control_xfer_cb = driver_control, .xfer_cb = driver_xfer, .sof = NULL
  };
  *count = 1;
  return &driver;
}
