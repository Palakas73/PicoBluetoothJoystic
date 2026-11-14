#include "xinput_dual_protocol.h"
#include "xinput_protocol.h"
const uint8_t xinput_dual_device_descriptor[18]={
  18,1,0,2,0,0,0,64,(uint8_t)BRIDGE_XINPUT_VID,(uint8_t)(BRIDGE_XINPUT_VID>>8),
  (uint8_t)BRIDGE_XINPUT_PID,(uint8_t)(BRIDGE_XINPUT_PID>>8),
  (uint8_t)BRIDGE_XINPUT_BCD_DEVICE,(uint8_t)(BRIDGE_XINPUT_BCD_DEVICE>>8),1,2,3,1};
const uint8_t xinput_dual_config_descriptor[XINPUT_DUAL_CONFIG_BYTES]={
  9,2,XINPUT_DUAL_CONFIG_BYTES,0,BRIDGE_GUIDE_MSC?3:2,1,0,0x80,125,
  9,4,0,0,2,0xff,0x5d,1,0,17,0x21,0,1,1,0x25,0x81,20,0,0,0,0,0x13,0x02,8,0,0,
  7,5,0x81,3,32,0,1,7,5,0x02,3,32,0,8,
  9,4,1,0,2,0xff,0x5d,1,0,17,0x21,0,1,1,0x25,0x83,20,0,0,0,0,0x13,0x04,8,0,0,
  7,5,0x83,3,32,0,1,7,5,0x04,3,32,0,8
#if BRIDGE_GUIDE_MSC
  ,9,4,2,0,2,0x08,0x06,0x50,4,
  7,5,0x06,0x02,64,0,0,7,5,0x85,0x02,64,0,0
#endif
};
const uint8_t xinput_dual_os_compat_descriptor[XINPUT_DUAL_OS_COMPAT_BYTES]={
  XINPUT_DUAL_OS_COMPAT_BYTES,0,0,0,0,1,4,0,2,0,0,0,0,0,0,0,
  0,0,'X','U','S','B','1','0',0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
  1,0,'X','U','S','B','1','0',0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0};
_Static_assert(sizeof(xinput_dual_config_descriptor)==XINPUT_DUAL_CONFIG_BYTES,"dual config size");
_Static_assert(sizeof(xinput_dual_os_compat_descriptor)==XINPUT_DUAL_OS_COMPAT_BYTES,"dual OS size");
