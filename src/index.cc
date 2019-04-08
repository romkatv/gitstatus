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

#include <dirent.h>
#include <fcntl.h>
#include <unistd.h>

#include <algorithm>
#include <condition_variable>
#include <cstdint>
#include <cstring>
#include <iterator>
#include <mutex>
#include <stack>

#include "algorithm.h"
#include "check.h"
#include "dir.h"
#include "index.h"
#include "port.h"
#include "scope_guard.h"
#include "stat.h"
#include "string_cmp.h"
#include "thread_pool.h"

namespace gitstatus {

namespace {

void CommonDir(Str<> str, const char* a, const char* b, size_t* dir_len, size_t* dir_depth) {
  *dir_len = 0;
  *dir_depth = 0;
  for (size_t i = 1; str.Eq(*a, *b) && *a; ++i, ++a, ++b) {
    if (*a == '/') {
      *dir_len = i;
      ++*dir_depth;
    }
  }
}

size_t Weight(const IndexDir& dir) { return 1 + dir.subdirs.size() + dir.files.size(); }

mode_t Mode(mode_t mode) {
  if (S_ISREG(mode)) {
    mode_t perm = mode & 0111 ? 0755 : 0644;
    return S_IFREG | perm;
  }
  return mode & S_IFMT;
}

bool IsModified(const git_index_entry* entry, const struct stat& st) {
  return entry->mtime.seconds != MTim(st).tv_sec ||
         int64_t{entry->mtime.nanoseconds} != MTim(st).tv_nsec || entry->ino != st.st_ino ||
         entry->mode != Mode(st.st_mode) || entry->gid != st.st_gid ||
         int64_t{entry->file_size} != st.st_size;
}

int OpenDir(int parent_fd, const char* name) {
  return openat(parent_fd, name, kNoATime | O_RDONLY | O_DIRECTORY | O_CLOEXEC);
}

void OpenTail(int* fds, size_t nfds, int root_fd, StringView dirname, Arena& arena) {
  CHECK(fds && nfds && root_fd >= 0);
  std::fill(fds, fds + nfds, -1);
  if (!dirname.len) return;
  CHECK(dirname.len > 1);
  CHECK(dirname.ptr[0] != '/');
  CHECK(dirname.ptr[dirname.len - 1] == '/');

  char* begin = arena.StrDup(dirname.ptr, dirname.len - 1);
  WithArena<std::vector<const char*>> subdirs(&arena);
  subdirs.reserve(nfds + 1);

  for (char* sep = begin + dirname.len - 1; subdirs.size() < nfds;) {
    sep = FindLast(begin, sep, '/');
    if (sep == begin) break;
    *sep = 0;
    subdirs.push_back(sep + 1);
  }
  subdirs.push_back(begin);
  if (subdirs.size() < nfds + 1) subdirs.push_back(".");
  CHECK(subdirs.size() <= nfds + 1);

  for (size_t i = subdirs.size(); i != 1; --i) {
    const char* path = subdirs[i - 1];
    if ((root_fd = OpenDir(root_fd, path)) < 0) {
      for (; i != subdirs.size(); ++i) {
        CHECK(!close(fds[i - 1])) << Errno();
        fds[i - 1] = -1;
      }
      return;
    }
    fds[i - 2] = root_fd;
  }
}

std::vector<const char*> ScanDirs(git_index* index, int root_fd, IndexDir* const* begin,
                                  IndexDir* const* end, Tribool untracked_cache) {
  const Str<> str(git_index_is_case_sensitive(index));
  Arena arena({.min_block_size = 8 << 10, .max_block_size = 8 << 10});
  std::vector<const char*> res;
  std::vector<char*> entries;
  entries.reserve(128);

  constexpr ssize_t kDirStackSize = 5;
  int dir_fd[kDirStackSize];
  std::fill(std::begin(dir_fd), std::end(dir_fd), -1);
  auto Close = [](int& fd) {
    if (fd >= 0) {
      CHECK(!close(fd)) << Errno();
      fd = -1;
    }
  };
  auto CloseAll = [&] { std::for_each(std::begin(dir_fd), std::end(dir_fd), Close); };
  ON_SCOPE_EXIT(&) { CloseAll(); };
  if (begin != end) OpenTail(dir_fd, kDirStackSize, root_fd, (*begin)->path, arena);

  for (IndexDir* const* it = begin; it != end; ++it) {
    IndexDir& dir = **it;

    auto Basename = [&](const git_index_entry* e) { return e->path + dir.path.len; };

    auto AddUnmached = [&](StringView basename) {
      if (!basename.len) {
        dir.st = {};
        dir.unmatched.clear();
        dir.arena.Reuse();
      } else if (str.Eq(basename, StringView(".git/"))) {
        return;
      }
      char* path = dir.arena.Allocate<char>(dir.path.len + basename.len + 1);
      std::memcpy(path, dir.path.ptr, dir.path.len);
      std::memcpy(path + dir.path.len, basename.ptr, basename.len);
      path[dir.path.len + basename.len] = 0;
      dir.unmatched.push_back(path);
      res.push_back(path);
    };

    ssize_t d = 0;
    if ((it == begin || (d = it[-1]->depth + 1 - dir.depth) < kDirStackSize) && dir_fd[d] >= 0) {
      CHECK(d >= 0);
      int fd = OpenDir(dir_fd[d], arena.StrDup(dir.basename.ptr, dir.basename.len));
      for (ssize_t i = 0; i != d; ++i) Close(dir_fd[i]);
      std::rotate(dir_fd, dir_fd + (d ? d : kDirStackSize) - 1, dir_fd + kDirStackSize);
      Close(*dir_fd);
      *dir_fd = fd;
    } else {
      CloseAll();
      if (dir.path.len) {
        CHECK(dir.path.ptr[0] != '/');
        CHECK(dir.path.ptr[dir.path.len - 1] == '/');
        *dir_fd = OpenDir(root_fd, arena.StrDup(dir.path.ptr, dir.path.len - 1));
      } else {
        VERIFY((*dir_fd = dup(root_fd)) >= 0) << Errno();
      }
    }
    if (*dir_fd < 0) {
      CloseAll();
      AddUnmached("");
      continue;
    }

    if (untracked_cache != Tribool::kFalse) {
      struct stat st;
      if (fstat(*dir_fd, &st)) {
        AddUnmached("");
        continue;
      }
      if (untracked_cache == Tribool::kTrue && StatEq(st, dir.st)) {
        for (const git_index_entry* file : dir.files) {
          if (fstatat(*dir_fd, Basename(file), &st, AT_SYMLINK_NOFOLLOW)) st = {};
          if (IsModified(file, st)) res.push_back(file->path);  // modified or deleted
        }
        res.insert(res.end(), dir.unmatched.begin(), dir.unmatched.end());
        continue;
      }
      dir.st = st;
    }

    arena.Reuse();
    if (!ListDir(*dir_fd, arena, entries, str.case_sensitive)) {
      AddUnmached("");
      continue;
    }
    dir.unmatched.clear();
    dir.arena.Reuse();

    const git_index_entry* const* file = dir.files.data();
    const git_index_entry* const* file_end = file + dir.files.size();
    const StringView* subdir = dir.subdirs.data();
    const StringView* subdir_end = subdir + dir.subdirs.size();

    for (char* entry : entries) {
      bool matched = false;

      for (; file != file_end; ++file) {
        int cmp = str.Cmp(Basename(*file), entry);
        if (cmp < 0) {
          res.push_back((*file)->path);  // deleted
        } else if (cmp == 0) {
          if (git_index_entry_newer_than_index(*file, index)) {
            res.push_back((*file)->path);  // racy
          } else {
            struct stat st;
            if (fstatat(*dir_fd, entry, &st, AT_SYMLINK_NOFOLLOW)) st = {};
            if (IsModified(*file, st)) res.push_back((*file)->path);  // modified
          }
          matched = true;
          ++file;
          break;
        } else {
          break;
        }
      }

      if (matched) continue;

      for (; subdir != subdir_end; ++subdir) {
        int cmp = str.Cmp(*subdir, entry);
        if (cmp > 0) break;
        if (cmp == 0) {
          matched = true;
          ++subdir;
          break;
        }
      }

      if (!matched) {
        StringView basename(entry);
        if (entry[-1] == DT_DIR) entry[basename.len++] = '/';
        AddUnmached(basename);  // new
      }
    }
  }

  return res;
}

}  // namespace

Index::Index(const char* root_dir, git_index* index)
    : dirs_(&arena_), splits_(&arena_), git_index_(index), root_dir_(root_dir) {
  size_t total_weight = InitDirs(index);
  InitSplits(total_weight);
}

size_t Index::InitDirs(git_index* index) {
  const Str<> str(git_index_is_case_sensitive(index));
  const size_t index_size = git_index_entrycount(index);
  dirs_.reserve(index_size / 8);
  std::stack<IndexDir*> stack;
  stack.push(arena_.DirectInit<IndexDir>(&arena_));

  size_t total_weight = 0;
  auto PopDir = [&] {
    CHECK(!stack.empty());
    IndexDir* top = stack.top();
    CHECK(top->depth + 1 == stack.size());
    if (!std::is_sorted(top->subdirs.begin(), top->subdirs.end(), str.Lt)) {
      StrSort(top->subdirs.begin(), top->subdirs.end(), str.case_sensitive);
    }
    total_weight += Weight(*top);
    dirs_.push_back(top);
    stack.pop();
  };

  for (size_t i = 0; i != index_size; ++i) {
    const git_index_entry* entry = git_index_get_byindex(index, i);
    IndexDir* prev = stack.top();
    size_t common_len, common_depth;
    CommonDir(str, prev->path.ptr, entry->path, &common_len, &common_depth);
    CHECK(common_depth <= prev->depth);

    for (size_t i = common_depth; i != prev->depth; ++i) PopDir();

    for (const char* p = entry->path + common_len; (p = std::strchr(p, '/')); ++p) {
      IndexDir* top = stack.top();
      StringView subdir(entry->path + top->path.len, p);
      top->subdirs.push_back(subdir);
      IndexDir* dir = arena_.DirectInit<IndexDir>(&arena_);
      dir->path = StringView(entry->path, p - entry->path + 1);
      dir->basename = subdir;
      dir->depth = stack.size();
      CHECK(dir->path.ptr[dir->path.len - 1] == '/');
      stack.push(dir);
    }

    CHECK(!stack.empty());
    IndexDir* dir = stack.top();
    dir->files.push_back(entry);
  }

  CHECK(!stack.empty());
  do {
    PopDir();
  } while (!stack.empty());
  std::reverse(dirs_.begin(), dirs_.end());

  return total_weight;
}

void Index::InitSplits(size_t total_weight) {
  constexpr size_t kMinShardWeight = 512;
  const size_t kNumShards = 16 * GlobalThreadPool()->num_threads();
  const size_t shard_weight = std::max(kMinShardWeight, total_weight / kNumShards);

  splits_.reserve(kNumShards + 1);
  splits_.push_back(0);

  for (size_t i = 0, w = 0; i != dirs_.size(); ++i) {
    w += Weight(*dirs_[i]);
    if (w >= shard_weight) {
      w = 0;
      splits_.push_back(i + 1);
    }
  }

  if (splits_.back() != dirs_.size()) splits_.push_back(dirs_.size());
  CHECK(splits_.size() <= kNumShards + 1);
  CHECK(std::is_sorted(splits_.begin(), splits_.end()));
  CHECK(std::adjacent_find(splits_.begin(), splits_.end()) == splits_.end());
}

std::vector<const char*> Index::GetDirtyCandidates(Tribool untracked_cache) {
  int root_fd = open(root_dir_, kNoATime | O_RDONLY | O_DIRECTORY | O_CLOEXEC);
  VERIFY(root_fd >= 0);
  ON_SCOPE_EXIT(&) { CHECK(!close(root_fd)) << Errno(); };

  CHECK(!splits_.empty());

  std::mutex mutex;
  std::condition_variable cv;
  size_t inflight = splits_.size() - 1;
  bool error = false;
  std::vector<const char*> res;

  for (size_t i = 0; i != splits_.size() - 1; ++i) {
    size_t from = splits_[i];
    size_t to = splits_[i + 1];

    GlobalThreadPool()->Schedule([&, from, to]() {
      ON_SCOPE_EXIT(&) {
        std::unique_lock<std::mutex> lock(mutex);
        CHECK(inflight);
        if (--inflight == 0) cv.notify_one();
      };
      try {
        std::vector<const char*> candidates =
            ScanDirs(git_index_, root_fd, dirs_.data() + from, dirs_.data() + to, untracked_cache);
        if (!candidates.empty()) {
          std::unique_lock<std::mutex> lock(mutex);
          res.insert(res.end(), candidates.begin(), candidates.end());
        }
      } catch (const Exception&) {
        std::unique_lock<std::mutex> lock(mutex);
        error = true;
      }
    });
  }

  {
    std::unique_lock<std::mutex> lock(mutex);
    while (inflight) cv.wait(lock);
  }

  VERIFY(!error);
  StrSort(res.begin(), res.end(), git_index_is_case_sensitive(git_index_));
  return res;
}

}  // namespace gitstatus
