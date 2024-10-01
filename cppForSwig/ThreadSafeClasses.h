////////////////////////////////////////////////////////////////////////////////
//                                                                            //
//  Copyright (C) 2016, goatpig.                                              //
//  Distributed under the MIT license                                         //
//  See LICENSE-MIT or https://opensource.org/licenses/MIT                    //                                      
//                                                                            //
////////////////////////////////////////////////////////////////////////////////

#ifndef _H_ATOMICVECTOR_ 
#define _H_ATOMICVECTOR_

#include <atomic>
#include <memory>
#include <future>
#include <vector>
#include <map>
#include <set>
#include <chrono>
#include <thread>
#include <exception>
#include <iostream>
#include <condition_variable>
#include <deque>

namespace Armory
{
   namespace Threading
   {
      class IsEmpty
      {};

      class StopBlockingLoop
      {};

      struct StackTimedOutException
      {};

      //////////////////////////////////////////////////////////////////////////
      template <typename T> class Entry
      {
      private:
         T obj_;

      public:
         Entry<T>* next_ = nullptr;

      public:
         Entry<T>(const T& obj) :
            obj_(obj)
         {}

         Entry<T>(T&& obj) :
            obj_(obj)
         {}

         T get(void)
         {
            return move(obj_);
         }
      };

      //////////////////////////////////////////////////////////////////////////
      template <typename T> class AtomicEntry
      {
      private:
         T obj_;

      public:
         std::atomic<AtomicEntry<T>*> next_;

      public:
         AtomicEntry(const T& obj) :
            obj_(obj)
         {
            next_.store(nullptr, std::memory_order_relaxed);
         }

         AtomicEntry(T&& obj)  :
            obj_(move(obj))
         {
            next_.store(nullptr, std::memory_order_relaxed);
         }

         T get(void)
         {
         return std::move(obj_);
         }
      };

      //////////////////////////////////////////////////////////////////////////
      template <typename T> class AtomicEntry2
      {
      private:
         T obj_;
         std::atomic<int> count_;
         std::atomic<int> pos_;

      public:
         std::atomic<AtomicEntry2<T>*> next_;

      public:
         AtomicEntry2(const T& obj) :
            obj_(obj)
         {
            count_.store(0, std::memory_order_relaxed);
            pos_.store(0, std::memory_order_relaxed);
            next_.store(nullptr, std::memory_order_relaxed);
         }

         AtomicEntry2(T&& obj) :
            obj_(move(obj))
         {
            count_.store(0, std::memory_order_relaxed);
            pos_.store(0, std::memory_order_relaxed);
            next_.store(nullptr, std::memory_order_relaxed);
         }

         T get(void)
         {
            return move(obj_);
         }
      };


      //////////////////////////////////////////////////////////////////////////
      template<typename T> class Pile
      {
         /***
         lockless LIFO container class
         ***/
      private:
         std::atomic<AtomicEntry<T>*> top_;
         AtomicEntry<T>* maxptr_;

         std::atomic<size_t> count_;

      public:
         Pile()
         {
            maxptr_ = (AtomicEntry<T>*)SIZE_MAX;
            top_.store(nullptr, std::memory_order_relaxed);
            count_.store(0, std::memory_order_relaxed);
         }

         ~Pile()
         {
            clear();
         }

         void push_back(const T& obj)
         {
            AtomicEntry<T>* nextentry = new AtomicEntry<T>(obj);
            nextentry->next_.store(maxptr_, std::memory_order_release);

            auto topentry = top_.load(std::memory_order_acquire);
            do
            {
               while (topentry == maxptr_)
               topentry = top_.load(std::memory_order_acquire);
            }
            while (!top_.compare_exchange_weak(topentry, nextentry,
               std::memory_order_release, std::memory_order_relaxed));

            nextentry->next_.store(topentry, std::memory_order_release);

            count_.fetch_add(1, std::memory_order_relaxed);
         }

         T pop_back(void)
         {
            AtomicEntry<T>* topentry = top_.load(std::memory_order_acquire);

            do
            {
               //1: make sure the value we got out of top_ is not the marker 
               //invalid value, otherwise keep load top_
               while (topentry == maxptr_)
               topentry = top_.load(std::memory_order_acquire);

               //2: with a valid topentry, try to compare_exchange top_ for
               //the invalid value
            }
            while (!top_.compare_exchange_weak(topentry, maxptr_,
               std::memory_order_release, std::memory_order_relaxed));

            //3: if topentry is empty, the container is emtpy, throw
            if (topentry == nullptr)
            {
               //make sure the replace the marker value with nullptr in top_
               top_.store(nullptr, std::memory_order_release);
               throw IsEmpty();
            }

            /*4: if we got this far we guarantee 2 things:
            - topentry is neither null nor the invalid marker
            - topentry has yet to be derefenced in any thread, in other
            words it is safe to read and delete in this particular thread
            - top_ is set to the invalid marker so we have to set it
            before other threads can get this far
            */

            while (topentry->next_.load(std::memory_order_acquire) == maxptr_);
            top_.store(topentry->next_, std::memory_order_release);

            auto&& retval = topentry->get();

            count_.fetch_sub(1, std::memory_order_relaxed);

            delete topentry;
            return std::move(retval);
         }

         void clear(void)
         {
            try
            {
               while (1)
               pop_back();
            }
            catch (IsEmpty&)
            {}

            count_.store(0, std::memory_order_relaxed);
         }

         size_t count(void) const
         {
            return count_.load(std::memory_order_relaxed);
         }
      };

