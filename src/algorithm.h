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

#ifndef ROMKATV_GITSTATUS_ALGORITHM_H_
#define ROMKATV_GITSTATUS_ALGORITHM_H_

#include <string.h>  // because there is no std::strcasecmp in C++

#include <algorithm>
#include <cstring>
#include <vector>

#include "string_cmp.h"
#include "string_view.h"

namespace gitstatus {

template <class A>
void Sort(std::vector<const char*, A>& v, bool case_sensitive = true) {
  if (case_sensitive) {
    std::qsort(v.data(), v.size(), sizeof(const char*), [](const void* a, const void* b) {
      auto Str = [](const void* p) { return *static_cast<const char* const*>(p); };
      return std::strcmp(Str(a), Str(b));
    });
  } else {
    std::qsort(v.data(), v.size(), sizeof(const char*), [](const void* a, const void* b) {
      auto Str = [](const void* p) { return *static_cast<const char* const*>(p); };
      return strcasecmp(Str(a), Str(b));
    });
  }
}

template <class A>
void Sort(std::vector<StringView, A>& v, bool case_sensitive = true) {
  if (case_sensitive) {
    std::qsort(v.data(), v.size(), sizeof(StringView), [](const void* a, const void* b) {
      auto Str = [](const void* p) { return *static_cast<const StringView*>(p); };
      return StrCmp(true)(Str(a), Str(b));
    });
  } else {
    std::qsort(v.data(), v.size(), sizeof(StringView), [](const void* a, const void* b) {
      auto Str = [](const void* p) { return *static_cast<const StringView*>(p); };
      return StrCmp(false)(Str(a), Str(b));
    });
  }
}

// Requires: Iter is a BidirectionalIterator.
//
// Returns iterator pointing to the last value in [begin, end) that compares equal to the value, or
// begin if none compare equal.
template <class Iter, class T>
Iter FindLast(Iter begin, Iter end, const T& val) {
  while (begin != end && !(*--end == val)) {}
  return end;
}

}  // namespace gitstatus

#endif  // ROMKATV_GITSTATUS_ALGORITHM_H_
