#include "bluetooth/reconnect_policy.h"
#include <assert.h>
#include <stdio.h>

int main(void) {
  reconnect_policy_t p;
  const uint8_t identity[6] = {0x12, 0x34, 0x56, 0x78, 0x9a, 0xbc};
  const uint8_t unknown[6] = {0x11, 0x22, 0x33, 0x44, 0x55, 0x66};
  const uint8_t rpa[6] = {0x45, 0x11, 0x22, 0x33, 0x44, 0x55};
  reconnect_policy_init(&p, true);
  assert(reconnect_policy_allow_advertisement(&p, unknown, 0, true));
  assert(!reconnect_policy_allow_advertisement(&p, unknown, 0, false));
  reconnect_policy_set_target(&p, identity, 0);
  assert(reconnect_policy_allow_advertisement(&p, identity, 0, false));
  assert(reconnect_policy_allow_advertisement(&p, identity, 2, false));
  assert(!reconnect_policy_allow_advertisement(&p, unknown, 0, true));
  assert(!reconnect_policy_allow_advertisement(&p, identity, 1, true));
  assert(reconnect_policy_should_resolve(&p, rpa, 1, 100));
  assert(!reconnect_policy_should_resolve(&p, unknown, 1, 100));
  reconnect_policy_reject(&p, rpa, 1, 100);
  assert(!reconnect_policy_should_resolve(&p, rpa, 1, 101));
  assert(reconnect_policy_should_resolve(&p, rpa, 1, 10100));
  assert(reconnect_policy_failed(&p) == 250);
  assert(reconnect_policy_failed(&p) == 500);
  for (int i = 0; i < 10; ++i) assert(reconnect_policy_failed(&p) <= 4000);
  reconnect_policy_succeeded(&p);
  assert(p.failures == 0);
  assert(reconnect_policy_failed(&p) == 250);
  reconnect_policy_init(&p, false); /* invalid/ambiguous bond recovery */
  assert(!reconnect_policy_allow_advertisement(&p, unknown, 0, true));
  assert(!reconnect_policy_should_resolve(&p, rpa, 1, 100));
  puts("reconnect policy tests passed");
  return 0;
}
