////////////////////////////////////////////////////////////////////////////////
//                                                                            //
//  Copyright (C) 2016-2026, goatpig.                                         //
//  Distributed under the MIT license                                         //
//  See LICENSE-MIT or https://opensource.org/licenses/MIT                    //
//                                                                            //
////////////////////////////////////////////////////////////////////////////////

#pragma once

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

         virtual T pop_front(void)
         {
            std::unique_lock<std::mutex> lock(mu_);
            if (queue_.empty()) {
               throw IsEmpty();
            }

            T val = std::move(queue_.front());
            queue_.pop_front();
            count_.fetch_sub(1, std::memory_order_relaxed);
            return val;
         }

         virtual void push_back(T&& obj)
         {
            std::unique_lock<std::mutex> lock(mu_);
            queue_.emplace_back(std::move(obj));
            count_.fetch_add(1, std::memory_order_relaxed);
         }

         size_t count(void) const
         {
            return count_.load(std::memory_order_acquire);
         }

         virtual void clear(void)
         {
            std::unique_lock<std::mutex> lock(mu_);
            queue_.clear();
            count_.store(0, std::memory_order_relaxed);
         }
      };

      //////////////////////////////////////////////////////////////////////////
      template <typename T> class BlockingQueue
      {
         /***
         get() blocks as long as the container is empty

         terminate() halts all consumers and returns from them all
         completed() rejects all new producers, let's consumers empty the queue
         ***/

      private:
         std::atomic<int> waiting_;
         std::atomic<bool> terminated_;
         std::atomic<bool> completed_;
         std::mutex condVarMutex_;
         std::condition_variable condVar_;
         size_t count_;

         std::deque<T> queue_;

      public:
         BlockingQueue(void)
         {
            waiting_.store(0, std::memory_order_relaxed);
            terminated_.store(false, std::memory_order_relaxed);
            completed_.store(false, std::memory_order_relaxed);
            count_ = 0;
         }

         T pop_front(bool block = true)
         {
            //blocks as long as there is no data available in the chain.
            //run in loop until we get data or a throw

            std::unique_lock<std::mutex> lock(condVarMutex_);
            waiting_.fetch_add(1, std::memory_order_relaxed);
            try {
               while (true) {
                  if (terminated_.load(std::memory_order_acquire)) {
                     throw StopBlockingLoop{};
                  }

                  if (!queue_.empty()) {
                     auto retval = std::move(queue_.front());
                     queue_.pop_front();
                     waiting_.fetch_sub(1, std::memory_order_relaxed);
                     --count_;
                     return std::move(retval);
                  } else {
                     if (block == false) {
                        throw IsEmpty{};
                     } else if (completed_.load(std::memory_order_acquire)) {
                        throw StopBlockingLoop{};
                     }
                  }

                  //block until an entry is available
                  condVar_.wait(lock);
               }
            } catch (...) {
               //loop stopped
               waiting_.fetch_sub(1, std::memory_order_relaxed);
               std::rethrow_exception(std::current_exception());
            }
         }

         void push_back(T&& obj)
         {
            if (completed_.load(std::memory_order_acquire)) {
               return;
            }

            {
               std::unique_lock<std::mutex> lock(condVarMutex_);
               queue_.emplace_back(std::move(obj));
               ++count_;
            }
            condVar_.notify_all();
         }

         void terminate(void)
         {
            terminated_.store(true, std::memory_order_release);
            completed_.store(true, std::memory_order_release);
            condVar_.notify_all();
         }

         void clear(void)
         {
            completed();
            std::unique_lock<std::mutex> lock(condVarMutex_);
            queue_.clear();
            count_ = 0;

            terminated_.store(false, std::memory_order_relaxed);
            completed_.store(false, std::memory_order_relaxed);
         }

         void completed(void)
         {
            completed_.store(true, std::memory_order_release);
            while (waiting_.load(std::memory_order_relaxed) > 0) {
               condVar_.notify_all();
            }
         }

         int waiting(void) const
         {
            return waiting_.load(std::memory_order_relaxed);
         }

         bool isValid(void) const
         {
            return !terminated_.load(std::memory_order_relaxed);
         }

         size_t count(void) const
         {
            return count_;
         }
      };

      //////////////////////////////////////////////////////////////////////////
      template <typename T> class TimedQueue
      {
         /***
         get(duration) blocks until data is ready or duration has expired

         terminate() halts and returns from all consumers
         ***/

      private:
         std::atomic<int> waiting_;
         size_t count_;
         std::atomic<bool> terminate_;
         std::mutex condVarMutex_;
         std::condition_variable condVar_;

         std::deque<T> queue_;

      public:
         TimedQueue(void)
         {
            terminate_.store(false, std::memory_order_relaxed);
            waiting_.store(0, std::memory_order_relaxed);
            count_ = 0;
         }

         T pop_front(std::chrono::milliseconds timeout = std::chrono::milliseconds(600000))
         {
            waiting_.fetch_add(1, std::memory_order_relaxed);
            std::unique_lock<std::mutex> lock(condVarMutex_);
            try {
               while (true) {
                  if (terminate_.load(std::memory_order_relaxed) == true) {
                     throw StopBlockingLoop();
                  }

                  if (!queue_.empty()) {
                     auto val = std::move(queue_.front());
                     queue_.pop_front();
                     --count_;
                     waiting_.fetch_sub(1, std::memory_order_relaxed);
                     return val;
                  }

                  auto before = std::chrono::high_resolution_clock::now();
                  if (condVar_.wait_for(lock, timeout) == std::cv_status::timeout) {
                     throw StackTimedOutException{};
                  }

                  auto after = std::chrono::high_resolution_clock::now();
                  auto timediff = std::chrono::duration_cast<std::chrono::milliseconds>(after - before);
                  if (timediff <= timeout) {
                     timeout -= timediff;
                  } else {
                     timeout = std::chrono::milliseconds(0);
                  }
               }
            } catch (...) {
               //loop stopped unexpectedly
               waiting_.fetch_sub(1, std::memory_order_relaxed);
               std::rethrow_exception(std::current_exception());
            }
         }

         void push_back(T&& obj)
         {
            {
               std::unique_lock<std::mutex> lock(condVarMutex_);
               queue_.emplace_back(std::move(obj));
               ++count_;
            }
            condVar_.notify_all();
         }

         std::vector<T> pop_all(void)
         {
            std::unique_lock<std::mutex> lock(condVarMutex_);
            std::vector<T> result;
            result.reserve(count_);
            for (auto& entry : queue_) {
               result.emplace_back(std::move(entry));
            }
            queue_.clear();
            count_ = 0;
            return result;
         }

         void reset(void)
         {
            std::unique_lock<std::mutex> lock(condVarMutex_);
            queue_.clear();
            count_ = 0;
            terminate_.store(false, std::memory_order_relaxed);
         }

         void terminate(void)
         {
            terminate_.store(true, std::memory_order_release);
            condVar_.notify_all();
         }

         bool isValid(void) const
         {
            return !terminate_.load(std::memory_order_relaxed);
         }

         int waiting(void) const
         {
            return waiting_.load(std::memory_order_relaxed);
         }

         size_t count(void) const
         {
            return count_;
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
         std::atomic<std::shared_ptr<std::map<T, U>>> map_;
         std::atomic<size_t> count_;

      public:

         TransactionalMap(void)
         {
            count_.store(0, std::memory_order_relaxed);
            map_.store(std::make_shared<std::map<T, U>>());
         }

         void insert(std::pair<T, U>&& mv)
         {
            auto thisMap = map_.load();
            auto newMap = std::make_shared<std::map<T, U>>();

            newMap->insert(thisMap->begin(), thisMap->end());
            newMap->emplace(std::move(mv));

            count_.store(newMap->size(), std::memory_order_relaxed);
            map_.store(newMap);
         }

         void insert(const std::pair<T, U>& obj)
         {
            auto thisMap = map_.load();
            auto newMap = std::make_shared<std::map<T, U>>();

            newMap->insert(thisMap->begin(), thisMap->end());
            newMap->emplace(obj);

            count_.store(newMap->size(), std::memory_order_relaxed);
            map_.store(newMap);
         }

         void update(std::map<T, U> updatemap)
         {
            if (updatemap.empty()) {
               return;
            }
            auto thisMap = map_.load();
            auto newMap = std::make_shared<std::map<T, U>>(std::move(updatemap));
            newMap->insert(thisMap->begin(), thisMap->end());

            count_.store(newMap->size(), std::memory_order_relaxed);
            map_.store(newMap);
         }

         void erase(const T& id)
         {
            auto thisMap = map_.load();
            auto iter = thisMap->find(id);
            if (iter == thisMap->end()) {
               return;
            }

            auto newMap = std::make_shared<std::map<T, U>>();
            newMap->insert(thisMap->begin(), thisMap->end());
            newMap->erase(id);

            count_.store(newMap->size(), std::memory_order_relaxed);
            map_.store(newMap);
         }

         void erase(const std::vector<T>& idVec)
         {
            if (idVec.empty()) {
               return;
            }
            auto thisMap = map_.load();
            auto newMap = std::make_shared<std::map<T, U>>();

            newMap->insert(thisMap->begin(), thisMap->end());
            for (const auto& id : idVec) {
               newMap->erase(id);
            }

            count_.store(newMap->size(), std::memory_order_relaxed);
            map_.store(newMap);
         }

         void erase(const std::deque<T>& idVec)
         {
            if (idVec.empty()) {
               return;
            }
            auto thisMap = map_.load();
            auto newMap = std::make_shared<std::map<T, U>>();
            newMap->insert(map_->begin(), map_->end());

            for (const auto& id : idVec) {
               newMap->erase(id);
            }

            count_.store(newMap->size(), std::memory_order_relaxed);
            map_.store(newMap);
         }

         std::shared_ptr<std::map<T, U>> pop_all(void)
         {
            auto thisMap = map_.load();
            auto newMap = std::make_shared<std::map<T, U>>();

            count_.store(0, std::memory_order_relaxed);
            map_.store(newMap);
            return thisMap;
         }

         std::shared_ptr<const std::map<T, U>> get(void) const
         {
            auto retMap = map_.load();
            auto retConstMap = std::static_pointer_cast<const std::map<T, U>>(
               retMap);
            return retConstMap;
         }

         void clear(void)
         {
            auto thisMap = map_.load();
            auto newMap = std::make_shared<std::map<T, U>>();

            count_.store(0, std::memory_order_relaxed);
            map_.store(newMap);
         }

         size_t size(void) const
         {
            return count_.load(std::memory_order_relaxed);
         }

         bool empty(void) const
         {
            return size() == 0;
         }
      };
   }; //namespace Threading
}; //namespace Armory
