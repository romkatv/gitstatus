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

#include "check.h"
#include "scope_guard.h"
#include "thread_pool.h"
#include "timer.h"

namespace gitstatus {

namespace {

using namespace std::string_literals;

static constexpr char kTagPrefix[] = "refs/tags/";

constexpr int8_t kUnhex[256] = {
    0, 0,  0,  0,  0,  0,  0,  0, 0, 0, 0, 0, 0, 0, 0, 0,  // 0
    0, 0,  0,  0,  0,  0,  0,  0, 0, 0, 0, 0, 0, 0, 0, 0,  // 1
    0, 0,  0,  0,  0,  0,  0,  0, 0, 0, 0, 0, 0, 0, 0, 0,  // 2
    0, 1,  2,  3,  4,  5,  6,  7, 8, 9, 0, 0, 0, 0, 0, 0,  // 3
    0, 10, 11, 12, 13, 14, 15, 0, 0, 0, 0, 0, 0, 0, 0, 0,  // 4
    0, 0,  0,  0,  0,  0,  0,  0, 0, 0, 0, 0, 0, 0, 0, 0,  // 5
    0, 10, 11, 12, 13, 14, 15, 0, 0, 0, 0, 0, 0, 0, 0, 0   // 6
};

ThreadPool* g_thread_pool = nullptr;

template <class Container, class T>
auto BinaryFind(Container& c, const T& val) {
  auto end = std::end(c);
  auto res = std::lower_bound(std::begin(c), end, val);
  if (res != end && val < *res) res = end;
  return res;
}

void ParseOid(unsigned char* oid, const char* begin, const char* end) {
  VERIFY(end >= begin + GIT_OID_HEXSZ);
  for (size_t i = 0; i != GIT_OID_HEXSZ; i += 2) {
    *oid++ = kUnhex[+begin[i]] << 4 | kUnhex[+begin[i + 1]];
  }
}

const char* StripTag(const char* ref) {
  for (size_t i = 0; i != sizeof(kTagPrefix) - 1; ++i) {
    if (*ref++ != kTagPrefix[i]) return nullptr;
  }
  return ref;
}

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

bool TagHasTarget(git_repository* repo, git_refdb* refdb, const char* name, const git_oid* target) {
  static constexpr size_t kMaxDerefCount = 10;

  git_reference* ref;
  if (git_refdb_lookup(&ref, refdb, name)) return false;
  ON_SCOPE_EXIT(&) { git_reference_free(ref); };

  for (int i = 0; i != kMaxDerefCount && git_reference_type(ref) == GIT_REFERENCE_SYMBOLIC; ++i) {
    git_reference* dst;
    if (git_refdb_lookup(&dst, refdb, git_reference_name(ref))) return false;
    git_reference_free(ref);
    ref = dst;
  }

  if (git_reference_type(ref) == GIT_REFERENCE_SYMBOLIC) return false;
  const git_oid* oid = git_reference_target_peel(ref) ?: git_reference_target(ref);
  if (git_oid_equal(oid, target)) return true;

  for (int i = 0; i != kMaxDerefCount; ++i) {
    git_tag* tag;
    if (git_tag_lookup(&tag, repo, oid)) return false;
    ON_SCOPE_EXIT(&) { git_tag_free(tag); };
    if (git_tag_target_type(tag) == GIT_OBJECT_COMMIT) {
      return git_oid_equal(git_tag_target_id(tag), target);
    }
    oid = git_tag_target_id(tag);
  }

  return false;
}

void ListTags(git_repository* repo, std::string& arena, std::vector<size_t>& positions) {
  arena.reserve(64 << 10);
  positions.reserve(8 << 10);

  git_reference_iterator* iter;
  VERIFY(!git_reference_iterator_glob_new(&iter, repo, "refs/tags/*")) << GitError();
  ON_SCOPE_EXIT(&) { git_reference_iterator_free(iter); };

  while (true) {
    const char* name;
    int error = git_reference_next_name(&name, iter);
    if (error == GIT_ITEROVER) return;
    VERIFY(!error) << "git_reference_next_name: " << GitError();
    positions.push_back(arena.size());
    arena += name;
    arena += '\0';
  }
}

bool StatEq(const struct stat& x, const struct stat& y) {
  return !std::memcmp(&x.st_mtim, &y.st_mtim, sizeof(x.st_mtim)) && x.st_size == y.st_size &&
         x.st_ino == y.st_ino;
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

const char* RemoteUrl(git_repository* repo, const git_reference* ref) {
  git_buf remote_name = {};
  if (git_branch_remote_name(&remote_name, repo, git_reference_name(ref))) return "";
  ON_SCOPE_EXIT(&) { git_buf_free(&remote_name); };

  git_remote* remote = nullptr;
  switch (git_remote_lookup(&remote, repo, remote_name.ptr)) {
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
  git_reference* symbolic = nullptr;
  switch (git_reference_lookup(&symbolic, repo, "HEAD")) {
    case 0:
      break;
    case GIT_ENOTFOUND:
      return nullptr;
    default:
      LOG(ERROR) << "git_reference_lookup: " << GitError();
      throw Exception();
  }

  git_reference* direct = nullptr;
  if (git_reference_resolve(&direct, symbolic)) {
    LOG(INFO) << "Empty git repo (no HEAD)";
    return symbolic;
  }
  git_reference_free(symbolic);
  return direct;
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

const char* LocalBranchName(const git_reference* ref) {
  CHECK(ref);
  git_reference_t type = git_reference_type(ref);
  switch (type) {
    case GIT_REFERENCE_DIRECT: {
      return git_reference_is_branch(ref) ? git_reference_shorthand(ref) : "";
    }
    case GIT_REFERENCE_SYMBOLIC: {
      static constexpr char kHeadPrefix[] = "refs/heads/";
      const char* target = git_reference_symbolic_target(ref);
      if (!target) return "";
      size_t len = std::strlen(target);
      if (len < sizeof(kHeadPrefix)) return "";
      if (std::memcmp(target, kHeadPrefix, sizeof(kHeadPrefix) - 1)) return "";
      return target + (sizeof(kHeadPrefix) - 1);
    }
    case GIT_REFERENCE_INVALID:
    case GIT_REFERENCE_ALL:
      break;
  }
  LOG(ERROR) << "Invalid reference type: " << type;
  throw Exception();
}

const char* RemoteBranchName(git_repository* repo, const git_reference* ref) {
  const char* branch = nullptr;
  if (git_branch_name(&branch, ref)) return "";
  git_buf remote = {};
  if (git_branch_remote_name(&remote, repo, git_reference_name(ref))) return "";
  ON_SCOPE_EXIT(&) { git_buf_free(&remote); };
  VERIFY(std::strstr(branch, remote.ptr) == branch);
  VERIFY(branch[remote.size] == '/');
  return branch + remote.size + 1;
}

std::future<std::string> GetTagName(git_repository* repo, const git_oid* target) {
  auto* promise = new std::promise<std::string>;
  std::future<std::string> res = promise->get_future();

  g_thread_pool->Schedule([=] {
    ON_SCOPE_EXIT(&) { delete promise; };
    if (!target) {
      promise->set_value("");
      return;
    }

    try {
      std::string arena;
      std::vector<size_t> positions;
      ListTags(repo, arena, positions);
      
      git_refdb* refdb;
      VERIFY(!git_repository_refdb(&refdb, repo)) << GitError();
      ON_SCOPE_EXIT(&) { git_refdb_free(refdb); };

      std::string tag;
      bool error = 0;
      size_t inflight = 0;
      std::mutex mutex;
      std::condition_variable cv;

      const size_t kNumShards = g_thread_pool->num_threads();
      for (size_t i = 0; i != kNumShards; ++i) {
        size_t begin = i * positions.size() / g_thread_pool->num_threads();
        size_t end = (i + 1) * positions.size() / g_thread_pool->num_threads();
        if (begin == end) continue;

        auto F = [&, begin, end]() {
          ON_SCOPE_EXIT(&) {
            std::unique_lock<std::mutex> lock(mutex);
            CHECK(inflight);
            if (--inflight == 0) cv.notify_one();
          };

          try {
            for (size_t i = begin; i != end; ++i) {
              const char* name = arena.c_str() + positions[i];
              if (TagHasTarget(repo, refdb, name, target)) {
                CHECK(std::strstr(name, kTagPrefix) == name);
                name += sizeof(kTagPrefix) - 1;
                std::unique_lock<std::mutex> lock(mutex);
                if (tag < name) tag = name;
                return;
              }
            }
          } catch (const Exception&) {
            std::unique_lock<std::mutex>{mutex}, error = true;
          }
        };

        std::unique_lock<std::mutex>{mutex}, ++inflight;
        if (i == kNumShards - 1) {
          F();
        } else {
          g_thread_pool->Schedule(std::move(F));
        }
      }

      {
        std::unique_lock<std::mutex> lock(mutex);
        while (inflight) cv.wait(lock);
      }

      VERIFY(!error);
      promise->set_value(std::move(tag));
    } catch (const Exception&) {
      promise->set_exception(std::current_exception());
    }
  });

  return res;
}

Repo::Repo(git_repository* repo) : repo_(repo), tag_db_(repo) {}

Repo::~Repo() {
  Wait();
  if (index_) git_index_free(index_);
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

IndexStats Repo::GetIndexStats(const git_oid* head, size_t dirty_max_index_size) {
  Wait();
  if (index_) {
    VERIFY(!git_index_read(index_, 0)) << GitError();
  } else {
    VERIFY(!git_repository_index(&index_, repo_)) << GitError();
    // Query an attribute (doesn't matter which) to initialize repo's attribute
    // cache. It's a workaround for synchronization bugs (data races) in libgit2
    // that result from lazy cache initialization with no synchrnonization whatsoever.
    const char* attr;
    VERIFY(!git_attr_get(&attr, repo_, 0, "x", "x")) << GitError();
  }
  UpdateShards();
  Store(error_, false);
  UpdateKnown();

  const size_t index_size = git_index_entrycount(index_);
  const bool scan_dirty = index_size <= dirty_max_index_size;

  auto Done = [&] {
    return (!head || !staged_.Empty()) &&
           (!scan_dirty || (!unstaged_.Empty() && !untracked_.Empty()));
  };

  if (!Done()) {
    CHECK(Load(inflight_) == 0);
    if (scan_dirty) StartDirtyScan();
    if (head) StartStagedScan(head);

    {
      std::unique_lock<std::mutex> lock(mutex_);
      while (Load(inflight_) && !Load(error_) && !Done()) cv_.wait(lock);
    }
  }

  if (Load(error_)) throw Exception();

  return {
      // An empty repo with non-empty index must have staged changes since it cannot have unstaged
      // changes.
      .has_staged = !staged_.Empty() || (!head && index_size),
      .has_unstaged = !unstaged_.Empty() ? kTrue : scan_dirty ? kFalse : kUnknown,
      .has_untracked = !untracked_.Empty() ? kTrue : scan_dirty ? kFalse : kUnknown,
  };
}

void Repo::StartDirtyScan() {
  if (!unstaged_.Empty() && !untracked_.Empty()) return;

  git_diff_options opt = GIT_DIFF_OPTIONS_INIT;
  opt.payload = this;
  opt.flags = GIT_DIFF_SKIP_BINARY_CHECK;
  if (untracked_.Empty()) {
    // We could remove GIT_DIFF_RECURSE_UNTRACKED_DIRS and manually check in OnDirty whether
    // the allegedly untracked file is an empty directory. Unfortunately, it'll break
    // UpdateDirty() because we cannot use git_status_file on a directory. Seems like there is no
    // way to quickly get any untracked file from a directory that definitely has untracked files.
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

  for (const Shard& shard : shards_) {
    RunAsync([this, opt, shard]() mutable {
      opt.range_start = shard.start.c_str();
      opt.range_end = shard.end.c_str();
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

void Repo::StartStagedScan(const git_oid* head) {
  if (!staged_.Empty()) return;
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
    repo->UpdateFile(repo->staged_, "staged", delta->new_file.path);
    return GIT_EUSER;
  };

  for (const Shard& shard : shards_) {
    RunAsync([this, tree, opt, shard]() mutable {
      opt.range_start = shard.start.c_str();
      opt.range_end = shard.end.c_str();
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

void Repo::UpdateShards() {
  constexpr size_t kEntriesPerShard = 512;
  static_assert(std::is_unsigned<char>(), "");

  size_t index_size = git_index_entrycount(index_);
  ON_SCOPE_EXIT(&) {
    LOG(INFO) << "Splitting " << index_size << " object(s) into " << shards_.size() << " shard(s)";
  };

  if (index_size <= kEntriesPerShard || g_thread_pool->num_threads() < 2) {
    shards_ = {{""s, ""s}};
    return;
  }

  size_t shards = std::min(index_size / kEntriesPerShard + 1, 2 * g_thread_pool->num_threads());
  shards_.clear();
  shards_.reserve(shards);
  std::string last;

  for (size_t i = 0; i != shards - 1; ++i) {
    std::string split = git_index_get_byindex(index_, (i + 1) * index_size / shards)->path;
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

std::future<std::string> Repo::GetTagName(const git_oid* target) {
  auto* promise = new std::promise<std::string>;
  std::future<std::string> res = promise->get_future();

  g_thread_pool->Schedule([=] {
    Timer timer;
    ON_SCOPE_EXIT(&) { timer.Report("GetTagName"); };
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

TagDb::TagDb(git_repository* repo) : repo_(repo) { CHECK(repo); }

TagDb::~TagDb() { Wait(); }

const char* TagDb::TagForCommit(const git_oid& oid) {
  const char* ref;
  if (UpdatePack(oid, &ref)) {
    if (ref) return StripTag(ref);
  } else {
    auto it = BinaryFind(peeled_tags_, Tag{nullptr, oid});
    if (it != peeled_tags_.end()) return StripTag(it->ref);
  }

  git_refdb* refdb;
  VERIFY(!git_repository_refdb(&refdb, repo_)) << GitError();
  ON_SCOPE_EXIT(&) { git_refdb_free(refdb); };

  for (const char* tag : loose_tags_) {
    if (TagHasTarget(repo_, refdb, tag, &oid)) return StripTag(tag);
  }

  return "";
}

bool TagDb::UpdatePack(const git_oid& commit, const char** ref) {
  Wait();

  auto Reset = [&] {
    std::memset(&pack_stat_, 0, sizeof(pack_stat_));
    pack_.clear();
    loose_tags_.clear();
    peeled_tags_.clear();
  };

  std::string pack_path = git_repository_path(repo_) + "packed-refs"s;
  struct stat st;
  if (lstat(pack_path.c_str(), &st)) {
    Reset();
    return false;
  }
  if (StatEq(pack_stat_, st)) return false;

  try {
    while (true) {
      int fd = open(pack_path.c_str(), O_RDONLY | O_NOFOLLOW | O_NOATIME | O_CLOEXEC);
      VERIFY(fd >= 0);
      ON_SCOPE_EXIT(&) { CHECK(!close(fd)) << Errno(); };
      pack_.resize(st.st_size + 1);
      ssize_t n = read(fd, &pack_[0], st.st_size + 1);
      VERIFY(n >= 0) << Errno();
      VERIFY(!fstat(fd, &pack_stat_)) << Errno();
      if (!StatEq(st, pack_stat_)) {
        st = pack_stat_;
        continue;
      }
      VERIFY(n == st.st_size);
      pack_.pop_back();
      break;
    }
    *ref = ParsePack(commit);
    return true;
  } catch (const Exception&) {
    Reset();
    throw;
  }
}

const char* TagDb::ParsePack(const git_oid& commit) {
  char* p = &pack_[0];
  char* e = p + pack_.size();
  bool peeled = false;
  const char* res = nullptr;

  if (*p == '#') {
    char* eol = std::strchr(p, '\n');
    if (!eol) return nullptr;
    *eol = 0;
    peeled = std::strstr(p, " fully-peeled ");
    p = eol + 1;
  }

  if (peeled) {
    peeled_tags_.reserve(pack_.size() / 128);
  } else {
    loose_tags_.reserve(pack_.size() / 128);
  }

  std::vector<Tag*> idx;
  idx.reserve(pack_.size() / 128);

  git_oid oid;
  while (p != e) {
    ParseOid(oid.id, p, e);
    p += GIT_OID_HEXSZ;
    VERIFY(*p++ == ' ');
    const char* ref = p;
    VERIFY(p = std::strchr(p, '\n'));
    p[p[-1] == '\r' ? -1 : 0] = 0;
    ++p;
    if (*p == '^') {
      ParseOid(oid.id, p + 1, e);
      p += GIT_OID_HEXSZ + 1;
      if (p != e) {
        VERIFY((p = std::strchr(p, '\n')));
        ++p;
      }
    }
    if (!StripTag(ref)) continue;
    if (peeled) {
      peeled_tags_.push_back({ref, oid});
      if (!std::memcmp(oid.id, commit.id, GIT_OID_RAWSZ)) res = ref;
    } else {
      loose_tags_.push_back(ref);
    }
  }

  sorting_ = true;
  g_thread_pool->Schedule([this] {
    std::sort(peeled_tags_.begin(), peeled_tags_.end());
    std::unique_lock<std::mutex> lock(mutex_);
    CHECK(sorting_);
    sorting_ = false;
    cv_.notify_one();
  });

  return res;
}

void TagDb::Wait() {
  std::unique_lock<std::mutex> lock(mutex_);
  while (sorting_) cv_.wait(lock);
}

}  // namespace gitstatus
