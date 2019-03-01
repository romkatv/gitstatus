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

#include <fcntl.h>
#include <getopt.h>
#include <signal.h>
#include <sys/resource.h>
#include <sys/select.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <sys/types.h>
#include <unistd.h>

#include <algorithm>
#include <cerrno>
#include <climits>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <deque>
#include <iostream>
#include <map>
#include <string>
#include <utility>

#include <git2.h>

#include "check.h"
#include "logging.h"

namespace {

using namespace std::string_literals;

template <class F>
class ScopeGuard {
 public:
  explicit ScopeGuard(F f) : f_(std::move(f)) {}
  ~ScopeGuard() { std::move(f_)(); }
  ScopeGuard(ScopeGuard&&) = delete;

 private:
  F f_;
};

struct ScopeGuardGenerator {
  template <class F>
  ScopeGuard<F> operator=(F f) const {
    return ScopeGuard<F>(std::move(f));
  }
};

#define CAT_I(x, y) x##y
#define CAT(x, y) CAT_I(x, y)
#define ON_SCOPE_EXIT(capture...) \
  auto CAT(_scope_guard_, __COUNTER__) = ScopeGuardGenerator() = [capture]()

double CpuTimeMs() {
  auto ToMs = [](const timeval& tv) { return 1e3 * tv.tv_sec + 1e-3 * tv.tv_usec; };
  rusage usage = {};
  CHECK(getrusage(RUSAGE_SELF, &usage) == 0);
  return ToMs(usage.ru_utime) + ToMs(usage.ru_stime);
}

class Timer {
 public:
  Timer() { Start(); }
  void Start() { ms_ = CpuTimeMs(); }
  void Report(std::string_view msg) {
    std::cerr << "prof: " << msg << " : " << CpuTimeMs() - ms_ << '\n';
    Start();
  }

  double ms_;
};

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

// Returns the number of commits in the range. Returns -1 on error.
ssize_t CountRange(git_repository* repo, const std::string& range) {
  git_revwalk* walk = nullptr;
  if (git_revwalk_new(&walk, repo)) {
    LOG(ERROR) << "git_revwalk_new: " << GitError();
    return -1;
  }
  ON_SCOPE_EXIT(=) { git_revwalk_free(walk); };
  if (git_revwalk_push_range(walk, range.c_str())) {
    LOG(ERROR) << "git_revwalk_push_range: " << GitError();
    return -1;
  }
  ssize_t res = 0;
  while (true) {
    git_oid oid;
    switch (git_revwalk_next(&oid, walk)) {
      case 0:
        ++res;
        break;
      case GIT_ITEROVER:
        return res;
      default:
        LOG(ERROR) << "git_revwalk_next: " << GitError();
        return -1;
    }
  }
}

// Finds and opens a repo from the specified directory.
git_repository* OpenRepo(const std::string& dir) {
  git_repository* repo = nullptr;
  switch (git_repository_open_ext(&repo, dir.c_str(), GIT_REPOSITORY_OPEN_FROM_ENV, nullptr)) {
    case 0:
      return repo;
    case GIT_ENOTFOUND:
      return nullptr;
    default:
      LOG(ERROR) << "git_repository_open_ext: " << GitError();
      return nullptr;
  }
}

// How many stashes are there?
ssize_t NumStashes(git_repository* repo) {
  size_t res = 0;
  auto* cb = +[](size_t index, const char* message, const git_oid* stash_id, void* payload) {
    ++*static_cast<size_t*>(payload);
    return 0;
  };
  if (git_stash_foreach(repo, cb, &res)) {
    LOG(ERROR) << "git_stash_foreach: " << GitError();
    return -1;
  }
  return res;
}

// Returns the origin URL or an empty string.
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
      return nullptr;
  }
}

class Output {
 public:
  Output() { strm_ << 1; }
  Output(Output&&) = delete;

  ~Output() {
    if (!done_) std::cout << 0 << std::endl;
  }

  void Print(ssize_t val) {
    strm_ << ' ';
    strm_ << val;
  }

  void Print(std::string_view val) {
    static constexpr char kEscape[] = "\n\"\\";
    strm_ << ' ';
    strm_ << '"';
    for (char c : val) {
      if (std::strchr(kEscape, c)) strm_ << '\\';
      strm_ << c;
    }
    strm_ << '"';
  }

  void Dump() {
    CHECK(!done_);
    done_ = true;
    std::cout << strm_.str() << std::endl;
  }

 private:
  bool done_ = false;
  std::ostringstream strm_;
};

