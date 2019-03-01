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

#include "response_writer.h"

#include <cctype>
#include <cstring>
#include <iostream>

#include "check.h"

namespace gitstatus {

namespace {

constexpr char kFieldSep = 31;  // ascii 31 is unit separator
constexpr char kReqSep = 30;    // ascii 30 is record separator

constexpr char kUnreadable = '?';

void WriteRecord(std::string_view rec) { std::cout << rec << kReqSep << std::flush; }

}  // namespace

ResponseWriter::ResponseWriter() { strm_ << '1'; }

ResponseWriter::~ResponseWriter() {
  if (!done_) WriteRecord("0");
}

void ResponseWriter::Print(ssize_t val) {
  strm_ << kFieldSep;
  strm_ << val;
}

void ResponseWriter::Print(std::string_view val) {
  strm_ << kFieldSep;
  for (char c : val) {
    strm_ << (c > 127 || std::isprint(c) ? c : kUnreadable);
  }
}

void ResponseWriter::Dump() {
  CHECK(!done_);
  done_ = true;
  WriteRecord(strm_.str());
}

}  // namespace gitstatus
