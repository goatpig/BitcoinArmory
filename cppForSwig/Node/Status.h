////////////////////////////////////////////////////////////////////////////////
//                                                                            //
//  Copyright (C) 2017-2026, goatpig                                          //
//  Distributed under the MIT license                                         //
//  See LICENSE-MIT or https://opensource.org/licenses/MIT                    //
//                                                                            //
////////////////////////////////////////////////////////////////////////////////

#pragma once

#include <cstdint>
#include <mutex>
#include <list>
#include <string>
#include <map>

namespace JSON
{
   class Object;
}

namespace Node
{
   /*
   state is the enum
   status is the class
   */

   enum class NodeState : int
   {
      Offline = 0,
      Online,
      OffSync
   };

   enum class RpcState : int
   {
      Disabled = 0,
      BadAuth,
      Online,
      Error_28
   };

   enum class ChainState : int
   {
      Unknown = 0,
      Syncing,
      Ready
   };

   ////////
   struct FeeEstimate
   {
      const bool smartFee;
      const float feeByte;
      const std::string error;
   };

   struct FeeSchedule
   {
      std::map<unsigned, FeeEstimate> estimates;
   };

   //to keep track of block count and timestamps during node sync
   struct HeightTime
   {
      const uint32_t timestampInHeader;
      const uint32_t blockHeight;
      const float    progressPct;
      const uint32_t thisTimestamp;

      HeightTime(uint32_t, uint32_t, float);
   };

   //chain status from RPC
   struct ChainStatus
   {
      const ChainState state;
      const float progressPct;

      const float blockSpeed;
      const uint64_t etaSeconds;
      const uint32_t blocksLeft;

      ChainStatus(void);
      ChainStatus(ChainState, float,
         float, uint32_t, uint32_t);

      std::string toString(const std::string&) const;
      static ChainStatus get(void);
   };

   //node status
   struct Status
   {
      const NodeState state;
      const RpcState rpcState;
      const ChainStatus chainStatus;
      const bool segWitEnabled;

      Status(void);
      Status(NodeState, RpcState, const ChainStatus&, bool);

      std::string toString(void) const;
   };

   //tracks and projects node sync ETA
   inline std::list<HeightTime> heightTimeList;
   inline std::mutex heightTimeMutex;
   bool updateHeightTime(const JSON::Object&, const JSON::Object&);

   //helpers
   uint32_t now(void);
} //namespace node
