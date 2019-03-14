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

#ifndef ROMKATV_GITSTATUS_GIT_H_
#define ROMKATV_GITSTATUS_GIT_H_

#include <git2.h>

#include <algorithm>
#include <atomic>
#include <condition_variable>
#include <cstddef>
#include <functional>
#include <future>
#include <mutex>
#include <string>
#include <utility>
#include <vector>

#include "check.h"
#include "time.h"

namespace gitstatus {

class OptionalFile {
 public:
  bool Empty() const { return !has_.load(std::memory_order_relaxed); }

  const std::string& Path() const { return path_; }

  std::string Clear() {
    std::string res;
    std::swap(path_, res);
    has_.store(false, std::memory_order_relaxed);
    return res;
  }

  bool TrySet(const char* s) {
    CHECK(s && *s);
    if (!Empty()) return false;
    path_ = s;
    has_.store(true, std::memory_order_relaxed);
    return true;
  }

  bool TrySet(std::string&& s) {
    CHECK(!s.empty());
    if (!Empty()) return false;
    path_ = std::move(s);
    has_.store(true, std::memory_order_relaxed);
    return true;
  }

 private:
  std::string path_;
  std::atomic<bool> has_{false};
};

struct IndexStats {
  bool has_staged = false;
  bool has_unstaged = false;
  bool has_untracked = false;
};

class Repo {
 public:
  explicit Repo(git_repository* repo);
  Repo(Repo&& other) = delete;
  ~Repo();

  git_repository* repo() const { return repo_; }
  git_index* index() const { return index_; }

  bool GetIndexStats(git_reference* head, bool scan_dirty, IndexStats* stats);

 private:
  void UpdateKnown();
  void UpdateSplits();

  void StartStagedScan(git_reference* head);
  void StartDirtyScan();

  void DecInflight();
  void RunAsync(std::function<void()> f);
  void Wait();

  void UpdateFile(OptionalFile& file, const char* label, const char* path);

  git_repository* const repo_;
  git_index* const index_;
  std::vector<std::string> splits_;
  Time splits_ts_;

  std::mutex mutex_;
  OptionalFile staged_;
  OptionalFile unstaged_;
  OptionalFile untracked_;
  std::condition_variable cv_;
  std::atomic<size_t> inflight_{0};
  std::atomic<bool> error_{false};
};

void InitThreadPool(size_t num_threads);

// Not null.
const char* GitError();

// Not null.
const char* RepoState(git_repository* repo);

// Returns the number of commits in the range.
size_t CountRange(git_repository* repo, const std::string& range);

// Finds and opens a repo from the specified directory. Returns null if not found.
git_repository* OpenRepo(const std::string& dir);

// How many stashes are there?
size_t NumStashes(git_repository* repo);

// Returns the origin URL or an empty string. Not null.
const char* RemoteUrl(git_repository* repo);

// Returns reference to HEAD or null if not found.
git_reference* Head(git_repository* repo);

// Returns reference to the upstream branch or null if there isn't one.
git_reference* Upstream(git_reference* local);

// Returns the name of the branch. This is the segment after the last '/'.
const char* BranchName(const git_reference* ref);

std::future<std::string> GetTagName(git_repository* repo, const git_oid* target);

}  // namespace gitstatus

#endif  // ROMKATV_GITSTATUS_GIT_H_
