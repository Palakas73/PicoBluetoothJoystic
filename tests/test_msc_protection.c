#include "usb/guide_msc.h"
#include "tusb.h"
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>

const uint8_t _binary_README_TXT_start[1]={0},_binary_README_TXT_end[1]={0};
const uint8_t _binary_MANUAL_HTM_start[1]={0},_binary_MANUAL_HTM_end[1]={0};
const uint8_t _binary_AUTOFIRE_PNG_start[1]={0},_binary_AUTOFIRE_PNG_end[1]={0};
const uint8_t _binary_VERSION_TXT_start[1]={0},_binary_VERSION_TXT_end[1]={0};
static uint8_t sense_key,sense_code,sense_qualifier;
void tud_msc_set_sense(uint8_t lun,uint8_t key,uint8_t code,uint8_t qualifier){
  assert(lun==0);sense_key=key;sense_code=code;sense_qualifier=qualifier;
}
uint64_t time_us_64(void){static uint64_t t;return ++t;}
extern bool tud_msc_is_writable_cb(uint8_t lun);
extern int32_t tud_msc_write10_cb(uint8_t,uint32_t,uint32_t,uint8_t*,uint32_t);
extern int32_t tud_msc_scsi_cb(uint8_t,const uint8_t[16],void*,uint16_t);
int main(void){
  uint8_t buf[16]={0},cmd[16]={0};
  assert(!tud_msc_is_writable_cb(0));
  assert(tud_msc_write10_cb(0,0,0,buf,sizeof(buf))==-1);
  assert(sense_key==SCSI_SENSE_DATA_PROTECT&&sense_code==0x27&&sense_qualifier==0);
  const uint8_t mutating[]={0x04,0x15,0x2a,0x2e,0x55,0xaa,0x8a};
  for(unsigned i=0;i<sizeof(mutating);i++){
    cmd[0]=mutating[i];sense_key=sense_code=0;
    assert(tud_msc_scsi_cb(0,cmd,buf,sizeof(buf))==-1);
    assert(sense_key==SCSI_SENSE_DATA_PROTECT&&sense_code==0x27);
  }
  cmd[0]=0x1e;assert(tud_msc_scsi_cb(0,cmd,buf,sizeof(buf))==0);
  cmd[0]=0x35;assert(tud_msc_scsi_cb(0,cmd,buf,sizeof(buf))==0);
  cmd[0]=0xff;assert(tud_msc_scsi_cb(0,cmd,buf,sizeof(buf))==-1);
  assert(sense_key==SCSI_SENSE_ILLEGAL_REQUEST&&sense_code==0x20);
  return 0;
}
