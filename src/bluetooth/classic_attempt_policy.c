#include "classic_attempt_policy.h"
#include <string.h>

void classic_attempt_policy_init(classic_attempt_policy_t *policy) {
  memset(policy, 0, sizeof(*policy));
}

uint32_t classic_attempt_direct_started(classic_attempt_policy_t *policy,
                                        uint16_t cid) {
  policy->generation++;
  if (!policy->generation) policy->generation = 1;
  policy->phase = CLASSIC_ATTEMPT_DIRECT;
  policy->cid = cid;
  policy->cancel_submitted = false;
  return policy->generation;
}

bool classic_attempt_timeout(classic_attempt_policy_t *policy,
                             uint32_t generation) {
  if (generation != policy->generation ||
      policy->phase != CLASSIC_ATTEMPT_DIRECT) return false;
  policy->phase = CLASSIC_ATTEMPT_CANCEL_PENDING;
  return true;
}

void classic_attempt_cancel_submitted(classic_attempt_policy_t *policy) {
  if (policy->phase == CLASSIC_ATTEMPT_CANCEL_PENDING)
    policy->cancel_submitted = true;
}

bool classic_attempt_completed(classic_attempt_policy_t *policy,
                               uint32_t generation, uint16_t cid) {
  if (generation != policy->generation || cid != policy->cid ||
      (policy->phase != CLASSIC_ATTEMPT_DIRECT &&
       policy->phase != CLASSIC_ATTEMPT_CANCEL_PENDING)) return false;
  policy->phase = CLASSIC_ATTEMPT_IDLE;
  policy->cid = 0;
  policy->cancel_submitted = false;
  return true;
}

void classic_attempt_inquiry_started(classic_attempt_policy_t *policy) {
  policy->phase = CLASSIC_ATTEMPT_INQUIRY;
}

void classic_attempt_inquiry_completed(classic_attempt_policy_t *policy) {
  if (policy->phase == CLASSIC_ATTEMPT_INQUIRY)
    policy->phase = CLASSIC_ATTEMPT_IDLE;
}

void classic_attempt_page_scan_started(classic_attempt_policy_t *policy) {
  policy->phase = CLASSIC_ATTEMPT_PAGE_SCAN;
}

void classic_attempt_page_scan_completed(classic_attempt_policy_t *policy) {
  if (policy->phase == CLASSIC_ATTEMPT_PAGE_SCAN)
    policy->phase = CLASSIC_ATTEMPT_IDLE;
}

const char *classic_attempt_phase_name(classic_attempt_phase_t phase) {
  switch (phase) {
    case CLASSIC_ATTEMPT_IDLE: return "idle";
    case CLASSIC_ATTEMPT_DIRECT: return "direct";
    case CLASSIC_ATTEMPT_CANCEL_PENDING: return "cancel-pending";
    case CLASSIC_ATTEMPT_INQUIRY: return "inquiry";
    case CLASSIC_ATTEMPT_PAGE_SCAN: return "page-scan";
    default: return "invalid";
  }
}

void classic_peer_rotation_init(classic_peer_rotation_t *rotation) {
  memset(rotation, 0, sizeof(*rotation));
  rotation->preferred = -1;
}

void classic_peer_rotation_set_count(classic_peer_rotation_t *rotation,
                                     uint8_t count) {
  rotation->count = count;
  if (count == 0) rotation->cursor = 0;
  else rotation->cursor %= count;
  if (rotation->preferred >= (int8_t)count) rotation->preferred = -1;
}

void classic_peer_rotation_prefer(classic_peer_rotation_t *rotation,
                                  uint8_t index) {
  if (index < rotation->count) rotation->preferred = (int8_t)index;
}

int classic_peer_rotation_next(classic_peer_rotation_t *rotation) {
  if (!rotation->count) return -1;
  uint8_t result;
  if (rotation->preferred >= 0) {
    result = (uint8_t)rotation->preferred;
    rotation->preferred = -1;
  } else {
    result = rotation->cursor;
  }
  rotation->cursor = (uint8_t)((result + 1) % rotation->count);
  return result;
}
