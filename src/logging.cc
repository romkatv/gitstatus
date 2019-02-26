// Copyright 2018 Roman Perepelitsa
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//   http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#include "logging.h"

#include <errno.h>

#include <cstdio>
#include <cstring>
#include <ctime>
#include <string>

namespace hcproxy {
namespace internal_logging {

namespace {

const char* Str(Severity severity) {
  switch (severity) {
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

}  // namespace

LogStreamBase::LogStreamBase(const char* file, int line, Severity severity)
    : errno_(errno), file_(file), line_(line), severity_(severity) {
  strm_ = std::make_unique<std::ostringstream>();
}

void LogStreamBase::Flush() {
  std::time_t time = std::time(nullptr);
  char time_str[64];
  if (std::strftime(time_str, sizeof(time_str), "%F %T", std::localtime(&time)) == 0) {
    std::strcpy(time_str, "undef");
  }
  std::string msg = strm_->str();
  std::fprintf(stderr, "[%s %s %s:%d] %s\n", time_str, Str(severity_), file_, line_, msg.c_str());
  strm_.reset();
  errno = errno_;
}

std::ostream& operator<<(std::ostream& strm, Errno e) {
  // GNU C Library uses a buffer of 1024 characters for strerror(). Mimic to avoid truncations.
  char buf[1024];
  // This temp variable ensures that we are calling the GNU-specific strerror_r().
  const char* desc = strerror_r(e.err, buf, sizeof(buf));
  return strm << desc;
}

}  // namespace internal_logging
}  // namespace hcproxy
