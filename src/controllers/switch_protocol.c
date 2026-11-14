#include "switch_protocol.h"
#include <string.h>

void switch_build_subcommand(uint8_t sequence, uint8_t subcommand,
                             const uint8_t *data, size_t data_len,
                             uint8_t out[SWITCH_SUBCOMMAND_REPORT_LEN]) {
  static const uint8_t neutral_rumble[8] = {0,1,0x40,0x40,0,1,0x40,0x40};
  memset(out, 0, SWITCH_SUBCOMMAND_REPORT_LEN);
  out[0] = sequence & 0x0f;
  memcpy(out + 1, neutral_rumble, sizeof(neutral_rumble));
  out[9] = subcommand;
  if (data && data_len) out[10] = data[0];
}

bool switch_parse_subcommand_ack(const uint8_t *report, size_t len,
                                 uint8_t expected_subcommand,
                                 const uint8_t **reply_data,
                                 size_t *reply_len) {
  if (!report || len < 15 || report[0] != 0x21 ||
      !(report[13] & 0x80) || report[14] != expected_subcommand) return false;
  if (reply_data) *reply_data = report + 15;
  if (reply_len) *reply_len = len - 15;
  return true;
}
