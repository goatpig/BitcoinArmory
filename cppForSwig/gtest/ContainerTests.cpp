////////////////////////////////////////////////////////////////////////////////
//                                                                            //
//  Copyright (C) 2016, goatpig                                               //
//  Distributed under the MIT license                                         //
//  See LICENSE-MIT or https://opensource.org/licenses/MIT                    //
//                                                                            //
////////////////////////////////////////////////////////////////////////////////

#include <iostream>
#include <stdlib.h>
#include <stdint.h>
#include <thread>
#include <gtest/gtest.h>

#include "Utils/ThreadSafeClasses.h"

using namespace std;
using namespace Armory::Threading;
using namespace std::chrono_literals;

////////////////////////////////////////////////////////////////////////////////
class ContainerTests : public ::testing::Test
{
protected:

   uint64_t threadCount_;

   void SetUp() override
   {
      threadCount_ = thread::hardware_concurrency() * 2;
   }

   void TearDown() override
   {}
};

////////////////////////////////////////////////////////////////////////////////
TEST_F(ContainerTests, TransactionalMap)
{
   unsigned iterations = 200;
   TransactionalMap<unsigned, unsigned> theMap;

   auto insert_thread = [&theMap, &iterations](unsigned id)
   {
      for (auto i = id * iterations; i < (id + 1) * iterations; i++) {
         theMap.insert(make_pair(i, i));
      }
   };

   auto find_thread = [&theMap, &iterations](unsigned id, uint32_t* tally)
   {
      *tally = 0;
      for (auto i = id * iterations; i < (id + 1) * iterations; i++) {
         auto mapptr = theMap.get();
         auto iter = mapptr->find(i);
         if (iter != mapptr->end()) {
            *tally += iter->second;
         }
      }
   };

   std::vector<std::thread> vecthr;
   vecthr.reserve(threadCount_);
   for (unsigned i = 0; i < threadCount_; i++) {
      vecthr.emplace_back(std::thread(insert_thread, i));
   }

   for (auto& thr : vecthr) {
      if (thr.joinable()) {
         thr.join();
      }
   }

   vecthr.clear();
   std::vector<uint32_t> tallies(threadCount_);
   for (unsigned i = 0; i < threadCount_; i++) {
      vecthr.emplace_back(std::thread(find_thread, i, &tallies[i]));
   }
   insert_thread(threadCount_);

   for (auto& thr : vecthr) {
      if (thr.joinable()) {
         thr.join();
      }
   }

   uint32_t total = 0;
   for (const auto& tally : tallies) {
      total += tally;
   }

   uint32_t maxtotal = threadCount_ * iterations - 1;
   uint32_t calctotal = (maxtotal + 1) * maxtotal;
   calctotal /= 2;

   EXPECT_EQ(total, calctotal);
}

////////////////////////////////////////////////////////////////////////////////
TEST_F(ContainerTests, Queue_Sequential)
{
   Queue<uint64_t> theStack;
   unsigned iterCount = 100000;

   auto push_thread = [&theStack, iterCount](uint64_t* tally)
   {
      //create random numbers, push to queue, increment tally
      srand(time(0));

      *tally = 0;
      for (unsigned i = 0; i < iterCount; i++) {
         uint64_t val = rand();
         *tally += val;
         theStack.push_back(std::move(val));
      }
   };

   auto pop_thread = [&theStack](uint64_t* tally)
   {
      //pop from queue, increment tally
      *tally = 0;
      while (true) {
         try {
            *tally += theStack.pop_front();
         } catch (const IsEmpty&) {
            break;
         }
      }
   };

   //producers
   std::vector<std::thread> push_threads;
   push_threads.reserve(threadCount_);
   std::vector<uint64_t> push_tallies(threadCount_);
   for (unsigned i = 0; i < threadCount_; i++) {
      push_threads.emplace_back(std::thread(push_thread, &push_tallies[i]));
   }

   for (auto& pushthr : push_threads) {
      if (pushthr.joinable()) {
         pushthr.join();
      }
   }
   EXPECT_EQ(theStack.count(), threadCount_ * iterCount);

   //consumers
   std::vector<std::thread> pop_threads;
   pop_threads.reserve(threadCount_);
   std::vector<uint64_t> pop_tallies(threadCount_);
   for (unsigned y = 0; y < threadCount_; y++) {
      pop_threads.emplace_back(std::thread(pop_thread, &pop_tallies[y]));
   }

   for (auto& popthr : pop_threads) {
      if (popthr.joinable()) {
         popthr.join();
      }
   }

   uint64_t pushtally = 0;
   for (const auto& tally : push_tallies) {
      pushtally += tally;
   }

   uint64_t poptally = 0;
   for (const auto& tally : pop_tallies) {
      poptally += tally;
   }

   EXPECT_EQ(pushtally, poptally);
   EXPECT_EQ(theStack.count(), 0ULL);
}

