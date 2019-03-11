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

#include "git.h"

#include <algorithm>
#include <atomic>
#include <cstring>
#include <memory>
#include <type_traits>
#include <utility>

#include "check.h"
#include "scope_guard.h"
#include "thread_pool.h"
#include "timer.h"

namespace gitstatus {

namespace {

using namespace std::string_literals;

ThreadPool* g_thread_pool = nullptr;

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

git_index* Index(git_repository* repo) {
  git_index* res = nullptr;
  VERIFY(!git_repository_index(&res, repo)) << GitError();
  return res;
}

}  // namespace

void InitThreadPool(size_t num_threads) {
  LOG(INFO) << "Spawning " << num_threads << " thread(s)";
  g_thread_pool = new ThreadPool(num_threads);
}

const char* GitError() {
  // There is no git_error_last() on OSX, so we use a deprecated alternative.
  const git_error* err = giterr_last();
  return err && err->message ? err->message : "unknown error";
}

const char* RepoState(git_repository* repo) {
  // These names mostly match gitaction in vcs_info:
  // https://github.com/zsh-users/zsh/blob/master/Functions/VCS_Info/Backends/VCS_INFO_get_data_git.
  switch (git_repository_state(repo)) {
    case GIT_REPOSITORY_STATE_NONE:
      return "";
    case GIT_REPOSITORY_STATE_MERGE:
      return "merge";
    case GIT_REPOSITORY_STATE_REVERT:
      return "revert";
    case GIT_REPOSITORY_STATE_REVERT_SEQUENCE:
      return "revert-seq";
    case GIT_REPOSITORY_STATE_CHERRYPICK:
      return "cherry";
    case GIT_REPOSITORY_STATE_CHERRYPICK_SEQUENCE:
      return "cherry-seq";
    case GIT_REPOSITORY_STATE_BISECT:
      return "bisect";
    case GIT_REPOSITORY_STATE_REBASE:
      return "rebase";
    case GIT_REPOSITORY_STATE_REBASE_INTERACTIVE:
      return "rebase-i";
    case GIT_REPOSITORY_STATE_REBASE_MERGE:
      return "rebase-m";
    case GIT_REPOSITORY_STATE_APPLY_MAILBOX:
      return "am";
    case GIT_REPOSITORY_STATE_APPLY_MAILBOX_OR_REBASE:
      return "am/rebase";
  }
  return "action";
}

size_t CountRange(git_repository* repo, const std::string& range) {
  git_revwalk* walk = nullptr;
  VERIFY(!git_revwalk_new(&walk, repo)) << GitError();
  ON_SCOPE_EXIT(=) { git_revwalk_free(walk); };
  VERIFY(!git_revwalk_push_range(walk, range.c_str())) << GitError();
  size_t res = 0;
  while (true) {
    git_oid oid;
    switch (git_revwalk_next(&oid, walk)) {
      case 0:
        ++res;
        break;
      case GIT_ITEROVER:
        return res;
      default:
        LOG(ERROR) << "git_revwalk_next: " << range << ": " << GitError();
        throw Exception();
    }
  }
}

git_repository* OpenRepo(const std::string& dir) {
  git_repository* repo = nullptr;
  switch (git_repository_open_ext(&repo, dir.c_str(), GIT_REPOSITORY_OPEN_FROM_ENV, nullptr)) {
    case 0:
      return repo;
    case GIT_ENOTFOUND:
      return nullptr;
    default:
      LOG(ERROR) << "git_repository_open_ext: " << dir << ": " << GitError();
      throw Exception();
  }
}

size_t NumStashes(git_repository* repo) {
  size_t res = 0;
  auto* cb = +[](size_t index, const char* message, const git_oid* stash_id, void* payload) {
    ++*static_cast<size_t*>(payload);
    return 0;
  };
  VERIFY(!git_stash_foreach(repo, cb, &res)) << GitError();
  return res;
}

const char* RemoteUrl(git_repository* repo) {
  git_remote* remote = nullptr;
  switch (git_remote_lookup(&remote, repo, "origin")) {
    case 0:
      return git_remote_url(remote) ?: "";
    case GIT_ENOTFOUND:
    case GIT_EINVALIDSPEC:
      return "";
    default:
      LOG(ERROR) << "git_remote_lookup: " << GitError();
      throw Exception();
  }
}

git_reference* Head(git_repository* repo) {
  git_reference* head = nullptr;
  switch (git_repository_head(&head, repo)) {
    case 0:
      return head;
    case GIT_ENOTFOUND:
    case GIT_EUNBORNBRANCH:
      return nullptr;
    default:
      LOG(ERROR) << "git_repository_head: " << GitError();
      throw Exception();
  }
}

git_reference* Upstream(git_reference* local) {
  git_reference* upstream = nullptr;
  switch (git_branch_upstream(&upstream, local)) {
    case 0:
      return upstream;
    case GIT_ENOTFOUND:
      return nullptr;
    default:
      // There is no git_error_last() or GIT_ERROR_INVALID on OSX, so we use deprecated
      // alternatives.
      VERIFY(giterr_last()->klass == GITERR_INVALID) << "git_branch_upstream: " << GitError();
      return nullptr;
  }
}

const char* BranchName(const git_reference* ref) {
  const char* name = git_reference_name(ref);
  const char* sep = std::strrchr(name, '/');
  return sep ? sep + 1 : name;
}

Repo::Repo(git_repository* repo) try : repo_(repo), index_(Index(repo_)) {
  UpdateSplits();
} catch (...) {
  git_repository_free(repo);
  throw;
}

Repo::~Repo() {
  Wait();
  git_index_free(index_);
  git_repository_free(repo_);
}

void Repo::UpdateKnown() {
  struct File {
    unsigned flags = 0;
    std::string path;
  };

  auto Fetch = [&](OptionalFile& path) {
    File res;
    if (!path.Empty()) {
      res.path = path.Clear();
      if (git_status_file(&res.flags, repo_, res.path.c_str())) res.flags = 0;
    }
    return res;
  };

  File files[] = {Fetch(staged_), Fetch(unstaged_), Fetch(untracked_)};

  auto Snatch = [&](unsigned mask, OptionalFile& file, const char* label) {
    for (File& f : files) {
      if (f.flags & mask) {
        f.flags = 0;
        LOG(INFO) << "Fast path for " << label << " file: " << f.path;
        CHECK(file.TrySet(std::move(f.path)));
        return;
      }
    }
  };

  Snatch(GIT_STATUS_INDEX_NEW | GIT_STATUS_INDEX_MODIFIED | GIT_STATUS_INDEX_DELETED |
             GIT_STATUS_INDEX_RENAMED | GIT_STATUS_INDEX_TYPECHANGE,
         staged_, "staged");
  Snatch(GIT_STATUS_WT_MODIFIED | GIT_STATUS_WT_DELETED | GIT_STATUS_WT_TYPECHANGE |
             GIT_STATUS_WT_RENAMED | GIT_STATUS_CONFLICTED,
         unstaged_, "unstaged");
  Snatch(GIT_STATUS_WT_NEW, untracked_, "untracked");
}

bool Repo::GetIndexStats(git_reference* head, bool scan_dirty, IndexStats* stats) {
  auto Done = [&] {
    return !staged_.Empty() && (!scan_dirty || (!unstaged_.Empty() && !untracked_.Empty()));
  };

  Wait();
  Store(error_, false);
  UpdateKnown();

  if (!Done()) {
    CHECK(Load(inflight_) == 0);
    Inc(inflight_);
    ON_SCOPE_EXIT(&) { DecInflight(); };
    if (scan_dirty) StartDirtyScan();
    StartStagedScan(head);

    {
      std::unique_lock<std::mutex> lock(mutex_);
      while (Load(inflight_) > 1 && !Load(error_) && !Done()) cv_.wait(lock);
    }
    RunAsync([this] { UpdateSplits(); });
  }

  *stats = {};
  if (Load(error_)) return false;
  stats->has_staged = !staged_.Empty();
  stats->has_unstaged = !unstaged_.Empty();
  stats->has_untracked = !untracked_.Empty();
  return true;
}

void Repo::StartDirtyScan() {
  if (!unstaged_.Empty() && !untracked_.Empty()) return;

  git_diff_options opt = GIT_DIFF_OPTIONS_INIT;
  opt.payload = this;
  opt.flags = GIT_DIFF_SKIP_BINARY_CHECK;
  if (untracked_.Empty()) {
    // We could remove GIT_DIFF_RECURSE_UNTRACKED_DIRS and manually check in OnDirty whether
    // the allegedly untracked file is an empty directory. Unfortunately, it'll break UpdateDirty()
    // because we cannot use git_status_file on a directory. Seems like there is no way to quickly
    // get any untracked file from a directory that definitely has untracked files.
    // git_diff_index_to_workdir actually computes this before telling us that a directory is
    // untracked, but it doesn't give us the file path.
    opt.flags |= GIT_DIFF_INCLUDE_UNTRACKED | GIT_DIFF_RECURSE_UNTRACKED_DIRS;
  }
  opt.ignore_submodules = GIT_SUBMODULE_IGNORE_DIRTY;
  opt.notify_cb = +[](const git_diff* diff, const git_diff_delta* delta,
                      const char* matched_pathspec, void* payload) -> int {
    Repo* repo = static_cast<Repo*>(payload);
    if (Load(repo->error_)) return GIT_EUSER;
    if (delta->status == GIT_DELTA_UNTRACKED) {
      repo->UpdateFile(repo->untracked_, "untracked", delta->new_file.path);
      return repo->unstaged_.Empty() ? 1 : GIT_EUSER;
    } else {
      repo->UpdateFile(repo->unstaged_, "unstaged", delta->new_file.path);
      return repo->untracked_.Empty() ? 1 : GIT_EUSER;
    }
  };

  for (size_t i = 0; i != splits_.size() - 1; ++i) {
    RunAsync([this, opt, start = splits_[i], end = splits_[i + 1]]() mutable {
      opt.range_start = start.c_str();
      opt.range_end = end.c_str();
      git_diff* diff = nullptr;
      switch (git_diff_index_to_workdir(&diff, repo_, index_, &opt)) {
        case 0:
          git_diff_free(diff);
          break;
        case GIT_EUSER:
          break;
        default:
          LOG(ERROR) << "git_diff_index_to_workdir: " << GitError();
          throw Exception();
      }
    });
  }
}

void Repo::StartStagedScan(git_reference* head) {
  if (!staged_.Empty()) return;

  const git_oid* oid = git_reference_target(head);
  VERIFY(oid);
  git_commit* commit = nullptr;
  VERIFY(!git_commit_lookup(&commit, repo_, oid)) << GitError();
  ON_SCOPE_EXIT(=) { git_commit_free(commit); };
  git_tree* tree = nullptr;
  VERIFY(!git_commit_tree(&tree, commit)) << GitError();

  git_diff_options opt = GIT_DIFF_OPTIONS_INIT;
  opt.payload = this;
  opt.notify_cb = +[](const git_diff* diff, const git_diff_delta* delta,
                      const char* matched_pathspec, void* payload) -> int {
    Repo* repo = static_cast<Repo*>(payload);
    repo->UpdateFile(repo->staged_, "staged", delta->new_file.path);
    return GIT_EUSER;
  };

  for (size_t i = 0; i != splits_.size() - 1; ++i) {
    RunAsync([this, tree, opt, start = splits_[i], end = splits_[i + 1]]() mutable {
      opt.range_start = start.c_str();
      opt.range_end = end.c_str();
      git_diff* diff = nullptr;
      switch (git_diff_tree_to_index(&diff, repo_, tree, index_, &opt)) {
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

void Repo::UpdateSplits() {
  constexpr size_t kEntriesPerShard = 1024;

  size_t n = git_index_entrycount(index_);
  ON_SCOPE_EXIT(&) {
    LOG(INFO) << "Index size = " << n << "; number of shards = " << (splits_.size() - 1);
  };

  if (n <= kEntriesPerShard || g_thread_pool->num_threads() < 2) {
    splits_ = {""s, ""s};
    return;
  }

  std::vector<const char*> entries_lo(n);
  std::vector<char*> slashes;
  slashes.reserve(8 * n);

  ON_SCOPE_EXIT(&) {
    for (char* p : slashes) *p = '/';
  };

  constexpr char kSep[] = {1, 255, 0};

  for (size_t i = 0; i != n; ++i) {
    char* path = const_cast<char*>(git_index_get_byindex(index_, i)->path);
    if (std::strstr(path, kSep)) {
      splits_ = {""s, ""s};
      return;
    }
    entries_lo[i] = path;
    while ((path = std::strchr(path, '/'))) {
      static_assert(std::is_unsigned<char>(), "");
      *path = kSep[0];
      slashes.push_back(path);
    }
  }
  std::sort(entries_lo.begin(), entries_lo.end(),
            [](const char* x, const char* y) { return std::strcmp(x, y) < 0; });

  std::vector<const char*> entries_hi = entries_lo;
  for (char* p : slashes) *p = kSep[1];
  std::sort(entries_hi.begin(), entries_hi.end(),
            [](const char* x, const char* y) { return std::strcmp(x, y) < 0; });

  const char* last = "";
  for (size_t i = 0; i != n; ++i) {
    if (entries_lo[i] == entries_hi[i]) {
      last = entries_lo[i];
    } else {
      entries_hi[i] = last;
    }
  }

  size_t shards = std::min(n / kEntriesPerShard + 1, g_thread_pool->num_threads());
  splits_.clear();
  splits_.reserve(shards + 1);
  splits_.push_back("");
  for (size_t i = 0; i != shards - 1; ++i) {
    std::string split = entries_hi[(i + 1) * n / shards];
    std::replace(split.begin(), split.end(), kSep[1], '/');
    if (split != splits_.back()) splits_.push_back(std::move(split));
  }
  splits_.push_back("");
  CHECK(splits_.size() <= shards + 1);
}

void Repo::DecInflight() {
  std::unique_lock<std::mutex> lock(mutex_);
  CHECK(Load(inflight_) > 0);
  if (Dec(inflight_) < 3) cv_.notify_one();
}

void Repo::RunAsync(std::function<void()> f) {
  Inc(inflight_);
  try {
    g_thread_pool->Schedule([this, f = std::move(f)] {
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

void Repo::UpdateFile(OptionalFile& file, const char* label, const char* path) {
  if (!file.Empty()) return;
  std::unique_lock<std::mutex> lock(mutex_);
  if (file.TrySet(path)) {
    LOG(INFO) << "Found new " << label << " file: " << path;
    cv_.notify_one();
  }
}

void Repo::Wait() {
  std::unique_lock<std::mutex> lock(mutex_);
  while (inflight_) cv_.wait(lock);
}

}  // namespace gitstatus
