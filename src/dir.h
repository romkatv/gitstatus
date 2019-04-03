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
#include <vector>

#include "arena.h"

namespace gitstatus {

// On error, returns -1. Does not throw.
//
// On success, returns the number of elements written to `entries`. Every element is a
// null-terminated file name. At -1 offset is its d_type. All elements point into the arena.
// They are sorted either by strcmp or strcasecmp depending on case_sensitive.
//
// Does not close dir_fd.
//
// The reason this API is so fucked up is performance on Linux. Elsewhere it's 20% slower. For best
// results, do not clear entries between ListDir() calls to avoid uselessly zeroing memory. And
// reuse the arena of course.
ssize_t ListDir(int dir_fd, Arena& arena, std::vector<char*>& entries, bool case_sensitive);

}  // namespace gitstatus

#endif  // ROMKATV_GITSTATUS_DIR_H_
