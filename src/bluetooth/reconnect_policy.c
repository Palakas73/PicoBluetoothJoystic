#include "reconnect_policy.h"
#include <string.h>

void reconnect_policy_init(reconnect_policy_t *p, bool pairing_allowed) {
  memset(p, 0, sizeof(*p));
  p->pairing_allowed = pairing_allowed;
}

void reconnect_policy_set_target(reconnect_policy_t *p,
                                 const uint8_t address[6], uint8_t type) {
  memcpy(p->target.address, address, 6);
  p->target.type = type;
  p->target.valid = true;
  p->pairing_allowed = false;
}

bool reconnect_policy_is_target(const reconnect_policy_t *p,
                                const uint8_t address[6], uint8_t type) {
  return p->target.valid && p->target.type == (type & 1u) &&
         memcmp(p->target.address, address, 6) == 0;
}

bool reconnect_policy_allow_advertisement(const reconnect_policy_t *p,
                                          const uint8_t address[6], uint8_t type,
                                          bool xbox_candidate) {
  return reconnect_policy_is_target(p, address, type) ||
         (!p->target.valid && p->pairing_allowed && xbox_candidate);
}

bool reconnect_policy_should_resolve(const reconnect_policy_t *p,
                                     const uint8_t address[6], uint8_t type,
                                     uint32_t now_ms) {
  if (!p->target.valid || type != 1 || (address[0] & 0xc0u) != 0x40u)
    return false; /* only resolvable private random addresses */
  for (unsigned i = 0; i < 4; ++i)
    if (p->rejected_at[i] && p->rejected_type[i] == type &&
        memcmp(p->rejected[i], address, 6) == 0 &&
        now_ms - p->rejected_at[i] < 10000u)
      return false;
  return true;
}

void reconnect_policy_reject(reconnect_policy_t *p,
                             const uint8_t address[6], uint8_t type,
                             uint32_t now_ms) {
  unsigned i = p->reject_next++ % 4;
  memcpy(p->rejected[i], address, 6);
  p->rejected_type[i] = type;
  p->rejected_at[i] = now_ms ? now_ms : 1;
}

uint32_t reconnect_policy_failed(reconnect_policy_t *p) {
  if (p->failures < 5) ++p->failures;
  return 250u << (p->failures - 1); /* bounded: 250 .. 4000 ms */
}

void reconnect_policy_succeeded(reconnect_policy_t *p) { p->failures = 0; }
