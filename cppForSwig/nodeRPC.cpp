////////////////////////////////////////////////////////////////////////////////
//                                                                            //
//  Copyright (C) 2017, goatpig                                               //
//  Distributed under the MIT license                                         //
//  See LICENSE-MIT or https://opensource.org/licenses/MIT                    //
//                                                                            //
////////////////////////////////////////////////////////////////////////////////

#include "ArmoryErrors.h"
#include "nodeRPC.h"
#include "DBUtils.h"
#include "ArmoryConfig.h"
#include "SocketWritePayload.h"

#ifdef _WIN32
#include "leveldb_windows_port\win32_posix\dirent_win32.h"
#else
#include "dirent.h"
#endif

using namespace std;
using namespace CoreRPC;
using namespace Armory::Config;

////////////////////////////////////////////////////////////////////////////////
//
// NodeRPCInterface
//
////////////////////////////////////////////////////////////////////////////////
NodeRPCInterface::~NodeRPCInterface()
{}

////////////////////////////////////////////////////////////////////////////////
const NodeChainStatus& NodeRPCInterface::getChainStatus(void) const
{
   ReentrantLock lock(this);
   
   return nodeChainStatus_;
}

////////////////////////////////////////////////////////////////////////////////
map<unsigned, FeeEstimateResult> NodeRPCInterface::getFeeSchedule(
   const string& strategy)
{
   auto estimateCachePtr = atomic_load(&currentEstimateCache_);

   if (estimateCachePtr == nullptr)
      throw RpcError();

   auto iterStrat = estimateCachePtr->find(strategy);
   if (iterStrat == estimateCachePtr->end())
      throw RpcError();

   return iterStrat->second;
}

////////////////////////////////////////////////////////////////////////////////
//
// NodeRPC
//
////////////////////////////////////////////////////////////////////////////////
NodeRPC::NodeRPC()
{
   //start fee estimate polling thread
   auto pollLbd = [this](void)->void
   {
      this->pollThread();
   };

   if (!canPoll())
      return;

   thrVec_.push_back(thread(pollLbd));
}

////////////////////////////////////////////////////////////////////////////////
bool NodeRPC::setupConnection(HttpSocket& sock)
{
   ReentrantLock lock(this);

   //test the socket
   if(!sock.connectToRemote())
      return false;

   if (basicAuthString64_.size() == 0)
   {
      auto&& authString = getAuthString();
      if (authString.size() == 0)
         return false;

      basicAuthString64_ = move(BtcUtils::base64_encode(authString));
   }

   stringstream auth_header;
   auth_header << "Authorization: Basic " << basicAuthString64_;
   auto header_str = auth_header.str();
   sock.precacheHttpHeader(header_str);

   return true;
}

////////////////////////////////////////////////////////////////////////////////
void NodeRPC::resetAuthString()
{
   ReentrantLock lock(this);
   basicAuthString64_.clear();
}

////////////////////////////////////////////////////////////////////////////////
RpcState NodeRPC::testConnection()
{
   ReentrantLock lock(this);

   RpcState state = RpcState_Disabled;
   JSON_object json_obj;
   json_obj.add_pair("method", "getblockcount");

   try
   {
      auto&& response = queryRPC(json_obj);
      auto&& response_obj = JSON_decode(response);

      if (response_obj.isResponseValid(json_obj.id_))
      {
         state = RpcState_Online;
      }
      else
      {
         auto error_ptr = response_obj.getValForKey("error");
         auto error_obj = dynamic_pointer_cast<JSON_object>(error_ptr);
         if (error_obj != nullptr)
         {
            auto error_code_ptr = error_obj->getValForKey("code");
            auto error_code = dynamic_pointer_cast<JSON_number>(error_code_ptr);

            if (error_code == nullptr)
               throw JSON_Exception("failed to get error code");

            if ((int)error_code->val_ == -28)
            {
               state = RpcState_Error_28;
            }
         }
         else
         {
            state = RpcState_Disabled;

            auto error_val = dynamic_pointer_cast<JSON_string>(error_ptr);
            if (error_val != nullptr)
            {
               LOGWARN << "Rpc connection test failed with error: " <<
                  error_val->val_;
            }
         }
      }
   }
   catch (RpcError&)
   {
      state = RpcState_Disabled;
   }
   catch (SocketError&)
   {
      state = RpcState_Disabled;
   }
   catch (JSON_Exception& e)
   {
      LOGERR << "RPC connection test error: " << e.what();
      state = RpcState_BadAuth;
   }

   return state;
}

