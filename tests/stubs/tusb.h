#pragma once
#include <stdbool.h>
#include <stdint.h>
enum { SCSI_SENSE_NOT_READY=2, SCSI_SENSE_ILLEGAL_REQUEST=5, SCSI_SENSE_DATA_PROTECT=7 };
enum { SCSI_CMD_MODE_SELECT_6=0x15, SCSI_CMD_PREVENT_ALLOW_MEDIUM_REMOVAL=0x1e };
void tud_msc_set_sense(uint8_t lun,uint8_t key,uint8_t code,uint8_t qualifier);
