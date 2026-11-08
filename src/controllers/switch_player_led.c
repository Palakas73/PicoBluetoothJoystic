#include "switch_player_led.h"
#include <string.h>
int switch_player_led_build(uint8_t slot, uint8_t sequence,
                            uint8_t out[SWITCH_PLAYER_LED_REPORT_LEN]) {
  if (slot > 1 || !out) return 0;
  static const uint8_t neutral_rumble[8] = {0,1,0x40,0x40,0,1,0x40,0x40};
  out[0] = sequence & 0x0f;
  memcpy(out + 1, neutral_rumble, sizeof(neutral_rumble));
  out[9] = 0x30; /* SET_PLAYER_LIGHTS subcommand, not Report ID 0x30 */
  out[10] = (uint8_t)(1u << slot); /* steady player 1/2, no blink bits */
  return SWITCH_PLAYER_LED_REPORT_LEN;
}
int switch_player_led_ack(const uint8_t *report, uint16_t len) {
  if (!report || len < 15 || report[0] != 0x21 || report[14] != 0x30)
    return 0;
  return (report[13] & 0x80) ? 1 : -1;
}
