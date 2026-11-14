#include "controllers/switch_protocol.h"
#include <assert.h>
#include <string.h>

int main(void) {
  uint8_t out[SWITCH_SUBCOMMAND_REPORT_LEN];
  uint8_t mode = 0x30;
  switch_build_subcommand(0x12, SWITCH_SUBCMD_REPORT_MODE, &mode, 1, out);
  const uint8_t expected[] = {2,0,1,0x40,0x40,0,1,0x40,0x40,3,0x30};
  assert(memcmp(out, expected, sizeof(expected)) == 0);

  uint8_t reply[26] = {0x21};
  reply[13] = 0x82; reply[14] = SWITCH_SUBCMD_DEVICE_INFO;
  reply[15] = 4; reply[16] = 0x21; reply[17] = SWITCH_TYPE_PRO;
  const uint8_t *data = NULL; size_t len = 0;
  assert(switch_parse_subcommand_ack(reply, sizeof(reply),
                                     SWITCH_SUBCMD_DEVICE_INFO, &data, &len));
  assert(len == 11 && data[2] == SWITCH_TYPE_PRO);
  assert(!switch_parse_subcommand_ack(reply, sizeof(reply),
                                      SWITCH_SUBCMD_REPORT_MODE, NULL, NULL));
  reply[13] = 0;
  assert(!switch_parse_subcommand_ack(reply, sizeof(reply),
                                      SWITCH_SUBCMD_DEVICE_INFO, NULL, NULL));
  assert(!switch_parse_subcommand_ack(reply, 14,
                                      SWITCH_SUBCMD_DEVICE_INFO, NULL, NULL));
  return 0;
}
