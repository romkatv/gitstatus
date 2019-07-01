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

#include "logging.h"

#include <errno.h>
#include <pthread.h>

#include <cstdio>
#include <cstring>
#include <ctime>
#include <mutex>
#include <string>

namespace gitstatus {
namespace internal_logging {

namespace {

std::mutex g_log_mutex;

const char* Str(Severity severity) {
  switch (severity) {
    case DEBUG:
      return "DEBUG";
    case INFO:
      return "INFO";
    case WARN:
      return "WARN";
    case ERROR:
      return "ERROR";
    case FATAL:
      return "FATAL";
  }
  return "UNKNOWN";
}

constexpr char kHexLower[] = {'0', '1', '2', '3', '4', '5', '6', '7',
                              '8', '9', 'a', 'b', 'c', 'd', 'e', 'f'};

void FormatThreadId(char (&out)[2 * sizeof(pthread_t) + 1]) {
  pthread_t tid = pthread_self();
  char* p = out + sizeof(out) - 1;
  *p = 0;
  do {
    --p;
    *p = kHexLower[tid & 0xF];
    tid >>= 4;
  } while (p != out);
}

}  // namespace

LogStreamBase::LogStreamBase(const char* file, int line, Severity severity)
    : errno_(errno), file_(file), line_(line), severity_(severity) {
  strm_ = std::make_unique<std::ostringstream>();
}

void LogStreamBase::Flush() {
  char tid[2 * sizeof(pthread_t) + 1];
  FormatThreadId(tid);

  std::unique_lock<std::mutex> lock(g_log_mutex);
  std::time_t time = std::time(nullptr);
  char time_str[64];
  if (std::strftime(time_str, sizeof(time_str), "%F %T", std::localtime(&time)) == 0) {
    std::strcpy(time_str, "undef");
  }
  std::string msg = strm_->str();
  std::fprintf(stderr, "[%s %s %s %s:%d] %s\n", time_str, tid, Str(severity_), file_, line_,
               msg.c_str());
  strm_.reset();
  errno = errno_;
}

std::ostream& operator<<(std::ostream& strm, Errno e) {
  // GNU C Library uses a buffer of 1024 characters for strerror(). Mimic to avoid truncations.
  char buf[1024];
  auto x = strerror_r(e.err, buf, sizeof(buf));
  // There are two versions of strerror_r with different semantics. We can figure out which
  // one we've got by looking at the result type.
  if (std::is_same<decltype(x), int>::value) {
    // XSI-compliant version.
    strm << (x ? "unknown error" : buf);
  } else if (std::is_same<decltype(x), char*>::value) {
    // GNU-specific version.
    strm << x;
  } else {
    // Something else entirely.
    strm << "unknown error";
  }
  return strm;
}

}  // namespace internal_logging
}  // namespace gitstatus