////////////////////////////////////////////////////////////////////////////////
string NodeRPC::getDatadir()
{
   string datadir = Pathing::blkFilePath();
   auto len = datadir.size();

   if (len >= 6)
   {
      auto&& term = datadir.substr(len - 6, 6);
      if (term == "blocks")
         datadir = datadir.substr(0, len - 6);
   }

   return datadir;
}

////////////////////////////////////////////////////////////////////////////////
string NodeRPC::getAuthString()
{
   auto&& datadir = getDatadir();

   auto confPath = datadir;
   DBUtils::appendPath(confPath, "bitcoin.conf");

   auto getAuthStringFromCookieFile = [&datadir](void)->string
   {
      DBUtils::appendPath(datadir, ".cookie");
      auto&& lines = SettingsUtils::getLines(datadir);
      if (lines.size() != 1)
      {
         throw runtime_error("unexpected cookie file content");
      }

      auto&& keyVals = SettingsUtils::getKeyValsFromLines(lines, ':');
      auto keyIter = keyVals.find("__cookie__");
      if (keyIter == keyVals.end())
      {
         throw runtime_error("unexpected cookie file content");
      }

      return lines[0];
   };

   //open and parse .conf file
   try
   {
      auto&& lines = SettingsUtils::getLines(confPath);
      auto&& keyVals = SettingsUtils::getKeyValsFromLines(lines, '=');
      
      //get rpcuser
      auto userIter = keyVals.find("rpcuser");
      if (userIter == keyVals.end())
         return getAuthStringFromCookieFile();

      string authStr = userIter->second;

      //get rpcpassword
      auto passIter = keyVals.find("rpcpassword");
      if (passIter == keyVals.end())
         return getAuthStringFromCookieFile();

      authStr.append(":");
      authStr.append(passIter->second);

      return authStr;
   }
   catch (...)
   {
      return string();
   }
}

////////////////////////////////////////////////////////////////////////////////
float NodeRPC::queryFeeByte(HttpSocket& sock, unsigned blocksToConfirm)
{
   ReentrantLock lock(this);

   JSON_object json_obj;
   json_obj.add_pair("method", "estimatefee");

   auto json_array = make_shared<JSON_array>();
   json_array->add_value(blocksToConfirm);

   json_obj.add_pair("params", json_array);

   auto&& response = queryRPC(sock, json_obj);
   auto&& response_obj = JSON_decode(response);

   if (!response_obj.isResponseValid(json_obj.id_))
      throw JSON_Exception("invalid response");

   auto feeByteObj = response_obj.getValForKey("result");
   auto feeBytePtr = dynamic_pointer_cast<JSON_number>(feeByteObj);

   if (feeBytePtr == nullptr)
      throw JSON_Exception("invalid response");

   return feeBytePtr->val_;
}

