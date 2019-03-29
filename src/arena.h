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

#ifndef ROMKATV_GITSTATUS_ARENA_H_
#define ROMKATV_GITSTATUS_ARENA_H_

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <limits>
#include <new>
#include <type_traits>
#include <vector>

#include "string_view.h"

namespace gitstatus {

inline __attribute__((always_inline)) size_t Align(size_t n, size_t m) {
  return (n + m - 1) & ~(m - 1);
}

class Arena {
 public:
  Arena();
  Arena(Arena&&) = delete;
  ~Arena();

  __attribute__((always_inline)) void* Allocate(size_t size, size_t alignment) {
    uintptr_t p = Align(top->tip, alignment);
    uintptr_t e = p + size;
    if (e <= top->end) {
      top->tip = e;
      return reinterpret_cast<void*>(p);
    }
    return AllocateSlow(size, alignment);
  }

  template <class T>
  __attribute__((always_inline)) T* Allocate(size_t n) {
    static_assert(!std::is_reference<T>(), "");
    return static_cast<T*>(Allocate(n * sizeof(T), alignof(T)));
  }

  template <class T>
  __attribute__((always_inline)) T* Allocate() {
    return Allocate<T>(1);
  }

  __attribute__((always_inline)) char* MemDup(const char* p, size_t len) {
    char* res = Allocate<char>(len + 1);
    std::memcpy(res, p, len);
    res[len] = 0;
    return res;
  }

  __attribute__((always_inline)) StringView StrDup(const char* s) {
    size_t len = std::strlen(s);
    return StringView(MemDup(s, len), len);
  }

  template <class T>
  std::remove_const_t<std::remove_reference_t<T>>* Dup(T&& val) {
    return DirectInit<std::remove_const_t<std::remove_reference_t<T>>>(std::forward<T>(val));
  }

  template <class T, class... Args>
  T* DirectInit(Args&&... args) {
    T* res = Allocate<T>();
    ::new (const_cast<void*>(static_cast<const void*>(res))) T(std::forward<Args>(args)...);
    return res;
  }

  template <class T, class... Args>
  T* BraceInit(Args&&... args) {
    T* res = Allocate<T>();
    ::new (const_cast<void*>(static_cast<const void*>(res))) T{std::forward<Args>(args)...};
    return res;
  }

 private:
  enum { kBlockSize = 4 << 10 };

  struct Block {
    uintptr_t start;
    uintptr_t tip;
    uintptr_t end;
  };

  void AddBlock(size_t size);
  __attribute__((noinline)) void* AllocateSlow(size_t size, size_t alignment);

  Block* top = nullptr;
  std::vector<Block> blocks_;
};

template <class T>
class ArenaAllocator {
 public:
  using value_type = T;
  using pointer = T*;
  using const_pointer = const T*;
  using reference = T&;
  using const_reference = const T&;
  using size_type = size_t;
  using difference_type = std::ptrdiff_t;
  using propagate_on_container_move_assignment = std::true_type;
  template <class U>
  struct rebind {
    using other = ArenaAllocator<U>;
  };
  using is_always_equal = std::false_type;

  explicit ArenaAllocator(Arena* arena) : arena_(*arena) {}

  pointer address(reference x) const { return &x; }
  const_pointer address(const_reference x) const { return &x; }
  pointer allocate(size_type n, const void* hint = nullptr) { return arena_.Allocate<T>(n); }
  void deallocate(T* p, std::size_t n) {}
  size_type max_size() const { return std::numeric_limits<size_type>::max() / sizeof(value_type); }

  template <class U, class... Args>
  void construct(U* p, Args&&... args) {
    ::new (const_cast<void*>(static_cast<const void*>(p))) U(std::forward<Args>(args)...);
  }

  template <class U>
  void destroy(U* p) {
    p->~U();
  }

  bool operator==(const ArenaAllocator& other) const { return &arena_ == &other.arena_; }
  bool operator!=(const ArenaAllocator& other) const { return &arena_ != &other.arena_; }

 private:
  Arena& arena_;
};

template <class T>
using ArenaVector = std::vector<T, ArenaAllocator<T>>;

}  // namespace gitstatus

#endif  // ROMKATV_GITSTATUS_DIR_H_
