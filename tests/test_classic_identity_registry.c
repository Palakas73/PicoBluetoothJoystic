#include "bluetooth/classic_identity_registry.h"
#include <assert.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>

typedef struct {
  uint8_t version, count;
  uint8_t addresses[2][6];
} legacy_record_t;

int main(void) {
  uint8_t addresses[CLASSIC_IDENTITY_CAPACITY][6] = {{0}};
  uint8_t count = 0, version = 0;
  const uint8_t bitdo[6] = {1,2,3,4,5,6};
  const uint8_t switch_pro[6] = {7,8,9,10,11,12};
  const uint8_t ds4[6] = {13,14,15,16,17,18};
  const uint8_t fourth[6] = {19,20,21,22,23,24};
  const uint8_t fifth[6] = {25,26,27,28,29,30};

  /* Existing v2 record: migrate both old identities before adding key-DB
   * entries. The TLV link-key iterator still contains their original keys. */
  legacy_record_t old = {.version = 2, .count = 2,
                         .addresses = {{1,2,3,4,5,6}, {7,8,9,10,11,12}}};
  assert(classic_identity_record_decode((const uint8_t *)&old, sizeof(old),
                                         addresses, &count, &version));
  assert(version == 2 && count == 2);
  assert(classic_identity_add(addresses, &count, bitdo));
  assert(classic_identity_add(addresses, &count, switch_pro));
  assert(classic_identity_add(addresses, &count, ds4)); /* newly paired */
  assert(classic_identity_add(addresses, &count, fourth));
  assert(count == 4);
  assert(!classic_identity_add(addresses, &count, fifth));
  assert(classic_identity_contains(addresses, count, bitdo));
  assert(classic_identity_contains(addresses, count, switch_pro));
  assert(classic_identity_contains(addresses, count, ds4));
  assert(classic_identity_contains(addresses, count, fourth));

  classic_identity_record_t current;
  classic_identity_record_encode(&current, addresses, count);
  assert(current.version == 3 && current.count == 4);
  uint8_t restored[CLASSIC_IDENTITY_CAPACITY][6] = {{0}};
  uint8_t restored_count = 0, restored_version = 0;
  assert(classic_identity_record_decode((const uint8_t *)&current, sizeof(current),
                                         restored, &restored_count,
                                         &restored_version));
  assert(restored_version == 3 && restored_count == 4);
  assert(memcmp(restored, addresses, sizeof(addresses)) == 0);

  /* Selective removal frees one place without evicting other identities. */
  assert(classic_identity_remove(restored, &restored_count, switch_pro));
  assert(restored_count == 3);
  assert(!classic_identity_contains(restored, restored_count, switch_pro));
  assert(classic_identity_contains(restored, restored_count, bitdo));
  assert(classic_identity_contains(restored, restored_count, ds4));
  assert(classic_identity_add(restored, &restored_count, fifth));
  assert(restored_count == 4);

  bool connected[CLASSIC_IDENTITY_CAPACITY] = {false, false, false, false};
  bool reserved[CLASSIC_IDENTITY_CAPACITY] = {false, false, false, false};
  assert(classic_identity_lru_victim(restored, restored_count, connected,
                                     reserved) == 0);
  connected[0] = true;
  assert(classic_identity_lru_victim(restored, restored_count, connected,
                                     reserved) == 1);
  connected[0] = false;
  reserved[0] = true;
  assert(classic_identity_lru_victim(restored, restored_count, connected,
                                     reserved) == 1);
  reserved[1] = reserved[2] = reserved[3] = true;
  assert(classic_identity_lru_victim(restored, restored_count, connected,
                                     reserved) == -1);

  /* Preparation/failure is non-mutating; only explicit commit replaces the
   * chosen disconnected LRU identity. */
  uint8_t before[CLASSIC_IDENTITY_CAPACITY][6];
  memcpy(before, restored, sizeof(before));
  const uint8_t replacement[6] = {31,32,33,34,35,36};
  assert(memcmp(before, restored, sizeof(before)) == 0); /* failed pairing */
  assert(classic_identity_replace_lru(restored, &restored_count, before[0], replacement));
  assert(restored_count == 4);
  assert(classic_identity_contains(restored, restored_count, replacement));
  assert(!classic_identity_contains(restored, restored_count, before[0]));
  assert(classic_identity_contains(restored, restored_count, before[1]));
  assert(memcmp(restored[3], replacement, sizeof(replacement)) == 0);
  assert(!classic_identity_replace_lru(restored, &restored_count, before[1], before[1]));

  /* Truncated/invalid records never masquerade as a valid migration. */
  assert(!classic_identity_record_decode((const uint8_t *)&old, sizeof(old)-1,
                                          addresses, &count, &version));
  old.count = 3;
  assert(!classic_identity_record_decode((const uint8_t *)&old, sizeof(old),
                                          addresses, &count, &version));
  puts("classic identity capacity/migration tests passed");
  return 0;
}
