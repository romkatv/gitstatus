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

#include <iterator>
#include <algorithm>
#include <vector>

namespace gitstatus {

template <class Container, class T>
auto BinaryFindLast(Container& c, const T& val) {
  auto begin = std::begin(c);
  auto end = std::end(c);
  auto res = std::upper_bound(begin, end, val);
  if (res == begin) return end;
  --res;
  return *res < val ? end : res;
}

}  // namespace gitstatus

#endif  // ROMKATV_GITSTATUS_ALGORITHM_H_
