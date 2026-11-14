#pragma once
#include <stdint.h>
enum { XINPUT_DUAL_INTERFACE_BYTES=40,
       XINPUT_DUAL_CONFIG_BYTES=89+(BRIDGE_GUIDE_MSC?23:0),
       XINPUT_DUAL_OS_COMPAT_BYTES=64 };
extern const uint8_t xinput_dual_device_descriptor[18];
extern const uint8_t xinput_dual_config_descriptor[XINPUT_DUAL_CONFIG_BYTES];
extern const uint8_t xinput_dual_os_compat_descriptor[XINPUT_DUAL_OS_COMPAT_BYTES];
