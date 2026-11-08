#pragma once
#include <stdint.h>

enum {
  CTRL_A = 1u << 0,
  CTRL_B = 1u << 1,
  CTRL_X = 1u << 2,
  CTRL_Y = 1u << 3,
  CTRL_LB = 1u << 4,
  CTRL_RB = 1u << 5,
  CTRL_VIEW = 1u << 6,
  CTRL_MENU = 1u << 7,
  CTRL_GUIDE = 1u << 8,
  CTRL_L3 = 1u << 9,
  CTRL_R3 = 1u << 10
};
typedef struct {
  uint16_t buttons;
  uint8_t dpad;
  int32_t left_x, left_y, right_x, right_y;
  int32_t left_trigger, right_trigger;
  uint32_t share;
} controller_state_t;
void controller_state_neutral(controller_state_t *state);
