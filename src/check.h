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

#ifndef ROMKATV_GITSTATUS_CHECK_H_
#define ROMKATV_GITSTATUS_CHECK_H_

#include "logging.h"

// The argument must be an expression convertible to bool.
// Does nothing if the expression evalutes to true. Otherwise
// it's equivalent to LOG(FATAL).
#define CHECK(cond...) \
  static_cast<void>(0), (!!(cond)) ? static_cast<void>(0) : LOG(FATAL) << #cond << ": "

#endif  // ROMKATV_GITSTATUS_CHECK_H_
