#include "guide_scsi.h"
guide_scsi_policy_t guide_scsi_policy(uint8_t opcode) {
  switch (opcode) {
  case 0x1e: /* PREVENT/ALLOW MEDIUM REMOVAL */
  case 0x35: /* SYNCHRONIZE CACHE */
    return GUIDE_SCSI_SAFE;
  case 0x04: /* FORMAT UNIT */
  case 0x15: /* MODE SELECT(6) */
  case 0x2a: /* WRITE(10) */
  case 0x2e: /* WRITE AND VERIFY(10) */
  case 0x55: /* MODE SELECT(10) */
  case 0xaa: /* WRITE(12) */
  case 0x8a: /* WRITE(16) */
    return GUIDE_SCSI_WRITE;
  default:
    return GUIDE_SCSI_UNSUPPORTED;
  }
}
