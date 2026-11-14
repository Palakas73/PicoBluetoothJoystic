#include "usb_xinput.h"
#include "xinput_output.h"
#include "xinput_protocol.h"
#include "btstack_run_loop.h"
#include "led/status_led.h"
#include "pico/time.h"
#include "pico/unique_id.h"
#include "tusb.h"
#include "device/usbd_pvt.h"
#include <stdio.h>
#include <string.h>

static xinput_output_t output;
static xinput_feedback_t feedback;
static btstack_timer_source_t usb_timer;
static bool mounted, suspended, endpoints_open, in_flight;
static uint8_t in_buffer[XINPUT_REPORT_BYTES], out_buffer[32], control_buffer[32];
static uint8_t usb_events;
static uint32_t queued, completed, endpoint_busy, neutral_queued, out_errors;
static uint64_t latest_ble_us, queue_total_us;
static uint32_t queue_last_us, queue_max_us, queued_from_ble;
enum { EV_MOUNT=1, EV_UNMOUNT=2, EV_SUSPEND=4, EV_RESUME=8 };

static void try_send(bool arrival) {
  if (!mounted || suspended || !endpoints_open) return;
  uint8_t report[XINPUT_REPORT_BYTES];
  xinput_output_kind_t kind = xinput_output_peek(&output, report);
  if (kind == XINPUT_OUTPUT_NONE) return;
  if (in_flight || !usbd_edpt_claim(0, XINPUT_EP_IN)) {
    if (arrival) endpoint_busy++;
    return;
  }
  memcpy(in_buffer, report, sizeof(in_buffer));
  if (!usbd_edpt_xfer(0, XINPUT_EP_IN, in_buffer, sizeof(in_buffer))) {
    usbd_edpt_release(0, XINPUT_EP_IN);
    if (arrival) endpoint_busy++;
    return;
  }
  in_flight = true;
  xinput_output_mark_queued(&output, kind, report);
  queued++;
  if (kind == XINPUT_OUTPUT_NEUTRAL) neutral_queued++;
  else if (latest_ble_us) {
    uint32_t elapsed = (uint32_t)(time_us_64() - latest_ble_us);
    queue_last_us = elapsed;
    if (elapsed > queue_max_us) queue_max_us = elapsed;
    queue_total_us += elapsed;
    queued_from_ble++;
  }
}
static bool arm_out(uint8_t rhport) {
  if (!usbd_edpt_claim(rhport, XINPUT_EP_OUT)) return false;
  if (usbd_edpt_xfer(rhport, XINPUT_EP_OUT, out_buffer, sizeof(out_buffer)))
    return true;
  usbd_edpt_release(rhport, XINPUT_EP_OUT);
  out_errors++;
  return false;
}
static void timer_cb(btstack_timer_source_t *ts) {
  (void)ts;
  tud_task();
  try_send(false);
  uint8_t events = usb_events;
  usb_events = 0;
  if (events & EV_UNMOUNT) printf("[XUSB] unmounted\r\n");
  if (events & EV_SUSPEND) printf("[XUSB] suspended\r\n");
  if (events & EV_MOUNT) printf("[XUSB] mounted\r\n");
  if (events & EV_RESUME) printf("[XUSB] resumed\r\n");
  btstack_run_loop_set_timer(&usb_timer, 1);
  btstack_run_loop_add_timer(&usb_timer);
}
void usb_xinput_init(void) { xinput_output_init(&output); }
void usb_xinput_start(void) {
  btstack_run_loop_set_timer_handler(&usb_timer, timer_cb);
  btstack_run_loop_set_timer(&usb_timer, 1);
  btstack_run_loop_add_timer(&usb_timer);
}
void usb_xinput_submit(const controller_state_t *state, uint64_t ble_received_us) {
  if (xinput_output_submit(&output, state)) {
    latest_ble_us = ble_received_us;
    try_send(true);
  }
}
void usb_xinput_disconnect(void) {
  xinput_output_disconnect(&output);
  latest_ble_us = 0;
  try_send(false);
}
bool usb_xinput_mounted(void) { return mounted && !suspended; }
void usb_xinput_print_stats(void) {
  printf("[STATS-XUSB] mounted=%u suspended=%u queued=%lu sent=%lu "
         "busy=%lu stale=%lu neutral=%lu queue_us=%lu/%lu/%lu "
         "out_error=%lu rumble=%lu led=%lu malformed=%lu\r\n",
         mounted, suspended, (unsigned long)queued, (unsigned long)completed,
         (unsigned long)endpoint_busy, (unsigned long)output.stale_replaced,
         (unsigned long)neutral_queued, (unsigned long)queue_last_us,
         (unsigned long)(queued_from_ble ? queue_total_us/queued_from_ble : 0),
         (unsigned long)queue_max_us, (unsigned long)out_errors,
         (unsigned long)feedback.rumble_count, (unsigned long)feedback.led_count,
         (unsigned long)feedback.malformed_count);
}

void tud_mount_cb(void) {
  mounted = true; suspended = false; usb_events |= EV_MOUNT;
  xinput_output_usb_reset(&output); status_led_usb_mounted(true);
}
void tud_umount_cb(void) {
  mounted = false; endpoints_open = false; in_flight = false;
  usb_events |= EV_UNMOUNT; xinput_output_usb_reset(&output);
  status_led_usb_mounted(false);
}
void tud_suspend_cb(bool remote_wakeup_en) {
  (void)remote_wakeup_en;
  suspended = true; usb_events |= EV_SUSPEND; status_led_usb_mounted(false);
}
void tud_resume_cb(void) {
  suspended = false; usb_events |= EV_RESUME;
  xinput_output_usb_reset(&output); status_led_usb_mounted(true);
}

