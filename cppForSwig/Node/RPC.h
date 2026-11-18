////////////////////////////////////////////////////////////////////////////////
//                                                                            //
//  Copyright (C) 2017-2026, goatpig                                          //
//  Distributed under the MIT license                                         //
//  See LICENSE-MIT or https://opensource.org/licenses/MIT                    //
//                                                                            //
////////////////////////////////////////////////////////////////////////////////

#pragma once

#include <condition_variable>
#include <memory>
#include <string>
#include <map>
#include <functional>

#include <Utils/ReentrantLock.h>
#include "Status.h"

class BinaryDataRef;

namespace Armory
{
   namespace Network
   {
      class HttpSocket;
   }
}

namespace JSON
{
   class Object;
}

////////
namespace Node
{
   class RpcError : public std::runtime_error
   {
   public:
      RpcError(void);
      RpcError(const std::string&);
   };

   namespace Core
   {
      namespace RPC
      {
         /***
         NOTE:
            "state" suffix is for enums
            "status" suffix is for classes/structs
         ***/

         static constexpr std::string_view FEE_STRAT_CONSERVATIVE{"CONSERVATIVE"};
         static constexpr std::string_view FEE_STRAT_ECONOMICAL{"ECONOMICAL"};

         ////////
         struct FeeSchedulesCache
         {
            using ScheduleMap = std::map<std::string, FeeSchedule>;
            const ScheduleMap schedules;
            const uint32_t timestamp;

            FeeSchedulesCache(ScheduleMap);
            bool isValid(void) const;
         };

         ////////
         class Iface : public Lockable
         {
         protected:
            std::unique_ptr<FeeSchedulesCache> cachedFees_;
            std::atomic<bool> enabled_ = { true };
            std::condition_variable sleepCV_;
            std::mutex sleepMutex_;

         private:
            void initAfterLock(void) override {}
            void cleanUpBeforeUnlock(void) override {}

         public:
            Iface(void);
            virtual ~Iface(void) = 0;
            virtual bool requestNodeShutdown(void) = 0;
            void disable(void);

            virtual int broadcastTx(const BinaryDataRef&, std::string&) = 0;
            virtual RpcState testConnection() = 0;
            virtual void waitOnChainSync(std::function<void(void)>, bool) = 0;
            virtual FeeEstimate getFeeByte(
               unsigned, const std::string&) = 0;
            virtual FeeSchedule getFeeSchedule(const std::string&) = 0;

            //locals
            ChainStatus getChainStatus(void) const;
         };

         ////////
         class Client : public Iface
         {
         private:
            const bool explicitAuthString_;
            std::string basicAuthString64_;
            RpcState previousState_ = RpcState::Disabled;

         private:
            std::string queryRPC(JSON::Object&);
            std::string queryRPC(Armory::Network::HttpSocket&, JSON::Object&);
            bool setupConnection(Armory::Network::HttpSocket&);

            float queryFeeByte(Armory::Network::HttpSocket&, unsigned);
            FeeEstimate queryFeeByteSmart(
               Armory::Network::HttpSocket&, unsigned&, std::string_view);
            void aggregateFeeEstimates(void);
            void resetAuthString(void);
            bool updateChainStatus(void);

         public:
            Client(const std::string&, const std::string&);

            //virtuals
            bool requestNodeShutdown(void) override;
            RpcState testConnection(void) override;

            FeeSchedule getFeeSchedule(
               const std::string&) override;
            FeeEstimate getFeeByte(unsigned, const std::string&) override;
            int broadcastTx(const BinaryDataRef&, std::string&) override;
            void waitOnChainSync(std::function<void(void)>, bool) override;
         };
      } //namespace RPC
   } //namespace Core
} //namespace Node
