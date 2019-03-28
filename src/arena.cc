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

#include "arena.h"

#include "check.h"

namespace gitstatus {

namespace {}  // namespace

Arena::Arena() { AddBlock(kBlockSize); }

Arena::~Arena() {
  for (const Block& b : blocks_) delete[] reinterpret_cast<char*>(b.start);
}

void Arena::AddBlock(size_t size) {
  auto p = reinterpret_cast<uintptr_t>(new char[size]);
  blocks_.push_back(Block{p, p, p + size});
  top = &blocks_.back();
}

void* Arena::AllocateSlow(size_t size, size_t alignment) {
  CHECK(size >= alignment);
  AddBlock(size);
  return Allocate(size, alignment);
}

}  // namespace gitstatus