      //////////////////////////////////////////////////////////////////////////
      template <typename T> class Queue_LockFree
      {
      private:
         std::atomic<AtomicEntry2<T>*> head_;
         std::atomic<AtomicEntry2<T>*> tail_;

      protected:
         std::atomic<int> count_;
         std::exception_ptr exceptPtr_ = nullptr;

      public:
         Queue_LockFree()
         {
            head_.store(nullptr, std::memory_order_relaxed);
            tail_.store(nullptr, std::memory_order_relaxed);
            count_.store(0, std::memory_order_relaxed);
         }

         virtual T pop_front()
         {
            auto tailPtr = tail_.load();

            while(true)
            {
               //compare exchange till tail_ is replaced with its next

               if (tailPtr == nullptr)
                  throw IsEmpty();

               auto nextPtr = tailPtr->next_.load();
               if (!tail_.compare_exchange_weak(tailPtr, nextPtr))
                  continue;

               if (nextPtr == nullptr)
               {
                  auto tailPtrCopy = tailPtr;
                  if (!head_.compare_exchange_strong(tailPtrCopy, nullptr))
                  {
                     do
                     {
                        nextPtr = tailPtr->next_.load();
                     } while (nextPtr == nullptr);

                     tail_.store(nextPtr);
                  }
               }

               auto val = tailPtr->get();
               //delete tailPtr;

               count_.fetch_sub(1);
               return std::move(val);
            }
         }

         virtual void push_back(T&& obj)
         {
            //create new atomic entry
            AtomicEntry2<T>* newEntry = new AtomicEntry2<T>(std::move(obj));

            auto current_head = head_.load();
            while(true)
            {
               if (head_.compare_exchange_weak(current_head, newEntry))
               {
                  if(current_head != nullptr)
                  {
                     current_head->next_.store(newEntry);
                  }
                  else
                  {
                     tail_.store(newEntry);
                  }
               }
               else
               {
                  continue;
               }

               count_.fetch_add(1);
               return;
            }
         }

         size_t count(void) const
         {
            return count_.load(std::memory_order_acquire);
         }

         virtual void clear()
         {
            count_.store(0, std::memory_order_relaxed);
         }
      };

      //////////////////////////////////////////////////////////////////////////
      template <typename T> class Queue
      {
      private:
         std::mutex mu_;
         std::deque<T> queue_;

      protected:
         std::atomic<size_t> count_;
         std::exception_ptr exceptPtr_ = nullptr;

      public:
         Queue()
         {
            count_.store(0, std::memory_order_relaxed);
         }

         virtual T pop_front()
         {
            std::unique_lock<std::mutex> lock(mu_);
            if (queue_.size() == 0)
               throw IsEmpty();

            T val = std::move(queue_.front());
            queue_.pop_front();
            count_.fetch_sub(1, std::memory_order_relaxed);
            return val;
         }

         virtual void push_back(T&& obj)
         {
            std::unique_lock<std::mutex> lock(mu_);
            queue_.push_back(std::move(obj));
            count_.fetch_add(1, std::memory_order_relaxed);
         }

         size_t count(void) const
         {
            return count_.load(std::memory_order_acquire);
         }

         virtual void clear()
         {
            std::unique_lock<std::mutex> lock(mu_);
            queue_.clear();
            count_.store(0, std::memory_order_relaxed);
         }
      };

