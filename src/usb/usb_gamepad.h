#pragma once
#include "core/controller_state.h"
#include <stdbool.h>
#include <stdint.h>

void usb_gamepad_init(void);
void usb_gamepad_start(void);
void usb_gamepad_submit(const controller_state_t *state, uint64_t ble_received_us);
void usb_gamepad_disconnect(void);
void usb_gamepad_print_stats(void);
bool usb_gamepad_mounted(void);
void usb_gamepad_set_deadzone(uint16_t normalized_units);