////////////////////////////////////////////////////////////////////////////////
FeeEstimateResult NodeRPC::queryFeeByteSmart(HttpSocket& sock,
   unsigned& confTarget, string& strategy)
{
   auto fallback = [this, &confTarget, &sock](void)->FeeEstimateResult
   {
      FeeEstimateResult fer;
      fer.smartFee_ = false;
      auto feeByteSimple = queryFeeByte(sock, confTarget);
      if (feeByteSimple == -1.0f)
         fer.error_ = "error";
      else
         fer.feeByte_ = feeByteSimple;

      return fer;
   };

   FeeEstimateResult fer;

   ReentrantLock lock(this);

   JSON_object json_obj;
   json_obj.add_pair("method", "estimatesmartfee");

   auto json_array = make_shared<JSON_array>();
   json_array->add_value(confTarget);
   if(strategy == FEE_STRAT_CONSERVATIVE || strategy == FEE_STRAT_ECONOMICAL)
      json_array->add_value(strategy);

   json_obj.add_pair("params", json_array);

   auto&& response = queryRPC(sock, json_obj);
   auto&& response_obj = JSON_decode(response);

   if (!response_obj.isResponseValid(json_obj.id_))
      return fallback();

   auto resultPairObj = response_obj.getValForKey("result");
   auto resultPairPtr = dynamic_pointer_cast<JSON_object>(resultPairObj);

   if (resultPairPtr != nullptr)
   {
      auto feeByteObj = resultPairPtr->getValForKey("feerate");
      auto feeBytePtr = dynamic_pointer_cast<JSON_number>(feeByteObj);
      if (feeBytePtr != nullptr)
      {
         fer.feeByte_ = feeBytePtr->val_;
         fer.smartFee_ = true;

         auto blocksObj = resultPairPtr->getValForKey("blocks");
         auto blocksPtr = dynamic_pointer_cast<JSON_number>(blocksObj);

         if (blocksPtr != nullptr)
            if (blocksPtr->val_ != confTarget)
               confTarget = blocksPtr->val_;
      }
   }

   auto errorObj = response_obj.getValForKey("error");
   auto errorPtr = dynamic_pointer_cast<JSON_string>(errorObj);

   if (errorPtr != nullptr)
   {
      if (resultPairPtr == nullptr)
      {
         //fallback to the estimatefee if the method is missing
         return fallback();
      }
      else
      {
         //report smartfee error msg
         fer.error_ = errorPtr->val_;
         fer.smartFee_ = true;
      }
   }

   return fer;
}

////////////////////////////////////////////////////////////////////////////////
FeeEstimateResult NodeRPC::getFeeByte(
   unsigned confTarget, const string& strategy)
{
   auto estimateCachePtr = atomic_load(&currentEstimateCache_);

   if (estimateCachePtr == nullptr)
      throw RpcError();

   auto iterStrat = estimateCachePtr->find(strategy);
   if (iterStrat == estimateCachePtr->end())
      throw RpcError();

   if (iterStrat->second.empty())
      throw RpcError();

   auto targetIter = iterStrat->second.upper_bound(confTarget);
   if (targetIter != iterStrat->second.begin())
      --targetIter;

   return targetIter->second;
}

////////////////////////////////////////////////////////////////////////////////
void NodeRPC::aggregateFeeEstimates()
{
   //get fee/byte on both strategies
   vector<unsigned> confTargets = { 2, 3, 4, 5, 6, 10, 12, 20, 24, 48, 144 };
   static vector<string> strategies = {
      FEE_STRAT_CONSERVATIVE, FEE_STRAT_ECONOMICAL };

   HttpSocket sock("127.0.0.1", NetworkSettings::rpcPort());
   if (!setupConnection(sock))
      throw RpcError("aggregateFeeEstimates: failed to setup RPC socket");

   auto newCache = make_shared<EstimateCache>();

   for (auto& strat : strategies)
   {
      auto insertIter = newCache->insert(
         make_pair(strat, map<unsigned, FeeEstimateResult>()));
      auto& newMap = insertIter.first->second;

      for (auto& target : confTargets)
      {
         try
         {
            auto&& result = queryFeeByteSmart(sock, target, strat);
            newMap.insert(make_pair(target, move(result)));
         }
         catch (ConfMismatch&)
         {
            break;
         }
      }
   }

   atomic_store(&currentEstimateCache_, newCache);
}