      //////////////////////////////////////////////////////////////////////////
      template <typename T> class BlockingQueue : public Queue<T>
      {
         /***
         get() blocks as long as the container is empty

         terminate() halts all operations and returns on all waiting threads
         completed() lets the container serve it's remaining entries before
         halting
         ***/

      private:
         std::atomic<int> waiting_;
         std::atomic<bool> terminated_;
         std::atomic<bool> completed_;
         std::mutex condVarMutex_;
         std::condition_variable condVar_;

         int flag_ = 0;

      private:
         void wait_on_data(void)
         {
            std::unique_lock<std::mutex> lock(condVarMutex_);

            auto completed = completed_.load(std::memory_order_relaxed);
            if (completed)
            {
               if (Queue<T>::exceptPtr_ != nullptr)
                  std::rethrow_exception(Queue<T>::exceptPtr_);
               else
                  throw StopBlockingLoop();
            }

            if (flag_ > 0)
               return;

            condVar_.wait(lock);
         }

      public:
         BlockingQueue() : Queue<T>()
         {
            terminated_.store(false, std::memory_order_relaxed);
            completed_.store(false, std::memory_order_relaxed);
            waiting_.store(0, std::memory_order_relaxed);
         }

         T pop_front(bool block = true)
         {
            //blocks as long as there is no data available in the chain.
            //run in loop until we get data or a throw

            waiting_.fetch_add(1, std::memory_order_relaxed);

            try
            {
               while (1)
               {
                  auto terminate = terminated_.load(std::memory_order_acquire);
                  if (terminate)
                  {
                     if (Queue<T>::exceptPtr_ != nullptr)
                        std::rethrow_exception(Queue<T>::exceptPtr_);

                     throw StopBlockingLoop();
                  }

                  //try to pop_front
                  try
                  {
                     auto&& retval = Queue<T>::pop_front();
                     waiting_.fetch_sub(1, std::memory_order_relaxed);

                     std::unique_lock<std::mutex> lock(condVarMutex_);
                     --flag_;

                     return std::move(retval);
                  }
                  catch (IsEmpty& e)
                  {
                     if (block == false)
                           throw e;
                  }

                  wait_on_data();
               }
            }
            catch (...)
            {
               //loop stopped
               waiting_.fetch_sub(1, std::memory_order_relaxed);
               std::rethrow_exception(std::current_exception());
            }

            //to shut up the compiler warning
            return T();
         }

         void push_back(T&& obj)
         {
            auto completed = completed_.load(std::memory_order_acquire);
            if (completed)
               return;

            Queue<T>::push_back(std::move(obj));

            {
               std::unique_lock<std::mutex> lock(condVarMutex_);
               ++flag_;
               condVar_.notify_all();
            }
         }

         void terminate(std::exception_ptr exceptptr = nullptr)
         {
            std::unique_lock<std::mutex> lock(condVarMutex_);
            if (exceptptr == nullptr)
               exceptptr = std::make_exception_ptr(StopBlockingLoop());

            Queue<T>::exceptPtr_ = exceptptr;
            terminated_.store(true, std::memory_order_release);
            completed_.store(true, std::memory_order_release);

            condVar_.notify_all();
         }

         void clear(void)
         {
            completed();

            Queue<T>::clear();

            terminated_.store(false, std::memory_order_relaxed);
            completed_.store(false, std::memory_order_relaxed);
         }

         void completed(std::exception_ptr exceptptr = nullptr)
         {      
            if (exceptptr == nullptr)
               exceptptr = std::make_exception_ptr(StopBlockingLoop());

            Queue<T>::exceptPtr_ = exceptptr;
            completed_.store(true, std::memory_order_release);

            while (waiting_.load(std::memory_order_relaxed) > 0)
               condVar_.notify_all();

            std::unique_lock<std::mutex> lock(condVarMutex_);
            flag_ = 0;
         }

         int waiting(void) const
         {
            return waiting_.load(std::memory_order_relaxed);
         }
      };

