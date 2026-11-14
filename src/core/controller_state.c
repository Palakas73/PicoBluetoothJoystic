#include "controller_state.h"
#include <string.h>
void controller_state_neutral(controller_state_t *state) {
  memset(state, 0, sizeof(*state));
  state->dpad = 8;
}
