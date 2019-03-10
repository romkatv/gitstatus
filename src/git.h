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

#include <atomic>
#include <condition_variable>
#include <cstddef>
#include <functional>
#include <mutex>
#include <string>
#include <vector>

namespace gitstatus {

class Repo {
 public:
  explicit Repo(git_repository* repo);
  Repo(Repo&& other) = delete;
  ~Repo();

  git_repository* repo() const { return repo_; }
  git_index* index() const { return index_; }

  void UpdateKnown();

  bool HasStaged(git_reference* head);

  void ScanDirty();

  bool HasUnstaged() const;
  bool HasUntracked() const;

 private:
  void UpdateSplits();
  void DecInflight();
  void RunAsync(std::function<void()> f);
  int OnDelta(git_delta_t status, const char* path);
  void Wait(bool full_stop);

  git_repository* const repo_;
  git_index* const index_;
  std::vector<std::string> splits_;

  std::mutex mutex_;
  std::string staged_;
  std::string untracked_;
  std::string unstaged_;
  std::condition_variable cv_;
  std::atomic<bool> has_unstaged_{false};
  std::atomic<bool> has_untracked_{false};
  std::atomic<size_t> inflight_{0};
};

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

}  // namespace gitstatus

#endif  // ROMKATV_GITSTATUS_GIT_H_
