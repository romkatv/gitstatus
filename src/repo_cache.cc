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

#include "repo_cache.h"

#include "check.h"

namespace gitstatus {

RepoCache::~RepoCache() {
  for (const auto& kv : cache_) git_repository_free(kv.second);
}

git_repository* RepoCache::Intern(git_repository* repo) {
  try {
    const char* work_dir = git_repository_workdir(repo);
    VERIFY(work_dir);
    auto x = cache_.emplace(work_dir, repo);
    if (!x.second) git_repository_free(repo);
    return x.first->second;
  } catch(...) {
    git_repository_free(repo);
    throw;
  }
}

}  // namespace gitstatus
