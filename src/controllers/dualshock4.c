#include "dualshock4.h"
#include <string.h>

static uint32_t crc32_byte(uint32_t crc, uint8_t value) {
  crc ^= value;
  for (unsigned bit = 0; bit < 8; ++bit)
    crc = (crc >> 1) ^ ((crc & 1u) ? 0xedb88320u : 0u);
  return crc;
}

uint32_t ds4_input_crc32(const uint8_t *data, size_t length) {
  uint32_t crc = crc32_byte(0xffffffffu, 0xa1u);
  for (size_t i = 0; i < length; ++i) crc = crc32_byte(crc, data[i]);
  return ~crc;
}

static uint32_t get_le32(const uint8_t *p) {
  return (uint32_t)p[0] | ((uint32_t)p[1] << 8) |
         ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

/* DS4 Bluetooth report descriptors use Sony's vendor page and report IDs.
 * This is deliberately only a signature check; the name is not an identity. */
bool ds4_report_descriptor_signature(const uint8_t *d, size_t n) {
  if (!d || n < 8) return false;
  bool vendor_page = false, report_01 = false, report_11 = false;
  bool feature_02 = false;
  for (size_t i = 0; i + 1 < n; ++i) {
    if (d[i] == 0x06 && d[i + 1] == 0x00 && i + 2 < n && d[i + 2] == 0xff)
      vendor_page = true;
    if (d[i] == 0x85 && i + 1 < n) {
      report_01 |= d[i + 1] == 0x01;
      report_11 |= d[i + 1] == 0x11;
      feature_02 |= d[i + 1] == 0x02;
    }
  }
  return vendor_page && report_01 && report_11 && feature_02;
}

static uint8_t hat_to_dpad(uint8_t h) {
  return h <= 7 ? h : 8;
}
static int32_t axis8(uint8_t value) {
  int32_t delta = (int32_t)value - 128;
  return delta < 0 ? delta * 256 : delta * 258;
}

static void map_buttons(controller_state_t *s, uint8_t b0, uint8_t b1,
                        uint8_t b2) {
  /* Physical Sony positions, mapped to Xbox face labels. */
  if (b0 & 0x20) s->buttons |= CTRL_A; /* Cross */
  if (b0 & 0x40) s->buttons |= CTRL_B; /* Circle */
  if (b0 & 0x10) s->buttons |= CTRL_X; /* Square */
  if (b0 & 0x80) s->buttons |= CTRL_Y; /* Triangle */
  if (b1 & 0x01) s->buttons |= CTRL_LB;
  if (b1 & 0x02) s->buttons |= CTRL_RB;
  if (b1 & 0x10) s->buttons |= CTRL_VIEW;
  if (b1 & 0x20) s->buttons |= CTRL_MENU;
  if (b1 & 0x40) s->buttons |= CTRL_L3;
  if (b1 & 0x80) s->buttons |= CTRL_R3;
  if (b2 & 0x01) s->buttons |= CTRL_GUIDE;
}

bool ds4_parse_bluetooth_report_debug(const uint8_t *d, size_t n,
                                      controller_state_t *s,
                                      ds4_parser_stats_t *st,
                                      ds4_report_debug_t *debug) {
  if (debug) {
    *debug = (ds4_report_debug_t){0};
    debug->report_id = d && n ? d[0] : 0;
  }
  if (st) st->received++;
  if (!d || !s) {
    if (st) st->rejected_length++;
    if (debug) debug->reject_reason = DS4_REJECT_NULL;
    return false;
  }
  if (n == 0) {
    if (st) st->rejected_length++;
    if (debug) debug->reject_reason = DS4_REJECT_LENGTH;
    return false;
  }
  size_t axes, buttons, triggers;
  if (d[0] == 0x01) {
    if (n != 10) {
      if (st) st->rejected_length++;
      if (debug) debug->reject_reason = DS4_REJECT_LENGTH;
      return false;
    }
    axes = 1; buttons = 5; triggers = 8;
  } else if (d[0] == 0x11) {
    if (n != 78) {
      if (st) st->rejected_length++;
      if (debug) debug->reject_reason = DS4_REJECT_LENGTH;
      return false;
    }
    axes = 3; buttons = 7; triggers = 10;
    if (debug) {
      memcpy(debug->axis_bytes, d + axes, sizeof(debug->axis_bytes));
      debug->axes_valid = 1;
    }
    uint32_t actual = get_le32(d + n - 4);
    uint32_t calculated = ds4_input_crc32(d, n - 4);
    if (debug) {
      debug->crc_checked = 1;
      debug->crc_valid = calculated == actual;
      debug->crc_calculated = calculated;
      debug->crc_reported = actual;
    }
    if (calculated != actual) {
      if (st) st->rejected_crc++;
      if (debug) debug->reject_reason = DS4_REJECT_CRC;
      return false;
    }
  } else {
    if (st) st->rejected_id++;
    if (debug) debug->reject_reason = DS4_REJECT_ID;
    return false;
  }
  if (debug && !debug->axes_valid) {
    memcpy(debug->axis_bytes, d + axes, sizeof(debug->axis_bytes));
    debug->axes_valid = 1;
  }
  controller_state_neutral(s);
  s->left_x = axis8(d[axes]);
  s->left_y = axis8(d[axes + 1]);
  s->right_x = axis8(d[axes + 2]);
  s->right_y = axis8(d[axes + 3]);
  map_buttons(s, d[buttons], d[buttons + 1], d[buttons + 2]);
  s->dpad = hat_to_dpad(d[buttons] & 0x0f);
  s->left_trigger = (uint16_t)d[triggers] * 257u;
  s->right_trigger = (uint16_t)d[triggers + 1] * 257u;
  if (st) st->accepted++;
  return true;
}

bool ds4_parse_bluetooth_report(const uint8_t *d, size_t n,
                                controller_state_t *s,
                                ds4_parser_stats_t *st) {
  return ds4_parse_bluetooth_report_debug(d, n, s, st, NULL);
}