////////////////////////////////////////////////////////////////////////////////
bool NodeRPC::updateChainStatus(void)
{
   ReentrantLock lock(this);

   //get top block header
   JSON_object json_getblockchaininfo;
   json_getblockchaininfo.add_pair("method", "getblockchaininfo");

   auto&& response = JSON_decode(queryRPC(json_getblockchaininfo));
   if (!response.isResponseValid(json_getblockchaininfo.id_))
      throw JSON_Exception("invalid response");

   auto getblockchaininfo_result = response.getValForKey("result");
   auto getblockchaininfo_object = dynamic_pointer_cast<JSON_object>(
                                    getblockchaininfo_result);

   auto hash_obj = getblockchaininfo_object->getValForKey("bestblockhash");
   if (hash_obj == nullptr)
      return false;
   
   auto params_obj = make_shared<JSON_array>();
   params_obj->add_value(hash_obj);

   JSON_object json_getheader;
   json_getheader.add_pair("method", "getblockheader");
   json_getheader.add_pair("params", params_obj);

   auto&& block_header = JSON_decode(queryRPC(json_getheader));

   if (!block_header.isResponseValid(json_getheader.id_))
      throw JSON_Exception("invalid response");

   auto block_header_ptr = block_header.getValForKey("result");
   auto block_header_result = dynamic_pointer_cast<JSON_object>(block_header_ptr);
   if (block_header_result == nullptr)
      throw JSON_Exception("invalid response");

   //append timestamp and height
   auto height_obj = block_header_result->getValForKey("height");
   auto height_val = dynamic_pointer_cast<JSON_number>(height_obj);
   if (height_val == nullptr)
      throw JSON_Exception("invalid response");

   auto time_obj = block_header_result->getValForKey("time");
   auto time_val = dynamic_pointer_cast<JSON_number>(time_obj);
   if (time_val == nullptr)
      throw JSON_Exception("invalid response");

   nodeChainStatus_.appendHeightAndTime(height_val->val_, time_val->val_);

   //figure out state
   return nodeChainStatus_.processState(getblockchaininfo_object);
}

////////////////////////////////////////////////////////////////////////////////
void NodeRPC::waitOnChainSync(function<void(void)> callbck)
{
   nodeChainStatus_.reset();
   callbck();

   while (1)
   {
      //keep trying as long as the node is initializing
      auto state = testConnection();
      if (state != RpcState_Error_28)
      {
         if (state != RpcState_Online)
            return;

         break;
      }

      //sleep for 1sec
      this_thread::sleep_for(chrono::seconds(1));
   }

   callbck();

   while (1)
   {
      float blkSpeed = 0.0f;
      try
      {
         ReentrantLock lock(this);

         if (updateChainStatus())
            callbck();

         auto& chainStatus = getChainStatus();
         if (chainStatus.state() == ChainState_Ready)
            break;
      
         blkSpeed = chainStatus.getBlockSpeed();
      }
      catch (...)
      {
         auto state = testConnection();
         if (state == RpcState_Online)
            throw runtime_error("unsupported RPC method");
      }

      unsigned dur = 1; //sleep delay in seconds

      if (blkSpeed != 0.0f)
      {
         auto singleBlkEta = max(1.0f / blkSpeed, 1.0f);
         dur = min(unsigned(singleBlkEta), unsigned(5)); //don't sleep for more than 5sec
      }

      this_thread::sleep_for(chrono::seconds(dur));
   }

   LOGINFO << "RPC is ready";
}