git_reference* Head(git_repository* repo) {
  git_reference* head = nullptr;
  switch (git_repository_head(&head, repo)) {
    case 0:
      return head;
    case GIT_ENOTFOUND:
    case GIT_EUNBORNBRANCH:
      return nullptr;
      break;
    default:
      LOG(ERROR) << "git_repository_head: " << GitError();
      return nullptr;
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
      if (git_error_last()->klass != GIT_ERROR_INVALID) {
        LOG(ERROR) << "git_branch_upstream: " << GitError();
      }
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
  int parent_pid = -1;
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

long ParseInt(const char* s) {
  long res = ParseLong(s);
  if (res < INT_MIN || res > INT_MAX) {
    std::cerr << "gitstatus: integer out of bounds: " << s << std::endl;
    std::exit(1);
  }
  return res;
}

void PrintUsage() {
  std::cout << "Usage: gitstatus [OPTION]...\n"
            << "Print machine-readable status of the git repos for directores in stdin..\n"
            << "\n"
            << "OPTIONS\n"
            << "  -p, --parent-pid=NUM [default=-1]\n"
            << "   If positive, exit when there is no process with the specified pid.\n"
            << "\n"
            << "  -m, --dirty-max-index-size=NUM [default=-1]\n"
            << "   Report -1 unstaged and untracked if there are more than this many files in\n"
            << "   the index; negative value means infinity.\n"
            << "\n"
            << "  -h, --help\n"
            << "  Display this help and exit.\n"
            << "\n"
            << "INPUT\n"
            << "\n"
            << "  Standard input should consist of absolute directory paths, one per line, with\n"
            << "  no quoting.\n"
            << "\n"
            << "OUTPUT\n"
            << "\n"
            << "  For every input directory there is one line of output written to stdout. Each\n"
            << "  line is either just \"0\" (without quotes) or 11 space-separated values, the\n"
            << "  first of which is \"1\" (without quotes). The remaining values are:\n"
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
            << "  All string values are enclosed in double quotes. Embedded quotes, backslashes\n"
            << "  and LF characters are backslash-escaped.\n"
            << "\n"
            << "  Example:\n"
            << "\n"
            << "    1 \"master\" \"master\" \"git@github.com:foo/bar.git\" \"\" 1 1 0 3 0 2\n"
            << "\n"
            << "EXIT STATUS\n"
            << "\n"
            << "  The command returns zero on success, non-zero on failure. In the latter case\n"
            << "  the output is unspecified.\n"
            << "\n"
            << "COPYRIGHT\n"
            << "\n"
            << "  Copyright 2019 Roman Perepelitsa\n"
            << "  This is free software; see https://github.com/romkatv/gitstatus for copying\n"
            << "  conditions. There is NO warranty; not even for MERCHANTABILITY or FITNESS FOR\n"
            << "  A PARTICULAR PURPOSE." << std::endl;
}

Options ParseOpts(int argc, char** argv) {
  const struct option opts[] = {{"help", no_argument, nullptr, 'h'},
                                {"parent-pid", required_argument, nullptr, 'p'},
                                {"dirty-max-index-size", required_argument, nullptr, 'm'},
                                {}};
  Options res;
  while (true) {
    switch (getopt_long(argc, argv, "hp:m:", opts, nullptr)) {
      case -1:
        return res;
      case 'h':
        PrintUsage();
        std::exit(0);
      case 'p':
        res.parent_pid = ParseInt(optarg);
        break;
      case 'm':
        res.dirty_max_index_size = ParseLong(optarg);
        break;
      default:
        std::exit(1);
    }
  }
}

class LineReader {
 public:
  LineReader(int fd, int parent_pid) : fd_(fd), parent_pid_(parent_pid) {}

  std::string ReadLine() {
    auto eol = std::find(read_.begin(), read_.end(), '\n');
    if (eol != read_.end()) {
      std::string res(read_.begin(), eol);
      read_.erase(read_.begin(), eol + 1);
      return res;
    }
    char buf[256];
    while (true) {
      fd_set fd_set;
      FD_ZERO(&fd_set);
      FD_SET(fd_, &fd_set);
      struct timeval timeout = {.tv_sec = 1};
      int ret = select(fd_ + 1, &fd_set, NULL, NULL, parent_pid_ > 0 ? &timeout : nullptr);
      if (ret < 0) {
        perror("select");
        std::exit(1);
      }
      if (ret == 0) {
        if (parent_pid_ > 0 && kill(parent_pid_, 0) != 0) {
          std::cerr << "Parent [pid=" << parent_pid_ << "] is dead. Exiting." << std::endl;
          std::quick_exit(0);
        }
        continue;
      }
      int n = read(fd_, buf, sizeof(buf));
      if (n < 0) {
        perror("read");
        std::exit(1);
      }
      if (n == 0) {
        std::cerr << "EOF. Exiting." << std::endl;
        std::quick_exit(0);
      }
      read_.insert(read_.end(), buf, buf + n);
      int eol = std::find(buf, buf + n, '\n') - buf;
      if (eol != n) {
        std::string res(read_.begin(), read_.end() - (n - eol));
        read_.erase(read_.begin(), read_.begin() + res.size() + 1);
        return res;
      }
    }
  }

 private:
  int fd_;
  int parent_pid_;
  std::deque<char> read_;
};

bool StartsWith(std::string_view s, std::string_view prefix) {
  return s.size() >= prefix.size() && s.substr(0, prefix.size()) == prefix;
}

using RepoCache = std::map<std::string, git_repository*>;

git_repository* Find(const RepoCache& cache, const std::string& dir) {
  auto it = cache.lower_bound(dir);
  if (it != cache.end() && it->first == dir) return it->second;
  if (it == cache.begin()) return nullptr;
  --it;
  return StartsWith(dir, it->first) ? it->second : nullptr;
}

bool Store(RepoCache& cache, git_repository* repo) {
  const char* work_dir = git_repository_workdir(repo);
  if (!work_dir || *work_dir != '/') return false;
  std::string key = work_dir;
  if (key.back() != '/') return false;
  auto x = cache.insert(std::make_pair(std::move(key), repo));
  if (!x.second) {
    git_repository_free(x.first->second);
    x.first->second = repo;
  }
  return true;
}

}  // namespace

int main(int argc, char** argv) {
  Options opts = ParseOpts(argc, argv);
  LineReader reader(fileno(stdin), opts.parent_pid);
  std::map<std::string, git_repository*> cache;
  git_libgit2_init();

  while (true) {
    Output out;
    std::string dir = reader.ReadLine();
    if (dir.empty() || dir.front() != '/') continue;
    if (dir.back() != '/') dir += '/';

    git_repository* repo = Find(cache, dir);
    if (!repo) {
      repo = OpenRepo(dir.c_str());
      if (!repo) continue;
      if (git_repository_is_bare(repo) || git_repository_is_empty(repo) || !Store(cache, repo)) {
        git_repository_free(repo);
        continue;
      }
    }

    git_reference* head = Head(repo);
    if (!head) continue;
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
    const char* remote_url = RemoteUrl(repo);
    if (!remote_url) continue;
    out.Print(remote_url);

    // Repository state, A.K.A. action.
    out.Print(RepoState(repo));

    git_index* index = nullptr;
    if (git_repository_index(&index, repo)) {
      LOG(ERROR) << "git_repository_index: " << GitError();
      continue;
    }
    ON_SCOPE_EXIT(=) { git_index_free(index); };

    if (git_index_read(index, 0)) {
      LOG(ERROR) << "git_index_read: " << GitError();
      continue;
    }

    // 1 if there are staged changes, 0 otherwise.
    {
      const git_oid* oid = git_reference_target(head);
      if (!oid) {
        LOG(ERROR) << "git_reference_target returned null";
        continue;
      }
      git_commit* commit = nullptr;
      if (git_commit_lookup(&commit, repo, oid)) {
        LOG(ERROR) << "git_commit_lookup: " << GitError();
        continue;
      }
      ON_SCOPE_EXIT(=) { git_commit_free(commit); };
      git_tree* tree = nullptr;
      if (git_commit_tree(&tree, commit)) {
        LOG(ERROR) << "git_commit_tree: " << GitError();
        continue;
      }
      git_diff_options opt = GIT_DIFF_OPTIONS_INIT;
      opt.notify_cb = [](auto...) -> int { return GIT_EUSER; };
      git_diff* diff = nullptr;
      switch (git_diff_tree_to_index(&diff, repo, tree, index, &opt)) {
        case 0:
          git_diff_free(diff);
          out.Print(0);
          break;
        case GIT_EUSER:
          out.Print(1);
          break;
        default:
          LOG(ERROR) << "git_diff_tree_to_index: " << GitError();
          continue;
      }
    }

    if (git_index_entrycount(index) <= opts.dirty_max_index_size) {
      struct Dirty {
        // Are there unstaged changes?
        bool unstaged;
        // Are there untracked files?
        bool untracked;
      } dirty = {};
      git_diff_options opt = GIT_DIFF_OPTIONS_INIT;
      opt.payload = &dirty;
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
          // 1 if there are unstaged changes, 0 otherwise.
          out.Print(dirty.unstaged);
          // 1 if there are untracked files, 0 otherwise.
          out.Print(dirty.untracked);
          break;
        default:
          LOG(ERROR) << "git_diff_index_to_workdir: " << GitError();
          continue;
      }
    } else {
      out.Print(-1);
      out.Print(-1);
    }

    // Number of commits we are ahead of upstream.
    if (upstream) {
      ssize_t ahead = CountRange(repo, git_reference_shorthand(upstream) + "..HEAD"s);
      if (ahead < 0) continue;
      out.Print(ahead);
    } else {
      out.Print(0);
    }

    // Number of commits we are behind upstream.
    if (upstream) {
      ssize_t ahead = CountRange(repo, "HEAD.."s + git_reference_shorthand(upstream));
      if (ahead < 0) continue;
      out.Print(ahead);
    } else {
      out.Print(0);
    }

    // Number of stashes.
    ssize_t stashes = NumStashes(repo);
    if (stashes < 0) continue;
    out.Print(stashes);

    out.Dump();
  }
}
