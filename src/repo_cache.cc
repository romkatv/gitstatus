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

#include <cstring>

#include "check.h"
#include "git.h"
#include "print.h"
#include "scope_guard.h"

namespace gitstatus {

Repo* RepoCache::Open(const std::string& dir) {
  git_buf git_dir = {};
  if (!git_repository_discover(&git_dir, dir.c_str(), 0, nullptr)) {
    ON_SCOPE_EXIT(&) { git_buf_free(&git_dir); };
    if (StringView(git_dir.ptr, git_dir.size).EndsWith("/.git/")) {
      std::string work_dir(git_dir.ptr, git_dir.size - std::strlen(".git/"));
      auto it = cache_.find(work_dir);
      if (it != cache_.end()) {
        lru_.erase(it->second->lru);
        it->second->lru = lru_.insert({Clock::now(), it});
        return it->second.get();
      }
    }
  }
  git_repository* repo = OpenRepo(dir);
  if (!repo) {
    Erase(cache_.find(dir + '/'));
    Erase(cache_.find(dir));
    return nullptr;
  }
  ON_SCOPE_EXIT(&) {
    if (repo) git_repository_free(repo);
  };
  if (git_repository_is_bare(repo)) return nullptr;
  const char* work_dir = git_repository_workdir(repo);
  if (!work_dir) return nullptr;
  auto x = cache_.emplace(work_dir, nullptr);
  std::unique_ptr<Entry>& elem = x.first->second;
  if (elem) {
    lru_.erase(elem->lru);
  } else {
    LOG(INFO) << "Initializing new repository: " << Print(work_dir);

    // Libgit2 initializes odb and refdb lazily with double-locking. To avoid useless work
    // when multiple threads attempt to initialize the same db at the same time, we trigger
    // initialization manually before threads are in play.
    git_odb* odb;
    VERIFY(!git_repository_odb(&odb, repo)) << GitError();
    git_odb_free(odb);

    git_refdb* refdb;
    VERIFY(!git_repository_refdb(&refdb, repo)) << GitError();
    git_refdb_free(refdb);

    elem = std::make_unique<Entry>(std::exchange(repo, nullptr), lim_);
  }
  elem->lru = lru_.insert({Clock::now(), x.first});
  return elem.get();
}

void RepoCache::Free(Time cutoff) {
  while (true) {
    if (lru_.empty()) break;
    auto it = lru_.begin();
    if (it->first > cutoff) break;
    Erase(it->second);
  }
}

void RepoCache::Erase(Cache::iterator it) {
  if (it == cache_.end()) return;
  LOG(INFO) << "Closing repository: " << Print(git_repository_workdir(it->second->repo()));
  lru_.erase(it->second->lru);
  cache_.erase(it);
}

}  // namespace gitstatus
