#pragma once
#include "core/controller_state.h"
#include <stdbool.h>
#include <stdint.h>

void usb_xinput_init(void);
void usb_xinput_start(void);
void usb_xinput_submit(const controller_state_t *state, uint64_t ble_received_us);
void usb_xinput_disconnect(void);
void usb_xinput_submit_slot(uint8_t slot, const controller_state_t *state, uint64_t received_us);
void usb_xinput_disconnect_slot(uint8_t slot);
void usb_xinput_print_stats(void);
bool usb_xinput_mounted(void);
