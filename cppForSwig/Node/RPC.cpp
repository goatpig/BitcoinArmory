////////////////////////////////////////////////////////////////////////////////
//                                                                            //
//  Copyright (C) 2017-2026, goatpig                                          //
//  Distributed under the MIT license                                         //
//  See LICENSE-MIT or https://opensource.org/licenses/MIT                    //
//                                                                            //
////////////////////////////////////////////////////////////////////////////////

#include "RPC.h"

#include <filesystem>

#include <Utils/ArmoryErrors.h>
#include <Utils/BinaryData.h>
#include <Utils/BtcUtils.h>
#include <Utils/ArmoryConfig.h>
#include <Utils/JSON_codec.h>
#include <Network/StringSockets.h>
#include <Network/SocketWritePayload.h>

using namespace std::chrono_literals;
using namespace std::string_view_literals;

using namespace Armory;
using namespace Node;
using namespace Node::Core;

#define VALID_FEE_ESTIMATE_LIFETIME 30
#define INVALID_FEE_ESTIMATE_LIFETIME 5

namespace
{
   static std::vector<unsigned> confTargets{ 2, 3, 4, 5, 6, 10, 20 };
   static std::vector<std::string_view> strategies{
      RPC::FEE_STRAT_CONSERVATIVE,
      RPC::FEE_STRAT_ECONOMICAL
   };

   std::filesystem::path getDatadir()
   {
      auto datadir = Config::Pathing::blkFilePath();
      if (datadir.filename() == "blocks") {
         datadir = datadir.parent_path();
      }
      return datadir;
   }

   std::string getAuthStringFromCookie()
   {
      auto cookiePath = getDatadir() / ".cookie";
      auto lines = Config::SettingsUtils::getLines(cookiePath);
      if (lines.size() != 1) {
         throw std::runtime_error("unexpected cookie file content");
      }

      auto keyVals = Config::SettingsUtils::getKeyValsFromLines(lines, ':');
      auto keyIter = keyVals.find("__cookie__");
      if (keyIter == keyVals.end()) {
         throw std::runtime_error("missing cookie key");
      }
      return lines[0];
   }

   std::string getAuthString()
   {
      //open and parse .conf file
      try {
         auto datadir = getDatadir();
         auto confPath = datadir / "bitcoin.conf";
         auto lines = Config::SettingsUtils::getLines(confPath);
         auto keyVals = Config::SettingsUtils::getKeyValsFromLines(lines, '=');

         //get rpcuser
         try {
            return std::string{
               keyVals.at("rpcuser") + ":" +
               keyVals.at("rpcpassword")
            };
         } catch (const std::out_of_range&) {
            return getAuthStringFromCookie();
         }
      } catch (const std::exception& e) {
         return {};
      }
   }
}

////////////////////////////////////////////////////////////////////////////////
// FeeSchedulesCache
RPC::FeeSchedulesCache::FeeSchedulesCache(ScheduleMap sMap) :
   schedules{std::move(sMap)}, timestamp{now()}
{}

bool RPC::FeeSchedulesCache::isValid() const
{
   if (schedules.empty()) {
      //we don't want to spam the RPC if the fee estimate is wrong
      return timestamp + INVALID_FEE_ESTIMATE_LIFETIME > now();
   } else {
      return timestamp + VALID_FEE_ESTIMATE_LIFETIME > now();
   }
}

////////////////////////////////////////////////////////////////////////////////
// RpcError
RpcError::RpcError() :
   std::runtime_error("generic core RPC error")
{}

RpcError::RpcError(const std::string& err) :
   std::runtime_error(err)
{}

////////////////////////////////////////////////////////////////////////////////
// NodeRPCInterface
RPC::Iface::Iface()
{
   enabled_.store(true, std::memory_order_relaxed);
}

RPC::Iface::~Iface()
{}

