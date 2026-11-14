#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef enum {
  CLASSIC_PROFILE_UNKNOWN,
  CLASSIC_PROFILE_SWITCH_COMPATIBLE,
  CLASSIC_PROFILE_DUALSHOCK4
} classic_profile_t;

bool classic_name_is_candidate(const uint8_t *name, size_t length);
bool classic_name_is_ds4_hint(const uint8_t *name, size_t length);
classic_profile_t classic_profile_from_descriptor(const uint8_t *data,
                                                   size_t length);
