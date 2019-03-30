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

#ifndef ROMKATV_GITSTATUS_TAG_DB_H_
#define ROMKATV_GITSTATUS_TAG_DB_H_

#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#include <git2.h>

#include <condition_variable>
#include <cstring>
#include <mutex>
#include <string>
#include <vector>

namespace gitstatus {

class TagDb {
 public:
  explicit TagDb(git_repository* repo);
  TagDb(TagDb&&) = delete;
  ~TagDb();

  std::string TagForCommit(const git_oid& oid);

 private:
  struct Tag {
    bool operator<(const Tag& other) const {
      return std::memcmp(commit.id, other.commit.id, GIT_OID_RAWSZ) < 0;
    }

    const char* ref = nullptr;
    git_oid commit = {};
  };

  bool UpdatePack(const git_oid& commit, std::vector<const char*>& match);
  std::vector<const char*> ParsePack(const git_oid& commit);
  void Wait();

  git_repository* const repo_;
  struct stat pack_stat_ = {};
  std::string pack_;
  std::vector<const char*> unpeeled_tags_;
  std::vector<Tag> peeled_tags_;

  std::mutex mutex_;
  std::condition_variable cv_;
  bool sorting_ = false;
};

}  // namespace gitstatus

#endif  // ROMKATV_GITSTATUS_TAG_DB_H_
