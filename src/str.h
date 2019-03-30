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

#ifndef ROMKATV_GITSTATUS_STR_H_
#define ROMKATV_GITSTATUS_STR_H_

#include <ctype.h>
#include <string.h>

#include "string_view.h"

namespace gitstatus {

struct StrCmp {
  explicit StrCmp(bool case_sensitive) : case_sensitive(case_sensitive) {}

  int operator()(char x, char y) const { return case_sensitive ? x - y : tolower(x) - tolower(y); }

  int operator()(const char* x, const char* y) const {
    return case_sensitive ? strcmp(x, y) : strcasecmp(x, y);
  }

  int operator()(StringView x, StringView y) const {
    size_t n = std::min(x.len, y.len);
    int cmp = case_sensitive ? std::memcmp(x.ptr, y.ptr, n) : strncasecmp(x.ptr, y.ptr, n);
    if (cmp) return cmp;
    return static_cast<int>(x.len) - static_cast<int>(y.len);
  }

  int operator()(StringView x, const char* y) const {
    for (const char *p = x.ptr, *e = p + x.len; p != e; ++p, ++y) {
      int cmp = tolower(*p) - tolower(*y);
      if (cmp) return cmp;
    }
    return 0 - *y;
  }

  int operator()(const char* x, StringView y) const { return -operator()(y, x); }

  bool case_sensitive;
};

struct StrLt : private StrCmp {
  explicit StrLt(bool case_sensitive) : StrCmp(case_sensitive) {}

  bool operator()(char x, char y) const { return StrCmp::operator()(x, y) < 0; }
  bool operator()(const char* x, const char* y) const { return StrCmp::operator()(x, y) < 0; }
  bool operator()(StringView x, const char* y) const { return StrCmp::operator()(x, y) < 0; }
  bool operator()(const char* x, StringView y) const { return StrCmp::operator()(x, y) < 0; }
  bool operator()(StringView x, StringView y) const { return StrCmp::operator()(x, y) < 0; }
};

struct StrEq : private StrCmp {
  explicit StrEq(bool case_sensitive) : StrCmp(case_sensitive) {}

  bool operator()(char x, char y) const { return StrCmp::operator()(x, y) == 0; }
  bool operator()(const char* x, const char* y) const { return StrCmp::operator()(x, y) == 0; }
  bool operator()(StringView x, const char* y) const { return StrCmp::operator()(x, y) == 0; }
  bool operator()(const char* x, StringView y) const { return StrCmp::operator()(x, y) == 0; }
  bool operator()(StringView x, StringView y) const {
    return x.len == y.len && StrCmp::operator()(x, y) == 0;
  }
};

struct StrStartsWith {
  explicit StrStartsWith(bool case_sensitive) : case_sensitive(case_sensitive) {}

  bool operator()(StringView s, StringView prefix) const {
    if (s.len < prefix.len) return false;
    return case_sensitive ? std::memcmp(s.ptr, prefix.ptr, prefix.len) == 0
                          : strncasecmp(s.ptr, prefix.ptr, prefix.len) == 0;
  }

  bool case_sensitive;
};

struct Str {
  explicit Str(bool case_sensitive)
      : case_sensitive(case_sensitive),
        Cmp(case_sensitive),
        Lt(case_sensitive),
        Eq(case_sensitive),
        StartsWith(case_sensitive) {}

  bool case_sensitive;
  StrCmp Cmp;
  StrLt Lt;
  StrEq Eq;
  StrStartsWith StartsWith;
};

}  // namespace gitstatus

#endif  // ROMKATV_GITSTATUS_STR_H_
