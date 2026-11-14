#pragma once
#if defined(_MSC_VER) && !defined(__clang__)
/* MSVC's C compiler lacks stdbool.h; the firmware uses standard C11. */
#ifndef bool
#define bool unsigned char
#define true 1
#define false 0
#endif
#else
#include <stdbool.h>
#endif
#include <stdint.h>

typedef struct {
  uint8_t address[6];
  uint8_t type;
  bool valid;
} ble_identity_t;

typedef struct {
  ble_identity_t target;
  bool pairing_allowed;
  uint8_t failures;
  uint8_t rejected[4][6];
  uint8_t rejected_type[4];
  uint32_t rejected_at[4];
  uint8_t reject_next;
} reconnect_policy_t;

void reconnect_policy_init(reconnect_policy_t *policy, bool pairing_allowed);
void reconnect_policy_set_target(reconnect_policy_t *policy,
                                 const uint8_t address[6], uint8_t type);
bool reconnect_policy_is_target(const reconnect_policy_t *policy,
                                const uint8_t address[6], uint8_t type);
bool reconnect_policy_allow_advertisement(const reconnect_policy_t *policy,
                                          const uint8_t address[6], uint8_t type,
                                          bool xbox_candidate);
bool reconnect_policy_should_resolve(const reconnect_policy_t *policy,
                                     const uint8_t address[6], uint8_t type,
                                     uint32_t now_ms);
void reconnect_policy_reject(reconnect_policy_t *policy,
                             const uint8_t address[6], uint8_t type,
                             uint32_t now_ms);
uint32_t reconnect_policy_failed(reconnect_policy_t *policy);
void reconnect_policy_succeeded(reconnect_policy_t *policy);