////////////////////////////////////////////////////////////////////////////////
int NodeRPC::broadcastTx(const BinaryDataRef& rawTx, string& verbose)
{
   ReentrantLock lock(this);

   JSON_object json_obj;
   json_obj.add_pair("method", "sendrawtransaction");

   auto json_array = make_shared<JSON_array>();
   string rawTxHex = rawTx.toHexStr();
   json_array->add_value(rawTxHex);

   json_obj.add_pair("params", json_array);

   string response;
   try
   {
      response = queryRPC(json_obj);
      auto&& response_obj = JSON_decode(response);

      if (!response_obj.isResponseValid(json_obj.id_))
      {
         auto error_field = response_obj.getValForKey("error");
         auto error_obj = dynamic_pointer_cast<JSON_object>(error_field);
         if (error_obj == nullptr)
            throw JSON_Exception("invalid response");

         auto msg_field = error_obj->getValForKey("message");
         auto msg_val = dynamic_pointer_cast<JSON_string>(msg_field);
         verbose = msg_val->val_;
         
         auto code_field = error_obj->getValForKey("code");
         auto code_val = dynamic_pointer_cast<JSON_number>(code_field);
         return (int)code_val->val_;
      }

      return (int)ArmoryErrorCodes::Success;
   }
   catch (RpcError& e)
   {
      LOGWARN << "RPC internal error: " << e.what();
      return (int)ArmoryErrorCodes::RPCFailure_Internal;
   }
   catch (JSON_Exception& e)
   {
      LOGWARN << "RPC JSON error: " << e.what();
      LOGWARN << "Node response was: ";
      LOGWARN << response;

      return (int)ArmoryErrorCodes::RPCFailure_JSON;
   }
   catch (exception& e)
   {
      LOGWARN << "Unkown RPC error: " << e.what();
      return (int)ArmoryErrorCodes::RPCFailure_Unknown;
   }
}

////////////////////////////////////////////////////////////////////////////////
void NodeRPC::shutdown()
{
   ReentrantLock lock(this);

   JSON_object json_obj;
   json_obj.add_pair("method", "stop");

   auto&& response = queryRPC(json_obj);
   auto&& response_obj = JSON_decode(response);

   if (!response_obj.isResponseValid(json_obj.id_))
      throw JSON_Exception("invalid response");

   auto responseStr_obj = response_obj.getValForKey("result");
   auto responseStr = dynamic_pointer_cast<JSON_string>(responseStr_obj);

   if (responseStr == nullptr)
      throw JSON_Exception("invalid response");

   LOGINFO << responseStr->val_;
}

////////////////////////////////////////////////////////////////////////////////
string NodeRPC::queryRPC(JSON_object& request)
{
   HttpSocket sock("127.0.0.1", NetworkSettings::rpcPort());
   if (!setupConnection(sock))
      throw RpcError("node_down");

   return queryRPC(sock, request);
}

////////////////////////////////////////////////////////////////////////////////
string NodeRPC::queryRPC(HttpSocket& sock, JSON_object& request)
{
   auto write_payload = make_unique<WritePayload_StringPassthrough>();
   write_payload->data_ = move(JSON_encode(request));

   auto promPtr = make_shared<promise<string>>();
   auto fut = promPtr->get_future();

   auto callback = [promPtr](string body)->void
   {
      promPtr->set_value(move(body));
   };

   auto read_payload = make_shared<Socket_ReadPayload>(request.id_);
   read_payload->callbackReturn_ = 
      make_unique<CallbackReturn_HttpBody>(callback);
   sock.pushPayload(move(write_payload), read_payload);

   return fut.get();
}

////////////////////////////////////////////////////////////////////////////////
void NodeRPC::pollThread()
{
   auto pred = [this](void)->bool
   {
      return !run_.load(memory_order_acquire);
   };

   mutex mu;
   bool status = false;
   while (true)
   {
      if (!status)
      {
         //test connection
         try
         {
            resetAuthString();
            auto rpcState = testConnection();
            bool doCallback = false;
            if (rpcState != previousState_)
               doCallback = true;

            previousState_ = rpcState;

            if (doCallback)
               callback();

            if (rpcState == RpcState_Online)
            {
               LOGINFO << "RPC connection established";
               status = true;
               continue;
            }
         }
         catch (exception& e)
         {
            LOGWARN << "fee poll check failed with error: " << e.what();
            status = false;
         }
      }
      else
      {
         //update fee estimate
         try
         {
            aggregateFeeEstimates();
         }
         catch (exception&)
         {
            status = false;
            continue;
         }
      }

      unique_lock<mutex> lock(mu);
      if (pollCondVar_.wait_for(lock, chrono::seconds(10), pred))
         break;
   }

   LOGWARN << "out of rpc poll loop";
}