////////////////////////////////////////////////////////////////////////////////
TEST_F(ContainerTests, Queue_Concurrent)
{
   Queue<uint64_t> theStack;
   unsigned iterCount = 100000;

   auto push_thread = [&theStack, iterCount](uint64_t* tally)
   {
      //create random numbers, push to pile, increment tally
      srand(time(0));

      *tally = 0;
      for (unsigned i = 0; i < iterCount; i++) {
         uint64_t val = rand();
         *tally += val;
         theStack.push_back(std::move(val));
      }
   };

   auto pop_thread = [&theStack](uint64_t* tally, std::shared_ptr<std::atomic<bool>> done)
   {
      //pop from pile, increment tally
      *tally = 0;
      while (!done->load(memory_order_acquire)) {
         try {
            while (true) {
               *tally += theStack.pop_front();
            }
         } catch (const IsEmpty&) {}
      }
   };

   auto stop_pop_threads = std::make_shared<std::atomic<bool>>();
   stop_pop_threads->store(false, memory_order_relaxed);

   std::vector<std::thread> push_threads, pop_threads;
   std::vector<uint64_t> push_tallies(threadCount_), pop_tallies(threadCount_);

   push_threads.reserve(threadCount_);
   for (unsigned i = 0; i < threadCount_; i++) {
      push_threads.emplace_back(
         std::thread(push_thread, &push_tallies[i]));
   }

   for (unsigned y = 0; y < threadCount_; y++) {
      pop_threads.emplace_back(
         std::thread(pop_thread, &pop_tallies[y], stop_pop_threads));
   }

   for (auto& pushthr : push_threads) {
      if (pushthr.joinable()) {
         pushthr.join();
      }
   }
   stop_pop_threads->store(true, memory_order_release);

   for (auto& popthr : pop_threads) {
      if (popthr.joinable()) {
         popthr.join();
      }
   }

   uint64_t pushtally = 0;
   for (const auto& tally : push_tallies) {
      pushtally += tally;
   }

   uint64_t poptally = 0;
   for (const auto& tally : pop_tallies) {
      poptally += tally;
   }

   EXPECT_EQ(pushtally, poptally);
   EXPECT_EQ(theStack.count(), 0ULL);
}

////////////////////////////////////////////////////////////////////////////////
TEST_F(ContainerTests, BlockingQueue_Sequential)
{
   BlockingQueue<uint64_t> theStack;
   unsigned iterCount = 100000;

   auto push_thread = [&theStack, iterCount](uint64_t* tally)
   {
      //create random numbers, push to pile, increment tally
      srand(time(0));

      *tally = 0;
      for (unsigned i = 0; i < iterCount; i++) {
         uint64_t val = rand();
         *tally += val;
         theStack.push_back(std::move(val));
      }
   };

   auto pop_thread = [&theStack](uint64_t* tally)
   {
      //pop from pile, increment tally
      *tally = 0;
      try {
         while (true) {
            *tally += theStack.pop_front();
         }
      } catch (const StopBlockingLoop&) {}
   };

   //run producers first
   std::vector<thread> push_threads;
   push_threads.reserve(threadCount_);
   std::vector<uint64_t> push_tallies(threadCount_);
   for (unsigned i = 0; i < threadCount_; i++) {
      push_threads.emplace_back(
         std::thread(push_thread, &push_tallies[i]));
   }

   for (auto& pushthr : push_threads) {
      if (pushthr.joinable()) {
         pushthr.join();
      }
   }

   uint64_t pushtally = 0;
   for (auto& tally : push_tallies) {
      pushtally += tally;
   }

   EXPECT_EQ(theStack.count(), threadCount_ * iterCount);
   theStack.completed();

   //run consumers now
   std::vector<thread> pop_threads;
   pop_threads.reserve(threadCount_);
   std::vector<uint64_t> pop_tallies(threadCount_);
   for (unsigned y = 0; y < threadCount_; y++) {
      pop_threads.emplace_back(
         std::thread(pop_thread, &pop_tallies[y]));
   }

   for (auto& popthr : pop_threads) {
      if (popthr.joinable()) {
         popthr.join();
      }
   }

   uint64_t poptally = 0;
   for (auto& tally : pop_tallies) {
      poptally += tally;
   }

   //final checks
   EXPECT_EQ(pushtally, poptally);
   EXPECT_EQ(theStack.count(), 0ULL);
   EXPECT_EQ(theStack.waiting(), 0);
}

