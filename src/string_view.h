// Copyright 2019 Roman Perepelitsa.
//
// This file is part of GitStatus.
//
// GitStatus is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, either version 3 of the License, or
// (at your option) any later version.
//
// GitStatus is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
// GNU General Public License for more details.
//
// You should have received a copy of the GNU General Public License
// along with GitStatus. If not, see <https://www.gnu.org/licenses/>.

#ifndef ROMKATV_GITSTATUS_STRING_VIEW_H_
#define ROMKATV_GITSTATUS_STRING_VIEW_H_

#include <algorithm>
#include <cstddef>
#include <cstring>
#include <ostream>
#include <string>

namespace gitstatus {

// WARNING: Embedded null characters cause UB.
// WARNING: Strings longer than INT_MAX cause UB.
struct StringView {
  StringView() : StringView("") {}
  StringView(const std::string& ptr) : StringView(ptr.c_str(), ptr.size()) {}
  StringView(const char* ptr) : StringView(ptr, ptr ? std::strlen(ptr) : 0) {}
  StringView(const char* ptr, size_t len) : ptr(ptr), len(len) {}
  StringView(const char* begin, const char* end) : StringView(begin, end - begin) {}

  bool StartsWith(StringView prefix) const {
    return len >= prefix.len && !std::memcmp(ptr, prefix.ptr, prefix.len);
  }

  const char* ptr;
  size_t len;
};

inline std::ostream& operator<<(std::ostream& strm, StringView s) {
  if (s.ptr) strm.write(s.ptr, s.len);
  return strm;
}

inline int Cmp(StringView x, StringView y) {
  size_t n = std::min(x.len, y.len);
  int cmp = std::memcmp(x.ptr, y.ptr, n);
  if (cmp) return cmp;
  return static_cast<int>(x.len) - static_cast<int>(y.len);
}

inline int Cmp(StringView x, const char* y) {
  for (const char *p = x.ptr, *e = p + x.len; p != e; ++p, ++y) {
    int cmp = *p - *y;
    if (cmp) return cmp;
  }
  return 0 - *y;
}

inline int Cmp(const char* x, StringView y) { return -Cmp(y, x); }

inline bool operator<(StringView x, StringView y) { return Cmp(x, y) < 0; }
inline bool operator<(StringView x, const char* y) { return Cmp(x, y) < 0; }
inline bool operator<(const char* x, StringView y) { return Cmp(x, y) < 0; }

}  // namespace gitstatus

#endif  // ROMKATV_GITSTATUS_STRING_VIEW_H_
