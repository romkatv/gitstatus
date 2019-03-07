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

#include "request.h"

#include <fcntl.h>
#include <signal.h>
#include <sys/select.h>
#include <sys/types.h>
#include <unistd.h>

#include <algorithm>
#include <cstdlib>
#include <iostream>

#include "check.h"
#include "logging.h"
#include "serialization.h"

namespace gitstatus {

namespace {

Request ParseRequest(const std::string& s) {
  auto sep = s.find(kFieldSep);
  VERIFY(sep != std::string::npos) << "Malformed request: " << s;
  VERIFY(s.find(kFieldSep, sep + 1) == std::string::npos) << "Malformed request: " << s;
  Request res;
  res.id.assign(s.begin(), s.begin() + sep);
  res.dir.assign(s.begin() + sep + 1, s.end());
  return res;
}

}  // namespace

std::ostream& operator<<(std::ostream& strm, const Request& req) {
  strm << req.id << " [" << req.dir << "]";
  return strm;
}

Request RequestReader::ReadRequest() {
  auto eol = std::find(read_.begin(), read_.end(), kMsgSep);
  if (eol != read_.end()) {
    std::string msg(read_.begin(), eol);
    read_.erase(read_.begin(), eol + 1);
    return ParseRequest(msg);
  }

  char buf[256];
  while (true) {
    int n;
    fd_set fds;
    FD_ZERO(&fds);
    FD_SET(fd_, &fds);
    struct timeval timeout = {.tv_sec = 1};

    CHECK((n = select(fd_ + 1, &fds, NULL, NULL, parent_pid_ > 0 ? &timeout : nullptr)) >= 0)
        << Errno();
    if (n == 0) {
      if (parent_pid_ > 0 && kill(parent_pid_, 0) != 0) {
        LOG(INFO) << "Parent [pid=" << parent_pid_ << "] is dead. Exiting.";
        std::exit(0);
      }
      continue;
    }

    CHECK((n = read(fd_, buf, sizeof(buf))) >= 0) << Errno();
    if (n == 0) {
      LOG(INFO) << "EOF. Exiting.";
      std::exit(0);
    }
    read_.insert(read_.end(), buf, buf + n);
    int eol = std::find(buf, buf + n, kMsgSep) - buf;
    if (eol != n) {
      std::string msg(read_.begin(), read_.end() - (n - eol));
      read_.erase(read_.begin(), read_.begin() + msg.size() + 1);
      return ParseRequest(msg);
    }
  }
}

}  // namespace gitstatus
