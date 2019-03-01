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

#include <climits>
#include <cstdlib>
#include <iostream>

namespace gitstatus {

namespace {

long ParseLong(const char* s) {
  errno = 0;
  char* end = nullptr;
  long res = std::strtol(s, &end, 10);
  if (*end || errno) {
    std::cerr << "gitstatusd: not an integer: " << s << std::endl;
    std::quick_exit(1);
  }
  return res;
}

long ParseInt(const char* s) {
  long res = ParseLong(s);
  if (res < INT_MIN || res > INT_MAX) {
    std::cerr << "gitstatusd: integer out of bounds: " << s << std::endl;
    std::quick_exit(1);
  }
  return res;
}

void PrintUsage() {
  std::cout << "Usage: gitstatusd [OPTION]...\n"
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

}  // namespace

Options ParseOptions(int argc, char** argv) {
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
        std::quick_exit(0);
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

}  // namespace gitstatus
