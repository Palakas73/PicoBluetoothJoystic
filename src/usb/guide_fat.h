#pragma once
#include <stdbool.h>
#include <stdint.h>

enum { GUIDE_BLOCK_SIZE = 512, GUIDE_BLOCK_COUNT = 2048 };
typedef struct {
  char name[8], ext[3];
  const uint8_t *data;
  uint32_t size;
} guide_file_t;

/* Synthetic immutable FAT12; no backing storage and no write API. */
int32_t guide_fat_read(const guide_file_t *files, uint8_t file_count,
                       uint32_t lba, uint32_t offset,
                       void *buffer, uint32_t size);
bool guide_fat_layout_valid(const guide_file_t *files, uint8_t file_count);
