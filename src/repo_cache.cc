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
#include "git.h"
#include "scope_guard.h"

namespace gitstatus {

Repo* RepoCache::Open(const std::string& dir) {
  git_repository* repo = OpenRepo(dir);
  if (!repo) {
    cache_.erase(dir + '/') || cache_.erase(dir);
    return nullptr;
  }
  ON_SCOPE_EXIT(&) {
    if (repo) git_repository_free(repo);
  };
  const char* work_dir = git_repository_workdir(repo);
  if (!work_dir) return nullptr;
  auto x = cache_.emplace(work_dir, nullptr);
  if (x.first->second == nullptr) {
    if (git_repository_is_bare(repo)) {
      LOG(INFO) << "Bare repository";
      return nullptr;
    }

    LOG(INFO) << "Initializing new repository: " << work_dir;

    // Libgit2 initializes odb and refdb lazily with double-locking. To avoid useless work
    // when multiple threads attempt to initialize the same db at the same time, we trigger
    // initialization manually before threads are in play.
    git_odb* odb;
    VERIFY(!git_repository_odb(&odb, repo)) << GitError();
    git_odb_free(odb);

    git_refdb* refdb;
    VERIFY(!git_repository_refdb(&refdb, repo)) << GitError();
    git_refdb_free(refdb);

    x.first->second = std::make_unique<Repo>(std::exchange(repo, nullptr));
  }
  return x.first->second.get();
}

}  // namespace gitstatus
