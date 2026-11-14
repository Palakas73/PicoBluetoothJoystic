#include "hid_gamepad.h"
#include <string.h>

typedef struct {
  uint16_t page;
  uint32_t min, max;
  int32_t logical_min, logical_max;
  uint8_t size, count, id;
} globals_t;
typedef struct {
  uint8_t page;
  uint32_t min, max;
  int32_t logical_min, logical_max;
  uint8_t size, count, id;
} global_stack_t;
typedef struct {
  uint16_t page, usage;
} local_t;
static uint32_t uval(const uint8_t *p, uint8_t n) {
  uint32_t v = 0;
  for (uint8_t i = 0; i < n; i++)
    v |= (uint32_t)p[i] << (8 * i);
  return v;
}
static int32_t sval(uint32_t v, uint8_t n) {
  if (n && n < 4 && (v & (1u << (n * 8 - 1))))
    v |= ~((1u << (n * 8)) - 1u);
  return (int32_t)v;
}
static uint32_t bits(const uint8_t *p, uint16_t offset, uint8_t width) {
  uint32_t v = 0;
  for (uint8_t i = 0; i < width; i++)
    v |= ((p[(offset + i) >> 3] >> ((offset + i) & 7)) & 1u) << i;
  return v;
}
static int32_t decode_value(uint32_t v, const hid_gamepad_field_t *f) {
  if (f->signed_value && f->bit_size < 32 && (v & (1u << (f->bit_size - 1))))
    v |= ~((1u << f->bit_size) - 1u);
  return (int32_t)v;
}
static int32_t scale_signed_axis(int32_t v, const hid_gamepad_field_t *f) {
  int64_t range = (int64_t)f->logical_max - f->logical_min;
  if (range <= 0)
    return v;
  int64_t clamped = v;
  if (clamped < f->logical_min)
    clamped = f->logical_min;
  if (clamped > f->logical_max)
    clamped = f->logical_max;
  return (int32_t)(((clamped - f->logical_min) * 65535) / range - 32768);
}
static int32_t scale_trigger(int32_t v, const hid_gamepad_field_t *f) {
  int64_t range = (int64_t)f->logical_max - f->logical_min;
  if (range <= 0)
    return v;
  int64_t clamped = v;
  if (clamped < f->logical_min)
    clamped = f->logical_min;
  if (clamped > f->logical_max)
    clamped = f->logical_max;
  return (int32_t)(((clamped - f->logical_min) * 65535) / range);
}
static void map_field(controller_state_t *out, const hid_gamepad_field_t *f,
                      int32_t v) {
  if (f->usage_page == 1) {
    switch (f->usage) {
    case 0x30:
      out->left_x = scale_signed_axis(v, f);
      break;
    case 0x31:
      out->left_y = scale_signed_axis(v, f);
      break;
    case 0x32:
      out->right_x = scale_signed_axis(v, f);
      break;
    case 0x35:
      out->right_y = scale_signed_axis(v, f);
      break;
    case 0x39:
      out->dpad = (v >= 1 && v <= 8) ? (uint8_t)(v - 1) : 8;
      break;
    default:
      break;
    }
  } else if (f->usage_page == 2) {
    if (f->usage == 0xc5)
      out->left_trigger = scale_trigger(v, f);
    else if (f->usage == 0xc4)
      out->right_trigger = scale_trigger(v, f);
  } else if (f->usage_page == 9 && f->usage >= 1 && f->usage <= 15 && v) {
    /* Xbox BLE HID map verified against the public 045e:0b13 descriptor and the
       independent Joypad OS driver. Device-specific usage assignment is
       isolated here. */
    static const uint16_t map[15] = {CTRL_A,     CTRL_B,  0,         CTRL_X,
                                     CTRL_Y,     0,       CTRL_LB,   CTRL_RB,
                                     0,          0,       CTRL_VIEW, CTRL_MENU,
                                     CTRL_GUIDE, CTRL_L3, CTRL_R3};
    out->buttons |= map[f->usage - 1];
    if (f->usage == 16)
      out->share = 1;
  } else if (f->usage_page == 0x0c && f->usage == 0x00b2 && v)
    out->share = 1;
}
bool hid_gamepad_parser_reset(hid_gamepad_parser_t *p, const uint8_t *d,
                              uint16_t n) {
  if (!p || !d || !n)
    return false;
  memset(p, 0, sizeof(*p));
  globals_t g = {0};
  global_stack_t stack[4];
  uint8_t sp = 0;
  local_t l[32];
  uint8_t ln = 0;
  uint16_t usage_min = 0, usage_max = 0;
  bool usage_range = false;
  uint16_t offsets[256] = {0};
  for (uint16_t i = 0; i < n;) {
    uint8_t prefix = d[i++];
    if (prefix == 0xfe)
      return false;
    uint8_t sz = prefix & 3u;
    if (sz == 3)
      sz = 4;
    if (i + sz > n)
      return false;
    uint8_t type = (prefix >> 2) & 3u, tag = (prefix >> 4) & 15u;
    uint32_t v = uval(&d[i], sz);
    i += sz;
    if (type == 1) {
      switch (tag) {
      case 0:
        g.page = (uint16_t)v;
        break;
      case 1:
        g.logical_min = sval(v, sz);
        break;
      case 2:
        g.logical_max = (g.logical_min < 0) ? sval(v, sz) : (int32_t)v;
        break;
      case 7:
        g.size = (uint8_t)v;
        break;
      case 8:
        g.id = (uint8_t)v;
        p->has_report_ids = true;
        break;
      case 9:
        g.count = (uint8_t)v;
        break;
      case 10:
        if (sp >= 4)
          return false;
        stack[sp++] =
            (global_stack_t){(uint8_t)g.page, g.min,  g.max,   g.logical_min,
                             g.logical_max,   g.size, g.count, g.id};
        break;
      case 11:
        if (!sp)
          return false;
        {
          global_stack_t q = stack[--sp];
          g = (globals_t){q.page,        q.min,  q.max,   q.logical_min,
                          q.logical_max, q.size, q.count, q.id};
        }
        break;
      default:
        break;
      }
    } else if (type == 2) {
      if (tag == 0 && ln < 32) {
        l[ln++] =
            (local_t){(uint16_t)(sz == 4 ? (v >> 16) : g.page), (uint16_t)v};
      } else if (tag == 1) {
        usage_min = (uint16_t)v;
        usage_range = true;
      } else if (tag == 2)
        usage_max = (uint16_t)v;
    } else if (type == 0) {
      if (tag == 8) {
        uint16_t bits_total = (uint16_t)g.size * g.count;
        uint16_t start = offsets[g.id];
        if (!g.size || g.size > 32 || !g.count || start + bits_total > 2048)
          return false;
        /* Retain gamepad axes, hat, buttons, and consumer Share fields. */
        bool constant = (v & 1u) != 0, variable = (v & 2u) != 0;
        if (!constant) {
          for (uint8_t k = 0; k < g.count; k++) {
            uint16_t page = g.page,
                     usage = ln ? l[(k < ln) ? k : ln - 1].usage
                                : (usage_range && usage_min + k <= usage_max
                                       ? (uint16_t)(usage_min + k)
                                       : 0);
            if (ln)
              page = l[(k < ln) ? k : ln - 1].page;
            if (p->field_count >= HID_GAMEPAD_MAX_FIELDS)
              return false;
            hid_gamepad_field_t *f = &p->fields[p->field_count++];
            f->usage_page = page;
            f->usage = usage;
            f->bit_offset = start + (uint16_t)k * g.size;
            f->bit_size = g.size;
            f->report_id = g.id;
            f->logical_min = g.logical_min;
            f->logical_max = g.logical_max;
            f->variable = variable;
            f->signed_value = g.logical_min < 0;
          }
        }
        offsets[g.id] = start + bits_total;
        p->input_bits[g.id] = offsets[g.id];
        ln = 0;
      } else if (tag == 10) {
        ln = 0;
        usage_range = false;
        usage_min = usage_max = 0;
      }
    }
  }
  return p->field_count > 0;
}
bool hid_gamepad_parser_decode(hid_gamepad_parser_t *p, uint8_t id,
                               const uint8_t *r, uint16_t n,
                               controller_state_t *out) {
  if (!p || !r || !out)
    return false;
  uint16_t needed = p->input_bits[id];
  if (!needed)
    return false;
  uint16_t payload = (needed + 7) / 8;
  if (p->has_report_ids) {
    if (n != payload + 1 || r[0] != id)
      return false;
    r++;
  } else if (n != payload)
    return false;
  controller_state_neutral(out);
  for (uint8_t i = 0; i < p->field_count; i++) {
    const hid_gamepad_field_t *f = &p->fields[i];
    if (f->report_id != id || !f->variable)
      continue;
    uint32_t raw = bits(r, f->bit_offset, f->bit_size);
    map_field(out, f, decode_value(raw, f));
  }
  p->reports++;
  return true;
}
