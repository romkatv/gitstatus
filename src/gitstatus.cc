// Copyright 2018 Roman Perepelitsa.
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

#include <getopt.h>
#include <unistd.h>

#include <git2.h>

#include <cerrno>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <string>

#include "check.h"
#include "logging.h"

namespace {

using namespace std::string_literals;

const char* GitError() {
  const git_error* err = git_error_last();
  return err && err->message ? err->message : "unknown error";
}

// These names mostly match gitaction in vcs_info:
// https://github.com/zsh-users/zsh/blob/master/Functions/VCS_Info/Backends/VCS_INFO_get_data_git.
const char* RepoState(git_repository* repo) {
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

// Returns the number of commits in the range.
size_t CountRange(git_repository* repo, const std::string& range) {
  git_revwalk* walk = nullptr;
  CHECK(git_revwalk_new(&walk, repo) == 0) << GitError();
  CHECK(git_revwalk_push_range(walk, range.c_str()) == 0) << GitError();
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
        LOG(FATAL) << GitError();
    }
  }
}

// Are there staged changes?
bool HasStaged(git_repository* repo, git_index* index, const git_reference* head) {
  const git_oid* oid = git_reference_target(head);
  CHECK(oid);
  git_commit* commit = nullptr;
  CHECK(git_commit_lookup(&commit, repo, oid) == 0) << GitError();
  git_tree* tree = nullptr;
  CHECK(git_commit_tree(&tree, commit) == 0) << GitError();
  git_diff_options opt = GIT_DIFF_OPTIONS_INIT;
  opt.notify_cb = [](auto...) -> int { return GIT_EUSER; };
  git_diff* diff = nullptr;
  switch (git_diff_tree_to_index(&diff, repo, tree, index, &opt)) {
    case 0:
      return false;
    case GIT_EUSER:
      return true;
    default:
      LOG(FATAL) << GitError();
  }
}

struct Dirty {
  // Are there unstaged changes?
  bool unstaged;
  // Are there untracked files?
  bool untracked;
};

// Scans files in the repo to see what's dirty. This is by far the slowest operation and it can be
// prohibitively slow on large repos.
Dirty GetDirty(git_repository* repo, git_index* index) {
  Dirty res = {};
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
    case GIT_EUSER:
      return res;
    default:
      LOG(FATAL) << GitError();
  }
}

// Finds and opens a repo from the current directory.
git_repository* OpenRepo() {
  git_repository* repo = nullptr;
  switch (git_repository_open_ext(&repo, ".", GIT_REPOSITORY_OPEN_FROM_ENV, nullptr)) {
    case 0:
      break;
    case GIT_ENOTFOUND:
      std::quick_exit(1);
    default:
      LOG(FATAL) << GitError();
  }
  if (git_repository_is_bare(repo)) std::quick_exit(1);
  return repo;
}

// How many stashes are there?
size_t NumStashes(git_repository* repo) {
  size_t res = 0;
  auto* cb = +[](size_t index, const char* message, const git_oid* stash_id, void* payload) {
    ++*static_cast<size_t*>(payload);
    return 0;
  };
  CHECK(git_stash_foreach(repo, cb, &res) == 0) << GitError();
  return res;
}

// Returns the origin URL or an empty string.
const char* RemoteUrl(git_repository* repo) {
  git_remote* remote = nullptr;
  switch (git_remote_lookup(&remote, repo, "origin")) {
    case 0:
      return git_remote_url(remote);
    case GIT_ENOTFOUND:
    case GIT_EINVALIDSPEC:
      return "";
    default:
      LOG(FATAL) << GitError();
  }
}

void Print(ssize_t val) { std::cout << val << '\n'; }

void Print(std::string_view val) {
  static constexpr char kEscape[] = "\"\\";
  std::cout << '"';
  for (char c : val) {
    if (std::strchr(kEscape, c)) std::cout << '\\';
    std::cout << c;
  }
  std::cout << '"';
  std::cout << '\n';
}

git_reference* Head(git_repository* repo) {
  git_reference* head = nullptr;
  switch (git_repository_head(&head, repo)) {
    case 0:
      return head;
    case GIT_ENOTFOUND:
    case GIT_EUNBORNBRANCH:
      std::quick_exit(1);
      break;
    default:
      LOG(FATAL) << GitError();
  }
}

// Returns a reference to the upstream branch or null.
git_reference* Upstream(git_reference* local) {
  git_reference* upstream = nullptr;
  switch (git_branch_upstream(&upstream, local)) {
    case 0:
      return upstream;
    case GIT_ENOTFOUND:
      return nullptr;
    default:
      CHECK(git_error_last()->klass == GIT_ERROR_INVALID) << GitError();
      return nullptr;
  }
}

// Returns the name of the branch. This is the segment after the last '/'.
std::string_view BranchName(const git_reference* ref) {
  std::string_view name = git_reference_name(ref);
  return name.substr(name.find_last_of('/') + 1);
}

