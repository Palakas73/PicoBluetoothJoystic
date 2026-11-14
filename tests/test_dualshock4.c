#include "controllers/dualshock4.h"
#include "controllers/classic_profile.h"
#include "usb/xinput_report.h"
#include <assert.h>
#include <string.h>

static void put_crc(uint8_t *p) {
  uint32_t crc = ds4_input_crc32(p, 74);
  p[74] = (uint8_t)crc; p[75] = (uint8_t)(crc >> 8);
  p[76] = (uint8_t)(crc >> 16); p[77] = (uint8_t)(crc >> 24);
}

int main(void) {
  controller_state_t s; ds4_parser_stats_t st = {0};
  ds4_report_debug_t debug;
  uint8_t basic[10] = {0x01, 128,128,128,128, 8,0,0,0,0};
  assert(ds4_parse_bluetooth_report_debug(basic, sizeof(basic), &s, &st, &debug));
  assert(debug.report_id == 0x01 && debug.axes_valid && !debug.crc_checked);
  assert(debug.axis_bytes[0] == 128 && debug.axis_bytes[3] == 128);
  assert(s.left_x == 0 && s.left_y == 0 && s.dpad == 8);
  basic[1] = 0; basic[2] = 255; basic[3] = 0; basic[4] = 255;
  basic[5] = (uint8_t)(8 | 0xf0); /* All four face buttons, neutral hat */
  basic[6] = 0xf3; basic[7] = 0x03;
  basic[8] = 255; basic[9] = 127;
  assert(ds4_parse_bluetooth_report(basic, sizeof(basic), &s, &st));
  assert(s.left_x == -32768 && s.left_y == 32766);
  assert(s.right_x == -32768 && s.right_y == 32766);
  assert((s.buttons & (CTRL_A|CTRL_B|CTRL_X|CTRL_Y|CTRL_LB|CTRL_RB|CTRL_VIEW|CTRL_MENU|CTRL_L3|CTRL_R3|CTRL_GUIDE)) ==
         (CTRL_A|CTRL_B|CTRL_X|CTRL_Y|CTRL_LB|CTRL_RB|CTRL_VIEW|CTRL_MENU|CTRL_L3|CTRL_R3|CTRL_GUIDE));
  assert(s.left_trigger == 65535 && s.right_trigger == 32639);
  basic[2] = 0; basic[4] = 0; /* Up on both sticks -> positive XInput Y. */
  assert(ds4_parse_bluetooth_report(basic, sizeof(basic), &s, &st));
  uint8_t xinput[XINPUT_REPORT_BYTES];
  xinput_report_encode(&s, xinput);
  assert(xinput[8] == 0xff && xinput[9] == 0x7f);
  assert(xinput[12] == 0xff && xinput[13] == 0x7f);
  assert(!ds4_parse_bluetooth_report_debug(basic, 0, &s, &st, &debug));
  assert(debug.reject_reason == DS4_REJECT_LENGTH && !debug.axes_valid);
  assert(!ds4_parse_bluetooth_report(basic, 9, &s, &st));
  basic[0] = 0x30;
  assert(!ds4_parse_bluetooth_report(basic, sizeof(basic), &s, &st));

  uint8_t full[78] = {0}; full[0] = 0x11;
  full[3]=128; full[4]=128; full[5]=128; full[6]=128;
  full[7]=8; full[8]=0x10; full[9]=0x01;
  full[10]=42; full[11]=200; put_crc(full);
  assert(ds4_parse_bluetooth_report_debug(full, sizeof(full), &s, &st, &debug));
  assert(debug.report_id == 0x11 && debug.axes_valid && debug.crc_checked && debug.crc_valid);
  assert(debug.axis_bytes[0] == 128 && debug.axis_bytes[3] == 128);
  assert((s.buttons & CTRL_VIEW) && (s.buttons & CTRL_GUIDE));
  assert(s.left_trigger == 42u*257u && s.right_trigger == 200u*257u);
  full[77] ^= 1;
  assert(!ds4_parse_bluetooth_report_debug(full, sizeof(full), &s, &st, &debug));
  assert(debug.reject_reason == DS4_REJECT_CRC && debug.crc_checked && !debug.crc_valid);
  assert(debug.axes_valid && debug.axis_bytes[0] == 128);
  assert(st.rejected_crc == 1);
  assert(!ds4_parse_bluetooth_report(full, 77, &s, &st));
  assert(st.rejected_length == 3);

  const uint8_t descriptor[] = {0x06,0x00,0xff,0x09,0x01,0xa1,0x01,
    0x85,0x01,0x85,0x11,0x85,0x02,0xc0};
  assert(ds4_report_descriptor_signature(descriptor, sizeof(descriptor)));
  assert(classic_profile_from_descriptor(descriptor,sizeof(descriptor)) == CLASSIC_PROFILE_DUALSHOCK4);
  assert(classic_name_is_ds4_hint((const uint8_t *)"Wireless Controller",20));
  assert(classic_name_is_candidate((const uint8_t *)"Wireless Controller",20));
  assert(!classic_name_is_candidate((const uint8_t *)"Unknown Pad",11));
  return 0;
}
