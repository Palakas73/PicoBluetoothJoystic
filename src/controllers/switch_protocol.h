#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

enum switch_subcommand { SWITCH_SUBCMD_DEVICE_INFO = 0x02,
                         SWITCH_SUBCMD_REPORT_MODE = 0x03 };
enum switch_controller_type { SWITCH_TYPE_UNKNOWN = 0,
                              SWITCH_TYPE_PRO = 0x03 };
enum { SWITCH_SUBCOMMAND_REPORT_LEN = 11 };

void switch_build_subcommand(uint8_t sequence, uint8_t subcommand,
                             const uint8_t *data, size_t data_len,
                             uint8_t out[SWITCH_SUBCOMMAND_REPORT_LEN]);
bool switch_parse_subcommand_ack(const uint8_t *report, size_t len,
                                 uint8_t expected_subcommand,
                                 const uint8_t **reply_data,
                                 size_t *reply_len);
