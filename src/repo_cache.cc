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

namespace {

bool StartsWith(std::string_view s, std::string_view prefix) {
  return s.size() >= prefix.size() && s.substr(0, prefix.size()) == prefix;
}

}  // namespace

RepoCache::~RepoCache() {
  for (const auto& kv : cache_) git_repository_free(kv.second);
}

git_repository* RepoCache::Find(const std::string& dir) const {
  CHECK(!dir.empty() && dir.back() == '/') << dir;
  auto it = cache_.lower_bound(dir);
  if (it != cache_.end() && it->first == dir) return it->second;
  if (it == cache_.begin()) return nullptr;
  --it;
  return StartsWith(dir, it->first) ? it->second : nullptr;
}

bool RepoCache::Put(git_repository* repo) {
  const char* work_dir = git_repository_workdir(repo);
  if (!work_dir || *work_dir != '/') return false;
  std::string key = work_dir;
  if (key.back() != '/') return false;
  auto x = cache_.insert(std::make_pair(std::move(key), repo));
  if (!x.second) {
    git_repository_free(x.first->second);
    x.first->second = repo;
  }
  return true;
}

}  // namespace gitstatus
