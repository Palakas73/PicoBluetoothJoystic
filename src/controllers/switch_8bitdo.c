#include "switch_8bitdo.h"
#include <string.h>

static uint16_t u12(const uint8_t *p) { return (uint16_t)p[0] | ((uint16_t)(p[1] & 0x0f) << 8); }
static int32_t axis12(uint16_t value) { return value < 2048 ? ((int32_t)value-2048)*16 : ((int32_t)value-2048)*32767/2047; }
static uint16_t packed_y(const uint8_t *p) { return (p[1] >> 4) | ((uint16_t)p[2] << 4); }
static int32_t axis16(const uint8_t *p) {
  return (int32_t)((uint16_t)p[0] | ((uint16_t)p[1] << 8)) - 32768;
}
static bool simple_report(const uint8_t *d, size_t n, controller_state_t *s) {
  if (n != 12 || d[3] > 8) return false;
  controller_state_neutral(s);
  /* HID simple mode observed in the Ultimate 2C capture. Face bit names
   * remain provisional until isolated held-button captures confirm them. */
  if (d[1] & 0x01) s->buttons |= CTRL_A;
  if (d[1] & 0x02) s->buttons |= CTRL_B;
  if (d[1] & 0x04) s->buttons |= CTRL_X;
  if (d[1] & 0x08) s->buttons |= CTRL_Y;
  if (d[1] & 0x10) s->buttons |= CTRL_LB;
  if (d[1] & 0x20) s->buttons |= CTRL_RB;
  if (d[1] & 0x40) s->left_trigger = 65535;
  if (d[1] & 0x80) s->right_trigger = 65535;
  if (d[2] & 0x01) s->buttons |= CTRL_VIEW;
  if (d[2] & 0x02) s->buttons |= CTRL_MENU;
  if (d[2] & 0x04) s->buttons |= CTRL_L3;
  if (d[2] & 0x08) s->buttons |= CTRL_R3;
  if (d[2] & 0x10) s->buttons |= CTRL_GUIDE;
  s->dpad = d[3];
  s->left_x = axis16(d + 4);
  /* Normalized Y grows downward; XUSB serialization flips it once. */
  s->left_y = axis16(d + 6);
  s->right_x = axis16(d + 8);
  s->right_y = -axis16(d + 10);
  return true;
}
static void buttons(controller_state_t *s, uint8_t b0, uint8_t b1, uint8_t b2) {
  /* Switch face: B bottom -> A, A right -> B, Y left -> X, X top -> Y. */
  if (b0 & 0x01) s->buttons |= CTRL_X; /* physical Y -> X */
  if (b0 & 0x02) s->buttons |= CTRL_Y; /* physical X -> Y */
  if (b0 & 0x04) s->buttons |= CTRL_A; /* physical B -> A */
  if (b0 & 0x08) s->buttons |= CTRL_B; /* physical A -> B */
  if (b1 & 1) s->buttons |= CTRL_VIEW;
  if (b1 & 2) s->buttons |= CTRL_MENU;
  if (b1 & 4) s->buttons |= CTRL_R3;
  if (b1 & 8) s->buttons |= CTRL_L3;
  if (b1 & 16) s->buttons |= CTRL_GUIDE;
  if (b2 & 64) s->buttons |= CTRL_LB;
  if (b0 & 64) s->buttons |= CTRL_RB;
}
static uint8_t hat(uint8_t d) {
  /* Switch D-pad nibble: down=0, right=1, up=2, left=3, diagonals. */
  static const uint8_t map[16] = {8,4,0,8,2,3,1,8,6,5,7,8,8,8,8,8};
  return map[d & 0x0f];
}
bool switch_8bitdo_parse_report(const uint8_t *d, size_t n,
                                controller_state_t *s,
                                switch_parser_stats_t *st) {
  if (st) st->reports_received++;
  /* 0x30 + timer/status/buttons + four packed 12-bit stick values. */
  if (!d || !s || n < 12) {
    if (st) st->reports_rejected++;
    return false;
  }
  if (d[0] == 0x3f) {
    if (!simple_report(d, n, s)) {
      if (st) st->reports_rejected++;
      return false;
    }
    if (st) st->reports_processed++;
    return true;
  }
  if (d[0] != 0x30) {
    if (st) st->reports_rejected++;
    return false;
  }
  controller_state_neutral(s);
  buttons(s, d[3], d[4], d[5]);
  s->dpad = hat((uint8_t)(d[5] & 0x0f));
  s->left_x = axis12(u12(d + 6));
  s->left_y = -axis12(packed_y(d + 6));
  s->right_x = axis12(u12(d + 9));
  s->right_y = -axis12(packed_y(d + 9));
  /* ZL/ZR are often digital in compatible 8BitDo reports. */
  if (d[5] & 0x80) s->left_trigger = 65535;
  if (d[3] & 0x80) s->right_trigger = 65535;
  if (st) st->reports_processed++;
  return true;
}