void RPC::Iface::disable()
{
   //grab the lock before flipping the flag, so that ongoing RPC
   //reqeust are not rugpulled
   ReentrantLock lock(this);
   enabled_.store(false, std::memory_order_relaxed);

   //whatever loops on the enabled_ flag sleeps on this cv, so
   //wake them all up
   sleepCV_.notify_all();
}

ChainStatus RPC::Iface::getChainStatus() const
{
   return Node::ChainStatus::get();
}

////////////////////////////////////////////////////////////////////////////////
// NodeRPC
RPC::Client::Client(const std::string& rpcUser, const std::string& rpcPass) :
   explicitAuthString_{!rpcUser.empty() && !rpcPass.empty()}
{
   if (explicitAuthString_) {
      basicAuthString64_ = BtcUtils::base64_encode(
         std::format("{}:{}", rpcUser, rpcPass));
   }
}

////////
bool RPC::Client::setupConnection(Network::HttpSocket& sock)
{
   if (enabled_.load(std::memory_order_relaxed) == false) {
      return false;
   }

   //test the socket
   ReentrantLock lock(this);
   if (!sock.connectToRemote()) {
      return false;
   }

   //grab/generate the authentication string if missing
   if (basicAuthString64_.empty()) {
      ReentrantLock lock(this);
      auto authString = getAuthString();
      if (authString.empty()) {
         return false;
      }
      basicAuthString64_ = std::move(
         BtcUtils::base64_encode(authString));
   }

   std::string authHeader{ "Authorization: Basic " + basicAuthString64_ };
   sock.precacheHttpHeader(authHeader);
   return true;
}

RpcState RPC::Client::testConnection()
{
   ReentrantLock lock(this);

   RpcState state = RpcState::Disabled;
   JSON::Object jsonObj{"method"sv, "getblockcount"sv};

   try {
      auto response = queryRPC(jsonObj);
      auto responseObj = JSON::decode(response);

      if (responseObj.isResponseValid(jsonObj.id)) {
         state = RpcState::Online;
      } else {
         auto errorObj = std::dynamic_pointer_cast<JSON::Object>(
            responseObj.getValForKey("error"sv));
         if (errorObj != nullptr) {
            auto errorCode = std::dynamic_pointer_cast<JSON::Number>(
               errorObj->getValForKey("code"sv));

            if (errorCode == nullptr) {
               throw JSON::Exception("failed to get error code");
            }

            if ((int)errorCode->val == -28) {
               state = RpcState::Error_28;
            }
         } else {
            state = RpcState::Disabled;
            auto errorVal = std::dynamic_pointer_cast<JSON::String>(
               responseObj.getValForKey("error"sv));
            if (errorVal != nullptr) {
               LOGWARN << "Rpc connection test failed with error: " <<
                  errorVal->val;
            }
         }
      }
   } catch (const RpcError& e) {
      state = RpcState::Disabled;
   } catch (const Network::SocketError& e) {
      state = RpcState::Disabled;
   } catch (const JSON::Exception& e) {
      LOGERR << "RPC connection test error: " << e.what();
      resetAuthString();
      state = RpcState::BadAuth;
   }
   return state;
}

////////
void RPC::Client::resetAuthString()
{
   if (explicitAuthString_) {
      LOGWARN << "tried to reset the RPC AuthString while using explicit credentials";
      return;
   }
   ReentrantLock lock(this);
   basicAuthString64_.clear();
}

////////
// grabbing fees from the RPC
float RPC::Client::queryFeeByte(Network::HttpSocket& sock, unsigned blocksToConfirm)
{
   ReentrantLock lock(this);

   JSON::Object jsonObj{"method"sv, "estimatefee"sv};

   auto jsonArray = std::make_shared<JSON::Array>();
   jsonArray->append(blocksToConfirm);
   jsonObj.append("params"sv, jsonArray);

   auto response = queryRPC(sock, jsonObj);
   auto responseObj = JSON::decode(response);

   if (!responseObj.isResponseValid(jsonObj.id)) {
      throw JSON::Exception("queryFeeByte id mismatch");
   }

   auto feeBytePtr = std::dynamic_pointer_cast<JSON::Number>(
      responseObj.getValForKey("result"sv));
   if (feeBytePtr == nullptr) {
      throw JSON::Exception("queryFeeByte missing result");
   }
   return feeBytePtr->val;
}

