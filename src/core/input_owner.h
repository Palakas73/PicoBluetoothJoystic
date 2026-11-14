#pragma once
#include "controller_state.h"
/* Runtime controller contexts, not controller models. Classic contexts remain
 * bound to their persisted peer record so RAM-only Autofire settings follow
 * the physical controller across a reconnect. */
enum input_source {
  INPUT_IDLE,
  INPUT_XBOX,
  INPUT_CLASSIC_0,
  INPUT_CLASSIC_1,
  INPUT_SOURCE_COUNT
};
#define INPUT_8BITDO INPUT_CLASSIC_0
void input_session_reset(void);
void input_processing_start(void);
uint32_t input_connection_begin(enum input_source source);
uint32_t input_classic_connection_begin(enum input_source source,
                                       const uint8_t identity[6]);
uint8_t input_classic_autofire_target_hz(const uint8_t identity[6],
                                         unsigned target);
void input_classic_forget_autofire_identity(const uint8_t identity[6]);
void input_classic_bind_autofire_identity(const uint8_t identity[6]);
uint8_t input_usb_slots_used(void);
int input_report_for(enum input_source source, uint32_t generation,
                     const controller_state_t *state, uint64_t timestamp);
void input_release_for(enum input_source source, uint32_t generation);
void input_quiesce_for(enum input_source source, uint32_t generation);
void input_report(enum input_source source, const controller_state_t *state, uint64_t timestamp);
void input_release(enum input_source source);