////////////////////////////////////////////////////////////////////////////////
TEST_F(ContainerTests, BlockingQueue_Concurrent)
{
   BlockingQueue<uint64_t> theStack;
   unsigned iterCount = 100000;

   auto push_thread = [&theStack, iterCount](uint64_t* tally)
   {
      //create random numbers, push to pile, increment tally
      srand(time(0));

      *tally = 0;
      for (unsigned i = 0; i < iterCount; i++) {
         uint64_t val = rand();
         *tally += val;
         theStack.push_back(std::move(val));
      }
   };

   auto pop_thread = [&theStack](uint64_t* tally)
   {
      //pop from pile, increment tally
      *tally = 0;
      try {
         while (true) {
            *tally += theStack.pop_front();
         }
      } catch (const StopBlockingLoop&) {}
   };

   //run producers and consumers concurrently
   std::vector<std::thread> push_threads, pop_threads;

   push_threads.reserve(threadCount_);
   std::vector<uint64_t> push_tallies(threadCount_);
   for (unsigned i = 0; i < threadCount_; i++) {
      push_threads.emplace_back(
         std::thread(push_thread, &push_tallies[i]));
   }

   pop_threads.reserve(threadCount_);
   std::vector<uint64_t> pop_tallies(threadCount_);
   for (unsigned y = 0; y < threadCount_; y++) {
      pop_threads.emplace_back(
         std::thread(pop_thread, &pop_tallies[y]));
   }

   //wait on producers
   for (auto& pushthr : push_threads) {
      if (pushthr.joinable()) {
         pushthr.join();
      }
   }
   theStack.completed();

   //wait on consumers
   for (auto& popthr : pop_threads) {
      if (popthr.joinable()) {
         popthr.join();
      }
   }

   //tally everything
   uint64_t pushtally = 0;
   for (auto& tally : push_tallies) {
      pushtally += tally;
   }

   uint64_t poptally = 0;
   for (auto& tally : pop_tallies) {
      poptally += tally;
   }

   //check tallies
   EXPECT_EQ(theStack.waiting(), 0);
   EXPECT_EQ(pushtally, poptally);
   EXPECT_EQ(theStack.count(), 0ULL);

   theStack.clear();
   push_threads.clear();
   pop_threads.clear();

   //run again
   for (unsigned i = 0; i < threadCount_; i++) {
      push_threads.emplace_back(
         std::thread(push_thread, &push_tallies[i]));
   }

   for (unsigned y = 0; y < threadCount_; y++) {
      pop_threads.emplace_back(
         std::thread(pop_thread, &pop_tallies[y]));
   }

   for (auto& pushthr : push_threads) {
      if (pushthr.joinable()) {
         pushthr.join();
      }
   }
   theStack.terminate();

   for (auto& popthr : pop_threads) {
      if (popthr.joinable()) {
         popthr.join();
      }
   }

   EXPECT_NE(theStack.count(), 0ULL);
   theStack.clear();
   EXPECT_EQ(theStack.count(), 0ULL);
}

