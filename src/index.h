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

#ifndef ROMKATV_GITSTATUS_INDEX_H_
#define ROMKATV_GITSTATUS_INDEX_H_

#include <sys/stat.h>

#include <git2.h>

#include <cstddef>
#include <string>
#include <vector>

#include "arena.h"
#include "string_view.h"

namespace gitstatus {

struct IndexDir {
  explicit IndexDir(Arena* arena) : entries(arena) {}

  StringView path;
  size_t depth = 0;
  struct stat st = {};
  ArenaVector<const git_index_entry*> entries;

  std::string arena;
  std::vector<size_t> unmatched;
};

class Index {
 public:
  Index(const char* root_dir, git_index* index);

  void GetDirtyCandidates(ArenaVector<const char*>& candidates);

 private:
  void InitDirs(git_index* index);
  void InitSplits(size_t index_size);

  Arena arena_;
  ArenaVector<IndexDir*> dirs_;
  ArenaVector<size_t> splits_;
  const char* root_dir_;
};

}  // namespace gitstatus

#endif  // ROMKATV_GITSTATUS_GIT_H_
