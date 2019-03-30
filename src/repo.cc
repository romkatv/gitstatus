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

#include "repo.h"

#include <fcntl.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#include <algorithm>
#include <atomic>
#include <cstdlib>
#include <cstring>
#include <exception>
#include <iterator>
#include <memory>
#include <type_traits>
#include <utility>

#include "algorithm.h"
#include "arena.h"
#include "check.h"
#include "check_dir_mtime.h"
#include "dir.h"
#include "git.h"
#include "port.h"
#include "scope_guard.h"
#include "stat.h"
#include "thread_pool.h"
#include "timer.h"

namespace gitstatus {

namespace {

using namespace std::string_literals;

template <class T>
T Load(const std::atomic<T>& x) {
  return x.load(std::memory_order_relaxed);
}

template <class T>
void Store(std::atomic<T>& x, T v) {
  x.store(v, std::memory_order_relaxed);
}

template <class T>
T Inc(std::atomic<T>& x) {
  return x.fetch_add(1, std::memory_order_relaxed);
}

template <class T>
T Dec(std::atomic<T>& x) {
  return x.fetch_sub(1, std::memory_order_relaxed);
}

template <class T>
T Exchange(std::atomic<T>& x, T v) {
  return x.exchange(v, std::memory_order_relaxed);
}

}  // namespace

Repo::Repo(git_repository* repo) : repo_(repo), tag_db_(repo) {
  GlobalThreadPool()->Schedule([this] {
    bool check = CheckDirMtime(git_repository_path(repo_));
    std::unique_lock<std::mutex> lock(mutex_);
    untracked_cache_ = check ? kTrue : kFalse;
    cv_.notify_one();
  });
}

Repo::~Repo() {
  Wait();
  {
    std::unique_lock<std::mutex> lock(mutex_);
    while (untracked_cache_ == kUnknown) cv_.wait(lock);
  }
  if (git_index_) git_index_free(git_index_);
  git_repository_free(repo_);
}

IndexStats Repo::GetIndexStats(const git_oid* head, size_t dirty_max_index_size) {
  Wait();
  int new_index = 1;
  if (git_index_) {
    VERIFY(!git_index_read_ex(git_index_, 0, &new_index)) << GitError();
  } else {
    VERIFY(!git_repository_index(&git_index_, repo_)) << GitError();
    // Query an attribute (doesn't matter which) to initialize repo's attribute
    // cache. It's a workaround for synchronization bugs (data races) in libgit2
    // that result from lazy cache initialization with no synchrnonization whatsoever.
    const char* attr;
    VERIFY(!git_attr_get(&attr, repo_, 0, "x", "x")) << GitError();
  }

  UpdateShards();
  Store(error_, false);
  Store(staged_, false);
  Store(unstaged_, false);
  Store(untracked_, false);

  if (head) StartStagedScan(head);

  Arena arena;
  ArenaVector<const char*> dirty_candidates(&arena);
  const size_t index_size = git_index_entrycount(git_index_);
  const bool scan_dirty = index_size <= dirty_max_index_size;

  if (scan_dirty) {
    if (new_index) index_ = std::make_unique<Index>(git_repository_workdir(repo_), git_index_);
    index_->GetDirtyCandidates(dirty_candidates, Load(untracked_cache_) == kTrue);
    LOG(INFO) << "Found " << dirty_candidates.size() << " dirty candidate(s)";
    StartDirtyScan(dirty_candidates);
  }

  Wait();
  VERIFY(!Load(error_));

  return {
      // An empty repo with non-empty index must have staged changes since it cannot have unstaged
      // changes.
      .has_staged = Load(staged_) || (!head && index_size),
      .has_unstaged = Load(unstaged_) ? kTrue : scan_dirty ? kFalse : kUnknown,
      .has_untracked = Load(untracked_) ? kTrue : scan_dirty ? kFalse : kUnknown,
  };
}

void Repo::StartDirtyScan(const ArenaVector<const char*>& paths) {
  if (paths.empty()) return;

  git_diff_options opt = GIT_DIFF_OPTIONS_INIT;
  opt.payload = this;
  opt.flags = GIT_DIFF_SKIP_BINARY_CHECK | GIT_DIFF_DISABLE_PATHSPEC_MATCH | GIT_DIFF_INCLUDE_UNTRACKED | GIT_DIFF_RECURSE_UNTRACKED_DIRS;
  opt.ignore_submodules = GIT_SUBMODULE_IGNORE_DIRTY;
  opt.notify_cb = +[](const git_diff* diff, const git_diff_delta* delta,
                      const char* matched_pathspec, void* payload) -> int {
    Repo* repo = static_cast<Repo*>(payload);
    if (Load(repo->error_)) return GIT_EUSER;
    if (delta->status == GIT_DELTA_UNTRACKED) {
      if (!Exchange(repo->untracked_, true)) {
        LOG(INFO) << "Found untracked file: " << delta->new_file.path;
      }
      return Load(repo->unstaged_) ? 1 : GIT_EUSER;
    } else {
      if (!Exchange(repo->unstaged_, true)) {
        LOG(INFO) << "Found unstaged file: " << delta->new_file.path;
      }
      return Load(repo->untracked_) ? 1 : GIT_EUSER;
    }
  };

  constexpr size_t kEntriesPerShard = 512;
  const size_t num_shards =
      std::min(paths.size() / kEntriesPerShard + 1, 2 * GlobalThreadPool()->num_threads());

  // TODO: better sharding.
  for (size_t i = 0; i != num_shards; ++i) {
    size_t start = i * paths.size() / num_shards;
    size_t end = (i + 1) * paths.size() / num_shards;
    if (start == end) continue;
    auto F = [&, opt, start, end]() mutable {
      opt.range_start = paths[start];
      opt.range_end = paths[end - 1];
      opt.pathspec.strings = const_cast<char**>(paths.data() + start);
      opt.pathspec.count = end - start;
      git_diff* diff = nullptr;
      switch (git_diff_index_to_workdir(&diff, repo_, git_index_, &opt)) {
        case 0:
          git_diff_free(diff);
          break;
        case GIT_EUSER:
          break;
        default:
          LOG(ERROR) << "git_diff_index_to_workdir: " << GitError();
          throw Exception();
      }
    };
    if (i == num_shards - 1) {
      F();
    } else {
      RunAsync(std::move(F));
    }
  }
}

void Repo::StartStagedScan(const git_oid* head) {
  git_commit* commit = nullptr;
  VERIFY(!git_commit_lookup(&commit, repo_, head)) << GitError();
  ON_SCOPE_EXIT(=) { git_commit_free(commit); };
  git_tree* tree = nullptr;
  VERIFY(!git_commit_tree(&tree, commit)) << GitError();

  git_diff_options opt = GIT_DIFF_OPTIONS_INIT;
  opt.payload = this;
  opt.notify_cb = +[](const git_diff* diff, const git_diff_delta* delta,
                      const char* matched_pathspec, void* payload) -> int {
    Repo* repo = static_cast<Repo*>(payload);
    if (!Exchange(repo->staged_, true)) {
      LOG(INFO) << "Found staged file: " << delta->new_file.path;
    }
    return GIT_EUSER;
  };

  for (const Shard& shard : shards_) {
    RunAsync([this, tree, opt, shard]() mutable {
      opt.range_start = shard.start.c_str();
      opt.range_end = shard.end.c_str();
      git_diff* diff = nullptr;
      switch (git_diff_tree_to_index(&diff, repo_, tree, git_index_, &opt)) {
        case 0:
          git_diff_free(diff);
          break;
        case GIT_EUSER:
          break;
        default:
          LOG(ERROR) << "git_diff_tree_to_index: " << GitError();
          throw Exception();
      }
    });
  }
}

void Repo::UpdateShards() {
  constexpr size_t kEntriesPerShard = 512;

  size_t index_size = git_index_entrycount(git_index_);
  ON_SCOPE_EXIT(&) {
    LOG(INFO) << "Splitting " << index_size << " object(s) into " << shards_.size() << " shard(s)";
  };

  if (index_size <= kEntriesPerShard || GlobalThreadPool()->num_threads() < 2) {
    shards_ = {{""s, ""s}};
    return;
  }

  size_t shards =
      std::min(index_size / kEntriesPerShard + 1, 2 * GlobalThreadPool()->num_threads());
  shards_.clear();
  shards_.reserve(shards);
  std::string last;

  for (size_t i = 0; i != shards - 1; ++i) {
    std::string split = git_index_get_byindex(git_index_, (i + 1) * index_size / shards)->path;
    auto pos = split.find_last_of('/');
    if (pos == std::string::npos) continue;
    split = split.substr(0, pos + 1);
    Shard shard;
    shard.end = split;
    --shard.end.back();
    if (shard.end <= last) continue;
    shard.start = std::move(last);
    last = std::move(split);
    shards_.push_back(std::move(shard));
  }
  shards_.push_back({std::move(last), ""});

  CHECK(!shards_.empty());
  CHECK(shards_.size() <= shards);
  CHECK(shards_.front().start.empty());
  CHECK(shards_.back().end.empty());
  for (size_t i = 0; i != shards_.size(); ++i) {
    if (i) CHECK(shards_[i - 1].end < shards_[i].start);
    if (i != shards_.size() - 1) CHECK(shards_[i].start < shards_[i].end);
  }
}

void Repo::DecInflight() {
  std::unique_lock<std::mutex> lock(mutex_);
  CHECK(Load(inflight_) > 0);
  if (Dec(inflight_) == 1) cv_.notify_one();
}

void Repo::RunAsync(std::function<void()> f) {
  Inc(inflight_);
  try {
    GlobalThreadPool()->Schedule([this, f = std::move(f)] {
      try {
        ON_SCOPE_EXIT(&) { DecInflight(); };
        f();
      } catch (const Exception&) {
        if (!Load(error_)) {
          std::unique_lock<std::mutex> lock(mutex_);
          if (!Load(error_)) {
            Store(error_, true);
            cv_.notify_one();
          }
        }
      }
    });
  } catch (...) {
    DecInflight();
    throw;
  }
}

void Repo::Wait() {
  std::unique_lock<std::mutex> lock(mutex_);
  while (inflight_) cv_.wait(lock);
}

std::future<std::string> Repo::GetTagName(const git_oid* target) {
  auto* promise = new std::promise<std::string>;
  std::future<std::string> res = promise->get_future();

  GlobalThreadPool()->Schedule([=] {
    ON_SCOPE_EXIT(&) { delete promise; };
    if (!target) {
      promise->set_value("");
      return;
    }
    try {
      promise->set_value(tag_db_.TagForCommit(*target));
    } catch (const Exception&) {
      promise->set_exception(std::current_exception());
    }
  });

  return res;
}

}  // namespace gitstatus
