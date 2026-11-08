#pragma once
#include <stdint.h>

#ifndef BRIDGE_XINPUT_VID
#define BRIDGE_XINPUT_VID 0x2e8a
#endif
#ifndef BRIDGE_XINPUT_PID
#define BRIDGE_XINPUT_PID 0x000a
#endif
#ifndef BRIDGE_XINPUT_BCD_DEVICE
#define BRIDGE_XINPUT_BCD_DEVICE 0x0301
#endif

enum { XINPUT_CONFIG_BYTES = 49, XINPUT_OS_COMPAT_BYTES = 40,
       XINPUT_EP_IN = 0x81, XINPUT_EP_OUT = 0x02,
       XINPUT_OS_VENDOR_CODE = 0x20 };
extern const uint8_t xinput_device_descriptor[18];
extern const uint8_t xinput_config_descriptor[XINPUT_CONFIG_BYTES];
extern const uint8_t xinput_os_compat_descriptor[XINPUT_OS_COMPAT_BYTES];
extern const uint16_t xinput_os_string_descriptor[9];

typedef enum {
  XINPUT_REQUEST_UNSUPPORTED = 0,
  XINPUT_REQUEST_OS_COMPAT,
  XINPUT_REQUEST_GET_REPORT,
  XINPUT_REQUEST_SET_REPORT,
  XINPUT_REQUEST_GET_DEVICE_ID
} xinput_request_kind_t;

xinput_request_kind_t xinput_classify_request(uint8_t bm_request_type,
    uint8_t request, uint16_t value, uint16_t index, uint16_t length);
