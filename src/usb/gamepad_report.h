#pragma once
#include "core/controller_state.h"
#include <stddef.h>
#include <stdint.h>

#define USB_GAMEPAD_REPORT_BYTES 15
#define USB_GAMEPAD_REPORT_ID 0
#define USB_GAMEPAD_DESCRIPTOR_BYTES 95

typedef struct {
  uint16_t stick_deadzone; /* normalized units; zero disables processing */
} gamepad_profile_t;

extern const uint8_t usb_gamepad_report_descriptor[USB_GAMEPAD_DESCRIPTOR_BYTES];
extern const size_t usb_gamepad_report_descriptor_len;

void gamepad_report_encode(const controller_state_t *state,
                           const gamepad_profile_t *profile,
                           uint8_t out[USB_GAMEPAD_REPORT_BYTES]);
