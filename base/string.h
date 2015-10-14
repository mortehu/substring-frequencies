#ifndef BASE_STRING_H_
#define BASE_STRING_H_

#include <string>

#include "base/stringref.h"

namespace ev {

inline bool HasPrefix(const ev::StringRef& haystack,
                      const ev::StringRef& needle) {
  if (haystack.size() < needle.size()) return false;
  return 0 == memcmp(haystack.begin(), needle.begin(), needle.size());
}

inline bool HasSuffix(const ev::StringRef& haystack,
                      const ev::StringRef& needle) {
  if (haystack.size() < needle.size()) return false;
  return 0 == memcmp(haystack.begin() + haystack.size() - needle.size(),
                     needle.begin(), needle.size());
}
}  // namespace ev

#endif  // !BASE_STRING_H_
