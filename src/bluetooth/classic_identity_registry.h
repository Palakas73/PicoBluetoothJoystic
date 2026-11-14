#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define CLASSIC_IDENTITY_CAPACITY 4
#define CLASSIC_IDENTITY_RECORD_VERSION 3
#define CLASSIC_IDENTITY_LEGACY_VERSION 2
#define CLASSIC_IDENTITY_ADDRESS_BYTES 6

typedef struct {
  uint8_t version;
  uint8_t count;
  uint8_t addresses[CLASSIC_IDENTITY_CAPACITY][CLASSIC_IDENTITY_ADDRESS_BYTES];
} classic_identity_record_t;

bool classic_identity_record_decode(const uint8_t *data, size_t length,
                                    uint8_t addresses[CLASSIC_IDENTITY_CAPACITY][CLASSIC_IDENTITY_ADDRESS_BYTES],
                                    uint8_t *count, uint8_t *version);
void classic_identity_record_encode(classic_identity_record_t *record,
                                    const uint8_t addresses[CLASSIC_IDENTITY_CAPACITY][CLASSIC_IDENTITY_ADDRESS_BYTES],
                                    uint8_t count);
bool classic_identity_contains(const uint8_t addresses[CLASSIC_IDENTITY_CAPACITY][CLASSIC_IDENTITY_ADDRESS_BYTES],
                               uint8_t count, const uint8_t address[CLASSIC_IDENTITY_ADDRESS_BYTES]);
/* Returns false only when the address is new and the registry is full. */
bool classic_identity_add(uint8_t addresses[CLASSIC_IDENTITY_CAPACITY][CLASSIC_IDENTITY_ADDRESS_BYTES],
                          uint8_t *count, const uint8_t address[CLASSIC_IDENTITY_ADDRESS_BYTES]);
bool classic_identity_remove(uint8_t addresses[CLASSIC_IDENTITY_CAPACITY][CLASSIC_IDENTITY_ADDRESS_BYTES],
                             uint8_t *count, const uint8_t address[CLASSIC_IDENTITY_ADDRESS_BYTES]);
int classic_identity_lru_victim(
    const uint8_t addresses[CLASSIC_IDENTITY_CAPACITY][CLASSIC_IDENTITY_ADDRESS_BYTES],
    uint8_t count, const bool connected[CLASSIC_IDENTITY_CAPACITY],
    const bool reserved[CLASSIC_IDENTITY_CAPACITY]);
bool classic_identity_replace(
    uint8_t addresses[CLASSIC_IDENTITY_CAPACITY][CLASSIC_IDENTITY_ADDRESS_BYTES],
    uint8_t count, uint8_t victim_index,
    const uint8_t candidate[CLASSIC_IDENTITY_ADDRESS_BYTES]);
bool classic_identity_replace_lru(
    uint8_t addresses[CLASSIC_IDENTITY_CAPACITY][CLASSIC_IDENTITY_ADDRESS_BYTES],
    uint8_t *count, const uint8_t victim[CLASSIC_IDENTITY_ADDRESS_BYTES],
    const uint8_t candidate[CLASSIC_IDENTITY_ADDRESS_BYTES]);
