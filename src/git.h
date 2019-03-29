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
#include "time.h"

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

  bool UpdatePack(const git_oid& commit, const char** ref);
  const char* ParsePack(const git_oid& commit);
  void Wait();

  git_repository* const repo_;
  struct stat pack_stat_ = {};
  std::string pack_;
  std::vector<const char*> loose_tags_;
  std::vector<Tag> peeled_tags_;

  std::mutex mutex_;
  std::condition_variable cv_;
  bool sorting_ = false;
};

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

enum Tribool : int { kFalse = 0, kTrue = 1, kUnknown = -1 };

struct IndexStats {
  bool has_staged = false;
  Tribool has_unstaged = kUnknown;
  Tribool has_untracked = kUnknown;
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

  void UpdateKnown();
  void UpdateShards();

  void StartStagedScan(const git_oid* head);
  void StartDirtyScan(const ArenaVector<const char*>& paths);

  void DecInflight();
  void RunAsync(std::function<void()> f);
  void Wait();

  void UpdateFile(OptionalFile& file, const char* label, const char* path);

  git_repository* const repo_;
  git_index* git_index_ = nullptr;
  std::vector<Shard> shards_;
  TagDb tag_db_;

  std::unique_ptr<Index> index_;

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
const char* RemoteUrl(git_repository* repo, const git_reference* ref);

// Returns reference to HEAD or null if not found. The reference is symbolic if the repo is empty
// and direct otherwise.
git_reference* Head(git_repository* repo);

// Returns reference to the upstream branch or null if there isn't one.
git_reference* Upstream(git_reference* local);

// Returns the name of the local branch, or an empty string.
const char* LocalBranchName(const git_reference* ref);

// Returns the name of the remote tracking branch, or an empty string.
const char* RemoteBranchName(git_repository* repo, const git_reference* ref);

}  // namespace gitstatus

#endif  // ROMKATV_GITSTATUS_GIT_H_
