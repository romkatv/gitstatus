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

#include "options.h"

#include <getopt.h>
#include <unistd.h>

#include <algorithm>
#include <climits>
#include <cstdlib>
#include <iostream>

namespace gitstatus {

namespace {

long ParseLong(const char* s) {
  errno = 0;
  char* end = nullptr;
  long res = std::strtol(s, &end, 10);
  if (*end || end == s || errno) {
    std::cerr << "gitstatusd: not an integer: " << s << std::endl;
    std::exit(10);
  }
  return res;
}

long ParseInt(const char* s) {
  long res = ParseLong(s);
  if (res < INT_MIN || res > INT_MAX) {
    std::cerr << "gitstatusd: integer out of bounds: " << s << std::endl;
    std::exit(10);
  }
  return res;
}

void PrintUsage() {
  std::cout << "Usage: gitstatusd [OPTION]...\n"
            << "Print machine-readable status of the git repos for directores in stdin.\n"
            << "\n"
            << "OPTIONS\n"
            << "  -l, --lock-fd=NUM [default=-1]\n"
            << "   If non-negative, check whether the specified file descriptor is locked when\n"
            << "   not receiving any requests for one second; exit if it isn't locked.\n"
            << "\n"
            << "  -p, --parent-pid=NUM [default=-1]\n"
            << "   If non-negative, send signal 0 to the specified PID when not receiving any\n"
            << "   requests for one second; exit if signal sending fails.\n"
            << "\n"
            << "  -t, --num-threads=NUM [default=1]\n"
            << "   Use this many threads to scan git workdir for unstaged and untracked files.\n"
            << "   Empirically, setting this parameter to twice the number of virtual CPU yields\n"
            << "   maximum performance.\n"
            << "\n"
            << "  -v, --log-level=STR [default=INFO]\n"
            << "   Don't write entires to log whose log level is below this. Log levels in\n"
            << "   increasing order: DEBUG, INFO, WARN, ERROR, FATAL.\n"
            << "\n"
            << "  -r, --repo-ttl-seconds=NUM [default=3600]\n"
            << "   Close git repositories that haven't been used for this long. This is meant to\n"
            << "   release resources such as memory and file descriptors. The next request for a\n"
            << "   repo that's been closed is much slower than for a repo that hasn't been.\n"
            << "   Negative value means infinity.\n"
            << "\n"
            << "  -s, --max-num-staged=NUM [default=1]\n"
            << "   Report at most this many staged changes; negative value means infinity.\n"
            << "\n"
            << "  -u, --max-num-unstaged=NUM [default=1]\n"
            << "   Report at most this many unstaged changes; negative value means infinity.\n"
            << "\n"
            << "  -d, --max-num-untracked=NUM [default=1]\n"
            << "   Report at most this many untracked fles; negative value means infinity.\n"
            << "\n"
            << "  -m, --dirty-max-index-size=NUM [default=-1]\n"
            << "   If a repo has more files in its index than this, override --max-num-unstaged\n"
            << "   and --max-num-untracked (but not --max-num-staged) with zeros; negative value\n"
            << "   means infinity.\n"
            << "\n"
            << "  -e, --recurse-untracked-dirs\n"
            << "   Count files within untracked directories like `git status --untracked-files`.\n"
            << "\n"
            << "  -U, --ignore-status-show-untracked-files\n"
            << "   Unless this option is specified, report zero untracked files for repositories\n"
            << "   with status.showUntrackedFiles = false.\n"
            << "\n"
            << "  -W, --ignore-bash-show-untracked-files\n"
            << "   Unless this option is specified, report zero untracked files for repositories\n"
            << "   with bash.showUntrackedFiles = false.\n"
            << "\n"
            << "  -D, --ignore-bash-show-dirty-state\n"
            << "   Unless this option is specified, report zero staged, unstaged and conflicted\n"
            << "   changes for repositories with bash.showDirtyState = false.\n"
            << "\n"
            << "  -h, --help\n"
            << "  Display this help and exit.\n"
            << "\n"
            << "INPUT\n"
            << "\n"
            << "  Requests are read from stdin, separated by ascii 30 (record separator). Each\n"
            << "  request is made of the following fields, in the specified order, separated by\n"
            << "  ascii 31 (unit separator):\n"
            << "\n"
            << "    1. Request ID. Any string. Can be empty.\n"
            << "    2. Path to the directory for which git stats are being requested.\n"
            << "\n"
            << "OUTPUT\n"
            << "\n"
            << "  For every request read from stdin there is response written to stdout.\n"
            << "  Responses are separated by ascii 30 (record separator). Each response is made\n"
            << "  of the following fields, in the specified order, separated by ascii 31\n"
            << "  (unit separator):\n"
            << "\n"
            << "     1. Request id. The same as the first field in the request.\n"
            << "     2. 0 if the directory isn't a git repo, 1 otherwise. If 0, all the\n"
            << "        following fields are missing.\n"
            << "     3. Absolute path to the git repository workdir.\n"
            << "     4. Commit hash that HEAD is pointing to. 40 hex digits.\n"
            << "     5. Local branch name or empty if not on a branch.\n"
            << "     6. Upstream branch name. Can be empty.\n"
            << "     7. The remote name, e.g. \"upstream\" or \"origin\".\n"
            << "     8. Remote URL. Can be empty.\n"
            << "     9. Repository state, A.K.A. action. Can be empty.\n"
            << "    10. The number of files in the index.\n"
            << "    11. The number of staged changes.\n"
            << "    12. The number of unstaged changes.\n"
            << "    13. The number of untracked files.\n"
            << "    14. Number of commits the current branch is ahead of upstream.\n"
            << "    15. Number of commits the current branch is behind upstream.\n"
            << "    16. The number of stashes.\n"
            << "    17. The last tag (in lexicographical order) that points to the same\n"
            << "        commit as HEAD.\n"
            << "    18. The number of unstaged deleted files.\n"
            << "\n"
            << "EXAMPLE\n"
            << "\n"
            << "  Send a single request and print response (zsh syntax):\n"
            << "\n"
            << "    local req_id=id\n"
            << "    local dir=$PWD\n"
            << "    echo -nE $req_id$'\\x1f'$dir$'\\x1e' | ./gitstatusd | {\n"
            << "      local resp\n"
            << "      IFS=$'\\x1f' read -rd $'\\x1e' -A resp && print -lr -- \"${(@qq)resp}\"\n"
            << "    }\n"
            << "\n"
            << "  Output:"
            << "\n"
            << "    'id'\n"
            << "    '1'\n"
            << "    '/home/romka/gitstatus'\n"
            << "    'bf46bf03dbab7108801b53f8a720caee8464c9c3'\n"
            << "    'master'\n"
            << "    'master'\n"
            << "    'origin'\n"
            << "    'git@github.com:romkatv/gitstatus.git'\n"
            << "    ''\n"
            << "    '70'\n"
            << "    '1'\n"
            << "    '0'\n"
            << "    '0'\n"
            << "    '2'\n"
            << "    '0'\n"
            << "    '0'\n"
            << "    ''\n"
            << "    '0'\n"
            << "\n"
            << "EXIT STATUS\n"
            << "\n"
            << "  The command returns zero on success (when printing help or on EOF),\n"
            << "  non-zero on failure. In the latter case the output is unspecified.\n"
            << "\n"
            << "COPYRIGHT\n"
            << "\n"
            << "  Copyright 2019 Roman Perepelitsa\n"
            << "  This is free software; see https://github.com/romkatv/gitstatus for copying\n"
            << "  conditions. There is NO warranty; not even for MERCHANTABILITY or FITNESS FOR\n"
            << "  A PARTICULAR PURPOSE." << std::endl;
}

}  // namespace

Options ParseOptions(int argc, char** argv) {
  const struct option opts[] = {{"help", no_argument, nullptr, 'h'},
                                {"lock-fd", required_argument, nullptr, 'l'},
                                {"parent-pid", required_argument, nullptr, 'p'},
                                {"num-threads", required_argument, nullptr, 't'},
                                {"log-level", required_argument, nullptr, 'v'},
                                {"repo-ttl-seconds", required_argument, nullptr, 'r'},
                                {"max-num-staged", required_argument, nullptr, 's'},
                                {"max-num-unstaged", required_argument, nullptr, 'u'},
                                {"max-num-conflicted", required_argument, nullptr, 'c'},
                                {"max-num-untracked", required_argument, nullptr, 'd'},
                                {"dirty-max-index-size", required_argument, nullptr, 'm'},
                                {"recurse-untracked-dirs", no_argument, nullptr, 'e'},
                                {"ignore-status-show-untracked-files", no_argument, nullptr, 'U'},
                                {"ignore-bash-show-untracked-files", no_argument, nullptr, 'W'},
                                {"ignore-bash-show-dirty-state", no_argument, nullptr, 'D'},
                                {}};
  Options res;
  while (true) {
    switch (getopt_long(argc, argv, "hl:p:t:v:r:s:u:c:d:m:eUWD", opts, nullptr)) {
      case -1:
        if (optind != argc) {
          std::cerr << "unexpected positional argument: " << argv[optind] << std::endl;
          std::exit(10);
        }
        return res;
      case 'h':
        PrintUsage();
        std::exit(0);
      case 'l':
        res.lock_fd = ParseInt(optarg);
        break;
      case 'p':
        res.parent_pid = ParseInt(optarg);
        break;
      case 'v':
        if (!ParseLogLevel(optarg, res.log_level)) {
          std::cerr << "invalid log level: " << optarg << std::endl;
          std::exit(10);
        }
        break;
      case 'r':
        res.repo_ttl = std::chrono::seconds(ParseLong(optarg));
        break;
      case 't': {
        long n = ParseLong(optarg);
        if (n <= 0) {
          std::cerr << "invalid number of threads: " << n << std::endl;
          std::exit(10);
        }
        res.num_threads = n;
        break;
      }
      case 's':
        res.max_num_staged = ParseLong(optarg);
        break;
      case 'u':
        res.max_num_unstaged = ParseLong(optarg);
        break;
      case 'c':
        res.max_num_conflicted = ParseLong(optarg);
        break;
      case 'd':
        res.max_num_untracked = ParseLong(optarg);
        break;
      case 'm':
        res.dirty_max_index_size = ParseLong(optarg);
        break;
      case 'e':
        res.recurse_untracked_dirs = true;
        break;
      case 'U':
        res.ignore_status_show_untracked_files = true;
        break;
      case 'W':
        res.ignore_bash_show_untracked_files = true;
        break;
      case 'D':
        res.ignore_bash_show_dirty_state = true;
        break;
      default:
        std::exit(10);
    }
  }
}

}  // namespace gitstatus
