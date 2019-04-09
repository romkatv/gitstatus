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

#ifndef ROMKATV_GITSTATUS_LOGGING_H_
#define ROMKATV_GITSTATUS_LOGGING_H_

#include <cstdlib>
#include <memory>
#include <ostream>
#include <sstream>

#ifndef GITSTATUS_MIN_LOG_LVL
#define GITSTATUS_MIN_LOG_LVL INFO
#endif

#define LOG(severity) LOG_I(severity)

#define LOG_I(severity)                                                                            \
  (::gitstatus::internal_logging::severity < ::gitstatus::internal_logging::GITSTATUS_MIN_LOG_LVL) \
      ? static_cast<void>(0)                                                                       \
      : ::gitstatus::internal_logging::Assignable() =                                              \
            ::gitstatus::internal_logging::LogStream<::gitstatus::internal_logging::severity>(     \
                __FILE__, __LINE__, ::gitstatus::internal_logging::severity)                       \
                .ref()

namespace gitstatus {

namespace internal_logging {

enum Severity {
  DEBUG,
  INFO,
  WARN,
  ERROR,
  FATAL,
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
  int stashed_errno() const { return errno_; }

 protected:
  void Flush();

 private:
  int errno_;
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
    std::abort();
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

struct Errno {
  int err;
};

std::ostream& operator<<(std::ostream& strm, Errno e);

struct StashedErrno {};

inline LogStreamBase& operator<<(LogStreamBase& strm, StashedErrno) {
  return strm << Errno{strm.stashed_errno()};
}

}  // namespace internal_logging

inline internal_logging::Errno Errno(int err) { return {err}; }
inline internal_logging::StashedErrno Errno() { return {}; }

}  // namespace gitstatus

#endif  // ROMKATV_GITSTATUS_LOGGING_H_