////////////////////////////////////////////////////////////////////////////////
TEST_F(ContainerTests, TimedQueue_Concurrent)
{
   TimedQueue<uint64_t> theStack;
   unsigned iterCount = 35000;

   auto push_thread = [&](uint64_t* tally)
   {
      //create random numbers, push to pile, increment tally
      srand(time(0));
      for (unsigned i = 0; i < iterCount; i++) {
         uint64_t val = rand();
         *tally += val;
         theStack.push_back(move(val));
      }
   };

   auto pop_thread = [&theStack](uint64_t* tally)
   {
      //pop from pile, increment tally
      try {
         while (true) {
            *tally += theStack.pop_front(2s);
         }
      } catch (const StackTimedOutException&) {
         //thread will exit when control reaches here
      }
   };

   std::vector<std::thread> push_threads, pop_threads;
   push_threads.reserve(threadCount_); pop_threads.reserve(threadCount_);
   std::vector<uint64_t> push_tallies(threadCount_), pop_tallies(threadCount_);

   //start consumer threads
   for (unsigned y = 0; y < threadCount_; y++) {
      pop_threads.emplace_back(
         std::thread(pop_thread, &pop_tallies[0] + y));
   }

   //producer threads, wave 1
   for (unsigned i = 0; i < threadCount_; i++) {
      push_threads.emplace_back(
         std::thread(push_thread, &push_tallies[0] + i));
   }
   for (auto& pushthr : push_threads) {
      if (pushthr.joinable()) {
         pushthr.join();
      }
   }
   push_threads.clear();

   //wave 2
   std::this_thread::sleep_for(1s);
   for (unsigned i = 0; i < threadCount_; i++) {
      push_threads.emplace_back(
         std::thread(push_thread, &push_tallies[0] + i));
   }
   for (auto& pushthr : push_threads) {
      if (pushthr.joinable()) {
         pushthr.join();
      }
   }
   push_threads.clear();

   //wave 3
   std::this_thread::sleep_for(1s);
   for (unsigned i = 0; i < threadCount_; i++) {
      push_threads.emplace_back(
         std::thread(push_thread, &push_tallies[0] + i));
   }
   for (auto& pushthr : push_threads) {
      if (pushthr.joinable()) {
         pushthr.join();
      }
   }

   //wait on consumers to exit
   for (auto& popthr : pop_threads) {
      if (popthr.joinable()) {
         popthr.join();
      }
   }

   uint64_t pushtally = 0;
   for (auto& tally : push_tallies) {
      pushtally += tally;
   }

   uint64_t poptally = 0;
   for (auto& tally : pop_tallies) {
      poptally += tally;
   }

   EXPECT_EQ(theStack.waiting(), 0);
   EXPECT_EQ(pushtally, poptally);
   EXPECT_EQ(theStack.count(), 0ULL);

   //run producers again, without consumers
   push_threads.clear();
   push_tallies.clear(); push_tallies.resize(threadCount_);
   for (unsigned i = 0; i < threadCount_; i++) {
      push_threads.emplace_back(
         std::thread(push_thread, &push_tallies[0] + i));
   }
   for (auto& pushthr : push_threads) {
      if (pushthr.joinable()) {
         pushthr.join();
      }
   }

   pushtally = 0;
   for (auto& tally : push_tallies) {
      pushtally += tally;
   }
   ASSERT_NE(pushtally, 0);

   //check pop_all tally consistency
   auto values = theStack.pop_all();
   poptally = 0;
   for (auto& val : values) {
      poptally += val;
   }

   EXPECT_EQ(theStack.waiting(), 0);
   EXPECT_EQ(pushtally, poptally);
   EXPECT_EQ(theStack.count(), 0ULL);
}

////////////////////////////////////////////////////////////////////////////////
TEST_F(ContainerTests, TimedQueue_SingleConsumer)
{
   /*
   One consumer, many producers, random sleeps between pushes
   */
   TimedQueue<uint64_t> theStack;
   unsigned producerCount = 5;

   //consumer
   std::promise<uint64_t> consumerProm;
   auto consumerFut = consumerProm.get_future();
   auto consumer = [&theStack](std::promise<uint64_t> myProm)
   {
      uint64_t tally = 0;
      while (true) {
         try {
            tally += theStack.pop_front(1s);
         } catch (const StopBlockingLoop&) {
            break;
         } catch (const StackTimedOutException&) {
            continue;
         }
      }
      myProm.set_value(tally);
   };

   //producer
   auto producer = [&theStack](std::promise<uint64_t> prom)
   {
      srand(time(0));
      uint64_t tally = 0;
      for (unsigned i=0; i<200; i++) {
         //random sleep duration then push to queue
         std::chrono::milliseconds toSleep{ rand() % 300 };
         if (toSleep > 50ms) {
            std::this_thread::sleep_for(toSleep);
         }

         uint64_t val = rand();
         tally += val;
         theStack.push_back(std::move(val));
      }

      std::this_thread::sleep_for(100ms);
      prom.set_value(tally);
   };

   //start consumer thread
   std::thread consumerThr(consumer, std::move(consumerProm));
   if (consumerThr.joinable()) {
      consumerThr.detach();
   }

   //start producers
   std::vector<std::future<uint64_t>> futs;
   futs.reserve(producerCount);
   for (unsigned i=0; i<producerCount; i++) {
      std::promise<uint64_t> thrProm;
      futs.emplace_back(thrProm.get_future());
      std::thread producerThr(producer, std::move(thrProm));
      if (producerThr.joinable()) {
         producerThr.detach();
      }

      //stagger producers
      std::this_thread::sleep_for(1s);
   }

   //tally producer side
   uint64_t producerTally = 0;
   for (auto& fut : futs) {
      producerTally += fut.get();
   }
   ASSERT_NE(producerTally, 0);

   //shutdown consumer and get its tally
   theStack.terminate();
   auto consumerTally = consumerFut.get();

   //check tallies
   EXPECT_EQ(producerTally, consumerTally);
   EXPECT_EQ(theStack.waiting(), 0);
   EXPECT_EQ(theStack.count(), 0ULL);
}

////////////////////////////////////////////////////////////////////////////////
GTEST_API_ int main(int argc, char **argv)
{
#ifdef _MSC_VER
   _CrtSetDbgFlag(_CRTDBG_ALLOC_MEM_DF | _CRTDBG_LEAK_CHECK_DF);
#endif

   testing::InitGoogleTest(&argc, argv);
   int exitCode = RUN_ALL_TESTS();
   return exitCode;
}
