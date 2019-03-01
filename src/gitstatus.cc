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

#include <cstddef>

#include <string>

#include <git2.h>

#include "check.h"
#include "git.h"
#include "logging.h"
#include "options.h"
#include "repo_cache.h"
#include "request_reader.h"
#include "response_writer.h"
#include "scope_guard.h"
#include "timer.h"

namespace gitstatus {
namespace {

using namespace std::string_literals;

void ProcessRequest(const Options& opts, RepoCache& cache, std::string dir) {
  LOG(INFO) << "Processing request: " << dir;
  ResponseWriter out;
  if (dir.empty() || dir.front() != '/') return;
  if (dir.back() != '/') dir += '/';

  git_repository* repo = cache.Find(dir);
  if (!repo) {
    repo = OpenRepo(dir);
    if (!repo) return;
    if (git_repository_is_bare(repo) || git_repository_is_empty(repo) || !cache.Put(repo)) {
      git_repository_free(repo);
      return;
    }
  }

  git_reference* head = Head(repo);
  if (!head) return;
  ON_SCOPE_EXIT(=) { git_reference_free(head); };

  // Repository HEAD. Usually branch name.
  out.Print(git_reference_shorthand(head));

  git_reference* upstream = Upstream(head);
  ON_SCOPE_EXIT(=) {
    if (upstream) git_reference_free(upstream);
  };
  // Upstream branch name.
  out.Print(upstream ? BranchName(upstream) : "");

  // Remote url.
  out.Print(RemoteUrl(repo));

  // Repository state, A.K.A. action.
  out.Print(RepoState(repo));

  git_index* index = nullptr;
  VERIFY(!git_repository_index(&index, repo)) << GitError();
  ON_SCOPE_EXIT(=) { git_index_free(index); };
  VERIFY(!git_index_read(index, 0)) << GitError();

  // 1 if there are staged changes, 0 otherwise.
  out.Print(HasStaged(repo, head, index));

  if (git_index_entrycount(index) <= opts.dirty_max_index_size) {
    Dirty dirty = GetDirty(repo, index);
    // 1 if there are unstaged changes, 0 otherwise.
    out.Print(dirty.unstaged);
    // 1 if there are untracked files, 0 otherwise.
    out.Print(dirty.untracked);
  } else {
    out.Print(-1);
    out.Print(-1);
  }

  if (upstream) {
    // Number of commits we are ahead of upstream.
    out.Print(CountRange(repo, git_reference_shorthand(upstream) + "..HEAD"s));
    // Number of commits we are behind upstream.
    out.Print(CountRange(repo, "HEAD.."s + git_reference_shorthand(upstream)));
  } else {
    out.Print(0);
    out.Print(0);
  }

  // Number of stashes.
  out.Print(NumStashes(repo));

  out.Dump();
}

int GitStatus(int argc, char** argv) {
  Options opts = ParseOptions(argc, argv);
  LOG(INFO) << "Parent PID: " << opts.parent_pid;
  RequestReader reader(fileno(stdin), opts.parent_pid);
  RepoCache cache;

  git_libgit2_init();

  while (true) {
    try {
      ProcessRequest(opts, cache, reader.ReadRequest());
    } catch (const Exception&) {
      LOG(ERROR) << "Failed to process request";
    }
  }
}

}  // namespace
}  // namespace gitstatus

int main(int argc, char** argv) { gitstatus::GitStatus(argc, argv); }
