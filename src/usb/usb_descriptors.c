#include "gamepad_report.h"
#include "pico/unique_id.h"
#include "tusb.h"
#include <string.h>

/* Pico SDK development identity, not an Xbox/XInput device. */
#define USB_VID 0x2e8a
#define USB_PID 0x000a
#define EP_CDC_NOTIFY 0x81
#define EP_CDC_OUT 0x02
#define EP_CDC_IN 0x82
#define EP_HID_IN 0x83

enum { ITF_CDC_CONTROL, ITF_CDC_DATA, ITF_GAMEPAD, ITF_COUNT };
enum { STR_MANUFACTURER = 1, STR_PRODUCT, STR_SERIAL, STR_CDC, STR_GAMEPAD };

static const tusb_desc_device_t device_desc = {
    .bLength = sizeof(tusb_desc_device_t),
    .bDescriptorType = TUSB_DESC_DEVICE,
    .bcdUSB = 0x0200,
    .bDeviceClass = TUSB_CLASS_MISC,
    .bDeviceSubClass = MISC_SUBCLASS_COMMON,
    .bDeviceProtocol = MISC_PROTOCOL_IAD,
    .bMaxPacketSize0 = CFG_TUD_ENDPOINT0_SIZE,
    .idVendor = USB_VID,
    .idProduct = USB_PID,
    .bcdDevice = BRIDGE_DIAGNOSTIC_USB ? 0x0201 : 0x0200,
    .iManufacturer = STR_MANUFACTURER,
    .iProduct = STR_PRODUCT,
    .iSerialNumber = STR_SERIAL,
    .bNumConfigurations = 1,
};

#define CONFIG_TOTAL_LEN (TUD_CONFIG_DESC_LEN + TUD_CDC_DESC_LEN + TUD_HID_DESC_LEN)
static const uint8_t config_desc[] = {
    TUD_CONFIG_DESCRIPTOR(1, ITF_COUNT, 0, CONFIG_TOTAL_LEN, 0, 250),
    TUD_CDC_DESCRIPTOR(ITF_CDC_CONTROL, STR_CDC, EP_CDC_NOTIFY, 8,
                       EP_CDC_OUT, EP_CDC_IN, 64),
    TUD_HID_DESCRIPTOR(ITF_GAMEPAD, STR_GAMEPAD, HID_ITF_PROTOCOL_NONE,
                       USB_GAMEPAD_DESCRIPTOR_BYTES, EP_HID_IN, 16, 1),
};
_Static_assert(sizeof(config_desc) == CONFIG_TOTAL_LEN, "USB config size");

const uint8_t *tud_descriptor_device_cb(void) {
  return (const uint8_t *)&device_desc;
}
const uint8_t *tud_descriptor_configuration_cb(uint8_t index) {
  return index == 0 ? config_desc : NULL;
}
const uint8_t *tud_hid_descriptor_report_cb(uint8_t instance) {
  return instance == 0 ? usb_gamepad_report_descriptor : NULL;
}

const uint16_t *tud_descriptor_string_cb(uint8_t index, uint16_t langid) {
  (void)langid;
  static uint16_t desc[32];
  static char serial[PICO_UNIQUE_BOARD_ID_SIZE_BYTES * 2 + 1];
  if (!serial[0]) pico_get_unique_board_id_string(serial, sizeof(serial));
  if (index == 0) {
    desc[0] = (TUSB_DESC_STRING << 8) | 4;
    desc[1] = 0x0409;
    return desc;
  }
  const char *str = NULL;
  switch (index) {
  case STR_MANUFACTURER: str = "Raspberry Pi"; break;
  case STR_PRODUCT: str = BRIDGE_DIAGNOSTIC_USB ? "Pico Switch HID CDC Diagnostic" : "Pico Gamepad Bridge (dev)"; break;
  case STR_SERIAL: str = serial; break;
  case STR_CDC: str = "Bridge diagnostics"; break;
  case STR_GAMEPAD: str = BRIDGE_DIAGNOSTIC_USB ? "Switch-profile diagnostic HID" : "Gamepad HID"; break;
  default: return NULL;
  }
  uint8_t length = (uint8_t)strlen(str);
  if (length > 31) length = 31;
  desc[0] = (TUSB_DESC_STRING << 8) | (2u * length + 2u);
  for (uint8_t i = 0; i < length; i++) desc[i + 1] = (uint8_t)str[i];
  return desc;
}
