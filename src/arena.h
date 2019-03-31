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

#include <cassert>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <limits>
#include <new>
#include <type_traits>
#include <vector>

#include "string_view.h"

namespace gitstatus {

// Thread-compatible. Very fast and very flexible w.r.t. allocation size and alignment.
//
// Default constructor doesn't allocate any memory. The first call to Allocate() will allocate
// a small block. Subsequent blocks will be twice as big as the last until they saturate.
// The saturation threshold and the first block size are defined in NextBlockSize(). These constants
// can easily be made configurable per arena.
class Arena {
 public:
  // Doesn't allocate any memory.
  Arena();
  Arena(Arena&&);
  ~Arena();

  Arena& operator=(Arena&& other);

  // Requires: alignment is a power of 2.
  //
  // Result is never null and always aligned. If size is zero, the result may be equal to the last.
  // Alignment above alignof(std::max_align_t) is supported. There is no requirement for alignment
  // to be less than size or to divide it.
  inline void* Allocate(size_t size, size_t alignment) {
    assert(alignment && !(alignment & (alignment - 1)));
    uintptr_t p = Align(top_->tip, alignment);
    uintptr_t e = p + size;
    if (e <= top_->end) {
      top_->tip = e;
      return reinterpret_cast<void*>(p);
    }
    return AllocateSlow(size, alignment);
  }

  template <class T>
  inline T* Allocate(size_t n) {
    static_assert(!std::is_reference<T>(), "");
    return static_cast<T*>(Allocate(n * sizeof(T), alignof(T)));
  }

  template <class T>
  inline T* Allocate() {
    return Allocate<T>(1);
  }

  inline char* MemDup(const char* p, size_t len) {
    char* res = Allocate<char>(len);
    std::memcpy(res, p, len);
    return res;
  }

  // Copies the null-terminated string (including the trailing null character) to the arena and
  // returns a StringView pointing to it.
  inline StringView StrDup(const char* s) {
    size_t len = std::strlen(s);
    return StringView(MemDup(s, len + 1), len);
  }

  inline StringView StrDup(StringView s) { return StringView(MemDup(s.ptr, s.len), s.len); }

  // Copies/moves `val` to the arena and returns a pointer to it.
  template <class T>
  inline std::remove_const_t<std::remove_reference_t<T>>* Dup(T&& val) {
    return DirectInit<std::remove_const_t<std::remove_reference_t<T>>>(std::forward<T>(val));
  }

  // The same as `new T{args...}` but on the arena.
  template <class T, class... Args>
  inline T* DirectInit(Args&&... args) {
    T* res = Allocate<T>();
    ::new (const_cast<void*>(static_cast<const void*>(res))) T(std::forward<Args>(args)...);
    return res;
  }

  // The same as `new T(args...)` but on the arena.
  template <class T, class... Args>
  inline T* BraceInit(Args&&... args) {
    T* res = Allocate<T>();
    ::new (const_cast<void*>(static_cast<const void*>(res))) T{std::forward<Args>(args)...};
    return res;
  }

 private:
  struct Block {
    uintptr_t start;
    uintptr_t tip;
    uintptr_t end;
  };

  inline static size_t Align(size_t n, size_t m) { return (n + m - 1) & ~(m - 1); };

  void AddBlock(size_t size);

  __attribute__((noinline)) void* AllocateSlow(size_t size, size_t alignment);

  std::vector<Block> blocks_;
  Block* top_;
};

// Copies of ArenaAllocator use the same thread-compatible Arena without synchronization.
template <class T>
class ArenaAllocator {
 public:
  using value_type = T;
  using pointer = T*;
  using const_pointer = const T*;
  using reference = T&;
  using const_reference = const T&;
  using size_type = size_t;
  using difference_type = ptrdiff_t;
  using propagate_on_container_move_assignment = std::true_type;
  template <class U>
  struct rebind {
    using other = ArenaAllocator<U>;
  };
  using is_always_equal = std::false_type;

  ArenaAllocator(Arena* arena) : arena_(*arena) {}

  Arena& arena() const { return arena_; }

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

template <class C>
struct LazyWithArena;

template <template <class, class> class C, class T1, class A>
struct LazyWithArena<C<T1, A>> {
  using type = C<T1, ArenaAllocator<typename C<T1, A>::value_type>>;
};

template <class C>
using WithArena = typename LazyWithArena<C>::type;

}  // namespace gitstatus

#endif  // ROMKATV_GITSTATUS_DIR_H_
