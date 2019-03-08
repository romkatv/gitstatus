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

#ifndef ROMKATV_GITSTATUS_REPO_CACHE_H_
#define ROMKATV_GITSTATUS_REPO_CACHE_H_

#include <string>
#include <unordered_map>

#include <git2.h>

namespace gitstatus {

struct Repo {
  Repo(git_repository* repo) : repo(repo) {}

  git_repository* const repo;
  std::string untracked;
  std::string unstaged;
};

class RepoCache {
 public:
  RepoCache() {}
  RepoCache(RepoCache&&) = delete;
  ~RepoCache();

  Repo& Intern(git_repository* repo);

 private:
  std::unordered_map<std::string, Repo> cache_;
};

}  // namespace gitstatus

#endif  // ROMKATV_GITSTATUS_REPO_CACHE_H_