uint8_t const *tud_descriptor_device_cb(void) { return xinput_device_descriptor; }
uint8_t const *tud_descriptor_configuration_cb(uint8_t index) {
  return index == 0 ? xinput_config_descriptor : NULL;
}
uint16_t const *tud_descriptor_string_cb(uint8_t index, uint16_t langid) {
  (void)langid;
  static uint16_t desc[32];
  static char serial[PICO_UNIQUE_BOARD_ID_SIZE_BYTES * 2 + 1];
  if (!serial[0]) pico_get_unique_board_id_string(serial, sizeof(serial));
  if (index == 0) { desc[0] = 0x0304; desc[1] = 0x0409; return desc; }
  if (index == 0xee) return xinput_os_string_descriptor;
  const char *str = index == 1 ? "PicoBluetoothJoystic" :
                    index == 2 ? "PicoBluetoothJoystic XInput" :
                    index == 3 ? serial : NULL;
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
  xinput_request_kind_t kind = xinput_classify_request(request->bmRequestType,
      request->bRequest, request->wValue, request->wIndex, request->wLength);
  if (kind == XINPUT_REQUEST_OS_COMPAT)
    return tud_control_xfer(rhport, request,
        (void *)xinput_os_compat_descriptor, XINPUT_OS_COMPAT_BYTES);
  if (kind == XINPUT_REQUEST_GET_DEVICE_ID) {
    static uint8_t id[4];
    pico_unique_board_id_t uid;
    pico_get_unique_board_id(&uid);
    memcpy(id, uid.id, sizeof(id));
    return tud_control_xfer(rhport, request, (void *)id, sizeof(id));
  }
  return false;
}

static void driver_init(void) { endpoints_open = false; in_flight = false; }
static bool driver_deinit(void) { endpoints_open = false; return true; }
static void driver_reset(uint8_t rhport) {
  (void)rhport; endpoints_open = false; in_flight = false;
  xinput_output_usb_reset(&output);
}
static uint16_t driver_open(uint8_t rhport,
    tusb_desc_interface_t const *itf, uint16_t max_len) {
  if (max_len < XINPUT_CONFIG_BYTES - 9 || itf->bInterfaceClass != 0xff ||
      itf->bInterfaceSubClass != 0x5d || itf->bInterfaceProtocol != 1 ||
      itf->bInterfaceNumber != 0 || itf->bNumEndpoints != 2) return 0;
  const uint8_t *p = (const uint8_t *)itf;
  if (p[9] != 17 || p[10] != 0x21) return 0;
  tusb_desc_endpoint_t const *ep_in = (tusb_desc_endpoint_t const *)(p + 26);
  tusb_desc_endpoint_t const *ep_out = (tusb_desc_endpoint_t const *)(p + 33);
  if (ep_in->bEndpointAddress != XINPUT_EP_IN ||
      ep_out->bEndpointAddress != XINPUT_EP_OUT ||
      !usbd_edpt_open(rhport, ep_in) || !usbd_edpt_open(rhport, ep_out)) return 0;
  endpoints_open = true;
  if (!arm_out(rhport)) return 0;
  return XINPUT_CONFIG_BYTES - 9;
}
static bool driver_control(uint8_t rhport, uint8_t stage,
    tusb_control_request_t const *request) {
  xinput_request_kind_t kind = xinput_classify_request(request->bmRequestType,
      request->bRequest, request->wValue, request->wIndex, request->wLength);
  if (kind == XINPUT_REQUEST_GET_REPORT) {
    if (stage != CONTROL_STAGE_SETUP) return true;
    const uint8_t *report = output.pending_neutral || !output.have_last ?
        output.neutral : output.last_queued;
    memcpy(control_buffer, report, XINPUT_REPORT_BYTES);
    return tud_control_xfer(rhport, request, control_buffer, XINPUT_REPORT_BYTES);
  }
  if (kind == XINPUT_REQUEST_SET_REPORT) {
    if (stage == CONTROL_STAGE_SETUP)
      return tud_control_xfer(rhport, request, control_buffer, request->wLength);
    if (stage == CONTROL_STAGE_DATA)
      xinput_feedback_parse(&feedback, control_buffer, request->wLength);
    return true;
  }
  return false;
}
static bool driver_xfer(uint8_t rhport, uint8_t ep_addr,
    xfer_result_t result, uint32_t n) {
  if (ep_addr == XINPUT_EP_IN) {
    in_flight = false;
    if (result == XFER_RESULT_SUCCESS && n == XINPUT_REPORT_BYTES) completed++;
    else out_errors++;
    try_send(false);
    return true;
  }
  if (ep_addr == XINPUT_EP_OUT) {
    if (result == XFER_RESULT_SUCCESS)
      xinput_feedback_parse(&feedback, out_buffer, n);
    else out_errors++;
    return arm_out(rhport);
  }
  return false;
}
usbd_class_driver_t const *usbd_app_driver_get_cb(uint8_t *count) {
  static const usbd_class_driver_t driver = {
    .name = "Pico XUSB", .init = driver_init, .deinit = driver_deinit,
    .reset = driver_reset, .open = driver_open,
    .control_xfer_cb = driver_control, .xfer_cb = driver_xfer, .sof = NULL
  };
  *count = 1;
  return &driver;
}
