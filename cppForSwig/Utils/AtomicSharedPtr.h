////////////////////////////////////////////////////////////////////////////////
//                                                                            //
//  Copyright (C) 2026, goatpig                                               //
//  Copyright (C) 2026, devdavidejesus                                        //
//  Distributed under the MIT license                                         //
//  See LICENSE-MIT or https://opensource.org/licenses/MIT                    //
//                                                                            //
////////////////////////////////////////////////////////////////////////////////

#pragma once

#include <atomic>
#include <memory>
#include <version>

/*
std::atomic<std::shared_ptr<T>> is C++20, but libc++ (the macOS standard
library) does not implement it yet. Where the library provides it (libstdc++),
this wraps the C++20 type unchanged. Elsewhere it falls back to the
atomic_load/atomic_store free functions on a plain shared_ptr.
*/
template <typename T>
class AtomicSharedPtr
{
public:
   AtomicSharedPtr() = default;
   AtomicSharedPtr(const AtomicSharedPtr&) = delete;
   AtomicSharedPtr& operator=(const AtomicSharedPtr&) = delete;

   std::shared_ptr<T> load(
      std::memory_order order = std::memory_order_seq_cst) const
   {
#if defined(__cpp_lib_atomic_shared_ptr)
      return ptr_.load(order);
#else
      return std::atomic_load_explicit(&ptr_, order);
#endif
   }

   void store(std::shared_ptr<T> desired,
      std::memory_order order = std::memory_order_seq_cst)
   {
#if defined(__cpp_lib_atomic_shared_ptr)
      ptr_.store(std::move(desired), order);
#else
      std::atomic_store_explicit(&ptr_, std::move(desired), order);
#endif
   }

private:
#if defined(__cpp_lib_atomic_shared_ptr)
   std::atomic<std::shared_ptr<T>> ptr_;
#else
   std::shared_ptr<T> ptr_;
#endif
};
