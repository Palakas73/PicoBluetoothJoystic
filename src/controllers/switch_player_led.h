#pragma once
#include <stdint.h>
enum { SWITCH_PLAYER_LED_REPORT_LEN = 11 };
/* Payload for output Report ID 0x01. Returns 0 for unsupported slot. */
int switch_player_led_build(uint8_t slot, uint8_t sequence,
                            uint8_t out[SWITCH_PLAYER_LED_REPORT_LEN]);
/* Input Report ID 0x21, after any HIDP A1 prefix has been removed. */
int switch_player_led_ack(const uint8_t *report, uint16_t len);
