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

#include "tag_db.h"

#include <fcntl.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#include <algorithm>
#include <cstdlib>
#include <cstring>
#include <iterator>
#include <utility>

#include "check.h"
#include "dir.h"
#include "git.h"
#include "scope_guard.h"
#include "stat.h"
#include "string_cmp.h"
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

bool GetLooseTags(git_repository* repo, Arena& arena, std::vector<char*>& tags) {
  std::string dirname = git_repository_path(repo) + "refs/tags"s;
  int dir_fd = open(dirname.c_str(), O_RDONLY | O_DIRECTORY | O_CLOEXEC);
  if (dir_fd < 0) {
    tags.clear();
    return false;
  }
  ON_SCOPE_EXIT(&) { CHECK(!close(dir_fd)) << Errno(); };
  return ListDir(dir_fd, arena, tags, /* case_sensitive = */ true);
}

}  // namespace

TagDb::TagDb(git_repository* repo) : repo_(repo) { CHECK(repo); }

TagDb::~TagDb() { Wait(); }

std::string TagDb::TagForCommit(const git_oid& oid) {
  git_refdb* refdb;
  VERIFY(!git_repository_refdb(&refdb, repo_)) << GitError();
  ON_SCOPE_EXIT(&) { git_refdb_free(refdb); };

  auto StrLt = [](const char* a, const char* b) { return std::strcmp(a, b) < 0; };

  std::string res;
  Arena arena;
  std::vector<char*> loose_tags;
  loose_tags.reserve(128);

  if (GetLooseTags(repo_, arena, loose_tags)) {
    std::string ref = "refs/tags/";
    size_t prefix_len = ref.size();
    for (const char* tag : loose_tags) {
      ref.resize(prefix_len);
      ref += tag;
      if (TagHasTarget(repo_, refdb, ref.c_str(), &oid)) {
        if (res < tag) res = tag;
      }
    }
  }

  std::vector<const char*> matches;
  if (UpdatePack(oid, matches)) {
    for (auto it = matches.rbegin(); it != matches.rend(); ++it) {
      if (!std::binary_search(loose_tags.begin(), loose_tags.end(), *it, StrLt)) {
        if (res < *it) res = *it;
        return res;
      }
    }
  } else {
    auto r = std::equal_range(peeled_tags_.begin(), peeled_tags_.end(), Tag{nullptr, oid});
    for (auto it = r.first; it != r.second; ++it) {
      const char* tag = StripTag(it->ref);
      if (!std::binary_search(loose_tags.begin(), loose_tags.end(), tag, StrLt)) {
        if (res < tag) res = tag;
      }
    }
  }

  return res;
}

bool TagDb::UpdatePack(const git_oid& commit, std::vector<const char*>& match) {
  Wait();
  match.clear();

  auto Reset = [&] {
    std::memset(&pack_stat_, 0, sizeof(pack_stat_));
    pack_.clear();
    unpeeled_tags_.clear();
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
      LOG(INFO) << "Parsing " << pack_path;
      int fd = open(pack_path.c_str(), O_RDONLY | O_NOFOLLOW | O_CLOEXEC);
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
    match = ParsePack(commit);
    return true;
  } catch (const Exception&) {
    Reset();
    throw;
  }
}

std::vector<const char*> TagDb::ParsePack(const git_oid& commit) {
  char* p = &pack_[0];
  char* e = p + pack_.size();
  bool peeled = false;
  std::vector<const char*> res;

  if (*p == '#') {
    char* eol = std::strchr(p, '\n');
    if (!eol) return res;
    *eol = 0;
    peeled = std::strstr(p, " fully-peeled ");
    p = eol + 1;
  }

  if (peeled) {
    peeled_tags_.reserve(pack_.size() / 128);
  } else {
    unpeeled_tags_.reserve(pack_.size() / 128);
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
    const char* tag = StripTag(ref);
    if (!tag) continue;
    if (peeled) {
      peeled_tags_.push_back({ref, oid});
      if (!std::memcmp(oid.id, commit.id, GIT_OID_RAWSZ)) res.push_back(tag);
    } else {
      unpeeled_tags_.push_back(ref);
    }
  }

  StrSort(unpeeled_tags_.begin(), unpeeled_tags_.end(), /* case_sensitive = */ true);

  sorting_ = true;
  GlobalThreadPool()->Schedule([this] {
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
