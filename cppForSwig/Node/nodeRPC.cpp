////////////////////////////////////////////////////////////////////////////////
//                                                                            //
//  Copyright (C) 2017-2026, goatpig                                          //
//  Distributed under the MIT license                                         //
//  See LICENSE-MIT or https://opensource.org/licenses/MIT                    //
//                                                                            //
////////////////////////////////////////////////////////////////////////////////

#include "nodeRPC.h"

#include <filesystem>

#include <Utils/ArmoryErrors.h>
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

#define FEE_ESTIMATE_LIFETIME 30

namespace
{
   static std::vector<unsigned> confTargets{ 2, 3, 4, 5, 6, 10, 20 };
   static std::vector<std::string> strategies{
      std::string{ RPC::FEE_STRAT_CONSERVATIVE },
      std::string{ RPC::FEE_STRAT_ECONOMICAL }
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

   uint32_t now()
   {
      //all nimbly pimbly
      auto rightMeow = std::chrono::system_clock::now();
      return std::chrono::duration_cast<std::chrono::seconds>(
         rightMeow.time_since_epoch()).count();
   }
}

////////////////////////////////////////////////////////////////////////////////
// FeeEstimateCache
RPC::FeeEstimateCache::FeeEstimateCache(EstimateMap eMap) :
   estimates{std::move(eMap)}, timestamp{now()}
{}

bool RPC::FeeEstimateCache::isValid() const
{
   return timestamp + FEE_ESTIMATE_LIFETIME > now();
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

const ChainStatus& RPC::Iface::getChainStatus() const
{
   ReentrantLock lock(this);
   return nodeChainStatus_;
}

////////////////////////////////////////////////////////////////////////////////
// NodeRPC
RPC::Client::Client(const std::string& rpcUser, const std::string& rpcPass) :
   canResetAuthString_{true}
{
   if (!rpcUser.empty() && !rpcPass.empty()) {
      basicAuthString64_ = BtcUtils::base64_encode(std::format("{}:{}",
         rpcUser, rpcPass));
      canResetAuthString_ = false;
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
      state = RpcState::BadAuth;
   }
   return state;
}

////////
void RPC::Client::resetAuthString()
{
   if (!canResetAuthString_) {
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

RPC::FeeEstimate RPC::Client::queryFeeByteSmart(Network::HttpSocket& sock,
   unsigned& confTarget, const std::string& strategy)
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
      jsonArray->append(strategy);
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

   FeeEstimateCache::EstimateMap freshEstimates;
   for (const auto& strat : strategies) {
      auto insertIter = freshEstimates.emplace(
         strat, std::map<unsigned, FeeEstimate>{});
      auto& newMap = insertIter.first->second;

      for (auto target : confTargets) {
         newMap.emplace(target, queryFeeByteSmart(sock, target, strat));
      }
   }
   cachedFees_ = std::make_unique<FeeEstimateCache>(std::move(freshEstimates));
}

////////
// grabbing fees from the cache
const std::map<unsigned, RPC::FeeEstimate>& RPC::Client::getFeeSchedule(
   const std::string& strategy)
{
   ReentrantLock lock(this);
   while (true) {
      if (cachedFees_ != nullptr) {
         if (cachedFees_->isValid()) {
            //we have a valid fee cache, return from it
            auto iterStrat = cachedFees_->estimates.find(strategy);
            if (iterStrat == cachedFees_->estimates.end()) {
               throw RpcError{};
            }
            return iterStrat->second;
         }
      }

      /*
      If we got this far, we have either lack a fee estimate or it is stale.
      Let's try to constitute a fresh one. This call will throw on error.
      */
      aggregateFeeEstimates();
   }
}

RPC::FeeEstimate RPC::Client::getFeeByte(
   unsigned confTarget, const std::string& strategy)
{
   ReentrantLock lock(this);
   const auto& schedule = getFeeSchedule(strategy);
   auto targetIter = schedule.upper_bound(confTarget);
   if (targetIter != schedule.begin()) {
      --targetIter;
   }
   return targetIter->second;
}

////////////////////////////////////////////////////////////////////////////////
bool RPC::Client::updateChainStatus()
{
   ReentrantLock lock(this);

   //get top block header
   JSON::Object getblockchaininfo{"method"sv, "getblockchaininfo"sv};

   auto response = JSON::decode(queryRPC(getblockchaininfo));
   if (!response.isResponseValid(getblockchaininfo.id)) {
      throw JSON::Exception("getblockchaininfo id mismatch");
   }

   auto resultObj = std::dynamic_pointer_cast<JSON::Object>(
      response.getValForKey("result"sv));
   if (resultObj == nullptr) {
      return false;
   }

   auto hashObj = resultObj->getValForKey("bestblockhash"sv);
   if (hashObj == nullptr) {
      return false;
   }

   auto paramsObj = std::make_shared<JSON::Array>();
   paramsObj->append(hashObj);

   JSON::Object getheader{"method"sv, "getblockheader"sv};
   getheader.append("params"sv, paramsObj);

   auto blockHeader = JSON::decode(queryRPC(getheader));
   if (!blockHeader.isResponseValid(getheader.id)) {
      throw JSON::Exception("getblockheader id mismatch");
   }

   auto blockHeaderObj = std::dynamic_pointer_cast<JSON::Object>(
      blockHeader.getValForKey("result"sv));
   if (blockHeaderObj == nullptr) {
      throw JSON::Exception("getheader missing result");
   }

   //append timestamp and height
   auto heightVal = std::dynamic_pointer_cast<JSON::Number>(
      blockHeaderObj->getValForKey("height"sv));
   if (heightVal == nullptr) {
      throw JSON::Exception("getheader missing height");
   }

   auto timeVal = std::dynamic_pointer_cast<JSON::Number>(
      blockHeaderObj->getValForKey("time"sv));
   if (timeVal == nullptr) {
      throw JSON::Exception("getheader missing time");
   }

   //figure out state
   nodeChainStatus_.appendHeightAndTime(heightVal->val, timeVal->val);
   return nodeChainStatus_.processState(*resultObj);
}

////////
void RPC::Client::waitOnChainSync(std::function<void(void)> callbck, bool persist)
{
   nodeChainStatus_.reset();
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
         ReentrantLock lock(this);
         if (updateChainStatus()) {
            callbck();
         }
         const auto& chainStatus = getChainStatus();
         if (chainStatus.state() == ChainState::Ready) {
            break;
         }
         blkSpeed = chainStatus.getBlockSpeed();
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
      LOGWARN << "RPC internal error: " << e.what();
      return (int)ArmoryErrorCodes::RPCFailure_Internal;
   } catch (const JSON::Exception& e) {
      LOGWARN << "RPC JSON error: " << e.what();
      LOGWARN << "Node response was: ";
      LOGWARN << response;
      return (int)ArmoryErrorCodes::RPCFailure_JSON;
   } catch (const std::exception& e) {
      LOGWARN << "Unkown RPC error: " << e.what();
      return (int)ArmoryErrorCodes::RPCFailure_Unknown;
   }
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

////////////////////////////////////////////////////////////////////////////////
// NodeChainStatus
void ChainStatus::reset()
{
   heightTimeVec_.clear();
   state_ = ChainState::Unknown;
   blockSpeed_ = 0.0f;
   eta_ = 0;
}

////////
ChainState ChainStatus::state() const
{
   return state_;
}

float ChainStatus::getBlockSpeed() const
{
   return blockSpeed_;
}

float ChainStatus::getProgressPct() const
{
   return pct_;
}

uint64_t ChainStatus::getETA() const
{
   return eta_;
}

unsigned ChainStatus::getBlocksLeft() const
{
   return blocksLeft_;
}

////////
bool ChainStatus::processState(const JSON::Object& jsonObj)
{
   if (state_ == ChainState::Ready) {
      return false;
   }

   //progress status
   auto pct_val = std::dynamic_pointer_cast<JSON::Number>(
      jsonObj.getValForKey("verificationprogress"sv));
   if (pct_val == nullptr) {
      return false;
   }

   pct_ = std::min(pct_val->val, 1.0);
   auto pct_int = unsigned(pct_ * 10000.0);

   if (pct_int != prevPctInt_) {
      LOGINFO << "waiting on node sync: " << float(pct_ * 100.0) << "%";
      prevPctInt_ = pct_int;
   }

   if (pct_ >= 0.9999999) {
      state_ = ChainState::Ready;
      return true;
   }

   //compare top block timestamp to now
   if (heightTimeVec_.empty()) {
      return false;
   }
   uint64_t now = time(0);
   uint64_t diff = 0;

   auto blocktime = std::get<1>(heightTimeVec_.back());
   if (now > blocktime) {
      diff = now - blocktime;
   }

   //we got this far, node is still syncing, let's compute progress and eta
   state_ = ChainState::Syncing;

   //average amount of blocks left to sync based on timestamp diff
   auto blocksLeft = diff / 600;

   //compute block syncing speed based off of the last 20 top blocks
   auto iterend = heightTimeVec_.rbegin();
   auto time_end = std::get<2>(*iterend);

   auto iterbegin = heightTimeVec_.begin();
   auto time_begin = std::get<2>(*iterbegin);

   if (time_end <= time_begin) {
      return false;
   }

   auto blockdiff = std::get<0>(*iterend) - std::get<0>(*iterbegin);
   if (blockdiff == 0) {
      return false;
   }

   auto timediff = time_end - time_begin;
   blockSpeed_ = float(blockdiff) / float(timediff);
   eta_ = uint64_t(float(blocksLeft) * blockSpeed_);
   blocksLeft_ = blocksLeft;
   return true;
}

////////
unsigned ChainStatus::getTopBlock() const
{
   if (heightTimeVec_.empty()) {
      throw std::runtime_error("");
   }
   return std::get<0>(heightTimeVec_.back());
}

void ChainStatus::appendHeightAndTime(unsigned height, uint64_t timestamp)
{
   try {
      if (getTopBlock() == height) {
         return;
      }
   } catch (...) {}
   heightTimeVec_.emplace_back(std::make_tuple(height, timestamp, time(0)));

   //force the list at 20 max entries
   while (heightTimeVec_.size() > 20) {
      heightTimeVec_.pop_front();
   }
}
