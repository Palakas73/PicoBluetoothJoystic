#include "guide_fat.h"
#include <stdbool.h>
#include <string.h>

enum { RESERVED = 1, FAT_SECTORS = 6, ROOT_ENTRIES = 32,
       ROOT_SECTORS = 2, FAT_START = 1, ROOT_START = 7, DATA_START = 9 };
static uint16_t clusters_for(uint32_t size) {
  return (uint16_t)((size + GUIDE_BLOCK_SIZE - 1u) / GUIDE_BLOCK_SIZE);
}
static uint16_t first_cluster(const guide_file_t *files, uint8_t index) {
  uint16_t cluster = 2;
  for (uint8_t i = 0; i < index; i++) cluster += clusters_for(files[i].size);
  return cluster;
}
bool guide_fat_layout_valid(const guide_file_t *files, uint8_t count) {
  if (!files || !count || count > ROOT_ENTRIES - 1) return false;
  uint32_t used = 0;
  for (uint8_t i = 0; i < count; i++) {
    if (!files[i].data || !files[i].size) return false;
    used += clusters_for(files[i].size);
  }
  return used <= GUIDE_BLOCK_COUNT - DATA_START;
}
static uint16_t fat_entry(const guide_file_t *files, uint8_t count,
                          uint16_t cluster) {
  if (cluster == 0) return 0xff8;
  if (cluster == 1) return 0xfff;
  for (uint8_t i = 0; i < count; i++) {
    uint16_t first = first_cluster(files, i), n = clusters_for(files[i].size);
    if (cluster >= first && cluster < first + n)
      return cluster + 1u == first + n ? 0xfff : (uint16_t)(cluster + 1u);
  }
  return 0;
}
static uint8_t fat_byte(const guide_file_t *files, uint8_t count, uint32_t pos) {
  uint16_t pair = (uint16_t)((pos / 3u) * 2u);
  uint16_t a = fat_entry(files, count, pair);
  uint16_t b = fat_entry(files, count, pair + 1u);
  switch (pos % 3u) {
  case 0: return (uint8_t)a;
  case 1: return (uint8_t)((a >> 8) | ((b & 0x0f) << 4));
  default: return (uint8_t)(b >> 4);
  }
}
static void put16(uint8_t *p, uint16_t v) { p[0]=(uint8_t)v; p[1]=(uint8_t)(v>>8); }
static void put32(uint8_t *p, uint32_t v) {
  p[0]=(uint8_t)v; p[1]=(uint8_t)(v>>8); p[2]=(uint8_t)(v>>16); p[3]=(uint8_t)(v>>24);
}
static void boot_sector(uint8_t sector[GUIDE_BLOCK_SIZE]) {
  memset(sector, 0, GUIDE_BLOCK_SIZE);
  sector[0]=0xeb; sector[1]=0x3c; sector[2]=0x90;
  memcpy(sector+3,"PICOJOY ",8); put16(sector+11,GUIDE_BLOCK_SIZE);
  sector[13]=1; put16(sector+14,RESERVED); sector[16]=1;
  put16(sector+17,ROOT_ENTRIES); put16(sector+19,GUIDE_BLOCK_COUNT);
  sector[21]=0xf8; put16(sector+22,FAT_SECTORS); put16(sector+24,32);
  put16(sector+26,64); sector[36]=0x80; sector[38]=0x29;
  put32(sector+39,0x50474342u); memcpy(sector+43,"PICO_GUIDE ",11);
  memcpy(sector+54,"FAT12   ",8); sector[510]=0x55; sector[511]=0xaa;
}
static void root_entry(uint8_t *e, const guide_file_t *file, uint16_t cluster) {
  memset(e, 0, 32); memcpy(e,file->name,8); memcpy(e+8,file->ext,3);
  e[11]=0x21; /* read-only + archive */
  put16(e+22,0x6800); put16(e+24,0x5d68); /* fixed 2026-11-08 13:00 */
  put16(e+26,cluster); put32(e+28,file->size);
}
static void root_sector(const guide_file_t *files, uint8_t count,
                        uint32_t sector_index, uint8_t sector[GUIDE_BLOCK_SIZE]) {
  memset(sector,0,GUIDE_BLOCK_SIZE);
  for (uint32_t i=0;i<GUIDE_BLOCK_SIZE/32;i++) {
    uint32_t entry = sector_index*(GUIDE_BLOCK_SIZE/32)+i;
    uint8_t *e=sector+i*32;
    if (entry==0) { memcpy(e,"PICO_GUIDE ",11); e[11]=0x09; }
    else if (entry<=count) root_entry(e,&files[entry-1],first_cluster(files,(uint8_t)(entry-1)));
  }
}
static void data_sector(const guide_file_t *files, uint8_t count,
                        uint32_t data_index, uint8_t sector[GUIDE_BLOCK_SIZE]) {
  memset(sector,0,GUIDE_BLOCK_SIZE);
  uint32_t base=0;
  for (uint8_t i=0;i<count;i++) {
    uint32_t sectors=clusters_for(files[i].size);
    if (data_index>=base && data_index<base+sectors) {
      uint32_t pos=(data_index-base)*GUIDE_BLOCK_SIZE;
      uint32_t n=files[i].size-pos;
      if (n>GUIDE_BLOCK_SIZE) n=GUIDE_BLOCK_SIZE;
      if (n) memcpy(sector,files[i].data+pos,n);
      return;
    }
    base+=sectors;
  }
}
int32_t guide_fat_read(const guide_file_t *files, uint8_t count,
                       uint32_t lba, uint32_t offset,
                       void *buffer, uint32_t size) {
  if (!guide_fat_layout_valid(files,count) || !buffer || lba>=GUIDE_BLOCK_COUNT ||
      offset>=GUIDE_BLOCK_SIZE || size>GUIDE_BLOCK_SIZE-offset) return -1;
  /* One bounded scratch sector, never a RAM copy of the virtual disk. TinyUSB
   * invokes MSC callbacks serially on this firmware's single USB task. */
  static uint8_t sector[GUIDE_BLOCK_SIZE];
  if (lba==0) boot_sector(sector);
  else if (lba>=FAT_START && lba<FAT_START+FAT_SECTORS) {
    uint32_t start=(lba-FAT_START)*GUIDE_BLOCK_SIZE;
    for (uint32_t i=0;i<GUIDE_BLOCK_SIZE;i++) sector[i]=fat_byte(files,count,start+i);
  } else if (lba>=ROOT_START && lba<ROOT_START+ROOT_SECTORS)
    root_sector(files,count,lba-ROOT_START,sector);
  else if (lba>=DATA_START) data_sector(files,count,lba-DATA_START,sector);
  else memset(sector,0,sizeof(sector));
  memcpy(buffer,sector+offset,size);
  return (int32_t)size;
}
