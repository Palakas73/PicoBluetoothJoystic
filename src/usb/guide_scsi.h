#pragma once
#include <stdint.h>
typedef enum { GUIDE_SCSI_SAFE, GUIDE_SCSI_WRITE, GUIDE_SCSI_UNSUPPORTED } guide_scsi_policy_t;
guide_scsi_policy_t guide_scsi_policy(uint8_t opcode);
