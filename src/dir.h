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

#ifndef ROMKATV_GITSTATUS_DIR_H_
#define ROMKATV_GITSTATUS_DIR_H_

#include <cstddef>
#include <string>
#include <vector>

namespace gitstatus {

bool ListDir(const char* dirname, std::string& arena, std::vector<size_t>& entries);

}  // namespace gitstatus

#endif  // ROMKATV_GITSTATUS_DIR_H_
