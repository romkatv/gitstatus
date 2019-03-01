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

#include "timer.h"

#include <sys/resource.h>
#include <sys/time.h>

#include <iostream>

#include "check.h"

namespace gitstatus {

double CpuTimeMs() {
  auto ToMs = [](const timeval& tv) { return 1e3 * tv.tv_sec + 1e-3 * tv.tv_usec; };
  rusage usage = {};
  CHECK(getrusage(RUSAGE_SELF, &usage) == 0) << Errno();
  return ToMs(usage.ru_utime) + ToMs(usage.ru_stime);
}

void Timer::Report(std::string_view msg) {
  std::cerr << "CPU time (ms): " << msg << " : " << CpuTimeMs() - ms_ << std::endl;
  Start();
}

}  // namespace gitstatus
