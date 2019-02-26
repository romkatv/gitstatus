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

#ifndef ROMKATV_HCPROXY_CHECK_H_
#define ROMKATV_HCPROXY_CHECK_H_

#include "logging.h"

// The argument must be an expression convertible to bool.
// Does nothing if the expression evalutes to true. Otherwise
// it's equivalent to LOG(FATAL).
#define CHECK(cond...) \
  static_cast<void>(0), (!!(cond)) ? static_cast<void>(0) : LOG(FATAL) << #cond << ": "

#endif  // ROMKATV_HCPROXY_CHECK_H_
