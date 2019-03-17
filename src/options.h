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

#ifndef ROMKATV_GITSTATUS_OPTIONS_H_
#define ROMKATV_GITSTATUS_OPTIONS_H_

#include <string>

namespace gitstatus {

struct Options {
  // If a repo has more files in its index than this, don't scan its files to see what's dirty.
  // Instead, report -1 as the the number of unstaged changes and untracked files.
  size_t dirty_max_index_size = -1;
  // Use this many threads to scan git workdir for unstaged and untracked files. Must be positive.
  size_t num_threads = 1;
  // File descriptor locked by the parent process. Once the lock is gone, gitstatusd will exit.
  // Negative value disables lock monitoring.
  int lock_fd = -1;
};

Options ParseOptions(int argc, char** argv);

}  // namespace gitstatus

#endif  // ROMKATV_GITSTATUS_OPTIONS_H_
