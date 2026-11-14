#include "usb/xinput_dual_protocol.h"
#include <assert.h>
#include <string.h>
static unsigned le16(const uint8_t*p){return p[0]|((unsigned)p[1]<<8);}
int main(void){
  const uint8_t*d=xinput_dual_config_descriptor;
  assert(le16(d+2)==112&&d[4]==3);
  assert(d[14]==0xff&&d[15]==0x5d&&d[16]==1);
  assert(d[37]==0x81&&d[44]==0x02);
  assert(d[54]==0xff&&d[55]==0x5d&&d[56]==1);
  assert(d[77]==0x83&&d[84]==0x04);
  assert(d[94]==0x08&&d[95]==0x06&&d[96]==0x50);
  assert(d[100]==0x06&&d[101]==0x02&&le16(d+102)==64);
  assert(d[107]==0x85&&d[108]==0x02&&le16(d+109)==64);
  assert(xinput_dual_os_compat_descriptor[8]==2);
  assert(!memcmp(xinput_dual_os_compat_descriptor+18,"XUSB10",6));
  assert(xinput_dual_os_compat_descriptor[40]==1);
  assert(!memcmp(xinput_dual_os_compat_descriptor+42,"XUSB10",6));
  return 0;
}
