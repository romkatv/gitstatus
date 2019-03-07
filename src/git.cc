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

#include <cstring>

#include "check.h"
#include "scope_guard.h"

namespace gitstatus {

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

bool HasStaged(git_repository* repo, git_reference* head, git_index* index) {
  const git_oid* oid = git_reference_target(head);
  VERIFY(oid);
  git_commit* commit = nullptr;
  VERIFY(!git_commit_lookup(&commit, repo, oid)) << GitError();
  ON_SCOPE_EXIT(=) { git_commit_free(commit); };
  git_tree* tree = nullptr;
  VERIFY(!git_commit_tree(&tree, commit)) << GitError();
  git_diff_options opt = GIT_DIFF_OPTIONS_INIT;
  opt.notify_cb = [](auto...) -> int { return GIT_EUSER; };
  git_diff* diff = nullptr;
  switch (git_diff_tree_to_index(&diff, repo, tree, index, &opt)) {
    case 0:
      git_diff_free(diff);
      return false;
    case GIT_EUSER:
      return true;
    default:
      LOG(ERROR) << "git_diff_tree_to_index: " << GitError();
      throw Exception();
  }
}

Dirty GetDirty(git_repository* repo, git_index* index) {
  Dirty res;
  git_diff_options opt = GIT_DIFF_OPTIONS_INIT;
  opt.payload = &res;
  opt.flags = GIT_DIFF_INCLUDE_UNTRACKED;
  opt.ignore_submodules = GIT_SUBMODULE_IGNORE_DIRTY;
  opt.notify_cb = [](const git_diff* diff, const git_diff_delta* delta,
                     const char* matched_pathspec, void* payload) {
    Dirty* dirty = static_cast<Dirty*>(payload);
    (delta->status == GIT_DELTA_UNTRACKED ? dirty->untracked : dirty->unstaged) = true;
    return dirty->unstaged && dirty->untracked ? GIT_EUSER : 1;
  };
  git_diff* diff = nullptr;
  switch (git_diff_index_to_workdir(&diff, repo, index, &opt)) {
    case 0:
      git_diff_free(diff);
    case GIT_EUSER:
      return res;
    default:
      LOG(ERROR) << "git_diff_index_to_workdir: " << GitError();
      throw Exception();
  }
}

}  // namespace gitstatus
