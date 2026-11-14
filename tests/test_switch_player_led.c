#include "controllers/switch_player_led.h"
#include <assert.h>
#include <string.h>
int main(void) {
  uint8_t report[SWITCH_PLAYER_LED_REPORT_LEN];
  assert(switch_player_led_build(0, 17, report) == 11);
  assert(report[0] == 1 && report[9] == 0x30 && report[10] == 1);
  const uint8_t expected1[] = {1,0,1,0x40,0x40,0,1,0x40,0x40,0x30,1};
  assert(!memcmp(report, expected1, sizeof expected1));
  assert(switch_player_led_build(1, 18, report) == 11);
  assert(report[0] == 2 && report[9] == 0x30 && report[10] == 2);
  assert(!switch_player_led_build(2, 0, report));
  uint8_t ack[15] = {0};
  ack[0] = 0x21; ack[13] = 0x80; ack[14] = 0x30;
  assert(switch_player_led_ack(ack, sizeof ack) == 1);
  ack[13] = 0;
  assert(switch_player_led_ack(ack, sizeof ack) == -1);
  ack[14] = 3;
  assert(switch_player_led_ack(ack, sizeof ack) == 0);
  assert(switch_player_led_ack(ack, 14) == 0);
  return 0;
}