FeeEstimate RPC::Client::queryFeeByteSmart(Network::HttpSocket& sock,
   unsigned& confTarget, std::string_view strategy)
{
   auto fallback = [this, &confTarget, &sock]()->FeeEstimate
   {
      auto feeByteSimple = queryFeeByte(sock, confTarget);
      if (feeByteSimple == -1.0f) {
         return { false, -1.0f, "error" };
      } else {
         return { false, feeByteSimple, {} };
      }
   };

   ReentrantLock lock(this);
   JSON::Object jsonObj{"method"sv, "estimatesmartfee"sv};

   auto jsonArray = std::make_shared<JSON::Array>();
   jsonArray->append(confTarget);
   if (strategy == FEE_STRAT_CONSERVATIVE ||
      strategy == FEE_STRAT_ECONOMICAL) {
      jsonArray->append(std::string{strategy});
   }
   jsonObj.append("params"sv, jsonArray);

   auto response = queryRPC(sock, jsonObj);
   auto responseObj = JSON::decode(response);
   if (!responseObj.isResponseValid(jsonObj.id)) {
      return fallback();
   }

   auto resultPairPtr = std::dynamic_pointer_cast<JSON::Object>(
      responseObj.getValForKey("result"sv));

   if (resultPairPtr != nullptr) {
      auto feeBytePtr = std::dynamic_pointer_cast<JSON::Number>(
         resultPairPtr->getValForKey("feerate"sv));
      if (feeBytePtr != nullptr) {
         auto blocksPtr = std::dynamic_pointer_cast<JSON::Number>(
            resultPairPtr->getValForKey("blocks"sv));
         if (blocksPtr != nullptr) {
            if (blocksPtr->val != confTarget) {
               confTarget = blocksPtr->val;
            }
         }
         return { true, float(feeBytePtr->val) };
      }
   }

   auto errorPtr = std::dynamic_pointer_cast<JSON::String>(
      responseObj.getValForKey("error"sv));
   if (errorPtr != nullptr) {
      if (resultPairPtr == nullptr) {
         //fallback to estimatefee if estimatesmartfee method is missing
         return fallback();
      } else {
         //report estimatesmartfee error msg
         return { true, -1.0f, errorPtr->val };
      }
   } else {
      return { false, -1.0f, "unexpected error during queryFeeByteSmart" };
   }
}

void RPC::Client::aggregateFeeEstimates()
{
   //get fee/byte for 2-3-4-5-6-10-20 confs on both strategies
   ReentrantLock lock(this);
   Network::HttpSocket sock(
      "127.0.0.1", Config::NetworkSettings::rpcPort(),
      "satoshiRPC_fees");
   if (!setupConnection(sock)) {
      throw RpcError("[aggregateFeeEstimates] failed to setup RPC socket");
   }

   FeeSchedulesCache::ScheduleMap freshSchedules;
   for (const auto& strat : strategies) {
      auto insertIter = freshSchedules.emplace(
         strat, FeeSchedule{});
      auto& newSchedule = insertIter.first->second;

      for (auto target : confTargets) {
         newSchedule.estimates.emplace(
            target, queryFeeByteSmart(sock, target, strat));
      }
   }
   cachedFees_ = std::make_unique<FeeSchedulesCache>(std::move(freshSchedules));
}

