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

#include <algorithm>
#include <cstring>
#include <stack>

#include "check.h"

namespace gitstatus {

/*
typedef struct {
  int32_t seconds;
  uint32_t nanoseconds;
} git_index_time;

typedef struct git_index_entry {
  git_index_time ctime;
  git_index_time mtime;

  uint32_t dev;
  uint32_t ino;
  uint32_t mode;
  uint32_t uid;
  uint32_t gid;
  uint32_t file_size;

  git_oid id;

  uint16_t flags;
  uint16_t flags_extended;

  const char *path;
} git_index_entry;
*/

namespace {

void CommonDir(const char* a, const char* b, size_t* dir_len, size_t* dir_depth) {
  *dir_len = 0;
  *dir_depth = 0;
  for (size_t i = 1; *a == *b && *a; ++i, ++a, ++b) {
    if (*a == '/') {
      *dir_len = i;
      ++*dir_depth;
    }
  }
}

}  // namespace

Index::Index(const char* root_dir, git_index* index)
    : dirs_(&arena_), splits_(&arena_), root_dir_(root_dir) {
  const size_t index_size = git_index_entrycount(index);
  dirs_.reserve(index_size / 8);
  std::stack<IndexDir*> stack;
  stack.push(arena_.DirectInit<IndexDir>(&arena_));

  auto PopDir = [&] {
    CHECK(!stack.empty());
    CHECK(stack.top()->depth + 1 == stack.size());
    LOG(INFO) << " Pop: [" << stack.top()->depth << "] " << "'" << stack.top()->path << "'";
    dirs_.push_back(stack.top());
    stack.pop();
  };

  for (size_t i = 0; i != index_size; ++i) {
    const git_index_entry* entry = git_index_get_byindex(index, i);
    IndexDir* prev = stack.top();
    size_t common_len, common_depth;
    CommonDir(prev->path.ptr, entry->path, &common_len, &common_depth);
    CHECK(common_depth <= prev->depth);

    for (size_t i = common_depth; i != prev->depth; ++i) PopDir();

    for (const char* p = entry->path + common_len; (p = std::strchr(p, '/')); ++p) {
      IndexDir* dir = arena_.DirectInit<IndexDir>(&arena_);
      dir->path = StringView(entry->path, p - entry->path + 1);
      dir->depth = stack.size();
      CHECK(dir->path.ptr[dir->path.len - 1] == '/');
      stack.push(dir);
      LOG(INFO) << "Push: [" << dir->depth << "] " << "'" << dir->path << "'";
    }

    CHECK(!stack.empty());
    IndexDir* dir = stack.top();
    CHECK(!std::memcmp(entry->path, dir->path.ptr, dir->path.len));  // might be expensive
    CHECK(!std::strchr(entry->path + dir->path.len, '/'));
    dir->entries.push_back(entry);
    LOG(INFO) << "Add: [" << dir->depth << "] '" << dir->path << "' += " << entry->path;
  }

  CHECK(!stack.empty());
  do {
    PopDir();
  } while (!stack.empty());
}

}  // namespace gitstatus
