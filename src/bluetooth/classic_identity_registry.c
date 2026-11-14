#include "classic_identity_registry.h"
#include <stddef.h>
#include <string.h>

enum { LEGACY_ADDRESS_COUNT = 2 };
typedef struct {
  uint8_t version;
  uint8_t count;
  uint8_t addresses[LEGACY_ADDRESS_COUNT][CLASSIC_IDENTITY_ADDRESS_BYTES];
} classic_identity_record_v2_t;

bool classic_identity_contains(
    const uint8_t addresses[CLASSIC_IDENTITY_CAPACITY][CLASSIC_IDENTITY_ADDRESS_BYTES],
    uint8_t count, const uint8_t address[CLASSIC_IDENTITY_ADDRESS_BYTES]) {
  if (!addresses || !address || count > CLASSIC_IDENTITY_CAPACITY) return false;
  for (uint8_t i = 0; i < count; ++i)
    if (memcmp(addresses[i], address, CLASSIC_IDENTITY_ADDRESS_BYTES) == 0)
      return true;
  return false;
}

bool classic_identity_add(
    uint8_t addresses[CLASSIC_IDENTITY_CAPACITY][CLASSIC_IDENTITY_ADDRESS_BYTES],
    uint8_t *count, const uint8_t address[CLASSIC_IDENTITY_ADDRESS_BYTES]) {
  if (!addresses || !count || !address || *count > CLASSIC_IDENTITY_CAPACITY)
    return false;
  if (classic_identity_contains(addresses, *count, address)) return true;
  if (*count == CLASSIC_IDENTITY_CAPACITY) return false;
  memcpy(addresses[(*count)++], address, CLASSIC_IDENTITY_ADDRESS_BYTES);
  return true;
}

bool classic_identity_remove(
    uint8_t addresses[CLASSIC_IDENTITY_CAPACITY][CLASSIC_IDENTITY_ADDRESS_BYTES],
    uint8_t *count, const uint8_t address[CLASSIC_IDENTITY_ADDRESS_BYTES]) {
  if (!addresses || !count || !address || *count > CLASSIC_IDENTITY_CAPACITY)
    return false;
  for (uint8_t i = 0; i < *count; ++i) {
    if (memcmp(addresses[i], address, CLASSIC_IDENTITY_ADDRESS_BYTES) != 0)
      continue;
    if (i + 1 < *count)
      memmove(addresses[i], addresses[i + 1],
              (size_t)(*count - i - 1) * CLASSIC_IDENTITY_ADDRESS_BYTES);
    --*count;
    memset(addresses[*count], 0, CLASSIC_IDENTITY_ADDRESS_BYTES);
    return true;
  }
  return false;
}

int classic_identity_lru_victim(
    const uint8_t addresses[CLASSIC_IDENTITY_CAPACITY][CLASSIC_IDENTITY_ADDRESS_BYTES],
    uint8_t count, const bool connected[CLASSIC_IDENTITY_CAPACITY],
    const bool reserved[CLASSIC_IDENTITY_CAPACITY]) {
  (void)addresses;
  if (count > CLASSIC_IDENTITY_CAPACITY) return -1;
  for (uint8_t i = 0; i < count; ++i)
    if ((!connected || !connected[i]) && (!reserved || !reserved[i])) return i;
  return -1;
}

bool classic_identity_replace(
    uint8_t addresses[CLASSIC_IDENTITY_CAPACITY][CLASSIC_IDENTITY_ADDRESS_BYTES],
    uint8_t count, uint8_t victim_index,
    const uint8_t candidate[CLASSIC_IDENTITY_ADDRESS_BYTES]) {
  if (!addresses || !candidate || count != CLASSIC_IDENTITY_CAPACITY ||
      victim_index >= count ||
      classic_identity_contains((const uint8_t (*)[CLASSIC_IDENTITY_ADDRESS_BYTES])addresses,
                                count, candidate))
    return false;
  memcpy(addresses[victim_index], candidate, CLASSIC_IDENTITY_ADDRESS_BYTES);
  return true;
}

bool classic_identity_replace_lru(
    uint8_t addresses[CLASSIC_IDENTITY_CAPACITY][CLASSIC_IDENTITY_ADDRESS_BYTES],
    uint8_t *count, const uint8_t victim[CLASSIC_IDENTITY_ADDRESS_BYTES],
    const uint8_t candidate[CLASSIC_IDENTITY_ADDRESS_BYTES]) {
  if (!addresses || !count || !victim || !candidate ||
      *count != CLASSIC_IDENTITY_CAPACITY ||
      classic_identity_contains((const uint8_t (*)[CLASSIC_IDENTITY_ADDRESS_BYTES])addresses,
                                *count, candidate)) return false;
  if (!classic_identity_remove(addresses, count, victim)) return false;
  return classic_identity_add(addresses, count, candidate);
}

bool classic_identity_record_decode(
    const uint8_t *data, size_t length,
    uint8_t addresses[CLASSIC_IDENTITY_CAPACITY][CLASSIC_IDENTITY_ADDRESS_BYTES],
    uint8_t *count, uint8_t *version) {
  if (!data || !addresses || !count || !version || length < 2) return false;
  memset(addresses, 0, CLASSIC_IDENTITY_CAPACITY * CLASSIC_IDENTITY_ADDRESS_BYTES);
  *count = 0;
  *version = data[0];
  if (data[0] == CLASSIC_IDENTITY_LEGACY_VERSION) {
    if (length != sizeof(classic_identity_record_v2_t) || data[1] > LEGACY_ADDRESS_COUNT)
      return false;
    for (uint8_t i = 0; i < data[1]; ++i)
      if (!classic_identity_add(addresses, count,
                                data + offsetof(classic_identity_record_v2_t, addresses) +
                                       (size_t)i * CLASSIC_IDENTITY_ADDRESS_BYTES))
        return false;
    return true;
  }
  if (data[0] == CLASSIC_IDENTITY_RECORD_VERSION) {
    if (length != sizeof(classic_identity_record_t) || data[1] > CLASSIC_IDENTITY_CAPACITY)
      return false;
    for (uint8_t i = 0; i < data[1]; ++i)
      if (!classic_identity_add(addresses, count,
                                data + offsetof(classic_identity_record_t, addresses) +
                                       (size_t)i * CLASSIC_IDENTITY_ADDRESS_BYTES))
        return false;
    return true;
  }
  return false;
}

void classic_identity_record_encode(
    classic_identity_record_t *record,
    const uint8_t addresses[CLASSIC_IDENTITY_CAPACITY][CLASSIC_IDENTITY_ADDRESS_BYTES],
    uint8_t count) {
  if (!record) return;
  memset(record, 0, sizeof(*record));
  record->version = CLASSIC_IDENTITY_RECORD_VERSION;
  record->count = count > CLASSIC_IDENTITY_CAPACITY ? CLASSIC_IDENTITY_CAPACITY : count;
  if (addresses)
    memcpy(record->addresses, addresses,
           (size_t)record->count * CLASSIC_IDENTITY_ADDRESS_BYTES);
}
