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

#include "index.h"

#include <stack>

#include "check.h"

namespace gitstatus {

Index::Index(const char* root_dir, git_index* index)
    : dirs_(&arena_), splits_(&arena_), root_dir_(root_dir) {
  const size_t index_size = git_index_entrycount(index);
  dirs_.reserve(index_size / 8);
  std::stack<IndexDir*> stack;
  for (size_t i = 0; i != index_size; ++i) {
    const git_index_entry* entry = git_index_get_byindex(index, i);
    (void)entry;
  }
}

}  // namespace gitstatus
