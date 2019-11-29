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

namespace {

std::string DotGitDir(const std::string& dir, bool from_dotgit) {
  git_buf gitdir = {};
  int flags = from_dotgit ? GIT_REPOSITORY_OPEN_NO_SEARCH | GIT_REPOSITORY_OPEN_NO_DOTGIT : 0;
  switch (git_repository_discover_ex(&gitdir, dir.c_str(), flags, nullptr)) {
    case 0: {
      std::string res(gitdir.ptr, gitdir.size);
      git_buf_free(&gitdir);
      return res;
    }
    case GIT_ENOTFOUND:
      return "";
    default:
      LOG(ERROR) << "git_repository_open_ext: " << Print(dir) << ": " << GitError();
      throw Exception();
  }
}

git_repository* OpenRepo(const std::string& dotgit) {
  git_repository* repo = nullptr;
  int flags = GIT_REPOSITORY_OPEN_NO_SEARCH | GIT_REPOSITORY_OPEN_NO_DOTGIT;
  switch (git_repository_open_ext(&repo, dotgit.c_str(), flags, nullptr)) {
    case 0:
      return repo;
    case GIT_ENOTFOUND:
      return nullptr;
    default:
      LOG(ERROR) << "git_repository_open_ext: " << Print(dotgit) << ": " << GitError();
      throw Exception();
  }
}

std::string DirName(std::string path) {
  if (path.empty()) return "";
  while (path.back() == '/') {
    path.pop_back();
    if (path.empty()) return "";
  }
  do {
    path.pop_back();
    if (path.empty()) return "";
  } while (path.back() != '/');
  return path;
}

}  // namespace

Repo* RepoCache::Open(const std::string& dir, bool from_dotgit) {
  if (dir.empty() || dir.front() != '/') return nullptr;

  std::string gitdir = DotGitDir(dir, from_dotgit);
  if (gitdir.empty()) {
    if (from_dotgit) {
      Erase(cache_.find(dir.back() == '/' ? dir : dir + '/'));
    } else {
      std::string path = dir;
      if (path.back() != '/') path += '/';
      do {
        Erase(cache_.find(path + ".git/"));
        path = DirName(path);
      } while (!path.empty());
    }
    return nullptr;
  }
  std::string workdir = DirName(gitdir);
  if (workdir.empty()) {
    Erase(cache_.find(gitdir));
    return nullptr;
  }

  VERIFY(gitdir.front() == '/' && gitdir.back() == '/') << Print(gitdir);
  VERIFY(workdir.front() == '/' && workdir.back() == '/') << Print(workdir);
  auto it = cache_.find(gitdir);
  if (it != cache_.end()) {
    lru_.erase(it->second->lru);
    it->second->lru = lru_.insert({Clock::now(), it});
    return it->second.get();
  }

  git_repository* repo = OpenRepo(gitdir);
  if (!repo) return nullptr;
  ON_SCOPE_EXIT(&) {
    if (repo) git_repository_free(repo);
  };
  if (git_repository_is_bare(repo)) return nullptr;
  workdir = git_repository_workdir(repo) ?: "";
  if (workdir.empty()) return nullptr;
  VERIFY(workdir.front() == '/' && workdir.back() == '/') << Print(workdir);

  auto x = cache_.emplace(gitdir, nullptr);
  std::unique_ptr<Entry>& elem = x.first->second;
  if (elem) {
    lru_.erase(elem->lru);
  } else {
    LOG(INFO) << "Initializing new repository: " << Print(gitdir);

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
  LOG(INFO) << "Closing repository: " << Print(it->first);
  lru_.erase(it->second->lru);
  cache_.erase(it);
}

}  // namespace gitstatus
