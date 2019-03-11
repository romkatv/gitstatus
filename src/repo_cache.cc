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
#include "scope_guard.h"

namespace gitstatus {

Repo* RepoCache::Intern(git_repository* repo) {
  ON_SCOPE_EXIT(&) {
    if (repo) git_repository_free(repo);
  };
  if (!repo) return nullptr;
  const char* work_dir = git_repository_workdir(repo);
  if (!work_dir) return nullptr;
  auto x = cache_.emplace(work_dir, nullptr);
  if (x.first->second == nullptr) {
    if (git_repository_is_bare(repo) || git_repository_is_empty(repo)) return nullptr;
    x.first->second = std::make_unique<Repo>(std::exchange(repo, nullptr));
  }
  return x.first->second.get();
}

}  // namespace gitstatus
