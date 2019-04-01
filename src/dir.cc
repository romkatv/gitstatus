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

#include "dir.h"

#include <dirent.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <unistd.h>

#ifdef __linux__
#include <sys/syscall.h>
#endif

#include "check.h"
#include "port.h"
#include "scope_guard.h"

namespace gitstatus {

namespace {

bool Dots(const char* name) {
  if (name[0] == '.') {
    if (name[1] == 0) return true;
    if (name[1] == '.' && name[2] == 0) return true;
  }
  return false;
}

}  // namespace

#ifdef __linux__

bool ListDir(int dir_fd, std::string& arena, std::vector<const char*>& entries) {
  struct linux_dirent64 {
    ino64_t d_ino;
    off64_t d_off;
    unsigned short d_reclen;
    unsigned char d_type;
    char d_name[];
  };

  constexpr size_t kBufSize = 16 << 10;

  alignas(linux_dirent64) char buf[kBufSize];
  arena.clear();
  entries.clear();

  ON_SCOPE_EXIT(&) {
    for (const char*& p : entries) {
      p = &arena[reinterpret_cast<size_t>(p)];
    }
  };

  while (true) {
    int n = syscall(SYS_getdents64, dir_fd, buf, kBufSize);
    if (n < 0) return false;
    if (n == 0) return true;
    for (int pos = 0; pos < n;) {
      auto* ent = reinterpret_cast<linux_dirent64*>(buf + pos);
      if (!Dots(ent->d_name)) {
        arena += ent->d_type;
        entries.push_back(reinterpret_cast<const char*>(arena.size()));
        arena += ent->d_name;
        arena.append(2, '\0');
      }
      pos += ent->d_reclen;
    }
  }
}

#else

bool ListDir(int dir_fd, std::string& arena, std::vector<const char*>& entries) {
  VERIFY((dir_fd = dup(dir_fd)) >= 0);
  DIR* dir = fdopendir(dir_fd);
  if (!dir) {
    CHECK(!close(dir_fd)) << Errno();
    return false;
  }
  ON_SCOPE_EXIT(&) { CHECK(!closedir(dir)) << Errno(); };
  arena.clear();
  entries.clear();
  ON_SCOPE_EXIT(&) {
    for (const char*& p : entries) {
      p = &arena[reinterpret_cast<size_t>(p)];
    }
  };
  while (struct dirent* ent = readdir(dir)) {
    if (Dots(ent->d_name)) continue;
    arena += ent->d_type;
    entries.push_back(reinterpret_cast<const char*>(arena.size()));
    arena += ent->d_name;
    arena.append(2, '\0');
  }
  return true;
}

#endif

bool ListDir(const char* dirname, std::string& arena, std::vector<const char*>& entries) {
  int dir_fd = open(dirname, O_RDONLY | O_DIRECTORY | O_CLOEXEC | kNoATime);
  if (dir_fd < 0) return false;
  ON_SCOPE_EXIT(&) { CHECK(!close(dir_fd)) << Errno(); };
  return ListDir(dir_fd, arena, entries);
}

}  // namespace gitstatus