////////
// grabbing fees from the cache
FeeSchedule RPC::Client::getFeeSchedule(
   const std::string& strategy)
{
   ReentrantLock lock(this);
   while (true) {
      if (cachedFees_ != nullptr) {
         if (cachedFees_->isValid()) {
            //we have a valid fee cache, return from it
            auto iterStrat = cachedFees_->schedules.find(strategy);
            if (iterStrat == cachedFees_->schedules.end()) {
               throw RpcError{};
            }
            return iterStrat->second;
         }
      }

      /*
      If we got this far, we either lack a valid fee estimate or it is stale.
      Let's try to constitute a fresh one.
      */
      try {
         aggregateFeeEstimates();
      } catch (const RpcError&) {
         //set an empty estimate to avoid RPC spam, as it will set a
         //timestamp to check against regardless.
         cachedFees_ = std::make_unique<FeeSchedulesCache>(
            FeeSchedulesCache::ScheduleMap{});
      }
   }
}

FeeEstimate RPC::Client::getFeeByte(
   unsigned confTarget, const std::string& strategy)
{
   ReentrantLock lock(this);
   const auto& schedule = getFeeSchedule(strategy);
   auto targetIter = schedule.estimates.upper_bound(confTarget);
   if (targetIter != schedule.estimates.begin()) {
      --targetIter;
   }
   return targetIter->second;
}

////////
// node chain state
bool RPC::Client::updateChainStatus()
{
   ReentrantLock lock(this);

   //get chain info
   JSON::Object getblockchaininfo{"method"sv, "getblockchaininfo"sv};

   auto chainInfoReply = JSON::decode(queryRPC(getblockchaininfo));
   if (!chainInfoReply.isResponseValid(getblockchaininfo.id)) {
      throw JSON::Exception("getblockchaininfo id mismatch");
   }
   auto chainInfoObj = std::dynamic_pointer_cast<JSON::Object>(
      chainInfoReply.getValForKey("result"sv));
   if (chainInfoObj == nullptr) {
      return false;
   }

   //get top block header using hash from chain info reply
   auto hashVal = chainInfoObj->getValForKey("bestblockhash"sv);
   if (hashVal == nullptr) {
      return false;
   }
   auto paramsObj = std::make_shared<JSON::Array>();
   paramsObj->append(hashVal);

   JSON::Object getheader{"method"sv, "getblockheader"sv};
   getheader.append("params"sv, paramsObj);

   auto blockHeaderReply = JSON::decode(queryRPC(getheader));
   if (!blockHeaderReply.isResponseValid(getheader.id)) {
      throw JSON::Exception("getblockheader id mismatch");
   }
   auto blockHeaderObj = std::dynamic_pointer_cast<JSON::Object>(
      blockHeaderReply.getValForKey("result"sv));
   if (blockHeaderObj == nullptr) {
      throw JSON::Exception("getheader missing result");
   }

   //feed both to ChainStatus parser
   return Node::updateHeightTime(*chainInfoObj, *blockHeaderObj);
}

////////
// waiting until node is ready
void RPC::Client::waitOnChainSync(std::function<void(void)> callbck, bool persist)
{
   while (enabled_.load(std::memory_order_relaxed)) {
      //keep trying as long as the node is initializing
      auto state = testConnection();
      callbck();
      if (state != RpcState::Error_28) {
         if (state != RpcState::Online && !persist) {
            return;
         }
         break;
      }

      //sleep for 1sec
      std::unique_lock<std::mutex> lock{sleepMutex_};
      sleepCV_.wait_for(lock, 1s);
   }

   while (enabled_.load(std::memory_order_relaxed)) {
      float blkSpeed = 0.0f;
      try {
         if (updateChainStatus()) {
            callbck();
         }
         const auto& chainStatus = getChainStatus();
         if (chainStatus.state == ChainState::Ready) {
            break;
         }
         blkSpeed = chainStatus.blockSpeed;
      } catch (const std::exception& e) {
         auto state = testConnection();
         if (state == RpcState::Online) {
            throw std::runtime_error("unsupported RPC method");
         }
      }

      unsigned dur = 1; //sleep delay in seconds
      if (blkSpeed != 0.0f) {
         //don't sleep for more than 5sec
         auto singleBlkEta = std::max(1.0f / blkSpeed, 1.0f);
         dur = std::min(unsigned(singleBlkEta), 5u);
      }
      std::unique_lock<std::mutex> lock{sleepMutex_};
      sleepCV_.wait_for(lock, std::chrono::seconds(dur));
   }
   LOGINFO << "RPC is ready";
}

