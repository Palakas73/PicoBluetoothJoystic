#include "controllers/switch_8bitdo.h"
#include "usb/xinput_report.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
static void put12(uint8_t *p, uint16_t v) { p[0] = (uint8_t)v; p[1] = (uint8_t)(v >> 8); }
static int16_t signed_le16(const uint8_t *p) {
  return (int16_t)((uint16_t)p[0] | ((uint16_t)p[1] << 8));
}
int main(void) {
  uint8_t r[14] = {0x30,0,0x20,0x0f,0x0f,0x00,0,0,0,0,0,0,0,0};
  controller_state_t s; switch_parser_stats_t st = {0};
  put12(r+6, 2048); r[8]=128; put12(r+9,2048); r[11]=128;
  assert(switch_8bitdo_parse_report(r,sizeof r,&s,&st));
  assert((s.buttons & CTRL_X) && (s.buttons & CTRL_A) &&
         (s.buttons & CTRL_Y) && (s.buttons & CTRL_B));
  assert(s.left_x == 0 && s.left_y == 0 && s.right_x == 0 && s.right_y == 0);
  r[3]=128; r[4]=0; r[5]=128; r[6]=0; r[7]=0xf0; r[8]=255;
  put12(r+9,4095); r[11]=0;
  assert(switch_8bitdo_parse_report(r,sizeof r,&s,&st));
  assert(s.left_x < -30000 && s.left_y < -30000 && s.right_x > 30000 && s.right_y > 30000);
  assert(s.left_trigger == 65535 && s.right_trigger == 65535);
  r[0]=0x31; assert(!switch_8bitdo_parse_report(r,sizeof r,&s,&st));
  assert(!switch_8bitdo_parse_report(r,5,&s,&st));
  assert(st.reports_processed == 2 && st.reports_rejected == 2);
  /* Actual HIDP packet from 2026-11-08 capture: A1 stripped once. */
  const uint8_t neutral_3f[] = {0x3f,0,0,8,0,0x80,0,0x80,0,0x80,0xf0,0x7f};
  assert(switch_8bitdo_parse_report(neutral_3f,sizeof neutral_3f,&s,&st));
  assert(s.buttons == 0 && s.dpad == 8);
  assert(s.left_x == 0 && s.left_y == 0 && s.right_x == 0);
  const uint8_t y_seen[] = {0x3f,4,0,8,0,0x80,0,0x80,0,0x80,0xf0,0x7f};
  assert(switch_8bitdo_parse_report(y_seen,sizeof y_seen,&s,&st));
  assert(s.buttons & CTRL_X); /* observed bit, physical mapping still provisional */
  const uint8_t triggers_seen[] = {0x3f,0xc0,0,8,0,0x80,0,0x80,0,0x80,0xf0,0x7f};
  assert(switch_8bitdo_parse_report(triggers_seen,sizeof triggers_seen,&s,&st));
  assert(s.left_trigger == 65535 && s.right_trigger == 65535);
  const uint8_t left_moved[] = {0x3f,0,0,8,0xe0,0xca,0x20,0x09,0,0x80,0xf0,0x7f};
  assert(switch_8bitdo_parse_report(left_moved,sizeof left_moved,&s,&st));
  assert(s.left_x > 0 && s.left_y < 0 && s.right_x == 0);
  const uint8_t right_moved[] = {0x3f,0,0,8,0,0x80,0,0x80,0x60,0x18,0xd0,0xde};
  assert(switch_8bitdo_parse_report(right_moved,sizeof right_moved,&s,&st));
  assert(s.right_x < 0 && s.right_y < 0 && s.left_x == 0);
  assert(!switch_8bitdo_parse_report(right_moved,11,&s,&st));
  uint8_t vertical[] = {0x3f,0,0,8,0,0x80,0,0,0,0x80,0,0x80};
  uint8_t usb[XINPUT_REPORT_BYTES];
  assert(switch_8bitdo_parse_report(vertical,sizeof vertical,&s,&st));
  assert(s.left_y == -32768); /* up */
  xinput_report_encode(&s,usb);
  assert(signed_le16(usb + 8) == 32767); /* saturated -(-32768) */
  vertical[6]=0; vertical[7]=0x80;
  assert(switch_8bitdo_parse_report(vertical,sizeof vertical,&s,&st));
  assert(s.left_y == 0); /* center */
  xinput_report_encode(&s,usb);
  assert(signed_le16(usb + 8) == 0);
  vertical[6]=0xff; vertical[7]=0xff;
  assert(switch_8bitdo_parse_report(vertical,sizeof vertical,&s,&st));
  assert(s.left_y == 32767); /* down */
  xinput_report_encode(&s,usb);
  assert(signed_le16(usb + 8) == -32767);
  puts("8BitDo Switch parser: OK");
  return 0;
}
