#include "xinput_protocol.h"

/* Private development identity only. A dedicated PID must be assigned before
 * shipping. Windows xusb22.inf matches USB\MS_COMP_XUSB10, not this VID/PID. */
const uint8_t xinput_device_descriptor[18] = {
  18, 1, 0x00, 0x02, 0xff, 0xff, 0xff, 64,
  (uint8_t)BRIDGE_XINPUT_VID, (uint8_t)(BRIDGE_XINPUT_VID >> 8),
  (uint8_t)BRIDGE_XINPUT_PID, (uint8_t)(BRIDGE_XINPUT_PID >> 8),
  (uint8_t)BRIDGE_XINPUT_BCD_DEVICE, (uint8_t)(BRIDGE_XINPUT_BCD_DEVICE >> 8),
  1, 2, 3, 1
};

/* Microsoft MS-XUSBI: gamepad interface, XUSB descriptor, interrupt IN/OUT. */
const uint8_t xinput_config_descriptor[XINPUT_CONFIG_BYTES] = {
  9, 2, XINPUT_CONFIG_BYTES, 0, 1, 1, 0, 0x80, 125,
  9, 4, 0, 0, 2, 0xff, 0x5d, 0x01, 0,
  17, 0x21, 0x00, 0x01, 0x01, 0x25, 0x81, 0x14,
  0, 0, 0, 0, 0x13, 0x02, 0x08, 0, 0,
  7, 5, XINPUT_EP_IN, 3, 32, 0, 1,
  7, 5, XINPUT_EP_OUT, 3, 32, 0, 8
};

/* Microsoft OS 1.0 Extended Compatible ID. Interface 0 = XUSB10. */
const uint8_t xinput_os_compat_descriptor[XINPUT_OS_COMPAT_BYTES] = {
  40, 0, 0, 0, 0, 1, 4, 0, 1, 0, 0, 0, 0, 0, 0, 0,
  0, 1, 'X','U','S','B','1','0', 0, 0,
  0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0
};
const uint16_t xinput_os_string_descriptor[9] = {
  0x0312, 'M','S','F','T','1','0','0', XINPUT_OS_VENDOR_CODE
};

xinput_request_kind_t xinput_classify_request(uint8_t type, uint8_t req,
    uint16_t value, uint16_t index, uint16_t length) {
  if (type == 0xc0 && req == XINPUT_OS_VENDOR_CODE && value == 0 &&
      index == 4 && length) return XINPUT_REQUEST_OS_COMPAT;
  if (type == 0xa1 && req == 1 && value == 0x0100 && index == 0 &&
      length) return XINPUT_REQUEST_GET_REPORT;
  if (type == 0x21 && req == 9 && value == 0x0200 && index == 0 &&
      length > 0 && length <= 32) return XINPUT_REQUEST_SET_REPORT;
  if (type == 0xc0 && req == 1 && value == 0 && index == 0 &&
      length) return XINPUT_REQUEST_GET_DEVICE_ID;
  return XINPUT_REQUEST_UNSUPPORTED;
}
