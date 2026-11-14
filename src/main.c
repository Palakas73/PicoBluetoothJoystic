#if BRIDGE_CLASSIC_8BITDO
#include "bluetooth/classic_8bitdo_host.h"
#endif
#include "bluetooth/ble_host.h"
#include "btstack_run_loop.h"
#include "diagnostics/diag.h"
#include "led/status_led.h"
#include "pico/cyw43_arch.h"
#include "pico/stdlib.h"
#include "usb/usb_output.h"
#if BRIDGE_CLASSIC_8BITDO
#include "core/input_owner.h"
#endif
#if BRIDGE_USB_XINPUT
#include "tusb.h"
#include "pico/stdio_uart.h"
#else
#include "pico/stdio_usb.h"
#endif
#include <stdio.h>
int main(void) {
  usb_output_init();
#if BRIDGE_CLASSIC_8BITDO
  input_session_reset();
#endif
#if BRIDGE_USB_XINPUT
  stdio_uart_init();
  bool usb_ok = tusb_init();
  printf("\r\nPicoBluetoothJoystic XUSB slots=%u BLE-Xbox=1 Classic-HID=1 USB-CDC=0\r\n",
         BRIDGE_XINPUT_SLOTS);
#else
  bool usb_ok = stdio_usb_init();
  printf("\r\nPico Bluetooth Joystick CDC + HID Gamepad input_classic=%u diag=%u\r\n",
         BRIDGE_CLASSIC_8BITDO, BRIDGE_DIAGNOSTIC_USB);
#endif
  if (cyw43_arch_init() != 0) {
    printf("[FATAL] cyw43_arch_init failed\r\n");
    return 1;
  }
  status_led_init();
#if BRIDGE_CLASSIC_8BITDO
  input_processing_start();
#endif
  if (usb_ok) usb_output_start();
  else status_led_set(LED_CRITICAL_ERROR);
  ble_host_init();
  btstack_run_loop_execute();
  return 0;
}
