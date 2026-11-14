#pragma once
#include <stdint.h>
void guide_msc_get_stats(uint32_t *calls,uint32_t *bytes,uint32_t *errors,
                         uint32_t *writes,uint32_t *total_us,uint32_t *max_us);
