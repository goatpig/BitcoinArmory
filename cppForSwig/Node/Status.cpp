////////////////////////////////////////////////////////////////////////////////
//                                                                            //
//  Copyright (C) 2017-2026, goatpig                                          //
//  Distributed under the MIT license                                         //
//  See LICENSE-MIT or https://opensource.org/licenses/MIT                    //
//                                                                            //
////////////////////////////////////////////////////////////////////////////////

#include "Status.h"

#include <Utils/log.h>
#include <Utils/JSON_codec.h>
#include "capnp/Types.capnp.h"

using namespace Node;
using namespace std::string_view_literals;

#define READY_THRESHOLD 0.9999999f

namespace
{
   //heightTime helpers
   void appendHeightAndTime(
      const JSON::Number& headerTimestamp, const JSON::Number& height, float pct)
   {
      auto blockHeight = (uint32_t)height.val;
      if (!heightTimeList.empty()) {
         if (heightTimeList.back().blockHeight == blockHeight) {
            return;
         }
      }
      heightTimeList.emplace_back(
         HeightTime{(uint32_t)headerTimestamp.val, blockHeight, pct});
      if (heightTimeList.size() > 20) {
         heightTimeList.pop_front();
      }
   }

   //node state verbose
   std::string_view chainStateToStr(Node::ChainState state)
   {
      switch (state)
      {
         case Node::ChainState::Unknown:
            return "unknown"sv;

         case Node::ChainState::Syncing:
            return "syncing"sv;

         case Node::ChainState::Ready:
            return "up to date"sv;

         default:
            return "N/A"sv;
      }
   }

   std::string_view rpcStateToStr(Node::RpcState state)
   {
      switch (state)
      {
         case Node::RpcState::Disabled:
            return "disabled"sv;

         case Node::RpcState::BadAuth:
            return "failed authentication"sv;

         case Node::RpcState::Online:
            return "connected"sv;

         case Node::RpcState::Error_28:
            return "rpc error 28"sv;

         default:
            return "N/A"sv;
      }
   }

   std::string_view nodeStateToStr(Node::NodeState state)
   {
      switch (state)
      {
         case Node::NodeState::Offline:
            return "not connected"sv;

         case Node::NodeState::Online:
            return "connected"sv;

         case Node::NodeState::OffSync:
            return "offsync"sv;

         default:
            return "N/A"sv;
      }
   }
}

////////////////////////////////////////////////////////////////////////////////
// HeightTime
HeightTime::HeightTime(uint32_t timestampHeader, uint32_t height, float pct) :
   timestampInHeader(timestampHeader), blockHeight(height), progressPct(pct),
   thisTimestamp(now())
{}

////////////////////////////////////////////////////////////////////////////////
// namespace functions
uint32_t Node::now()
{
   //all nimbly pimbly
   auto rightMeow = std::chrono::system_clock::now();
   return std::chrono::duration_cast<std::chrono::seconds>(
      rightMeow.time_since_epoch()).count();
}

bool Node::updateHeightTime(
   const JSON::Object& chainInfoObj,
   const JSON::Object& blockHeaderObj)
{
   /* returns true if the list was appended */

   //grab height and timestamp from block header
   auto heightVal = std::dynamic_pointer_cast<JSON::Number>(
      blockHeaderObj.getValForKey("height"sv));
   if (heightVal == nullptr) {
      LOGWARN << "how did we get an invalid block header reply from the RPC?";
      return false;
   }
   auto timeVal = std::dynamic_pointer_cast<JSON::Number>(
      blockHeaderObj.getValForKey("time"sv));
   if (timeVal == nullptr) {
      LOGWARN << "how did we get an invalid block header reply from the RPC?";
      return false;
   }

   //get progress status JSON
   auto pctJson = std::dynamic_pointer_cast<JSON::Number>(
      chainInfoObj.getValForKey("verificationprogress"sv));
   if (pctJson == nullptr) {
      return false;
   }

   //append them all to the heightTime list
   std::unique_lock<std::mutex> lock{heightTimeMutex};
   auto pct = std::min((float)pctJson->val, 1.0f);
   appendHeightAndTime(timeVal->val, heightVal->val, pct);
   return true;
}

////////////////////////////////////////////////////////////////////////////////
// NodeChainStatus
ChainStatus::ChainStatus() :
   state{ChainState::Unknown}, progressPct(0.0f),
   blockSpeed(0.0f), etaSeconds(UINT32_MAX), blocksLeft(UINT32_MAX)
{}

ChainStatus::ChainStatus(ChainState cstate, float pct,
   float speed, uint32_t eta, uint32_t blocks) :
   state(cstate), progressPct(pct),
   blockSpeed(speed), etaSeconds(eta), blocksLeft(blocks)
{}

////////
ChainStatus ChainStatus::get()
{
   std::unique_lock<std::mutex> lock{heightTimeMutex};
   if (heightTimeList.empty()) {
      return ChainStatus{};
   }

   //compare with the last heightTime on record
   const auto& lastHeightTime = heightTimeList.back();
   if (lastHeightTime.progressPct >= READY_THRESHOLD) {
      return ChainStatus{ChainState::Ready, 1.0f, 0.0f, 0, 0};
   }

   uint32_t nowSeconds = now();
   uint32_t diff = nowSeconds > lastHeightTime.timestampInHeader ?
      nowSeconds - lastHeightTime.timestampInHeader : 0;

   //average amount of blocks left to sync based on timestamp diff
   uint32_t blocksLeft = diff / 600;

   /* project chain syncing speed off of the last 20 heightTime entries */

   const auto& firstHeightTime = heightTimeList.front();
   auto blockDiff = lastHeightTime.blockHeight - firstHeightTime.blockHeight;
   if (lastHeightTime.thisTimestamp < firstHeightTime.thisTimestamp ||
      blockDiff == 0) {
      return ChainStatus{};
   }

   auto timeDiff = lastHeightTime.thisTimestamp - firstHeightTime.thisTimestamp;
   auto blockSpeed = float(blockDiff) / float(timeDiff);
   return ChainStatus{
      ChainState::Syncing, lastHeightTime.progressPct,
      blockSpeed, uint32_t(blocksLeft * blockSpeed), blocksLeft
   };
}

////////
std::string ChainStatus::toString(const std::string& indent) const
{
   auto stateStr = std::format("chain state: {}",
      chainStateToStr(state));
   auto progressStr = std::format("progress: {}%, ETA: {}s, blocks left: {}",
      progressPct * 100.0f, etaSeconds, blocksLeft);
   if (state == ChainState::Syncing) {
      return std::format("{}. {}\n{}. {}",
         indent, stateStr, indent, progressStr);
   } else {
      return std::format("{}. {}",
         indent, stateStr);
   }
}

///////////////////////////////////////////////////////////////////////////////
// Status
Status::Status() :
   state(NodeState::Offline), rpcState(RpcState::Disabled),
   chainStatus{}, segWitEnabled(false)
{}

Status::Status(NodeState nodestate, RpcState rpcstate,
   const ChainStatus& chainstatus, bool issw) :
   state(nodestate), rpcState(rpcstate),
   chainStatus{chainstatus}, segWitEnabled(issw)
{}

std::string Status::toString() const
{
   auto stateStr = std::format("p2p state: {}", nodeStateToStr(state));
   auto rpcStateStr = std::format("rpc state: {}", rpcStateToStr(rpcState));
   if (rpcState != Node::RpcState::Online) {
      return std::format("- {}\n- {}", stateStr, rpcStateStr);
   } else {
      return std::format("- {}\n- {}\n{}",
         stateStr, rpcStateStr, chainStatus.toString("  "));
   }
}
