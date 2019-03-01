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

#include <cstring>
#include <iostream>

#include "check.h"

namespace gitstatus {

ResponseWriter::ResponseWriter() { strm_ << 1; }

ResponseWriter::~ResponseWriter() {
  if (!done_) std::cout << 0 << std::endl;
}

void ResponseWriter::Print(ssize_t val) {
  strm_ << ' ';
  strm_ << val;
}

void ResponseWriter::Print(std::string_view val) {
  static constexpr char kEscape[] = "\n\"\\";
  strm_ << ' ';
  strm_ << '"';
  for (char c : val) {
    if (std::strchr(kEscape, c)) strm_ << '\\';
    strm_ << c;
  }
  strm_ << '"';
}

void ResponseWriter::Dump() {
  CHECK(!done_);
  done_ = true;
  std::cout << strm_.str() << std::endl;
}

}  // namespace gitstatus
