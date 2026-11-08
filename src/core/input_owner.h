#pragma once
#include "controller_state.h"
enum input_source { INPUT_IDLE, INPUT_XBOX, INPUT_8BITDO };
void input_session_reset(void);
void input_processing_start(void);
uint32_t input_connection_begin(enum input_source source);
int input_report_for(enum input_source source, uint32_t generation,
                     const controller_state_t *state, uint64_t timestamp);
void input_release_for(enum input_source source, uint32_t generation);
void input_quiesce_for(enum input_source source, uint32_t generation);
void input_report(enum input_source source, const controller_state_t *state, uint64_t timestamp);
void input_release(enum input_source source);
