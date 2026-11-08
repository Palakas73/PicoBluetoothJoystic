#include "guide_fat.h"
#include "guide_scsi.h"
#include "tusb.h"
#include "pico/time.h"
#include <string.h>

extern const uint8_t _binary_README_TXT_start[], _binary_README_TXT_end[];
extern const uint8_t _binary_MANUAL_HTM_start[], _binary_MANUAL_HTM_end[];
extern const uint8_t _binary_AUTOFIRE_PNG_start[], _binary_AUTOFIRE_PNG_end[];
extern const uint8_t _binary_VERSION_TXT_start[], _binary_VERSION_TXT_end[];
static guide_file_t files[4];
static bool files_ready, ejected;
static uint32_t read_calls, read_bytes, read_errors, write_rejections;
static uint32_t read_total_us, read_max_us;

static void init_files(void) {
  if (files_ready) return;
  const guide_file_t init[] = {
    {{'R','E','A','D','M','E',' ',' '},{'T','X','T'},_binary_README_TXT_start,
      (uint32_t)(_binary_README_TXT_end-_binary_README_TXT_start)},
    {{'M','A','N','U','A','L',' ',' '},{'H','T','M'},_binary_MANUAL_HTM_start,
      (uint32_t)(_binary_MANUAL_HTM_end-_binary_MANUAL_HTM_start)},
    {{'A','U','T','O','F','I','R','E'},{'P','N','G'},_binary_AUTOFIRE_PNG_start,
      (uint32_t)(_binary_AUTOFIRE_PNG_end-_binary_AUTOFIRE_PNG_start)},
    {{'V','E','R','S','I','O','N',' '},{'T','X','T'},_binary_VERSION_TXT_start,
      (uint32_t)(_binary_VERSION_TXT_end-_binary_VERSION_TXT_start)}
  };
  memcpy(files,init,sizeof(files)); files_ready=true;
}
uint8_t tud_msc_get_maxlun_cb(void) { return 0; }
void tud_msc_inquiry_cb(uint8_t lun, uint8_t vid[8], uint8_t pid[16], uint8_t rev[4]) {
  (void)lun; memcpy(vid,"PICOJOY ",8); memcpy(pid,"READ ONLY GUIDE ",16); memcpy(rev,"1.0 ",4);
}
bool tud_msc_test_unit_ready_cb(uint8_t lun) {
  if (lun || ejected) { tud_msc_set_sense(lun,SCSI_SENSE_NOT_READY,0x3a,0); return false; }
  return true;
}
void tud_msc_capacity_cb(uint8_t lun, uint32_t *blocks, uint16_t *size) {
  (void)lun; *blocks=GUIDE_BLOCK_COUNT; *size=GUIDE_BLOCK_SIZE;
}
bool tud_msc_is_writable_cb(uint8_t lun) { (void)lun; return false; }
bool tud_msc_start_stop_cb(uint8_t lun, uint8_t power, bool start, bool load_eject) {
  (void)power; if (lun) return false; if (load_eject) ejected=!start; return true;
}
int32_t tud_msc_read10_cb(uint8_t lun,uint32_t lba,uint32_t offset,void *buf,uint32_t n) {
  if (lun || ejected) { read_errors++; tud_msc_set_sense(lun,SCSI_SENSE_NOT_READY,0x3a,0); return -1; }
  init_files(); uint64_t started=time_us_64();
  int32_t result=guide_fat_read(files,4,lba,offset,buf,n);
  uint32_t elapsed=(uint32_t)(time_us_64()-started);
  read_total_us+=elapsed;if(elapsed>read_max_us)read_max_us=elapsed;
  if (result<0) { read_errors++; tud_msc_set_sense(lun,SCSI_SENSE_ILLEGAL_REQUEST,0x21,0); }
  else { read_calls++; read_bytes+=(uint32_t)result; }
  return result;
}
int32_t tud_msc_write10_cb(uint8_t lun,uint32_t lba,uint32_t offset,uint8_t *buf,uint32_t n) {
  (void)lba;(void)offset;(void)buf;(void)n; write_rejections++;
  tud_msc_set_sense(lun,SCSI_SENSE_DATA_PROTECT,0x27,0); return -1;
}
int32_t tud_msc_scsi_cb(uint8_t lun,const uint8_t cmd[16],void *buffer,uint16_t size) {
  (void)buffer;(void)size;
  guide_scsi_policy_t policy=guide_scsi_policy(cmd[0]);
  if(policy==GUIDE_SCSI_SAFE) return 0;
  if(policy==GUIDE_SCSI_WRITE) {
    write_rejections++; tud_msc_set_sense(lun,SCSI_SENSE_DATA_PROTECT,0x27,0); return -1;
  }
  tud_msc_set_sense(lun,SCSI_SENSE_ILLEGAL_REQUEST,0x20,0); return -1;
}
void guide_msc_get_stats(uint32_t *calls,uint32_t *bytes,uint32_t *errors,
                         uint32_t *writes,uint32_t *total_us,uint32_t *max_us) {
  if(calls)*calls=read_calls;if(bytes)*bytes=read_bytes;if(errors)*errors=read_errors;
  if(writes)*writes=write_rejections;if(total_us)*total_us=read_total_us;if(max_us)*max_us=read_max_us;
}
