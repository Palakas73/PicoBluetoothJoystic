#pragma once

/* BLE owns a normalized state; only this selection layer knows the USB mode. */
#if BRIDGE_USB_XINPUT
#include "usb/usb_xinput.h"
#define usb_output_init usb_xinput_init
#define usb_output_start usb_xinput_start
#if BRIDGE_CLASSIC_8BITDO
#include "core/input_owner.h"
#define usb_output_submit(s,t) input_report(INPUT_XBOX,s,t)
#define usb_output_disconnect() input_release(INPUT_XBOX)
#else
#define usb_output_submit usb_xinput_submit
#define usb_output_disconnect usb_xinput_disconnect
#endif
#define usb_output_print_stats usb_xinput_print_stats
#else
#include "usb/usb_gamepad.h"
#define usb_output_init usb_gamepad_init
#define usb_output_start usb_gamepad_start
#define usb_output_submit usb_gamepad_submit
#define usb_output_disconnect usb_gamepad_disconnect
#define usb_output_print_stats usb_gamepad_print_stats
#endif
