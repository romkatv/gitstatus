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

#include "check_dir_mtime.h"

#include <fcntl.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <time.h>
#include <unistd.h>

#include <string>

#include "check.h"
#include "logging.h"
#include "scope_guard.h"
#include "stat.h"

namespace gitstatus {

namespace {

using namespace std::string_literals;

void Touch(const char* path) {
  int fd = creat(path, 0444);
  VERIFY(fd >= 0) << Errno();
  CHECK(!close(fd)) << Errno();
}

bool StatChanged(const char* path, const struct stat& prev) {
  struct stat cur;
  VERIFY(!lstat(path, &cur)) << Errno();
  return !StatEq(prev, cur);
}

}  // namespace

bool CheckDirMtime(const char* root_dir) {
  try {
    std::string tmp = root_dir + ".gitstatus.XXXXXX"s;
    VERIFY(mkdtemp(&tmp[0])) << Errno();
    ON_SCOPE_EXIT(&) { rmdir(tmp.c_str()); };

    std::string a_dir = tmp + "/a";
    VERIFY(!mkdir(a_dir.c_str(), 0755)) << Errno();
    ON_SCOPE_EXIT(&) { rmdir(a_dir.c_str()); };
    struct stat a_st;
    VERIFY(!lstat(a_dir.c_str(), &a_st)) << Errno();

    std::string b_dir = tmp + "/b";
    VERIFY(!mkdir(b_dir.c_str(), 0755)) << Errno();
    ON_SCOPE_EXIT(&) { rmdir(b_dir.c_str()); };
    struct stat b_st;
    VERIFY(!lstat(b_dir.c_str(), &b_st)) << Errno();

    while (sleep(1)) {
      // zzzz
    }

    std::string a1 = a_dir + "/1";
    VERIFY(!mkdir(a1.c_str(), 0755)) << Errno();
    ON_SCOPE_EXIT(&) { rmdir(a1.c_str()); };
    if (!StatChanged(a_dir.c_str(), a_st)) {
      LOG(WARN) << "Creating a directory doesn't change mtime of the parent: " << root_dir;
      return false;
    }

    std::string b1 = b_dir + "/1";
    Touch(b1.c_str());
    ON_SCOPE_EXIT(&) { unlink(b1.c_str()); };
    if (!StatChanged(b_dir.c_str(), b_st)) {
      LOG(WARN) << "Creating a file doesn't change mtime of the parent: " << root_dir;
      return false;
    }

    LOG(INFO) << "All mtime checks have passes. Enabling untracked cache: " << root_dir;
    return true;
  } catch (const Exception&) {
    LOG(WARN) << "Error while testing for mtime capability: " << root_dir;
    return false;
  }
}

}  // namespace gitstatus
