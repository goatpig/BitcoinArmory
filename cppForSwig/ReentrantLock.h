////////////////////////////////////////////////////////////////////////////////
//                                                                            //
//  Copyright (C) 2017, goatpig                                               //
//  Distributed under the MIT license                                         //
//  See LICENSE-MIT or https://opensource.org/licenses/MIT                    //
//                                                                            //
////////////////////////////////////////////////////////////////////////////////

#ifndef _H_REENTRANT_LOCK
#define _H_REENTRANT_LOCK

#include <thread>
#include <mutex>
#include <memory>
#include <string>

#include "make_unique.h"

class LockableException : public std::runtime_error
{
public:
   LockableException(const std::string& err) : std::runtime_error(err)
   {}
};

struct AlreadyLocked
{};

////////////////////////////////////////////////////////////////////////////////
class Lockable
{
   friend struct ReentrantLock;
   friend struct SingleLock;

protected:
   std::mutex mu_;
   std::thread::id mutexTID_;

public:
   virtual ~Lockable(void) = 0;

   bool ownsLock(void) const
   {
      auto thisthreadid = std::this_thread::get_id();
      return mutexTID_ == thisthreadid;
   }

   virtual void initAfterLock(void) = 0;
   virtual void cleanUpBeforeUnlock(void) = 0;
};

////////////////////////////////////////////////////////////////////////////////
struct SingleLock
{
private:
   Lockable* lockablePtr_;
   std::unique_ptr<std::unique_lock<std::mutex>> lock_;

private:
   SingleLock(const SingleLock&) = delete;
   SingleLock& operator=(const SingleLock&) = delete;
   SingleLock& operator=(SingleLock&&) = delete;

public:
   SingleLock(const Lockable* ptr)
   {
      lockablePtr_ = const_cast<Lockable*>(ptr);
      if (lockablePtr_ == nullptr)
         throw LockableException("null lockable ptr");

      if (lockablePtr_->mutexTID_ == std::this_thread::get_id())
         throw AlreadyLocked();

      lock_ =
         std::make_unique<std::unique_lock<std::mutex>>(lockablePtr_->mu_, std::defer_lock);

      lock_->lock();
      lockablePtr_->mutexTID_ = std::this_thread::get_id();

      lockablePtr_->initAfterLock();
   }

   SingleLock(SingleLock&& lock) :
      lockablePtr_(lock.lockablePtr_)
   {
      lock_ = std::move(lock.lock_);
   }

   ~SingleLock(void)
   {
      if (lock_ == nullptr)
         return;

      if (lock_->owns_lock())
      {
         if (lockablePtr_ != nullptr)
         {
            lockablePtr_->mutexTID_ = std::thread::id();
            lockablePtr_->cleanUpBeforeUnlock();
         }
      }
   }
};

////////////////////////////////////////////////////////////////////////////////
struct ReentrantLock
{
private:
   Lockable* lockablePtr_ = nullptr;
   std::unique_ptr<std::unique_lock<std::mutex>> lock_;

private:
   ReentrantLock(const ReentrantLock&) = delete;
   ReentrantLock& operator=(const ReentrantLock&) = delete;
   ReentrantLock& operator=(ReentrantLock&&) = delete;

public:
   ReentrantLock(const Lockable* ptr)
   {
      lockablePtr_ = const_cast<Lockable*>(ptr);
      if (lockablePtr_ == nullptr)
         throw LockableException("null lockable ptr");
      
      if (lockablePtr_->mutexTID_ != std::this_thread::get_id())
      {
         lock_ =
            std::make_unique<std::unique_lock<std::mutex>>(lockablePtr_->mu_, std::defer_lock);

         lock_->lock();
         lockablePtr_->mutexTID_ = std::this_thread::get_id();
         lockablePtr_->initAfterLock();
      }
   }
   
   ReentrantLock(ReentrantLock&& lock) :
      lockablePtr_(lock.lockablePtr_)
   {
      lock_ = std::move(lock.lock_);
   }



   ~ReentrantLock(void)
   {
      if (lock_ == nullptr)
         return;

      if (lock_->owns_lock())
      {
         if (lockablePtr_ != nullptr)
         {
            lockablePtr_->mutexTID_ = std::thread::id();
            lockablePtr_->cleanUpBeforeUnlock();
         }
      }
   }
};

#endif
