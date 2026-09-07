////////////////////////////////////////////////////////////////////////////////
//                                                                            //
//  Copyright (C) 2017-2025, goatpig                                          //
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
#include <stdexcept>

class LockableException : public std::runtime_error
{
public:
   LockableException(const std::string&);
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

   bool ownsLock(void) const;
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
   SingleLock(const Lockable*);
   SingleLock(SingleLock&&);
   ~SingleLock(void);
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
   ReentrantLock(const Lockable*);
   ReentrantLock(ReentrantLock&&);
   ~ReentrantLock(void);
};

////////////////////////////////////////////////////////////////////////////////
class ArmoryMutex : public Lockable
{
   void initAfterLock(void) override;
   void cleanUpBeforeUnlock(void) override;
};

#endif
