#include "gamepad_report.h"

/* One input report, no Report ID. Byte layout is documented in docs/usb-hid.md. */
const uint8_t usb_gamepad_report_descriptor[USB_GAMEPAD_DESCRIPTOR_BYTES] = {
    0x05, 0x01,       /* Generic Desktop */
    0x09, 0x05,       /* Game Pad */
    0xa1, 0x01,       /* Application collection */
    0x16, 0x00, 0x80, /* logical min -32768 */
    0x26, 0xff, 0x7f, /* logical max 32767 */
    0x75, 0x10, 0x95, 0x04,
    0x09, 0x30, 0x09, 0x31, 0x09, 0x33, 0x09, 0x34, /* X,Y,Rx,Ry */
    0x81, 0x02,
    0x15, 0x00, 0x27, 0xff, 0xff, 0x00, 0x00, /* 0..65535 */
    0x75, 0x10, 0x95, 0x02,
    0x09, 0x32, 0x09, 0x35, /* Z,Rz = LT,RT */
    0x81, 0x02,
    0x15, 0x00, 0x25, 0x07, 0x35, 0x00, 0x46, 0x3b, 0x01,
    0x65, 0x14, 0x75, 0x04, 0x95, 0x01,
    0x09, 0x39, 0x81, 0x42, /* hat, null state=8 */
    0x65, 0x00, 0x45, 0x00,
    0x75, 0x04, 0x95, 0x01, 0x81, 0x03, /* padding */
    0x05, 0x09, 0x19, 0x01, 0x29, 0x0c,
    0x15, 0x00, 0x25, 0x01, 0x75, 0x01, 0x95, 0x0c,
    0x81, 0x02, /* 12 buttons */
    0x75, 0x04, 0x95, 0x01, 0x81, 0x03, /* padding */
    0xc0
};
const size_t usb_gamepad_report_descriptor_len =
    sizeof(usb_gamepad_report_descriptor);

static int16_t clamp_axis(int32_t v) {
  if (v < -32768) return -32768;
  if (v > 32767) return 32767;
  return (int16_t)v;
}
static uint16_t clamp_trigger(int32_t v) {
  if (v < 0) return 0;
  if (v > 65535) return 65535;
  return (uint16_t)v;
}
static int16_t apply_deadzone(int16_t v, uint16_t d) {
  if (!d || d >= 32767) return v;
  int32_t magnitude = v < 0 ? -(int32_t)v : v;
  if (magnitude <= d) return 0;
  int32_t limit = v < 0 ? 32768 : 32767;
  int32_t result = (magnitude - d) * limit / (limit - d);
  return (int16_t)(v < 0 ? -result : result);
}
static void put16(uint8_t *out, uint16_t v) {
  out[0] = (uint8_t)v;
  out[1] = (uint8_t)(v >> 8);
}

void gamepad_report_encode(const controller_state_t *s,
                           const gamepad_profile_t *profile,
                           uint8_t out[USB_GAMEPAD_REPORT_BYTES]) {
  uint16_t deadzone = profile ? profile->stick_deadzone : 0;
  put16(out + 0, (uint16_t)apply_deadzone(clamp_axis(s->left_x), deadzone));
  put16(out + 2, (uint16_t)apply_deadzone(clamp_axis(s->left_y), deadzone));
  put16(out + 4, (uint16_t)apply_deadzone(clamp_axis(s->right_x), deadzone));
  put16(out + 6, (uint16_t)apply_deadzone(clamp_axis(s->right_y), deadzone));
  put16(out + 8, clamp_trigger(s->left_trigger));
  put16(out + 10, clamp_trigger(s->right_trigger));
  out[12] = s->dpad <= 8 ? s->dpad : 8;
  uint16_t buttons = s->buttons & 0x07ffu;
  if (s->share) buttons |= 1u << 11;
  put16(out + 13, buttons);
}
