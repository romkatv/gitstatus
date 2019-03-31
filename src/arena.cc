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

#include <algorithm>
#include <type_traits>

#include "check.h"

namespace gitstatus {

namespace {

size_t NextPow2(size_t n) {
  CHECK(n);
  n--;
  n |= n >> 1;
  n |= n >> 2;
  n |= n >> 4;
  n |= n >> 8;
  n |= n >> 16;
  n |= n >> 32;
  n++;
  return n;
}

size_t Clamp(size_t min, size_t val, size_t max) { return std::min(max, std::max(min, val)); }

size_t NextBlockSize(size_t prev_size, size_t req_size, size_t req_alignment) {
  constexpr size_t kMinBlockSize = 64;
  constexpr size_t kMaxBlockSize = 4 << 10;
  constexpr size_t kLargeAllocThreshold = 1 << 10;
  if (req_alignment > alignof(std::max_align_t)) {
    req_size += req_alignment - 1;
  } else {
    req_size = std::max(req_size, req_alignment);
  }
  if (req_size > kLargeAllocThreshold) return req_size;
  return std::max(req_size, Clamp(kMinBlockSize, NextPow2(prev_size + 1), kMaxBlockSize));
}

}  // namespace

Arena::Arena() {
  static uintptr_t x = reinterpret_cast<uintptr_t>(&x);
  static Block empty_block = {x, x, x};
  top_ = &empty_block;
}

Arena::Arena(Arena&& other) : Arena() {
  if (!other.blocks_.empty()) {
    blocks_.swap(other.blocks_);
    other.top_ = top_;
    top_ = &blocks_.back();
  }
}

Arena::~Arena() {
  for (const Block& b : blocks_) delete[] reinterpret_cast<char*>(b.start);
}

Arena& Arena::operator=(Arena&& other) {
  if (this != &other) {
    blocks_.swap(other.blocks_);
    std::swap(top_, other.top_);
    // In case std::vector ever implements small object optimization.
    if (!blocks_.empty()) top_ = &blocks_.back();
    if (!other.blocks_.empty()) other.top_ = &other.blocks_.back();
  }
  return *this;
}

void Arena::AddBlock(size_t size) {
  auto p = reinterpret_cast<uintptr_t>(new char[size]);
  blocks_.push_back(Block{p, p, p + size});
  top_ = &blocks_.back();
}

void* Arena::AllocateSlow(size_t size, size_t alignment) {
  CHECK(alignment && !(alignment & (alignment - 1)));
  AddBlock(NextBlockSize(top_->end - top_->start, size, alignment));
  CHECK(Align(top_->tip, alignment) + size <= top_->end);
  return Allocate(size, alignment);
}

}  // namespace gitstatus