////////
int RPC::Client::broadcastTx(const BinaryDataRef& rawTx, std::string& verbose)
{
   ReentrantLock lock(this);

   JSON::Object jsonObj{"method"sv, "sendrawtransaction"sv};
   auto jsonArray = std::make_shared<JSON::Array>();
   jsonArray->append(rawTx.toHexStr());
   jsonObj.append("params"sv, jsonArray);

   std::string response;
   try {
      response = queryRPC(jsonObj);
      auto responseObj = JSON::decode(response);

      if (!responseObj.isResponseValid(jsonObj.id)) {
         auto errorObj = std::dynamic_pointer_cast<JSON::Object>(
            responseObj.getValForKey("error"sv));
         if (errorObj == nullptr) {
            throw JSON::Exception("sendrawtransaction missing error");
         }

         auto msgVal = std::dynamic_pointer_cast<JSON::String>(
            errorObj->getValForKey("message"sv));
         verbose = msgVal->val;

         auto codeVal = std::dynamic_pointer_cast<JSON::Number>(
            errorObj->getValForKey("code"sv));
         return (int)codeVal->val;
      }
      return (int)ArmoryErrorCodes::Success;
   } catch (const RpcError& e) {
      verbose = std::format("RPC internal error: {}", e.what());
      return (int)ArmoryErrorCodes::RPCFailure_Internal;
   } catch (const JSON::Exception& e) {
      verbose = std::format(
         "RPC JSON error: {}\n"
         "Node response was: {}",
         e.what(), response);
      return (int)ArmoryErrorCodes::RPCFailure_JSON;
   } catch (const std::exception& e) {
      verbose = std::format("Unkown RPC error: {}", e.what());
   }
   return (int)ArmoryErrorCodes::RPCFailure_Unknown;
}

////////
bool RPC::Client::requestNodeShutdown()
{
   ReentrantLock lock(this);

   JSON::Object jsonObj{"method", "stop"};
   auto response = queryRPC(jsonObj);
   auto responseObj = JSON::decode(response);

   if (!responseObj.isResponseValid(jsonObj.id)) {
      throw JSON::Exception("stop id mismatch");
   }
   auto responseStr = std::dynamic_pointer_cast<JSON::String>(
      responseObj.getValForKey("result"sv));

   if (responseStr == nullptr) {
      throw JSON::Exception("stop missing result");
   }
   LOGINFO << responseStr->val;
   return true;
}

////////
std::string RPC::Client::queryRPC(JSON::Object& request)
{
   Network::HttpSocket sock(
      "127.0.0.1", Config::NetworkSettings::rpcPort(),
      "satoshiRPC");
   if (!setupConnection(sock)) {
      throw RpcError("satoshi_rpc_down");
   }
   return queryRPC(sock, request);
}

std::string RPC::Client::queryRPC(Network::HttpSocket& sock, JSON::Object& request)
{
   auto writePayload = std::make_unique<Network::WritePayload_StringPassthrough>();
   writePayload->data_ = std::move(JSON::encode(request));

   auto promPtr = std::make_shared<std::promise<std::string>>();
   auto fut = promPtr->get_future();

   auto callback = [promPtr](std::string body)
   {
      promPtr->set_value(std::move(body));
   };

   auto readPayload = std::make_shared<Network::Socket_ReadPayload>(request.id);
   readPayload->callbackReturn_ =
      std::make_unique<Network::CallbackReturn_HttpBody>(callback);

   sock.pushPayload(std::move(writePayload), readPayload);
   return fut.get();
}
