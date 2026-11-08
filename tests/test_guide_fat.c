#include "usb/guide_fat.h"
#include "usb/guide_scsi.h"
#include <assert.h>
#include <string.h>

static const uint8_t readme[]="hello guide";
static uint8_t image[700];
static guide_file_t files[]={
  {{'R','E','A','D','M','E',' ',' '},{'T','X','T'},readme,sizeof(readme)-1},
  {{'A','U','T','O','F','I','R','E'},{'P','N','G'},image,sizeof(image)}
};
static unsigned le16(const uint8_t *p){return p[0]|((unsigned)p[1]<<8);}
int main(void){
  for(unsigned i=0;i<sizeof(image);i++)image[i]=(uint8_t)i;
  assert(guide_fat_layout_valid(files,2));
  uint8_t b[512];
  assert(guide_fat_read(files,2,0,0,b,512)==512);
  assert(b[510]==0x55&&b[511]==0xaa&&le16(b+11)==512);
  assert(!memcmp(b+43,"PICO_GUIDE ",11));
  assert(guide_fat_read(files,2,7,0,b,512)==512);
  assert(!memcmp(b,"PICO_GUIDE ",11)&&b[11]==0x09);
  assert(!memcmp(b+32,"README  TXT",11)&&b[32+11]==0x21);
  assert(!memcmp(b+64,"AUTOFIREPNG",11));
  assert(guide_fat_read(files,2,9,0,b,512)==512);
  assert(!memcmp(b,readme,sizeof(readme)-1));
  assert(guide_fat_read(files,2,10,100,b,50)==50);
  assert(!memcmp(b,image+100,50));
  assert(guide_fat_read(files,2,11,0,b,188)==188);
  assert(!memcmp(b,image+512,188));
  assert(guide_fat_read(files,2,2048,0,b,1)==-1);
  assert(guide_fat_read(files,2,0,500,b,13)==-1);
  assert(guide_scsi_policy(0x1e)==GUIDE_SCSI_SAFE);
  assert(guide_scsi_policy(0x35)==GUIDE_SCSI_SAFE);
  const uint8_t writes[]={0x04,0x15,0x2a,0x2e,0x55,0xaa,0x8a};
  for(unsigned i=0;i<sizeof(writes);i++)assert(guide_scsi_policy(writes[i])==GUIDE_SCSI_WRITE);
  assert(guide_scsi_policy(0xff)==GUIDE_SCSI_UNSUPPORTED);
  return 0;
}
