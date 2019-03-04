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

#include <cstddef>
#include <string>

namespace gitstatus {

struct Dirty {
  // Are there unstaged changes?
  bool unstaged = false;
  // Are there untracked files?
  bool untracked = false;
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
std::string_view BranchName(const git_reference* ref);

bool HasStaged(git_repository* repo, git_reference* head, git_index* index);

Dirty GetDirty(git_repository* repo, git_index* index);

}  // namespace gitstatus

#endif  // ROMKATV_GITSTATUS_GIT_H_
