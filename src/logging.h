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

#ifndef ROMKATV_GITSTATUS_LOGGING_H_
#define ROMKATV_GITSTATUS_LOGGING_H_

#include <cstdlib>
#include <memory>
#include <optional>
#include <ostream>
#include <sstream>

#ifndef HCP_MIN_LOG_LVL
#define HCP_MIN_LOG_LVL INFO
#endif

#define LOG(severity) LOG_I(severity)

#define LOG_I(severity)                                                                        \
  (::gitstatus::internal_logging::severity < ::gitstatus::internal_logging::HCP_MIN_LOG_LVL)   \
      ? static_cast<void>(0)                                                                   \
      : ::gitstatus::internal_logging::Assignable() =                                          \
            ::gitstatus::internal_logging::LogStream<::gitstatus::internal_logging::severity>( \
                __FILE__, __LINE__, ::gitstatus::internal_logging::severity)                   \
                .ref()

namespace gitstatus {
namespace internal_logging {

enum Severity {
  INFO = 0,
  WARN = 1,
  ERROR = 2,
  FATAL = 3,
};

struct Assignable {
  template <class T>
  void operator=(const T&) const {}
};

class LogStreamBase {
 public:
  LogStreamBase(const char* file, int line, Severity severity);

  LogStreamBase& ref() { return *this; }
  std::ostream& strm() { return *strm_; }

 protected:
  void Flush();

 private:
  const char* file_;
  int line_;
  Severity severity_;
  std::unique_ptr<std::ostringstream> strm_;
};

template <Severity>
class LogStream : public LogStreamBase {
 public:
  using LogStreamBase::LogStreamBase;
  ~LogStream() { this->Flush(); }
};

template <>
class LogStream<FATAL> : public LogStreamBase {
 public:
  using LogStreamBase::LogStreamBase;
  ~LogStream() __attribute__((noreturn)) {
    this->Flush();
    std::exit(1);
  }
};

template <class T>
LogStreamBase& operator<<(LogStreamBase& strm, const T& val) {
  strm.strm() << val;
  return strm;
}

inline LogStreamBase& operator<<(LogStreamBase& strm, std::ostream& (*manip)(std::ostream&)) {
  strm.strm() << manip;
  return strm;
}

}  // namespace internal_logging
}  // namespace gitstatus

#endif  // ROMKATV_GITSTATUS_LOGGING_H_
