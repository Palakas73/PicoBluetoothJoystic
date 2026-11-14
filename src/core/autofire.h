#pragma once
#include "controller_state.h"
#include <stdbool.h>
#include <stdint.h>

enum { AUTOFIRE_TARGET_COUNT = 10 };
typedef enum {
  AF_A, AF_B, AF_X, AF_Y, AF_LB, AF_RB, AF_LT, AF_RT, AF_L3, AF_R3
} autofire_target_t;
typedef enum {
  AF_EVENT_NONE, AF_EVENT_ARMED, AF_EVENT_ENABLED_10,
  AF_EVENT_DISABLED, AF_EVENT_SPEED_5, AF_EVENT_SPEED_10,
  AF_EVENT_SPEED_15, AF_EVENT_SPEED_20, AF_EVENT_CANCELLED
} autofire_event_t;
typedef enum {
  AF_MODE_NORMAL, AF_MODE_WAIT_COMBO_RELEASE, AF_MODE_ARMED,
  AF_MODE_REJECT_UNTIL_RELEASE, AF_MODE_CONSUME_UNTIL_RELEASE
} autofire_mode_t;

typedef struct {
  uint8_t hz[AUTOFIRE_TARGET_COUNT];
  controller_state_t physical;
  uint64_t press_start_us[AUTOFIRE_TARGET_COUNT];
  uint64_t combo_start_us, config_deadline_us;
  uint16_t active_mask, previous_active_mask, await_release_mask;
  bool have_physical, combo_timing, combo_released_after_entry;
  autofire_mode_t mode;
} autofire_t;

void autofire_init(autofire_t *af);
void autofire_connect(autofire_t *af);
void autofire_disconnect(autofire_t *af);
/* Physical reports only. Settings remain in RAM across connect/disconnect. */
autofire_event_t autofire_physical(autofire_t *af,
    const controller_state_t *physical, uint64_t now_us,
    controller_state_t *transformed);
/* Recomputes timed phases/configuration without a new Bluetooth report. */
autofire_event_t autofire_tick(autofire_t *af, uint64_t now_us,
                               controller_state_t *transformed);
bool autofire_target_enabled(const autofire_t *af, autofire_target_t target);
uint8_t autofire_target_hz(const autofire_t *af, autofire_target_t target);
bool autofire_configuring(const autofire_t *af);
