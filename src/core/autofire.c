#include "autofire.h"
#include <string.h>

enum {
  COMBO_US = 2000000, CONFIG_US = 10000000,
  TRIGGER_ON = 32768, TRIGGER_OFF = 29490,
  ALL_TARGETS = (1u << AUTOFIRE_TARGET_COUNT) - 1u
};
static const uint16_t button_bits[AUTOFIRE_TARGET_COUNT] = {
  CTRL_A, CTRL_B, CTRL_X, CTRL_Y, CTRL_LB, CTRL_RB, 0, 0, CTRL_L3, CTRL_R3
};
static bool combo(const controller_state_t *s) {
  return (s->buttons & (CTRL_VIEW | CTRL_MENU)) == (CTRL_VIEW | CTRL_MENU);
}
static bool trigger_active(bool was, int32_t value) {
  if (was) return value > TRIGGER_OFF;
  return value >= TRIGGER_ON;
}
static uint16_t update_active(autofire_t *af) {
  uint16_t mask = 0;
  for (unsigned i = 0; i < AUTOFIRE_TARGET_COUNT; i++) {
    bool on;
    if (i == AF_LT) on = trigger_active((af->active_mask >> i) & 1,
                                         af->physical.left_trigger);
    else if (i == AF_RT) on = trigger_active((af->active_mask >> i) & 1,
                                              af->physical.right_trigger);
    else on = (af->physical.buttons & button_bits[i]) != 0;
    if (on) mask |= (uint16_t)(1u << i);
  }
  return mask;
}
static unsigned popcount16(uint16_t v) {
  unsigned n = 0;
  while (v) { n += v & 1u; v >>= 1; }
  return n;
}
static int one_target(uint16_t mask) {
  if (!mask || (mask & (mask - 1u))) return -1;
  int n = 0;
  while ((mask >>= 1u) != 0) n++;
  return n;
}
static uint8_t speed_for_dpad(uint8_t dpad) {
  switch (dpad) {
  case 0: return 5;
  case 2: return 10;
  case 4: return 15;
  case 6: return 20;
  case 8: return 10; /* neutral: toggle/default handled separately */
  default: return 0; /* diagonal or malformed */
  }
}
static autofire_event_t event_for_hz(uint8_t hz) {
  return hz == 5 ? AF_EVENT_SPEED_5 : hz == 10 ? AF_EVENT_SPEED_10 :
         hz == 15 ? AF_EVENT_SPEED_15 : AF_EVENT_SPEED_20;
}
static void suppress_reserved(controller_state_t *out) {
  out->buttons &= (uint16_t)~(CTRL_A | CTRL_B | CTRL_X | CTRL_Y | CTRL_LB |
                             CTRL_RB | CTRL_VIEW | CTRL_MENU | CTRL_L3 | CTRL_R3);
  out->left_trigger = 0;
  out->right_trigger = 0;
  out->dpad = 8;
}
static bool phase_on(uint64_t elapsed_us, uint8_t hz) {
  /* Half-cycle index avoids a rounded 15 Hz period and accumulated drift. */
  return (((elapsed_us * (uint64_t)hz * 2u) / 1000000u) & 1u) == 0;
}
static void transform_play(autofire_t *af, uint64_t now_us,
                           controller_state_t *out) {
  *out = af->physical;
  for (unsigned i = 0; i < AUTOFIRE_TARGET_COUNT; i++) {
    uint16_t bit = (uint16_t)(1u << i);
    if (!af->hz[i] || (af->await_release_mask & bit)) continue;
    bool active = (af->active_mask & bit) != 0;
    bool on = active && phase_on(now_us - af->press_start_us[i], af->hz[i]);
    if (i == AF_LT) out->left_trigger = on ? 65535 : 0;
    else if (i == AF_RT) out->right_trigger = on ? 65535 : 0;
    else if (on) out->buttons |= button_bits[i];
    else out->buttons &= (uint16_t)~button_bits[i];
  }
}
static autofire_event_t process(autofire_t *af, uint64_t now_us,
                                bool new_report, controller_state_t *out) {
  if (!af->have_physical) { controller_state_neutral(out); return AF_EVENT_NONE; }
  autofire_event_t event = AF_EVENT_NONE;
  bool held_combo = combo(&af->physical);
  if (af->mode == AF_MODE_NORMAL) {
    if (held_combo) {
      if (!af->combo_timing) { af->combo_timing = true; af->combo_start_us = now_us; }
      else if (now_us - af->combo_start_us >= COMBO_US) {
        af->mode = AF_MODE_WAIT_COMBO_RELEASE;
        af->config_deadline_us = now_us + CONFIG_US;
        af->combo_timing = false;
        af->combo_released_after_entry = false;
        af->await_release_mask |= af->active_mask;
        event = AF_EVENT_ARMED;
      }
    } else af->combo_timing = false;
  } else {
    if (af->mode != AF_MODE_CONSUME_UNTIL_RELEASE &&
        now_us >= af->config_deadline_us) {
      af->mode = AF_MODE_CONSUME_UNTIL_RELEASE;
      af->await_release_mask |= af->active_mask;
      event = AF_EVENT_CANCELLED;
    } else if (af->mode == AF_MODE_WAIT_COMBO_RELEASE) {
      if (!held_combo) {
        af->combo_released_after_entry = true;
        af->mode = AF_MODE_ARMED;
      }
    } else if (af->mode == AF_MODE_ARMED) {
      if (held_combo) {
        if (!af->combo_timing) { af->combo_timing = true; af->combo_start_us = now_us; }
        else if (now_us - af->combo_start_us >= COMBO_US) {
          af->mode = AF_MODE_CONSUME_UNTIL_RELEASE;
          af->await_release_mask |= af->active_mask;
          af->combo_timing = false;
          event = AF_EVENT_CANCELLED;
        }
      } else af->combo_timing = false;
      uint16_t eligible = af->active_mask & (uint16_t)~af->await_release_mask;
      if (new_report && (eligible || popcount16(af->active_mask) > 1)) {
        if (popcount16(af->active_mask) != 1) {
          af->await_release_mask |= af->active_mask;
          af->mode = AF_MODE_REJECT_UNTIL_RELEASE;
        } else {
          int target = one_target(eligible);
          uint8_t speed = speed_for_dpad(af->physical.dpad);
          if (!speed) {
            af->await_release_mask |= eligible;
            af->mode = AF_MODE_REJECT_UNTIL_RELEASE;
          } else {
            af->await_release_mask |= eligible;
            if (af->physical.dpad == 8) {
              if (af->hz[target]) { af->hz[target] = 0; event = AF_EVENT_DISABLED; }
              else { af->hz[target] = 10; event = AF_EVENT_ENABLED_10; }
            } else {
              af->hz[target] = speed;
              event = event_for_hz(speed);
            }
            af->mode = AF_MODE_CONSUME_UNTIL_RELEASE;
          }
        }
      }
    } else if (af->mode == AF_MODE_REJECT_UNTIL_RELEASE) {
      if (!af->active_mask && af->physical.dpad == 8) af->mode = AF_MODE_ARMED;
    } else if (af->mode == AF_MODE_CONSUME_UNTIL_RELEASE) {
      if (!held_combo && !af->active_mask && af->physical.dpad == 8) {
        af->mode = AF_MODE_NORMAL;
        af->combo_timing = false;
      }
    }
  }
  af->await_release_mask &= af->active_mask;
  if (af->mode == AF_MODE_NORMAL) transform_play(af, now_us, out);
  else { *out = af->physical; suppress_reserved(out); }
  return event;
}
void autofire_init(autofire_t *af) {
  memset(af, 0, sizeof(*af));
  controller_state_neutral(&af->physical);
}
void autofire_connect(autofire_t *af) {
  af->have_physical = false;
  af->active_mask = af->previous_active_mask = 0;
  af->await_release_mask = ALL_TARGETS;
  af->combo_timing = false;
  af->mode = AF_MODE_NORMAL;
}
void autofire_disconnect(autofire_t *af) {
  af->have_physical = false;
  af->active_mask = af->previous_active_mask = 0;
  af->await_release_mask = ALL_TARGETS;
  af->combo_timing = false;
  af->mode = AF_MODE_NORMAL;
  controller_state_neutral(&af->physical);
}
autofire_event_t autofire_physical(autofire_t *af,
    const controller_state_t *physical, uint64_t now_us,
    controller_state_t *transformed) {
  if (!af || !physical || !transformed) return AF_EVENT_NONE;
  af->physical = *physical;
  af->have_physical = true;
  af->previous_active_mask = af->active_mask;
  af->active_mask = update_active(af);
  uint16_t rising = af->active_mask & (uint16_t)~af->previous_active_mask;
  for (unsigned i = 0; i < AUTOFIRE_TARGET_COUNT; i++)
    if (rising & (1u << i)) af->press_start_us[i] = now_us;
  return process(af, now_us, true, transformed);
}
autofire_event_t autofire_tick(autofire_t *af, uint64_t now_us,
                               controller_state_t *transformed) {
  if (!af || !transformed) return AF_EVENT_NONE;
  return process(af, now_us, false, transformed);
}
bool autofire_target_enabled(const autofire_t *af, autofire_target_t target) {
  return af && (unsigned)target < AUTOFIRE_TARGET_COUNT && af->hz[target] != 0;
}
uint8_t autofire_target_hz(const autofire_t *af, autofire_target_t target) {
  return af && (unsigned)target < AUTOFIRE_TARGET_COUNT ? af->hz[target] : 0;
}
bool autofire_configuring(const autofire_t *af) {
  return af && af->mode != AF_MODE_NORMAL;
}