struct Options {
  // If a repo has more files in its index than this, don't scan its files to see what's dirty.
  // Instead, report -1 as the the number of unstaged changes and untracked files.
  size_t dirty_max_index_size = -1;
};

long ParseLong(const char* s) {
  errno = 0;
  char* end = nullptr;
  long res = std::strtol(s, &end, 10);
  if (*end || errno) {
    std::cerr << "gitstatus: not an integer: " << s << std::endl;
    std::exit(1);
  }
  return res;
}

void PrintUsage() {
  std::cout << "Usage: gitstatus [OPTION]...\n"
            << "Print machine-readable status of the git repository at the current directory.\n"
            << "\n"
            << "OPTIONS\n"
            << "  -m, --dirty-max-index-size=NUM [default=-1]\n"
            << "   Report -1 unstaged and untracked if there are more than this many files in\n"
            << "   the index; negative value means infinity.\n"
            << "\n"
            << "  -h, --help\n"
            << "  Display this help and exit.\n"
            << "\n"
            << "OUTPUT\n"
            << "\n"
            << "  Unless -h or --help is specified, the output of a successful invocation of\n"
            << "  gitstatus always consists of 10 lines. Strings are enclosed in double quotes.\n"
            << "\n"
            << "     1. Repository HEAD. Usually branch name. Not empty.\n"
            << "     2. Upstream branch name. Can be empty.\n"
            << "     3. Remote URL. Can be empty.\n"
            << "     4. Repository state, A.K.A. action. Can be empty.\n"
            << "     5. 1 if there are staged changes, 0 otherwise.\n"
            << "     6. 1 if there are unstaged changes, 0 if there aren't, -1 if unknown.\n"
            << "     7. 1 if there are untracked files, 0 if there aren't, -1 if unknown.\n"
            << "     8. Number of commits the current branch is ahead of upstream.\n"
            << "     9. Number of commits the current branch is behind upstream.\n"
            << "    10. Number of stashes.\n"
            << "\n"
            << "  Example:\n"
            << "\n"
            << "    \"master\"\n"
            << "    \"master\"\n"
            << "    \"git@github.com:romkatv/gitstatus.git\"\n"
            << "    \"\"\n"
            << "    1\n"
            << "    1\n"
            << "    0\n"
            << "    3\n"
            << "    0\n"
            << "    2\n"
            << "\n"
            << "EXIT STATUS\n"
            << "\n"
            << "  The command returns zero on success, non-zero on failure. In the latter case\n"
            << "  the output is unspecified."
            << "\n"
            << "COPYRIGHT\n"
            << "\n"
            << "  Copyright 2019 Roman Perepelitsa\n"
            << "  This is free software; see https://github.com/romkatv/gitstatus for copying\n"
            << "  conditions. There is NO warranty; not even for MERCHANTABILITY or FITNESS FOR\n"
            << "  A PARTICULAR PURPOSE." << std::endl;
}

Options ParseOpts(int argc, char** argv) {
  const struct option opts[] = {{"dirty-max-index-size", required_argument, nullptr, 'm'},
                                {"help", no_argument, nullptr, 'h'},
                                {}};
  Options res;
  while (true) {
    switch (getopt_long(argc, argv, "m:h", opts, nullptr)) {
      case -1:
        return res;
      case 'h':
        PrintUsage();
        std::exit(0);
      case 'm':
        res.dirty_max_index_size = ParseLong(optarg);
        break;
      default:
        std::exit(1);
    }
  }
}

}  // namespace

int main(int argc, char** argv) {
  Options opts = ParseOpts(argc, argv);

  git_libgit2_init();
  git_repository* repo = OpenRepo();
  git_reference* head = Head(repo);

  // Repository HEAD. Usually branch name.
  Print(git_reference_shorthand(head));

  // Upstream branch name.
  git_reference* upstream = Upstream(head);
  Print(upstream ? BranchName(upstream) : "");

  // Remote url.
  Print(RemoteUrl(repo) ?: "");

  // Repository state, A.K.A. action.
  Print(RepoState(repo));

  git_index* index = nullptr;
  CHECK(git_repository_index(&index, repo) == 0) << GitError();

  // 1 if there are staged changes, 0 otherwise.
  Print(HasStaged(repo, index, head));

  if (git_index_entrycount(index) <= opts.dirty_max_index_size) {
    Dirty dirty = GetDirty(repo, index);
    // 1 if there are unstaged changes, 0 otherwise.
    Print(dirty.unstaged);
    // 1 if there are untracked files, 0 otherwise.
    Print(dirty.untracked);
  } else {
    Print(-1);
    Print(-1);
  }

  // Number of commits we are ahead of upstream.
  Print(upstream ? CountRange(repo, git_reference_shorthand(upstream) + "..HEAD"s) : 0);
  // Number of commits we are behind upstream.
  Print(upstream ? CountRange(repo, "HEAD.."s + git_reference_shorthand(upstream)) : 0);

  // Number of stashes.
  Print(NumStashes(repo));

  std::cout << std::flush;
  // Exit without freeing any resources or destroying globals. Faster this way.
  std::quick_exit(0);
}
