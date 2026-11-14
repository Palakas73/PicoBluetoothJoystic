#include "bluetooth/classic_attempt_policy.h"
#include <assert.h>
#include <stdio.h>

int main(void) {
  classic_attempt_policy_t policy;
  classic_attempt_policy_init(&policy);

  /* Remembered 8BitDo is off: exactly one cancellation is requested. */
  uint32_t old_generation = classic_attempt_direct_started(&policy, 1);
  assert(classic_attempt_timeout(&policy, old_generation));
  assert(!classic_attempt_timeout(&policy, old_generation));
  classic_attempt_cancel_submitted(&policy);
  assert(policy.cancel_submitted);

  /* Resources are not reused until BTstack completes the old HID attempt. */
  assert(classic_attempt_completed(&policy, old_generation, 1));
  classic_attempt_inquiry_started(&policy);
  assert(policy.phase == CLASSIC_ATTEMPT_INQUIRY);
  classic_attempt_inquiry_completed(&policy);

  /* A new Switch Pro discovered by inquiry can now get its own generation. */
  uint32_t switch_generation = classic_attempt_direct_started(&policy, 2);
  assert(switch_generation != old_generation);
  assert(!classic_attempt_completed(&policy, old_generation, 1));
  assert(policy.phase == CLASSIC_ATTEMPT_DIRECT);
  assert(classic_attempt_completed(&policy, switch_generation, 2));

  classic_peer_rotation_t rotation;
  classic_peer_rotation_init(&rotation);
  classic_peer_rotation_set_count(&rotation, 2);
  /* A disconnected Switch Pro is tried first, then the offline 8BitDo. */
  classic_peer_rotation_prefer(&rotation, 1);
  assert(classic_peer_rotation_next(&rotation) == 1);
  assert(classic_peer_rotation_next(&rotation) == 0);
  assert(classic_peer_rotation_next(&rotation) == 1);

  /* Releasing a Switch connection makes it preferred without pinning rotation. */
  classic_peer_rotation_prefer(&rotation, 1);
  assert(classic_peer_rotation_next(&rotation) == 1);
  assert(classic_peer_rotation_next(&rotation) == 0); /* offline 8BitDo */
  assert(classic_peer_rotation_next(&rotation) == 1); /* Switch is retried */

  /* Quiet page scan accepts incoming reconnect before outgoing paging. */
  classic_attempt_page_scan_started(&policy);
  assert(policy.phase == CLASSIC_ATTEMPT_PAGE_SCAN);
  classic_attempt_page_scan_completed(&policy);
  assert(policy.phase == CLASSIC_ATTEMPT_IDLE);
  /* A late completion from the cancelled pre-disconnect generation is inert. */
  assert(!classic_attempt_completed(&policy, old_generation, 1));

  puts("classic attempt policy tests passed");
  return 0;
}
