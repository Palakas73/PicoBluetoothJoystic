#include "classic_profile.h"
#include "dualshock4.h"
#include <string.h>

static bool contains_casefold(const uint8_t *s, size_t n, const char *needle) {
  size_t m = strlen(needle);
  if (!s || n < m) return false;
  for (size_t i = 0; i + m <= n; ++i) {
    size_t j = 0;
    while (j < m) {
      uint8_t c = s[i + j];
      if (c >= 'A' && c <= 'Z') c = (uint8_t)(c + ('a' - 'A'));
      if (c != (uint8_t)needle[j]) break;
      ++j;
    }
    if (j == m) return true;
  }
  return false;
}

bool classic_name_is_ds4_hint(const uint8_t *n, size_t len) {
  return contains_casefold(n, len, "wireless controller") ||
         contains_casefold(n, len, "dualshock 4");
}

bool classic_name_is_candidate(const uint8_t *n, size_t len) {
  return contains_casefold(n, len, "8bitdo") ||
         (len == 14 && n && memcmp(n, "Pro Controller", 14) == 0) ||
         classic_name_is_ds4_hint(n, len);
}

classic_profile_t classic_profile_from_descriptor(const uint8_t *d, size_t n) {
  if (ds4_report_descriptor_signature(d, n)) return CLASSIC_PROFILE_DUALSHOCK4;
  return CLASSIC_PROFILE_UNKNOWN;
}