      //////////////////////////////////////////////////////////////////////////
      template <typename T> class TimedQueue : public Queue<T>
      {
         /***
         get() blocks as long as the container is empty
         ***/

      private:
         std::atomic<int> waiting_;
         std::atomic<bool> terminate_;
         std::mutex condVarMutex_;
         std::condition_variable condVar_;

         int flag_ = 0;

      private:
         std::cv_status wait_on_data(std::chrono::milliseconds timeout)
         {
            auto terminate = terminate_.load(std::memory_order_relaxed);
            if (terminate)
            {
               if (Queue<T>::exceptPtr_ != nullptr)
                  std::rethrow_exception(Queue<T>::exceptPtr_);
               else
                  throw StopBlockingLoop();
            }

            std::unique_lock<std::mutex> lock(condVarMutex_);
            if (flag_ > 0)
            {
               --flag_;
               return std::cv_status::no_timeout;
            }

            return condVar_.wait_for(lock, timeout);
         }

      public:
         TimedQueue() : Queue<T>()
         {
            terminate_.store(false, std::memory_order_relaxed);
            waiting_.store(0, std::memory_order_relaxed);
         }

         T pop_front(std::chrono::milliseconds timeout = std::chrono::milliseconds(600000))
         {
            //block until timeout expires or data is available
            //return data or throw IsEmpty or StackTimedOutException

            waiting_.fetch_add(1, std::memory_order_relaxed);
            try
            {
               while (1)
               {
               auto terminate = terminate_.load(std::memory_order_relaxed);
               if (terminate)
                  throw StopBlockingLoop();

               //try to pop_front
               try
               {
                  auto&& retval = Queue<T>::pop_front();
                  waiting_.fetch_sub(1, std::memory_order_relaxed);
                  return std::move(retval);
               }
               catch (IsEmpty&)
               {}

               auto before = std::chrono::high_resolution_clock::now();
               auto status = wait_on_data(timeout);

               if (status == std::cv_status::timeout) //future timed out
                  throw StackTimedOutException();

               auto after = std::chrono::high_resolution_clock::now();
               auto timediff = std::chrono::duration_cast<std::chrono::milliseconds>(after - before);
               if (timediff <= timeout)
                  timeout -= timediff;
               else
                  timeout = std::chrono::milliseconds(0);
               }
            }
            catch (...)
            {
               //loop stopped unexpectedly
               waiting_.fetch_sub(1, std::memory_order_relaxed);
               std::rethrow_exception(std::current_exception());
            }

            return T();
         }

         std::vector<T> pop_all(std::chrono::seconds timeout = std::chrono::seconds(600))
         {
            std::vector<T> vecT;

            vecT.push_back(std::move(pop_front(timeout)));

            try
            {
               while (1)
                  vecT.push_back(std::move(Queue<T>::pop_front()));
            }
            catch (IsEmpty&)
            {}

            return vecT;
         }

         void push_back(T&& obj)
         {
            Queue<T>::push_back(std::move(obj));

            {
               std::unique_lock<std::mutex> lock(condVarMutex_);
               ++flag_;
            }

            condVar_.notify_all();
         }


         void terminate(std::exception_ptr exceptptr = nullptr)
         {
            if (exceptptr == nullptr)
            {
               try
               {
                  throw StopBlockingLoop();
               }
               catch (...)
               {
                  exceptptr = std::current_exception();
               }
            }

            Queue<T>::exceptPtr_ = exceptptr;
            terminate_.store(true, std::memory_order_release);

            condVar_.notify_all();
         }

         void reset(void)
         {
            Queue<T>::clear();

            terminate_.store(false, std::memory_order_relaxed);
         }

         bool isValid(void) const
         {
            auto val = terminate_.load(std::memory_order_relaxed);
            return !val;
         }

         int waiting(void) const
         {
            return waiting_.load(std::memory_order_relaxed);
         }
      };

