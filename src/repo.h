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

#ifndef ROMKATV_GITSTATUS_REPO_H_
#define ROMKATV_GITSTATUS_REPO_H_

#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#include <git2.h>

#include <algorithm>
#include <atomic>
#include <condition_variable>
#include <cstddef>
#include <cstring>
#include <functional>
#include <future>
#include <memory>
#include <mutex>
#include <string>
#include <utility>
#include <vector>

#include "check.h"
#include "index.h"
#include "tag_db.h"
#include "time.h"

namespace gitstatus {

struct IndexStats {
  bool has_staged = false;
  Tribool has_unstaged = Tribool::kUnknown;
  Tribool has_untracked = Tribool::kUnknown;
};

class Repo {
 public:
  explicit Repo(git_repository* repo);
  Repo(Repo&& other) = delete;
  ~Repo();

  git_repository* repo() const { return repo_; }

  // Head can be null, in which case has_staged will be false.
  IndexStats GetIndexStats(const git_oid* head, size_t dirty_max_index_size);

  // Returns the last tag in lexicographical order whose target is equal to the given, or an
  // empty string. Target can be null, in which case the tag is empty.
  std::future<std::string> GetTagName(const git_oid* target);

 private:
  struct Shard {
    std::string start;
    std::string end;
  };

  void UpdateShards();

  void StartStagedScan(const git_oid* head);
  void StartDirtyScan(const std::vector<const char*>& paths);

  void DecInflight();
  void RunAsync(std::function<void()> f);
  void Wait();

  git_repository* const repo_;
  git_index* git_index_ = nullptr;
  std::vector<Shard> shards_;
  TagDb tag_db_;

  std::unique_ptr<Index> index_;

  std::mutex mutex_;
  std::condition_variable cv_;
  std::atomic<size_t> inflight_{0};
  std::atomic<bool> error_{false};
  std::atomic<bool> staged_{false};
  std::atomic<Tribool> unstaged_{Tribool::kUnknown};
  std::atomic<Tribool> untracked_{Tribool::kUnknown};
  std::atomic<Tribool> untracked_cache_{Tribool::kUnknown};
};

}  // namespace gitstatus

#endif  // ROMKATV_GITSTATUS_REPO_H_
