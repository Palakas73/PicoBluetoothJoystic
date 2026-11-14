#include "xinput_report.h"
#include <string.h>

static int16_t axis(int32_t value, int invert) {
  if (value < -32768) value = -32768;
  if (value > 32767) value = 32767;
  if (invert) value = -value;
  if (value > 32767) value = 32767;
  return (int16_t)value;
}
static uint8_t trigger(int32_t value) {
  if (value <= 0) return 0;
  if (value >= 65535) return 255;
  return (uint8_t)(((uint32_t)value * 255u + 32767u) / 65535u);
}
static void put16(uint8_t *out, uint16_t value) {
  out[0] = (uint8_t)value;
  out[1] = (uint8_t)(value >> 8);
}

void xinput_report_encode(const controller_state_t *s,
                          uint8_t out[XINPUT_REPORT_BYTES]) {
  memset(out, 0, XINPUT_REPORT_BYTES);
  out[1] = XINPUT_REPORT_BYTES;
  uint16_t b = 0;
  /* D-pad: N, NE, E, SE, S, SW, W, NW; 8 is neutral. */
  if (s->dpad == 0 || s->dpad == 1 || s->dpad == 7) b |= 0x0001;
  if (s->dpad == 3 || s->dpad == 4 || s->dpad == 5) b |= 0x0002;
  if (s->dpad == 5 || s->dpad == 6 || s->dpad == 7) b |= 0x0004;
  if (s->dpad == 1 || s->dpad == 2 || s->dpad == 3) b |= 0x0008;
  if (s->buttons & CTRL_MENU) b |= 0x0010;
  if (s->buttons & CTRL_VIEW) b |= 0x0020;
  if (s->buttons & CTRL_L3) b |= 0x0040;
  if (s->buttons & CTRL_R3) b |= 0x0080;
  if (s->buttons & CTRL_LB) b |= 0x0100;
  if (s->buttons & CTRL_RB) b |= 0x0200;
  if (s->buttons & CTRL_GUIDE) b |= 0x0400;
  if (s->buttons & CTRL_A) b |= 0x1000;
  if (s->buttons & CTRL_B) b |= 0x2000;
  if (s->buttons & CTRL_X) b |= 0x4000;
  if (s->buttons & CTRL_Y) b |= 0x8000;
  put16(out + 2, b);
  out[4] = trigger(s->left_trigger);
  out[5] = trigger(s->right_trigger);
  put16(out + 6, (uint16_t)axis(s->left_x, 0));
  put16(out + 8, (uint16_t)axis(s->left_y, 1));
  put16(out + 10, (uint16_t)axis(s->right_x, 0));
  put16(out + 12, (uint16_t)axis(s->right_y, 1));
  /* XUSB10 has no Share bit. Bytes 14..19 stay reserved/zero. */
}

int xinput_feedback_parse(xinput_feedback_t *f, const uint8_t *d, size_t n) {
  if (!d || n < 3 || n != d[1]) { f->malformed_count++; return 0; }
  if (d[0] == 0 && n == 8) {
    f->left_motor = d[3];
    f->right_motor = d[4];
    f->rumble_count++;
    return 1;
  }
  if (d[0] == 1 && n == 3) {
    f->led_pattern = d[2];
    f->led_count++;
    return 1;
  }
  f->malformed_count++;
  return 0;
}