      //////////////////////////////////////////////////////////////////////////
      template<typename T, typename U> class TransactionalMap
      {
         /*
         - locked writes, using a mutex for sequential updating
         - lockless reads as long as atomic_...<shared_ptr> operations are
           lockess on the target platform

         memory order is not set explicity, it defaults to seq_cst
         */

      private:
         mutable std::mutex mu_;
         std::shared_ptr<std::map<T, U>> map_;
         std::atomic<size_t> count_;

      public:

         TransactionalMap(void)
         {
            count_.store(0, std::memory_order_relaxed);
            map_ = std::make_shared<std::map<T, U>>();
         }

         void insert(std::pair<T, U>&& mv)
         {
            auto newMap = std::make_shared<std::map<T, U>>();

            std::unique_lock<std::mutex> lock(mu_);
            newMap->insert(map_->begin(), map_->end());
            newMap->emplace(std::move(mv));

            atomic_store(&map_, newMap);
            count_.store(map_->size(), std::memory_order_relaxed);
         }

         void insert(const std::pair<T, U>& obj)
         {
            auto newMap = std::make_shared<std::map<T, U>>();

            std::unique_lock<std::mutex> lock(mu_);
            newMap->insert(map_->begin(), map_->end());
            newMap->emplace(obj);

            std::atomic_store(&map_, newMap);
            count_.store(map_->size(), std::memory_order_relaxed);
         }

         void update(std::map<T, U> updatemap)
         {
            if (updatemap.empty()) {
               return;
            }
            auto newMap = std::make_shared<std::map<T, U>>(std::move(updatemap));

            std::unique_lock<std::mutex> lock(mu_);
            for (auto& data_pair : *map_) {
               newMap->emplace(data_pair);
            }

            std::atomic_store(&map_, newMap);
            count_.store(map_->size(), std::memory_order_relaxed);
         }

         void erase(const T& id)
         {
            std::unique_lock<std::mutex> lock(mu_);

            auto iter = map_->find(id);
            if (iter == map_->end())
               return;

            auto newMap = std::make_shared<std::map<T, U>>();
            newMap->insert(map_->begin(), map_->end());
            newMap->erase(id);

            std::atomic_store(&map_, newMap);

            count_.store(map_->size(), std::memory_order_relaxed);
         }

         void erase(const std::vector<T>& idVec)
         {
            if (idVec.size() == 0)
               return;

            auto newMap = std::make_shared<std::map<T, U>>();

            std::unique_lock<std::mutex> lock(mu_);
            newMap->insert(map_->begin(), map_->end());

            bool erased = false;
            for (auto& id : idVec)
            {
               if (newMap->erase(id) != 0)
                  erased = true;
            }

            if (erased)
               std::atomic_store(&map_, newMap);

            count_.store(map_->size(), std::memory_order_relaxed);
         }

         void erase(const std::deque<T>& idVec)
         {
            if (idVec.size() == 0)
               return;

            auto newMap = std::make_shared<std::map<T, U>>();

            std::unique_lock<std::mutex> lock(mu_);
            newMap->insert(map_->begin(), map_->end());

            bool erased = false;
            for (auto& id : idVec)
            {
               if (newMap->erase(id) != 0)
                  erased = true;
            }

            if (erased)
               std::atomic_store(&map_, newMap);

            count_.store(map_->size(), std::memory_order_relaxed);
         }

         std::shared_ptr<std::map<T, U>> pop_all(void)
         {
            auto newMap = std::make_shared<std::map<T, U>>();
            std::unique_lock<std::mutex> lock(mu_);

            auto retMap = atomic_load(&map_);
            std::atomic_store(&map_, newMap);

            count_.store(map_->size(), std::memory_order_relaxed);
            return retMap;
         }

         std::shared_ptr<const std::map<T, U>> get(void) const
         {
            auto retMap = std::atomic_load(&map_);
            auto retConstMap = 
               std::static_pointer_cast<const std::map<T, U>>(retMap);
            return retConstMap;
         }

         void clear(void)
         {
            auto newMap = std::make_shared<std::map<T, U>>();
            std::unique_lock<std::mutex> lock(mu_);

            std::atomic_store(&map_, newMap);

            count_.store(0, std::memory_order_relaxed);
         }

         size_t size(void) const
         {
            return count_.load(std::memory_order_relaxed);
         }
      };