////////////////////////////////////////////////////////////////////////////////
NodeRPC::~NodeRPC()
{
   run_.store(false, memory_order_release);
   pollCondVar_.notify_all();

   for (auto& thr : thrVec_)
   {
      if (thr.joinable())
         thr.join();
   }
}

////////////////////////////////////////////////////////////////////////////////
//
// NodeChainStatus
//
////////////////////////////////////////////////////////////////////////////////
bool NodeChainStatus::processState(
   shared_ptr<JSON_object> const getblockchaininfo_obj)
{
   if (state_ == ChainState_Ready)
      return false;

   //progress status
   auto pct_obj = getblockchaininfo_obj->getValForKey("verificationprogress");
   auto pct_val = dynamic_pointer_cast<JSON_number>(pct_obj);
   if (pct_val == nullptr)
      return false;

   pct_ = min(pct_val->val_, 1.0);
   auto pct_int = unsigned(pct_ * 10000.0);

   if (pct_int != prev_pct_int_)
   {
      LOGINFO << "waiting on node sync: " << float(pct_ * 100.0) << "%";
      prev_pct_int_ = pct_int;
   }

   if (pct_ >= 0.9995)
   {
      state_ = ChainState_Ready;
      return true;
   }

   //compare top block timestamp to now
   if (heightTimeVec_.size() == 0)
      return false;

   uint64_t now = time(0);
   uint64_t diff = 0;

   auto blocktime = get<1>(heightTimeVec_.back());
   if (now > blocktime)
      diff = now - blocktime;

   //we got this far, node is still syncing, let's compute progress and eta
   state_ = ChainState_Syncing;

   //average amount of blocks left to sync based on timestamp diff
   auto blocksLeft = diff / 600;

   //compute block syncing speed based off of the last 20 top blocks
   auto iterend = heightTimeVec_.rbegin();
   auto time_end = get<2>(*iterend);

   auto iterbegin = heightTimeVec_.begin();
   auto time_begin = get<2>(*iterbegin);

   if (time_end <= time_begin)
      return false;

   auto blockdiff = get<0>(*iterend) - get<0>(*iterbegin);
   if (blockdiff == 0)
      return false;

   auto timediff = time_end - time_begin;
   blockSpeed_ = float(blockdiff) / float(timediff);
   eta_ = uint64_t(float(blocksLeft) * blockSpeed_);

   blocksLeft_ = blocksLeft;

   return true;
}

////////////////////////////////////////////////////////////////////////////////
unsigned NodeChainStatus::getTopBlock() const
{
   if (heightTimeVec_.size() == 0)
      throw runtime_error("");

   return get<0>(heightTimeVec_.back());
}

////////////////////////////////////////////////////////////////////////////////
void NodeChainStatus::appendHeightAndTime(unsigned height, uint64_t timestamp)
{
   try
   {
      if (getTopBlock() == height)
         return;
   }
   catch (...)
   {
   }

   heightTimeVec_.push_back(make_tuple(height, timestamp, time(0)));

   //force the list at 20 max entries
   while (heightTimeVec_.size() > 20)
      heightTimeVec_.pop_front();
}

////////////////////////////////////////////////////////////////////////////////
void NodeChainStatus::reset()
{
   heightTimeVec_.clear();
   state_ = ChainState_Unknown;
   blockSpeed_ = 0.0f;
   eta_ = 0;
}