////////////////////////////////////////////////////////////////////////////////
//                                                                            //
//  Copyright (C) 2017-2025, goatpig                                          //
//  Distributed under the MIT license                                         //
//  See LICENSE-MIT or https://opensource.org/licenses/MIT                    //
//                                                                            //
////////////////////////////////////////////////////////////////////////////////

#include "ReentrantLock.h"

////////////////////////////////////////////////////////////////////////////////
LockableException::LockableException(const std::string& err) :
   std::runtime_error(err)
{}

////////////////////////////////////////////////////////////////////////////////
Lockable::~Lockable()
{}

////
bool Lockable::ownsLock() const
{
   auto thisthreadid = std::this_thread::get_id();
   return mutexTID_ == thisthreadid;
}

////////////////////////////////////////////////////////////////////////////////
SingleLock::SingleLock(const Lockable* ptr)
{
   lockablePtr_ = const_cast<Lockable*>(ptr);
   if (lockablePtr_ == nullptr) {
      throw LockableException("null lockable ptr");
   }

   if (lockablePtr_->mutexTID_ == std::this_thread::get_id()) {
      throw AlreadyLocked();
   }

   lock_ = std::make_unique<std::unique_lock<std::mutex>>(lockablePtr_->mu_);
   lockablePtr_->mutexTID_ = std::this_thread::get_id();
   lockablePtr_->initAfterLock();
}

////
SingleLock::SingleLock(SingleLock&& lock) :
   lockablePtr_(lock.lockablePtr_)
{
   lock_ = std::move(lock.lock_);
}

////
SingleLock::~SingleLock()
{
   if (lock_ == nullptr) {
      return;
   }

   if (lock_->owns_lock()) {
      if (lockablePtr_ != nullptr) {
         lockablePtr_->mutexTID_ = std::thread::id{};
         lockablePtr_->cleanUpBeforeUnlock();
      }
   }
}

////////////////////////////////////////////////////////////////////////////////
ReentrantLock::ReentrantLock(const Lockable* ptr)
{
   lockablePtr_ = const_cast<Lockable*>(ptr);
   if (lockablePtr_ == nullptr) {
      throw LockableException("null lockable ptr");
   }

   if (lockablePtr_->mutexTID_ != std::this_thread::get_id()) {
      lock_ = std::make_unique<std::unique_lock<std::mutex>>(
         lockablePtr_->mu_, std::defer_lock);

      lock_->lock();
      lockablePtr_->mutexTID_ = std::this_thread::get_id();
      lockablePtr_->initAfterLock();
   }
}

////
ReentrantLock::ReentrantLock(ReentrantLock&& lock) :
   lockablePtr_(lock.lockablePtr_)
{
   lock_ = std::move(lock.lock_);
}

////
ReentrantLock::~ReentrantLock()
{
   if (lock_ == nullptr) {
      return;
   }

   if (lock_->owns_lock()) {
      if (lockablePtr_ != nullptr) {
         lockablePtr_->mutexTID_ = std::thread::id{};
         lockablePtr_->cleanUpBeforeUnlock();
      }
   }
}

////////////////////////////////////////////////////////////////////////////////
void ArmoryMutex::initAfterLock()
{}

////
void ArmoryMutex::cleanUpBeforeUnlock()
{}