      //////////////////////////////////////////////////////////////////////////
      template<typename T> class TransactionalSet
      {
         /*
         - locked writes, using a mutex for sequential updating
         - lockless reads as long as atomic_...<shared_ptr> operations are
           lockess on the target platform

         memory order is not set explicity, it defaults to seq_cst
         */

      private:
         mutable std::mutex mu_;
         std::shared_ptr<std::set<T>> set_;
         std::atomic<size_t> count_;

      public:

         TransactionalSet(void)
         {
            count_.store(0, std::memory_order_relaxed);
            set_ = std::make_shared<std::set<T>>();
         }

         void insert(T&& mv)
         {
            auto newSet = std::make_shared<std::set<T>>();

            std::unique_lock<std::mutex> lock(mu_);
            newSet->insert(set_->begin(), set_->end());
            newSet->insert(move(mv));

            std::atomic_store(&set_, newSet);
            count_.store(set_->size(), std::memory_order_relaxed);
         }

         void insert(const T& obj)
         {
            auto newSet = std::make_shared<std::set<T>>();

            std::unique_lock<std::mutex> lock(mu_);
            newSet->insert(set_->begin(), set_->end());
            newSet->insert(obj);

            std::atomic_store(&set_, newSet);
            count_.store(set_->size(), std::memory_order_relaxed);
         }

         void insert(const std::set<T>& dataSet)
         {
            if (dataSet.size() == 0)
               return;

            auto newSet = std::make_shared<std::set<T>>();

            std::unique_lock<std::mutex> lock(mu_);
            newSet->insert(set_->begin(), set_->end());
            newSet->insert(dataSet.begin(), dataSet.end());

            atomic_store(&set_, newSet);
            count_.store(set_->size(), std::memory_order_relaxed);
         }

         void erase(const T& id)
         {
            std::unique_lock<std::mutex> lock(mu_);

            auto iter = set_->find(id);
            if (iter == set_->end())
               return;

            auto newSet = std::make_shared<std::set<T>>();
            newSet->insert(set_->begin(), set_->end());
            newSet->erase(id);

            std::atomic_store(&set_, newSet);
            count_.store(set_->size(), std::memory_order_relaxed);
         }

         void erase(const std::vector<T>& idVec)
         {
            if (idVec.size() == 0)
               return;

            auto newSet = std::make_shared<std::set<T>>();

            std::unique_lock<std::mutex> lock(mu_);
            newSet->insert(set_->begin(), set_->end());

            bool erased = false;
            for (auto& id : idVec)
            {
               if (newSet->erase(id) != 0)
               erased = true;
            }

            if (erased)
               std::atomic_store(&set_, newSet);

            count_.store(set_->size(), std::memory_order_relaxed);
         }

         void erase(const std::deque<T>& idVec)
         {
            if (idVec.size() == 0)
               return;

            auto newSet = std::make_shared<std::set<T>>();

            std::unique_lock<std::mutex> lock(mu_);
            newSet->insert(set_->begin(), set_->end());

            bool erased = false;
            for (auto& id : idVec)
            {
               if (newSet->erase(id) != 0)
                  erased = true;
            }

            if (erased)
               std::atomic_store(&set_, newSet);

            count_.store(set_->size(), std::memory_order_relaxed);
         }

         std::shared_ptr<std::set<T>> pop_all(void)
         {
            auto newSet = std::make_shared<std::set<T>>();
            std::unique_lock<std::mutex> lock(mu_);

            auto retSet = std::atomic_load(&set_);
            std::atomic_store(&set_, newSet);
            count_.store(set_->size(), std::memory_order_relaxed);

            return retSet;
         }

         std::shared_ptr<std::set<T>> get(void) const
         {
            auto retSet = std::atomic_load(&set_);
            return retSet;
         }

         void clear(void)
         {
            auto newSet = std::make_shared<std::set<T>>();
            std::unique_lock<std::mutex> lock(mu_);

            std::atomic_store(&set_, newSet);
            count_.store(0, std::memory_order_relaxed);
         }

         size_t size(void) const
         {
            return count_.load(std::memory_order_relaxed);
         }
      };
   }; //namespace Threading
}; //namespace Armory

#endif
