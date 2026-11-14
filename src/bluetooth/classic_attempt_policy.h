#ifndef CLASSIC_ATTEMPT_POLICY_H
#define CLASSIC_ATTEMPT_POLICY_H

#include <stdbool.h>
#include <stdint.h>

typedef enum {
  CLASSIC_ATTEMPT_IDLE = 0,
  CLASSIC_ATTEMPT_DIRECT,
  CLASSIC_ATTEMPT_CANCEL_PENDING,
  CLASSIC_ATTEMPT_INQUIRY,
  CLASSIC_ATTEMPT_PAGE_SCAN
} classic_attempt_phase_t;

typedef struct {
  classic_attempt_phase_t phase;
  uint32_t generation;
  uint16_t cid;
  bool cancel_submitted;
} classic_attempt_policy_t;

void classic_attempt_policy_init(classic_attempt_policy_t *policy);
uint32_t classic_attempt_direct_started(classic_attempt_policy_t *policy,
                                        uint16_t cid);
bool classic_attempt_timeout(classic_attempt_policy_t *policy,
                             uint32_t generation);
void classic_attempt_cancel_submitted(classic_attempt_policy_t *policy);
bool classic_attempt_completed(classic_attempt_policy_t *policy,
                               uint32_t generation, uint16_t cid);
void classic_attempt_inquiry_started(classic_attempt_policy_t *policy);
void classic_attempt_inquiry_completed(classic_attempt_policy_t *policy);
void classic_attempt_page_scan_started(classic_attempt_policy_t *policy);
void classic_attempt_page_scan_completed(classic_attempt_policy_t *policy);
const char *classic_attempt_phase_name(classic_attempt_phase_t phase);

typedef struct {
  uint8_t count;
  uint8_t cursor;
  int8_t preferred;
} classic_peer_rotation_t;
void classic_peer_rotation_init(classic_peer_rotation_t *rotation);
void classic_peer_rotation_set_count(classic_peer_rotation_t *rotation,
                                     uint8_t count);
void classic_peer_rotation_prefer(classic_peer_rotation_t *rotation,
                                  uint8_t index);
int classic_peer_rotation_next(classic_peer_rotation_t *rotation);

#endif
