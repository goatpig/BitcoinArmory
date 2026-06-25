////////////////////////////////////////////////////////////////////////////////
//                                                                            //
//  Copyright (C) 2016-2025, goatpig.                                         //
//  Distributed under the MIT license                                         //
//  See LICENSE-MIT or https://opensource.org/licenses/MIT                    //
//                                                                            //
////////////////////////////////////////////////////////////////////////////////

#include "BDM_Server.h"
#include <Utils/ArmoryErrors.h>
#include <Utils/ArmoryConfig.h>
#include <Utils/DBUtils.h>
#include <BlockchainDatabase/BlockUtils.h>
#include <BlockchainDatabase/lmdb_wrapper.h>
#include <BlockchainDatabase/BlockchainData.h>
#include <BlockchainDatabase/txio.h>
#include <BlockchainDatabase/StoredBlockObj.h>
#include <ZeroConf/Parser.h>
#include <ZeroConf/Utils.h>
#include <ZeroConf/Notifications.h>
#include <Network/SocketWritePayload.h>
#include <Network/WebSocketMessage.h>

#include "Server.h"
#include "BtcWallet.h"

#include <capnp/message.h>
#include <capnp/serialize.h>
#include "capnp/BDV.capnp.h"

using namespace Armory;
using namespace std::chrono_literals;

#define BDVID_LENGTH    8
#define SCRATCHPAD_SIZE 4048

namespace {
   using namespace Armory::Codec::BDV;

   void txioToCapn(const TxIOPair& txio,
      Codec::Types::TxioPair::Builder& capnTxio)
   {
      capnTxio.setAmount(txio.getAmount());
      capnTxio.setTxOut(txio.getTxIOKeyOfOutput());
      capnTxio.setTxIn(txio.getTxIOKeyOfInput());
      capnTxio.setTxTime(txio.getTxTime());
      const auto& scrAddr = txio.getScrAddr();
      capnTxio.setScrAddr(capnp::Data::Builder(
         (uint8_t*)scrAddr.getPtr(), scrAddr.getSize()));
      capnTxio.setRbf(txio.isRBF());
      capnTxio.setChained(txio.isChained());
   }

   ////
   struct ReplyBuilder
   {
      std::unique_ptr<capnp::MallocMessageBuilder> builder = nullptr;

      static ReplyBuilder getNew(std::shared_ptr<BDV_Server_Object> bdv)
      {
         if (bdv == nullptr) {
            throw std::runtime_error("null bdv");
         }
         auto& scratchPad = bdv->getScratchPad();
         kj::ArrayPtr arrayPtr(
            reinterpret_cast<capnp::word*>(scratchPad.data()),
            scratchPad.size() / sizeof(capnp::word));
         return ReplyBuilder {
            std::make_unique<capnp::MallocMessageBuilder>(
               arrayPtr, capnp::AllocationStrategy::FIXED_SIZE),
         };
      }

      void setError(const std::string& errStr)
      {
         if (builder == nullptr) {
            throw std::runtime_error("builder is not initialized");
         }
         auto reply = builder->getRoot<Codec::BDV::Reply>();
         reply.setError(errStr);
         reply.setSuccess(false);
      }

      bool isValid() const
      {
         return builder != nullptr;
      }
   };

   ////
   ReplyBuilder parseBDVCommand(
      BdvRequest::Reader request,
      std::shared_ptr<BDV_Server_Object> bdv,
      uint32_t msgId)
   {
      auto prepareReply = [&msgId](ReplyBuilder& rp)
         ->Codec::BDV::BdvReply::Builder
      {
         auto reply = rp.builder->initRoot<Codec::BDV::Reply>();
         reply.setMsgId(msgId);
         reply.setSuccess(true);
         return reply.initBdv();
      };

      switch (request.which())
      {
         case BdvRequest::Which::REGISTER_WALLET:
         {
            auto walletRequest = request.getRegisterWallet();
            std::string walletId{walletRequest.getWalletId()};

            auto capnAddresses = walletRequest.getAddresses();
            std::vector<Types::ScrAddr> addresses;
            addresses.reserve(capnAddresses.size());
            for (auto capnAddr : capnAddresses) {
               auto addrBody = capnAddr.getBody();
               addresses.emplace_back(
                  Types::ScrAddr{addrBody.begin(), addrBody.end()});
            }

            WalletRegistrationRequest regReq(walletId,
               addresses, walletRequest.getIsNew()
            );
            bdv->registerWallet(regReq);
            auto builder = ReplyBuilder::getNew(bdv);
            prepareReply(builder);
            return builder;
         }

         case BdvRequest::Which::UNREGISTER_WALLET:
         {
            auto wltId = std::string(request.getUnregisterWallet());
            bdv->unregisterWallet(wltId);
            break;
         }

         case BdvRequest::Which::GO_ONLINE:
         {
            bdv->startThreads();
            break;
         }


         case BdvRequest::Which::GET_TXS_BY_HASH:
         {
            auto db = bdv->getDB();
            auto txHashList = request.getGetTxsByHash();
            std::map<Types::TxKey, Tx> results;
            std::set<Types::TxHash> possibleZcHashes;
            for (auto txHash : txHashList) {
               BinaryDataRef hashBdr(txHash.begin(), txHash.end());
               try {
                  auto txKey = db->getDBKeyForHash(hashBdr);
                  auto tx = bdv->bdm()->blockchainData()->getTx(txKey);
                  results.emplace(txKey, std::move(tx));
               } catch (const std::exception&) {
                  //could not get the tx, maybe it's a zc?
                  possibleZcHashes.emplace(hashBdr);
                  continue;
               }
            }

            if (!possibleZcHashes.empty()) {
               auto mempool = bdv->zcContainer()->getSnapshot();
               for (const auto& txHash : possibleZcHashes) {
                  auto zcKey = mempool->getKeyForHash(txHash);
                  if (!Types::isTxKeyValid(zcKey)) {
                     //we don't know this txhash, ignore
                     continue;
                  }
                  auto zcTx = mempool->getTxByKey(zcKey);
                  results.emplace(zcKey, zcTx->getTxObj());
               }
            }

            auto builder = ReplyBuilder::getNew(bdv);
            auto bdvReply = prepareReply(builder);
            auto capnTxs = bdvReply.initGetTxsByHash(results.size());
            unsigned txCount = 0;
            for (const auto& tx : results) {
               auto capnTx = capnTxs[txCount++];
               capnTx.setRaw(capnp::Data::Builder(
                  (uint8_t*)tx.second.getPtr(), tx.second.getSize()
               ));
               capnTx.setKey(tx.first);
               capnTx.setIsChainedZc(tx.second.isChained());
               capnTx.setIsRbf(tx.second.isRBF());
            }
            return builder;
         }

         case BdvRequest::GET_TXS_BY_KEY:
         {
            auto txKeyList = request.getGetTxsByKey();
            std::map<Types::TxKey, Tx> results;
            std::vector<Types::TxKey> zcKeys;
            zcKeys.reserve(txKeyList.size());
            for (auto txKey : txKeyList) {
               if (Types::isThisATxIOKey(txKey)) {
                  continue;
               }
               if (!Types::isThisAZCKey(txKey)) {
                  try {
                     auto tx = bdv->bdm()->blockchainData()->getTx(txKey);
                     results.emplace(txKey, std::move(tx));
                  } catch (const std::exception&) {
                     //could not get the tx, ignore
                  }
               } else {
                  zcKeys.emplace_back(txKey);
               }
            }

            if (!zcKeys.empty()) {
               auto mempool = bdv->zcContainer()->getSnapshot();
               for (auto zcKey : zcKeys) {
                  auto tx = mempool->getTxByKey(zcKey);
                  if (tx != nullptr) {
                     results.emplace(zcKey, tx->getTxObj());
                  }
               }
            }

            auto builder = ReplyBuilder::getNew(bdv);
            auto bdvReply = prepareReply(builder);
            auto capnTxs = bdvReply.initGetTxsByKey(results.size());
            unsigned txCount = 0;
            for (const auto& tx : results) {
               auto capnTx = capnTxs[txCount++];
               capnTx.setRaw(capnp::Data::Builder(
                  (uint8_t*)tx.second.getPtr(), tx.second.getSize()
               ));
               capnTx.setKey(tx.first);
               capnTx.setIsChainedZc(tx.second.isChained());
               capnTx.setIsRbf(tx.second.isRBF());
            }
            return builder;
         }

         case BdvRequest::Which::GET_TXIOS:
         {
            auto fromHeight = request.getGetTxios();
            auto txioMap = bdv->getTxioForRange(fromHeight);
            auto zcTxioMap = bdv->getZcTxios();

            auto builder = ReplyBuilder::getNew(bdv);
            auto bdvReply = prepareReply(builder);
            auto txiosReply = bdvReply.initGetTxios(
               txioMap.size() + zcTxioMap.size());
            unsigned i=0;
            for (const auto& txioPair : txioMap) {
               auto capnTxio = txiosReply[i++];
               txioToCapn(txioPair.second, capnTxio);
            }
            for (const auto& txioPair : zcTxioMap) {
               auto capnTxio = txiosReply[i++];
               txioToCapn(*txioPair.second, capnTxio);
            }
            return builder;
         }

         default:
            auto builder = ReplyBuilder::getNew(bdv);
            prepareReply(builder);
            builder.setError("invalid bdv request");
            return builder;
      }

      return {};
   }

   ReplyBuilder parseWalletCommand(
      WalletRequest::Reader request,
      std::shared_ptr<BDV_Server_Object> bdv,
      uint32_t msgId)
   {
      auto prepareReply = [&msgId](ReplyBuilder& rp)
         ->Codec::BDV::WalletReply::Builder
      {
         auto reply = rp.builder->initRoot<Codec::BDV::Reply>();
         reply.setMsgId(msgId);
         reply.setSuccess(true);
         return reply.initWallet();
      };

      //get the wallet ptr, doubles as a sanity check
      std::string walletId(request.getWalletId());
      auto wltPtr = bdv->getWallet(walletId);
      if (wltPtr == nullptr) {
         auto builder = ReplyBuilder::getNew(bdv);
         prepareReply(builder);
         builder.setError("unknown wallet");
         return builder;
      }

      //switch on the method
      switch (request.which())
      {
         case WalletRequest::Which::UNREGISTER_ADDRESSES:
         {
            auto capnAddrs = request.getUnregisterAddresses();
            std::set<BinaryDataRef> addresses;
            for (auto capnAddr : capnAddrs) {
               auto addrBody = capnAddr.getBody();
               addresses.emplace(addrBody.begin(), addrBody.end());
            }

            //unregister the addresses
            wltPtr->unregisterAddresses(addresses);

            //push refersh notif for the wallet
            bdv->flagRefresh(BDV_registrationCompleted, walletId);
            break;
         }

         default:
            auto builder = ReplyBuilder::getNew(bdv);
            prepareReply(builder);
            builder.setError("invalid wallet request");
            return builder;
      }

      return {};
   }

   ////
   ReplyBuilder parseRequest(Request::Reader& request,
      unsigned msgId, std::shared_ptr<BDV_Server_Object> bdv)
   {
      switch (request.which())
      {
         case Request::Which::BDV:
            return parseBDVCommand(request.getBdv(), bdv,
               request.getMsgId());

         case Request::Which::WALLET:
            return parseWalletCommand(request.getWallet(), bdv,
               request.getMsgId());

         default:
            auto builder = ReplyBuilder::getNew(bdv);
            auto reply = builder.builder->initRoot<Codec::BDV::Reply>();
            reply.setMsgId(msgId);
            reply.setSuccess(false);
            reply.setError("invalid request");
            return builder;
      }
   }

   ////
   std::unique_ptr<capnp::MessageBuilder> parseStaticRequest(
      StaticRequest::Reader& request, unsigned msgId, Clients* clients,
      Types::BdvId bdvId, const btc_pubkey_& pubkey)
   {
      auto result = std::make_unique<capnp::MallocMessageBuilder>();
      auto reply = result->initRoot<Codec::BDV::Reply>();
      reply.setMsgId(msgId);
      reply.setSuccess(true);
      auto staticReply = reply.getStatic();

      switch (request.which())
      {
         case StaticRequest::Which::SHUTDOWN:
         {
            if (!WebSocketServer::isMasterKey(pubkey)) {
               //only a client that completed a 2-way AEAD handshake with
               //the peers db master key can call this method
               break;
            }

            //run shutdown sequence in its own thread so that the server listen
            //loop can exit properly.
            std::thread shutdownThr([]{ WebSocketServer::shutdown(); });
            if (shutdownThr.joinable()) {
               shutdownThr.detach();
            }
            return nullptr;
         }

         case StaticRequest::Which::SHUTDOWN_NODE:
         {
            if (!WebSocketServer::isMasterKey(pubkey)) {
               //only a client that completed a 2-way AEAD handshake with
               //the peers db master key can call this method
               break;
            }

            if (clients->bdm()->nodeRPC_ != nullptr) {
               clients->bdm()->nodeRPC_->shutdown();
            }
            break;
         }

         case StaticRequest::Which::REGISTER:
         {
            std::string magicWord(request.getMagicWord());
            if (!clients->registerBDV(magicWord, bdvId)) {
               reply.setSuccess(false);
               reply.setError("failed registration");
            } else {
               //we should NOT return the bdvId, it's the
               //lws context ptr for the connection
            }
            break;
         }

         case StaticRequest::Which::RPC_BROADCAST:
         {
            auto bdv = clients->get(bdvId);
            if (bdv == nullptr) {
               reply.setError("need bdv to broadcast");
               reply.setSuccess(false);
               break;
            }

            auto txData = request.getRpcBroadcast();
            if (txData.size() == 0) {
               reply.setError("invalid tx data");
               reply.setSuccess(false);
               break;
            }

            auto bdPtr = std::make_shared<BinaryData>(
               txData.begin(), txData.end()
            );
            RpcBroadcastPacket packet;
            packet.rawTx_ = bdPtr;
            packet.bdvPtr_ = bdv;
            clients->rpcBroadcast(packet);
            break;
         }

         case StaticRequest::Which::BROADCAST:
         {
            auto txList = request.getBroadcast();
            std::vector<BinaryDataRef> rawZcVec;
            rawZcVec.reserve(txList.size());
            for (auto txData : txList) {
               rawZcVec.emplace_back(txData.begin(), txData.end());
            }
            clients->p2pBroadcast(bdvId, rawZcVec);
            return nullptr;
         }

         case StaticRequest::Which::GET_NODE_STATUS:
         {
            auto nodeStatus = clients->bdm()->getNodeStatus();

            auto nodeReply = staticReply.initGetNodeStatus();
            nodeReply.setNode(
               (Codec::Types::NodeStatus::NodeState)nodeStatus->state);
            nodeReply.setIsSW(nodeStatus->segWitEnabled);
            nodeReply.setRpc(
               (Codec::Types::NodeStatus::RpcState)nodeStatus->rpcState);

            auto chainNotif = nodeReply.initChain();
            chainNotif.setChainState((Codec::Types::ChainStatus::ChainState)
               nodeStatus->chainStatus.state());
            chainNotif.setBlockSpeed(nodeStatus->chainStatus.getBlockSpeed());
            chainNotif.setEta(nodeStatus->chainStatus.getETA());
            chainNotif.setProgress(nodeStatus->chainStatus.getProgressPct());
            chainNotif.setBlocksLeft(nodeStatus->chainStatus.getBlocksLeft());
            break;
         }

         case StaticRequest::Which::GET_FEE_SCHEDULE:
         {
            try {
               std::string strat = request.getGetFeeSchedule();
               auto nodePtr = clients->bdm()->nodeRPC_;
               auto feeSchedule = nodePtr->getFeeSchedule(strat);
               auto capnFeeSchedule = staticReply.initGetFeeSchedule(feeSchedule.size());

               unsigned i=0;
               for (const auto& fee : feeSchedule) {
                  auto capnFee = capnFeeSchedule[i++];
                  capnFee.setTarget(fee.first);
                  capnFee.setFeeByte(fee.second.feeByte);
                  capnFee.setSmartFee(fee.second.smartFee);
               }
            } catch (const std::exception& e) {
               reply.setError(e.what());
               reply.setSuccess(false);
            }
            break;
         }

         case StaticRequest::Which::GET_HEADERS_BY_HEIGHT:
         {
            auto bcPtr = clients->bdm()->blockchain();
            if (bcPtr == nullptr) {
               reply.setSuccess(false);
               reply.setError("invalid bcPtr");
               break;
            }

            auto headersRequest = request.getGetHeadersByHeight();
            std::vector<std::shared_ptr<BlockHeader>> headers;
            headers.reserve(headersRequest.size());
            for (const auto height : headersRequest) {
               try {
                  auto header = bcPtr->getHeaderByHeight(height);
                  headers.emplace_back(std::move(header));
               } catch (const std::exception&) {
                  continue;
               }
            }

            auto capnHeaders = staticReply.initGetHeadersByHeight(headers.size());
            unsigned i = 0;
            for (const auto& header : headers) {
               auto capnHeader = capnHeaders[i++];
               const auto& thisHash = header->getThisHash();
               capnHeader.setThisHash(capnp::Data::Builder(
                  (uint8_t*)thisHash.data, 32));
               const auto& prevHash = header->getPrevHash();
               capnHeader.setPrevHash(capnp::Data::Builder(
                  (uint8_t*)prevHash.data, 32));

               capnHeader.setTimestamp(header->getTimestamp());
               capnHeader.setBlockSize(header->getBlockSize());
               capnHeader.setNumTxs(header->getNumTx());
               capnHeader.setHeight(header->getBlockHeight());
               capnHeader.setBlockId(header->getUniqueID());
               capnHeader.setMainBranch(header->isMainBranch());
            }
            break;
         }

         case StaticRequest::Which::GET_HEADERS_BY_ID:
         {
            auto bcPtr = clients->bdm()->blockchain();
            if (bcPtr == nullptr) {
               reply.setSuccess(false);
               reply.setError("invalid bcPtr");
               break;
            }

            auto headersRequest = request.getGetHeadersById();
            std::vector<std::shared_ptr<BlockHeader>> headers;
            headers.reserve(headersRequest.size());
            for (const auto blockId : headersRequest) {
               try {
                  auto header = bcPtr->getHeaderById(blockId);
                  headers.emplace_back(std::move(header));
               } catch (const std::exception&) {
                  continue;
               }
            }

            auto capnHeaders = staticReply.initGetHeadersById(headers.size());
            unsigned i = 0;
            for (const auto& header : headers) {
               auto capnHeader = capnHeaders[i++];
               const auto& thisHash = header->getThisHash();
               capnHeader.setThisHash(capnp::Data::Builder(
                  (uint8_t*)thisHash.data, 32));
               const auto& prevHash = header->getPrevHash();
               capnHeader.setPrevHash(capnp::Data::Builder(
                  (uint8_t*)prevHash.data, 32));

               capnHeader.setTimestamp(header->getTimestamp());
               capnHeader.setBlockSize(header->getBlockSize());
               capnHeader.setNumTxs(header->getNumTx());
               capnHeader.setHeight(header->getBlockHeight());
               capnHeader.setBlockId(header->getUniqueID());
               capnHeader.setMainBranch(header->isMainBranch());
            }
            break;
         }

         case StaticRequest::Which::GET_TOP_BLOCK_HEIGHT:
         {
            auto bcPtr = clients->bdm()->blockchain();
            if (bcPtr != nullptr) {
               auto top = bcPtr->top();
               staticReply.setGetTopBlockHeight(top->getBlockHeight());
            } else {
               reply.setSuccess(false);
               reply.setError("invalid bcPtr");
            }
            break;
         }

         default:
            std::cout << "request.which(): " << request.which() << std::endl;
            reply.setError("invalid static request");
            reply.setSuccess(false);
      }

      return result;
   }
}

///////////////////////////////////////////////////////////////////////////////
//BDV_Payload
BDV_Payload::BDV_Payload(BinaryData packet, BdvPtr bdv,
   Types::BdvId id, const btc_pubkey_& key) :
   packetData_(std::move(packet)), bdvPtr_(bdv), bdvID_(id), pubkey_(key)
{}

///////////////////////////////////////////////////////////////////////////////
uint32_t BDV_Payload::getMessageID() const
{
   if (messageID_ == UINT32_MAX) {
      throw std::runtime_error("messageID is unset");
   }
   return messageID_;
}

////
void BDV_Payload::setMessageID(uint32_t msgId)
{
   if (messageID_ != UINT32_MAX) {
      throw std::runtime_error("messageID is already set");
   }
   messageID_ = msgId;
}

///////////////////////////////////////////////////////////////////////////////
uint64_t BDV_Payload::getBdvID() const
{
   return bdvID_;
}

////
const btc_pubkey_& BDV_Payload::getPubkey() const
{
   return pubkey_;
}

///////////////////////////////////////////////////////////////////////////////
const BinaryData& BDV_Payload::getData() const
{
   return packetData_;
}

BinaryData&& BDV_Payload::moveData()
{
   return std::move(packetData_);
}

///////////////////////////////////////////////////////////////////////////////
BdvPtr BDV_Payload::getBdvPtr() const
{
   return bdvPtr_;
}

////
BdvPtr&& BDV_Payload::moveBdvPtr()
{
   return std::move(bdvPtr_);
}

///////////////////////////////////////////////////////////////////////////////
//BDV_Server_Object
void BDV_Server_Object::setup()
{
   started_.store(0, std::memory_order_relaxed);
   packetProcess_threadLock_.store(0, std::memory_order_relaxed);
   notificationProcess_threadLock_.store(0, std::memory_order_relaxed);

   isReadyPromise_ = std::make_shared<std::promise<bool>>();
   isReadyFuture_ = isReadyPromise_->get_future();
   auto lbdFut = isReadyFuture_;

   //unsafe, should consider creating the blockchain object as a shared_ptr
   auto bc = &blockchain();
   auto isReadyLambda = [lbdFut, bc]()->unsigned
   {
      if (lbdFut.wait_for(0s) == std::future_status::ready) {
         return bc->top()->getBlockHeight();
      }
      return UINT32_MAX;
   };

   switch (Armory::Config::DBSettings::getServiceType())
   {
      case SERVICE_WEBSOCKET:
      case SERVICE_UNITTEST_WITHWS:
      {
         notifications_ = std::make_unique<WS_Callback>(getID());
         break;
      }

      case SERVICE_UNITTEST:
         notifications_ = std::make_unique<UnitTest_Callback>();
         break;

      default:
         throw std::runtime_error("unexpected service type");
   }
}

std::vector<uint8_t>& BDV_Server_Object::getScratchPad()
{
   if (scratchPad_.empty()) {
      scratchPad_.resize(SCRATCHPAD_SIZE);
   }
   return scratchPad_;
}

///////////////////////////////////////////////////////////////////////////////
BDV_Server_Object::BDV_Server_Object(
   Types::BdvId id, std::shared_ptr<BlockDataManager> bdm) :
   BlockDataViewer(bdm), bdvID_(id)
{
   setup();
}

BDV_Server_Object::~BDV_Server_Object()
{
   haltThreads();
}

Types::BdvId BDV_Server_Object::getID() const
{
   return bdvID_;
}

///////////////////////////////////////////////////////////////////////////////
void BDV_Server_Object::startThreads()
{
   if (started_.fetch_or(1, std::memory_order_relaxed) != 0) {
      return;
   }
   initT_ = std::thread([this]{ this->init(); });
}

///////////////////////////////////////////////////////////////////////////////
void BDV_Server_Object::haltThreads()
{
   if(notifications_ != nullptr) {
      notifications_->shutdown();
   }
   if (initT_.joinable()) {
      initT_.join();
   }
}

///////////////////////////////////////////////////////////////////////////////
void BDV_Server_Object::init()
{
   bdm_->blockUntilReady();
   while (true) {
      //grab all pending wallet registration requests
      std::unique_lock<std::mutex> lock(registerWalletMutex_);
      if (walletRegistrationQueue_.empty()) {
         break;
      }
      auto regQueue = std::move(walletRegistrationQueue_);
      lock.unlock();

      //gather all addresses across each request
      std::vector<Types::ScrAddr> addresses;
      std::vector<std::string> walletIds;
      bool isNew = false;
      for (const auto& regReq : regQueue) {
         addresses.insert(addresses.end(),
            regReq.addresses.begin(), regReq.addresses.end());
         walletIds.emplace_back(regReq.walletId);
         isNew |= regReq.isNew;
      }

      //finality callback
      auto prom = std::make_shared<std::promise<bool>>();
      auto fut = prom->get_future();
      auto callback = [promPtr=prom] (bool success)
      { promPtr->set_value(success); };

      //push one big batch to scraddr filter
      auto batch = std::make_shared<RegistrationBatch>(
         walletIds, std::move(addresses), isNew, callback);
      saf_->pushAddressBatch(batch);

      //wait on registration callback
      auto result = fut.get();
      if (!result) {
         //TODO: should notify client and disconnect it
         LOGERR << "failed to register addresses for bdv!";
         isReadyPromise_->set_value(false);
         return;
      }

      //addresses are now registered, populate the wallet maps
      auto prom2 = std::make_shared<std::promise<bool>>();
      auto fut2 = prom2->get_future();
      size_t count = 0; bool success = true;
      auto regCallback = [prom2, &count, &success, total=regQueue.size()](bool s)
      {
         success |= s;
         if (++count == total) {
            prom2->set_value(success);
         }
      };
      for (const auto& regRequest : regQueue) {
         registerAWallet(regRequest, regCallback);
      }

      //wait on address population process
      //TODO: notify on failure
      fut2.get();
   }

   //mark bdv object as ready
   isReadyPromise_->set_value(true);

   //callback client with BDM_Ready packet
   auto& scratchPad = getScratchPad();
   kj::ArrayPtr<capnp::word> arrayPtr(
      reinterpret_cast<capnp::word*>(scratchPad.data()),
      SCRATCHPAD_SIZE / sizeof(capnp::word)
   );
   capnp::MallocMessageBuilder message(arrayPtr,
      capnp::AllocationStrategy::FIXED_SIZE);

   auto notifs = message.initRoot<Codec::BDV::Notifications>();
   auto notifList = notifs.initNotifs(1);
   auto notif = notifList[0];
   auto readyNotif = notif.initReady();
   readyNotif.setHeight(blockchain().top()->getBlockHeight());
   readyNotif.setBranchHeight(UINT32_MAX);

   //we expect this message to be smaller than our scratchpad
   auto flat = capnp::messageToFlatArray(message);
   auto bytes = flat.asBytes();
   std::vector<uint8_t> replyRaw(bytes.begin(), bytes.end());
   notifications_->push(std::make_unique<Network::WritePayload_Raw>(replyRaw));
}

///////////////////////////////////////////////////////////////////////////////
void BDV_Server_Object::processNotification(
   std::shared_ptr<BDV_Notification> notifPtr)
{
   auto action = notifPtr->actionType();
   if (action < BDV_Progress) {
      //skip all but progress notifications if BDV isn't ready
      if (isReadyFuture_.wait_for(0s) != std::future_status::ready) {
         return;
      }
   }

   std::vector<uint8_t> firstSegment(SCRATCHPAD_SIZE);
   kj::ArrayPtr<capnp::word> arrayPtr(
      reinterpret_cast<capnp::word*>(firstSegment.data()),
      SCRATCHPAD_SIZE / sizeof(capnp::word)
   );
   auto message = std::make_unique<capnp::MallocMessageBuilder>(
      arrayPtr, capnp::AllocationStrategy::FIXED_SIZE);
   auto notifs = message->initRoot<Codec::BDV::Notifications>();

   switch (action)
   {
      case BDV_NewBlock:
      {
         auto payload =
            std::dynamic_pointer_cast<BDV_Notification_NewBlock>(notifPtr);

         //init notif builder
         bool haveZcs = false;
         if (payload->zcPurgePacket != nullptr &&
            !payload->zcPurgePacket->invalidatedZcKeys.empty()) {
            notifs.initNotifs(2);
            haveZcs = true;
         } else {
            notifs.initNotifs(1);
         }

         //new block height
         auto notifList = notifs.getNotifs();
         auto heightNotif = notifList[0];
         auto blockData = heightNotif.initNewBlock();

         blockData.setHeight(payload->reorgState.newTop->getBlockHeight());
         if (!payload->reorgState.prevTopStillValid) {
            blockData.setBranchHeight(
               payload->reorgState.reorgBranchPoint->getBlockHeight());

            //for reorgs, we have to provide the set of blocks ids
            //that were moved on and off branch
            unsigned idCount = 0;
            auto invalidIds = blockData.initInvalidatedIds(
               payload->reorgState.invalidatedBlockIds.size());
            for (auto invalidId : payload->reorgState.invalidatedBlockIds) {
               invalidIds.set(idCount++, invalidId);
            }

            idCount = 0;
            auto validIds = blockData.initNewMainBranchIds(
               payload->reorgState.newMainBranchIds.size());
            for (auto validId : payload->reorgState.newMainBranchIds) {
               validIds.set(idCount++, validId);
            }
         } else {
            blockData.setBranchHeight(UINT32_MAX);
         }

         //invalidated zc ids
         if (haveZcs) {
            auto zcNotif = notifList[1];
            auto zcIdList = zcNotif.initInvalidatedZc(
               payload->zcPurgePacket->invalidatedZcKeys.size());

            unsigned i=0;
            for (const auto& zcId : payload->zcPurgePacket->invalidatedZcKeys) {
               zcIdList.set(i++, capnp::Data::Builder(
                  (uint8_t*)zcId.second.getPtr(), zcId.second.getSize()));
            }
         }

         break;
      }

      case BDV_Refresh:
      {
         auto notifList = notifs.initNotifs(1);
         auto notif = notifList[0];
         auto refreshNotif = notif.initRefresh();

         auto payload =
            std::dynamic_pointer_cast<BDV_Notification_Refresh>(notifPtr);
         refreshNotif.setType((uint32_t)payload->refresh);
         auto refreshIds = refreshNotif.initIds(1);
         refreshIds.set(0, payload->refreshID);
         break;
      }

      case BDV_ZC:
      {
         unsigned notifCount = 1;
         auto payload = std::dynamic_pointer_cast<BDV_Notification_ZC>(notifPtr);
         if (payload->packet->purgePacket != nullptr &&
            !payload->packet->purgePacket->invalidatedZcKeys.empty()) {
            notifCount = 2;
         }

         //new zc txios
         auto notifList = notifs.initNotifs(notifCount);
         auto notif = notifList[0];
         auto zcNotif = notif.initZc(payload->txios.size());
         unsigned i = 0;
         for (const auto& txio : payload->txios) {
            auto capnTxio = zcNotif[i++];
            txioToCapn(*txio, capnTxio);
         }

         if (notifCount == 2) {
            //invalidated zc hashes
            const auto& invalidatedHashes =
               payload->packet->purgePacket->invalidatedZcKeys;
            auto capnNotif = notifList[1];
            auto invalNotif = capnNotif.initInvalidatedZc(
               invalidatedHashes.size());

            unsigned i=0;
            for (const auto& zcHash : invalidatedHashes) {
               invalNotif.set(i++, capnp::Data::Builder(
                  (uint8_t*)zcHash.second.getPtr(), zcHash.second.getSize()
               ));
            }
         }
         break;
      }

      case BDV_Progress:
      {
         auto notifList = notifs.initNotifs(1);
         auto notif = notifList[0];
         auto progressNotif = notif.initProgress();

         auto payload =
            std::dynamic_pointer_cast<BDV_Notification_Progress>(notifPtr);

         progressNotif.setPhase((uint32_t)payload->phase);
         progressNotif.setProgress(payload->progress);
         progressNotif.setTime(payload->time);
         progressNotif.setNumericProgress(payload->numericProgress);

         if (payload->walletIDs.empty()) {
            break;
         }
         auto progressIds = progressNotif.initIds(payload->walletIDs.size());
         unsigned i=0;
         for (const auto& id : payload->walletIDs) {
            progressIds.set(i++, id);
         }
         break;
      }

      case BDV_NodeStatus:
      {
         auto notifList = notifs.initNotifs(1);
         auto notif = notifList[0];
         auto nodeNotif = notif.initNodeStatus();

         auto payload =
            std::dynamic_pointer_cast<BDV_Notification_NodeStatus>(notifPtr);
         auto& nodeStatus = payload->status;

         nodeNotif.setNode((Codec::Types::NodeStatus::NodeState)nodeStatus->state);
         nodeNotif.setIsSW(nodeStatus->segWitEnabled);
         nodeNotif.setRpc((Codec::Types::NodeStatus::RpcState)nodeStatus->rpcState);

         auto chainNotif = nodeNotif.getChain();
         chainNotif.setChainState((Codec::Types::ChainStatus::ChainState)
            nodeStatus->chainStatus.state());
         chainNotif.setBlockSpeed(nodeStatus->chainStatus.getBlockSpeed());
         chainNotif.setEta(nodeStatus->chainStatus.getETA());
         chainNotif.setProgress(nodeStatus->chainStatus.getProgressPct());
         chainNotif.setBlocksLeft(nodeStatus->chainStatus.getBlocksLeft());
         break;
      }

      case BDV_Action::BDV_Error:
      {
         auto notifList = notifs.initNotifs(1);
         auto notif = notifList[0];
         auto errorNotif = notif.initError();

         auto payload =
            std::dynamic_pointer_cast<BDV_Notification_Error>(notifPtr);

         errorNotif.setCode(payload->errStruct.errCode_);
         if (!payload->errStruct.errData_.empty()) {
            errorNotif.setErrData(capnp::Data::Builder(
               (uint8_t*)payload->errStruct.errData_.getPtr(),
               payload->errStruct.errData_.getSize()
            ));
         }

         if (!payload->errStruct.errorStr_.empty()) {
            errorNotif.setErrStr(payload->errStruct.errorStr_);
         }
         break;
      }

      default:
         return;
   }

   notifications_->push(
      std::make_unique<Network::WritePayload_Capnp>(
         std::move(message), std::move(firstSegment)));
}

///////////////////////////////////////////////////////////////////////////////
void BDV_Server_Object::registerWallet(WalletRegistrationRequest& regReq)
{
   if (isReadyFuture_.wait_for(0s) != std::future_status::ready) {
      //the bdv maintenance thread hasn't started yet, queue the request
      std::unique_lock<std::mutex> lock(registerWalletMutex_);
      walletRegistrationQueue_.emplace_back(std::move(regReq));
      return;
   }

   //TODO: notify on failed registration too
   registerAWallet(regReq, [this, wId=regReq.walletId](bool success){
      if (success) { flagRefresh(BDV_registrationCompleted, wId); }
   });
}

////////////////////////////////////////////////////////////////////////////////
void BDV_Server_Object::flagRefresh(BDV_refresh refresh,
   const std::string& refreshID)
{
   auto notif = std::make_unique<BDV_Notification_Refresh>(
      getID(), refresh, refreshID);

   if (notifLambda_) {
      notifLambda_(std::move(notif));
   }
}

////////////////////////////////////////////////////////////////////////////////
Network::WebSocketMessagePartial BDV_Server_Object::preparePayload(
   std::shared_ptr<BDV_Payload> packet)
{
   /*
   Only ever one thread gets this far at any given time, therefor none of the
   underlying objects need to be thread safe
   */

   if (packet == nullptr) {
      LOGWARN << "null packet";
      return {};
   }

   auto nextId = lastValidMessageId_ + 1;
   if (!packet->getData().empty()) {
      //grab and check the packet's message id
      auto msgId = Network::WebSocketMessagePartial::readMessageId(
         packet->getData());
      if (msgId != UINT32_MAX) {
         //get the PartialMessage object for this id
         auto msgIter = messageMap_.find(msgId);
         if (msgIter == messageMap_.end()) {
            //create this PartialMessage if it's missing
            msgIter = messageMap_.emplace(
               msgId, Network::WebSocketMessagePartial()).first;
         }
         auto& msgRef = msgIter->second;

         //try to reconstruct the message
         auto packetData = packet->moveData();
         auto parsed = msgRef.parsePacket(packetData);
         if (!parsed) {
            //failed to reconstruct from this packet, this
            //shouldn't happen anymore
            LOGWARN << "failed to parse packet, reinjecting. " <<
               "!This shouldn't happen anymore!";

            return {};
         }

         //some verbose, this can be removed later
         if (msgIter->second.isReady()) {
            if (msgId >= lastValidMessageId_ + 10) {
               LOGWARN << "completed a message that exceeds the counter by " <<
                  msgId - lastValidMessageId_;
            }

            if (msgId != nextId) {
               return {};
            }
         } else {
            return {};
         }
      }
   }

   //grab the expected next message
   auto msgIter = messageMap_.find(nextId);

   //exit if we dont have this message id
   if (msgIter == messageMap_.end()) {
      return {};
   }

   //or the message isn't complete
   if (!msgIter->second.isReady()) {
      return {};
   }

   //move in the completed message, it now lives within this scope
   auto msgObj = std::move(msgIter->second);

   //clean up from message map
   messageMap_.erase(msgIter);

   //update ids
   lastValidMessageId_ = nextId;
   packet->setMessageID(nextId);

   //return the message to be processed
   return msgObj;
}

///////////////////////////////////////////////////////////////////////////////
std::unique_ptr<BDV_Notification_ZC> BDV_Server_Object::createZcNotification(
   const std::set<Types::ScrAddr>& addrSet)
{
   auto packet = std::make_shared<ZeroConf::ZcNotificationPacket>(getID());

   //grab zc map
   auto ss = zeroConfCont_->getSnapshot();
   std::vector<std::shared_ptr<const TxIOPair>> txios;
   if (ss != nullptr) {
      for (auto& addr : addrSet) {
         auto txioMap = ss->getTxioMapForScrAddr(addr);
         auto iter = packet->scrAddrToTxioKeys.emplace(
            addr, std::set<Types::TxIOKey>{});

         for (const auto& txioPair : txioMap) {
            iter.first->second.emplace(txioPair.first);
            txios.emplace_back(txioPair.second);
         }
      }
   }

   packet->ssPtr = ss;
   return std::make_unique<BDV_Notification_ZC>(packet, std::move(txios));
}

///////////////////////////////////////////////////////////////////////////////
// Clients
Clients::Clients(std::shared_ptr<BlockDataManager> bdm) :
   bdm_(bdm)
{
   masterIsConnected_.store(false, std::memory_order_relaxed);
}

///////////////////////////////////////////////////////////////////////////////
std::shared_ptr<BlockDataManager> Clients::bdm() const
{
   return bdm_;
}

///////////////////////////////////////////////////////////////////////////////
void Clients::init()
{
   run_.store(true, std::memory_order_relaxed);

   auto mainthread = [this](void)->void {
      notificationThread();
   };

   auto outerthread = [this](void)->void {
      bdvMaintenanceLoop();
   };

   auto innerthread = [this](void)->void {
      bdvMaintenanceThread();
   };

   auto parserThread = [this](void)->void {
      this->messageParserThread();
   };

   auto unregistrationThread = [this](void)->void {
      this->unregisterBDVThread();
   };

   auto rpcThread = [this](void)->void {
      this->broadcastThroughRPC();
   };

   controlThreads_.push_back(std::thread(mainthread));
   controlThreads_.push_back(std::thread(outerthread));
   controlThreads_.push_back(std::thread(rpcThread));
   unregThread_ = std::thread(unregistrationThread);

   unsigned innerThreadCount = 2;
   if (Config::DBSettings::getDbType() == ARMORY_DB_TYPE::Super &&
      Config::DBSettings::getServiceType() != SERVICE_UNITTEST) {
      innerThreadCount = std::thread::hardware_concurrency();
   }
   for (unsigned i = 0; i < innerThreadCount; i++) {
      controlThreads_.push_back(std::thread(innerthread));
      controlThreads_.push_back(std::thread(parserThread));
   }

   auto callbackPtr = std::make_unique<ZeroConf::ZeroConfCallbacks_BDV>(this);
   bdm_->registerZcCallbacks(std::move(callbackPtr));

   if (Config::NetworkSettings::ephemeralPeers()) {
      //shutdown within 5sec of starting an ephemeral db if the master client
      //has yet to connect
      auto masterCheckThr = std::thread([this](){
         std::this_thread::sleep_for(5s);
         if (masterIsConnected_.load(std::memory_order_relaxed) == false) {
            LOGERR << "master client did not connect within imparted time, exiting";
            WebSocketServer::shutdown();
         }
      });
      if (masterCheckThr.joinable()) {
         masterCheckThr.detach();
      }
   }
}

///////////////////////////////////////////////////////////////////////////////
std::shared_ptr<BDV_Server_Object> Clients::get(Types::BdvId id) const
{
   return BDVs_.get(id);
}

///////////////////////////////////////////////////////////////////////////////
void Clients::bdvMaintenanceLoop()
{
   while (true) {
      std::shared_ptr<BDV_Notification> notifPtr;
      try {
         notifPtr = std::move(outerBDVNotifStack_.pop_front());
      } catch (const Threading::StopBlockingLoop&) {
         LOGINFO << "Exiting BDV event loop";
         break;
      }

      if (notifPtr->broadcast()) {
         const auto& bdvs = BDVs_.get();
         for (const auto& bdv_pair : bdvs) {
            auto notifPacket = std::make_shared<BDV_Notification_Packet>();
            notifPacket->bdvPtr = bdv_pair.second;
            notifPacket->notifPtr = notifPtr;
            innerBDVNotifStack_.push_back(std::move(notifPacket));
         }
      } else {
         //grab bdv
         auto bdvPtr = BDVs_.get(notifPtr->bdvID());
         auto notifPacket = std::make_shared<BDV_Notification_Packet>();
         notifPacket->bdvPtr = bdvPtr;
         notifPacket->notifPtr = notifPtr;
         innerBDVNotifStack_.push_back(std::move(notifPacket));
      }
   }
}

///////////////////////////////////////////////////////////////////////////////
void Clients::bdvMaintenanceThread()
{
   while (true) {
      std::shared_ptr<BDV_Notification_Packet> notifPtr;
      try {
         notifPtr = std::move(innerBDVNotifStack_.pop_front());
      } catch (const Threading::StopBlockingLoop&) {
         break;
      }

      if (notifPtr->bdvPtr == nullptr) {
         LOGWARN << "null bdvPtr in notification";
         continue;
      }

      auto bdvPtr = notifPtr->bdvPtr;
      unsigned zero = 0;
      if (!bdvPtr->notificationProcess_threadLock_.compare_exchange_weak(
         zero, 1)) {
         //Failed to grab lock, there's already a thread processing a payload
         //for this bdv. Insert the payload back into the queue. Another 
         //thread will eventually pick it up and successfully grab the lock 
         if (notifPtr == nullptr) {
            LOGERR << "!!!!!! empty notif at reinsertion";
         }

         innerBDVNotifStack_.push_back(std::move(notifPtr));
         continue;
      }

      bdvPtr->processNotification(notifPtr->notifPtr);
      bdvPtr->notificationProcess_threadLock_.store(0);
   }
}

///////////////////////////////////////////////////////////////////////////////
void Clients::shutdown()
{
   /*shutdown sequence*/
   std::unique_lock<std::mutex> lock(shutdownMutex_);
   if (!run_.load(std::memory_order_relaxed)) {
      return;
   }

   //prevent all new commands from running
   run_.store(false, std::memory_order_relaxed);

   //shutdown rpc write queue
   rpcBroadcastQueue_.terminate();

   //shutdown Clients gc thread
   gcCommands_.completed();

   //shutdown unregistration thread and wait on it
   unregBDVQueue_.terminate();
   if (unregThread_.joinable()) {
      unregThread_.join();
   }

   //cleanup all BDVs
   unregisterAllBDVs();

   //shutdown maintenance threads
   outerBDVNotifStack_.completed();
   innerBDVNotifStack_.completed();
   packetQueue_.terminate();

   //exit BDM maintenance thread
   bdm_->shutdown();
   for (auto& thr : controlThreads_) {
      if (thr.joinable()) {
         thr.join();
      }
   }
}

void Clients::setMasterIsConnected(bool isConnected)
{
   if (isConnected) {
      masterIsConnected_.store(true, std::memory_order_relaxed);
   } else if (masterIsConnected_.load(std::memory_order_relaxed) == true &&
      Config::NetworkSettings::ephemeralPeers()) {
      //master disconnected from ephemeral db, time to shut it down
      masterIsConnected_.store(false, std::memory_order_relaxed);
      std::thread shutdownThr([]{ WebSocketServer::shutdown(); });
      if (shutdownThr.joinable()) {
         shutdownThr.detach();
      }
   }
}

///////////////////////////////////////////////////////////////////////////////
void Clients::unregisterAllBDVs()
{
   std::unique_lock<std::mutex> lock(BDVs_.mu);
   for (auto& bdv : BDVs_.bdvs) {
      bdv.second->haltThreads();
   }
   BDVs_.bdvs.clear();
}

///////////////////////////////////////////////////////////////////////////////
bool Clients::registerBDV(const std::string& magicWord, Types::BdvId bdvId)
{
   if (magicWord.empty() || bdvId == BDV_NOTIF_BROADCAST) {
      return false;
   }
   auto thisMagicWord =
      Config::BitcoinSettings::getMagicBytes().toHexStr();
   if (thisMagicWord != magicWord) {
      return false;
   }

   auto newBDV = std::make_shared<BDV_Server_Object>(bdvId, bdm_);
   auto notiflbd = [this](std::unique_ptr<BDV_Notification> notifPtr)
   {
      this->outerBDVNotifStack_.push_back(std::move(notifPtr));
   };
   newBDV->notifLambda_ = notiflbd;

   //add to BDVs map
   BDVs_.add(newBDV);
   LOGINFO << "registered bdv: " << bdvId;
   return true;
}

///////////////////////////////////////////////////////////////////////////////
void Clients::unregisterBDV(Types::BdvId bdvId)
{
   unregBDVQueue_.push_back(std::move(bdvId));
}

///////////////////////////////////////////////////////////////////////////////
void Clients::unregisterBDVThread()
{
   while (true) {
      //grab bdv id
      Types::BdvId bdvId;
      try {
         bdvId = std::move(unregBDVQueue_.pop_front());
      } catch(const Threading::StopBlockingLoop&) {
         break;
      }

      //grab bdv ptr
      std::shared_ptr<BDV_Server_Object> bdvPtr;
      {
         std::unique_lock<std::mutex> lock(BDVs_.mu);
         auto bdvIter = BDVs_.bdvs.find(bdvId);
         if (bdvIter == BDVs_.bdvs.end()) {
            return;
         }

         //copy shared_ptr and erase from bdv map
         bdvPtr = bdvIter->second;
         BDVs_.bdvs.erase(bdvId);
      }

      if (bdvPtr == nullptr) {
         LOGERR << "empty bdv ptr before unregistration";
         return;
      }

      //shutdown bdv threads
      bdvPtr->haltThreads();

      //done
      bdvPtr.reset();
      LOGINFO << "unregistered bdv: " << bdvId;
   }
}

///////////////////////////////////////////////////////////////////////////////
void Clients::notificationThread()
{
   if (bdm_ == nullptr) {
      throw std::runtime_error("BDM is null!");
   }

   while (true) {
      bool timedout = true;
      std::shared_ptr<BDV_Notification> notifPtr;
      try {
         notifPtr = std::move(bdm_->notificationStack_.pop_front(60s));
         timedout = false;
      } catch (const Threading::StackTimedOutException&) {
         //nothing to do
      } catch (const Threading::StopBlockingLoop&) {
         return;
      } catch (const Threading::IsEmpty&) {
         LOGERR << "caught isEmpty in Clients maintenance loop";
         continue;
      }

      //trigger gc thread
      if (timedout == true || notifPtr->actionType() != BDV_Progress) {
         gcCommands_.push_back(true);
      }

      //grab notif type and move it to bdv notif queue
      if (notifPtr == nullptr) {
         continue;
      }
      auto aType = notifPtr->actionType();
      auto fatal = notifPtr->fatal();
      outerBDVNotifStack_.push_back(std::move(notifPtr));

      //is this a fatal error notif?
      if (aType == BDV_Error && fatal) {
         std::thread shutdownThr([this]{ this->shutdown(); });
         shutdownThr.detach();
         return;
      }
   }
}

///////////////////////////////////////////////////////////////////////////////
void Clients::parseStandAlonePayload(std::shared_ptr<BDV_Payload> payloadPtr)
{
   Network::WebSocketMessagePartial msg;
   auto packetData = payloadPtr->moveData();
   if (!msg.parsePacket(packetData)) {
      //we only allow single packet payloads in here
      return;
   }
   if (!msg.isReady()) {
      return;
   }

   auto msgReader = msg.getReader();
   auto capnReader = msgReader->getReader();
   try {
      auto request = capnReader->getRoot<Codec::BDV::Request>();
      if (!request.isStatic()) {
         //we only allow static requests in here
         return;
      }

      auto staticRequest = request.getStatic();
      auto builderPtr = parseStaticRequest(
         staticRequest, request.getMsgId(), this,
         payloadPtr->getBdvID(), payloadPtr->getPubkey());
      if (builderPtr != nullptr) {
         WebSocketServer::write(payloadPtr->getBdvID(), 0,
            std::make_unique<Network::WritePayload_Capnp>(
               std::move(builderPtr), std::vector<uint8_t>{})
         );
      }
   } catch (const std::runtime_error&) {}
}

///////////////////////////////////////////////////////////////////////////////
void Clients::messageParserThread()
{
   while (true) {
      std::shared_ptr<BDV_Payload> payloadPtr;
      try {
         payloadPtr = std::move(packetQueue_.pop_front());
      } catch (const Threading::StopBlockingLoop&) {
         break;
      }

      //sanity check
      if (payloadPtr == nullptr) {
         LOGERR << "????????? empty payload";
         continue;
      }

      auto bdvPtr = payloadPtr->getBdvPtr();
      if (bdvPtr == nullptr) {
         //no bdv, is this a static command?
         parseStandAlonePayload(payloadPtr);
         continue;
      }

      unsigned zero = 0;
      if (bdvPtr && !bdvPtr->packetProcess_threadLock_.compare_exchange_weak(
         zero, 1, std::memory_order_relaxed, std::memory_order_relaxed)) {
         //Failed to grab lock, there's already a thread processing a payload
         //for this bdv. Insert the payload back into the queue. Another 
         //thread will eventually pick it up and successfully grab the lock 
         if (payloadPtr == nullptr) {
            LOGERR << "!!!!!! empty payload at reinsertion";
         }

         packetQueue_.push_back(std::move(payloadPtr));
         continue;
      }

      /*
      Grabbed the thread lock, time to process the payload.

      However, since the thread lock is only a spin lock with loose ordering
      semantics (for speed), we need the current thread to be up to date with
      all changes previous threads have made to this bdv object, hence acquiring
      the object's process mutex
      */

      std::unique_lock<std::mutex> lock(bdvPtr->processPacketMutex_);
      auto result = processCommand(payloadPtr);

      //check if the map has the next message
      {
         auto msgIter = bdvPtr->messageMap_.find(
            bdvPtr->lastValidMessageId_ + 1);
         
         if (msgIter != bdvPtr->messageMap_.end() &&
            msgIter->second.isReady()) {
            /*
            We have the next message and it is ready, push a packet
            with no data on the queue to assign this bdv a new processing
            thread.

            This is done because we don't want one bdv to hog a thread
            constantly if it has a lot of queue up messages. It should
            complete for a thread like all other bdv objects, regardless
            of the its message queue depth.
            */
            auto flagPacket = std::make_shared<BDV_Payload>(
               BinaryData{}, bdvPtr, payloadPtr->getBdvID(),
               payloadPtr->getPubkey());
            packetQueue_.push_back(std::move(flagPacket));
         }
      }
      //release the locks
      lock.unlock();
      bdvPtr->packetProcess_threadLock_.store(0);

      //write return value if any
      if (result != nullptr) {
         WebSocketServer::write(
            payloadPtr->getBdvID(), payloadPtr->getMessageID(),
            std::move(result)
         );
      }
   }
}

///////////////////////////////////////////////////////////////////////////////
void Clients::broadcastThroughRPC()
{
   auto notifyError = [this](
      const BinaryData& hash, std::shared_ptr<BDV_Server_Object> bdvPtr,
      int errCode, const std::string& verbose)->void
   {
      auto notifPacket = std::make_shared<BDV_Notification_Packet>();
      notifPacket->bdvPtr = bdvPtr;
      notifPacket->notifPtr = std::make_shared<BDV_Notification_Error>(
         bdvPtr->getID(), errCode, hash, verbose);
      innerBDVNotifStack_.push_back(std::move(notifPacket));
   };

   while (true) {
      RpcBroadcastPacket packet;
      try {
         packet = std::move(rpcBroadcastQueue_.pop_front());
      }
      catch (const Threading::StopBlockingLoop&) {
         break;
      }

      //create & set a zc batch for this tx
      Tx tx(*packet.rawTx_);
      std::vector<BinaryData> hashes = { tx.getThisHash() };
      auto zcPtr = bdm_->zeroConfCont();

      //feed the watcher map with all relevant bdv ids
      {
         //if this is a RPC fallback from a timed out P2P zc push
         //we may have extra requestors attached to this broadcast
         std::set<Types::BdvId> extraRequestors;
         for (const auto& exReq : packet.extraRequestors_) {
            extraRequestors.emplace(exReq->getID());
         }

         if (!zcPtr->insertWatcherEntry(
            *hashes.begin(), packet.rawTx_, //tx
            packet.bdvPtr_->getID(),
            extraRequestors, //extra requestor, in case this is a fallback
            false)) { //do not process watcher node invs for this entry
            //there is already a watcher entry for this tx, our request has been 
            //attached to it, skip the RPC broadcast
            continue;
         }
      }

      auto batchPtr = zcPtr->initiateZcBatch(
         hashes,
         0, //no timeout, this batch promise has to be set to progress
         nullptr, //no error callback
         true,
         packet.bdvPtr_->getID()
      );

      //push to rpc
      std::string verbose;
      auto result = bdm_->nodeRPC_->broadcastTx(
         packet.rawTx_->getRef(), verbose);
      switch (ArmoryErrorCodes(result))
      {
         case ArmoryErrorCodes::Success:
         {
            /*
            RPC zc broadcast will return success whether the tx was in 
            the node's mempool or not.
            */

            //fulfill the batch to parse the tx
            try {
               //set the tx body and batch promise
               auto txPtr = batchPtr->zcMap.begin()->second;
               txPtr->setTx(*packet.rawTx_, time(0));
               batchPtr->isReadyPromise->set_value(ArmoryErrorCodes::Success);
            } catch (const std::future_error&) {
               LOGWARN << "rpc broadcast promise was already set";
            }

            //signal all extra requestors for an already-in-mempool error
            for (auto& requestor : packet.extraRequestors_) {
               notifyError(*hashes.begin(), requestor,
                  (int)ArmoryErrorCodes::ZcBroadcast_AlreadyInMempool,
                  "Extra requestor RPC broadcast error: Already in mempool");
            }

            LOGINFO << "rpc broadcast success";
            break;
         }

         default:
            LOGINFO << "RPC broadcast for tx: " << hashes.begin()->toHexStr() <<
               ", verbose: " << verbose;

            //cleanup watcher map
            auto watcherEntry = zcPtr->eraseWatcherEntry(*hashes.begin());
            if (watcherEntry != nullptr) {
               if (!watcherEntry->extraRequestors.empty()) {
                  std::unique_lock<std::mutex> lock(BDVs_.mu);
                  for (auto& extraReq : watcherEntry->extraRequestors) {
                     auto bdvIter = BDVs_.bdvs.find(extraReq);
                     if (bdvIter == BDVs_.bdvs.end()) {
                        continue;
                     }
                     packet.extraRequestors_.emplace(bdvIter->second);
                  }
               }
            }

            //fail the batch promise
            batchPtr->isReadyPromise->set_exception(
               std::make_exception_ptr(ZeroConf::ZcBatchError{}));

            //notify the bdv of the error
            std::stringstream errMsg;
            errMsg << "RPC broadcast error: " << verbose;
            notifyError(*hashes.begin(), packet.bdvPtr_,
               result, errMsg.str());

            //notify extra requestors of the error as well
            for (auto& requestor : packet.extraRequestors_) {
               std::stringstream reqMsg;
               reqMsg << "Extra requestor broadcast error: " << verbose;
               notifyError(*hashes.begin(), requestor,
                  result, reqMsg.str());
            }
      }
   }
}

///////////////////////////////////////////////////////////////////////////////
void Clients::queuePayload(std::shared_ptr<BDV_Payload>& payload)
{
   packetQueue_.push_back(std::move(payload));
}

////////
std::unique_ptr<Network::Socket_WritePayload> Clients::processCommand(
   std::shared_ptr<BDV_Payload> payload)
{
   //clear bdvPtr from the payload to avoid circular ownership
   auto bdvPtr = payload->moveBdvPtr();

   //process payload
   auto preparedPayload = bdvPtr->preparePayload(payload);
   if (!preparedPayload.isReady()) {
      return nullptr;
   }

   auto msgReader = preparedPayload.getReader();
   if (msgReader == nullptr) {
      throw std::runtime_error("invalid reader");
   }
   auto capnReader = msgReader->getReader();

   auto request = capnReader->getRoot<Codec::BDV::Request>();
   switch (request.which())
   {
      case Codec::BDV::Request::Which::STATIC:
      {
         //process static command
         auto staticRequest = request.getStatic();
         auto builderPtr = parseStaticRequest(
            staticRequest, request.getMsgId(), this,
            payload->getBdvID(), payload->getPubkey());
         if (builderPtr != nullptr) {
            return std::make_unique<Network::WritePayload_Capnp>(
               std::move(builderPtr), std::vector<uint8_t>{});
         }
         break;
      }

      default:
         auto builder = parseRequest(request, request.getMsgId(), bdvPtr);
         if (builder.isValid()) {
            size_t size = builder.builder->sizeInWords() * sizeof(capnp::word);
            if (size < SCRATCHPAD_SIZE) {
               /*
               Message is small enough to fit in the scratchpad, copy it
               over to a raw payload
               */

               //we can avoid this extra copy
               auto flat = capnp::messageToFlatArray(*builder.builder);
               auto bytes = flat.asBytes();
               std::vector<uint8_t> firstSegment(bytes.begin(), bytes.end());
               return std::make_unique<Network::WritePayload_Raw>(firstSegment);
            } else {
               /*
               Message lives across multiple segments, we have to pass it to a
               capnp payload, along with the scratchpad, which contains the
               first segment
               */
               return std::make_unique<Network::WritePayload_Capnp>(
                  std::move(builder.builder),
                  std::move(bdvPtr->getScratchPad())
               );
            }
         }
   }
   return nullptr;
}

///////////////////////////////////////////////////////////////////////////////
void Clients::rpcBroadcast(RpcBroadcastPacket& packet)
{
   rpcBroadcastQueue_.push_back(std::move(packet));
}

////////
void Clients::p2pBroadcast(Types::BdvId bdvId, std::vector<BinaryDataRef>& rawZCs)
{
   //run through submitted ZCs, prune already mined ones
   auto db = bdm_->getIFace();
   for (auto& rawZcRef : rawZCs) {
      Tx tx(rawZcRef);
      auto hash = tx.getThisHash();

      auto dbKey = db->getDBKeyForHash(hash);
      if (Types::isTxKeyValid(dbKey)) {
         //notify the bdv of the error
         auto notifPacket = std::make_shared<BDV_Notification_Packet>();
         notifPacket->bdvPtr = BDVs_.get(bdvId);

         notifPacket->notifPtr = std::make_shared<BDV_Notification_Error>(
            bdvId,
            (int)ArmoryErrorCodes::ZcBroadcast_AlreadyInChain,
            hash, "RPC broadcast error: Already in chain"
         );
         innerBDVNotifStack_.push_back(std::move(notifPacket));

         //reset data ref so as to not parse the zc
         rawZcRef.reset();
      }
   }

   auto errorCallback = [this, bdvId](
      std::vector<ZeroConf::ZeroConfBatchFallbackStruct> zcVec)->void
   {
      std::vector<RpcBroadcastPacket> rpcPackets;
      auto bdvPtr = BDVs_.get(bdvId);
      for (const auto& fallbackStruct : zcVec) {
         std::set<std::shared_ptr<BDV_Server_Object>> extraRequestors;
         for (const auto& extraBdvId : fallbackStruct.extraRequestors) {
            auto secondBdv = BDVs_.get(extraBdvId);
            if (secondBdv == nullptr) {
               continue;
            }
            extraRequestors.emplace(secondBdv);
         }

         if (fallbackStruct.err != ArmoryErrorCodes::ZcBatch_Timeout) {
            //signal error to caller
            auto notifPacket = std::make_shared<BDV_Notification_Packet>();
            notifPacket->bdvPtr = bdvPtr;
            notifPacket->notifPtr = std::make_shared<BDV_Notification_Error>(
               bdvId,
               (int)fallbackStruct.err, fallbackStruct.txHash, std::string{}
            );
            innerBDVNotifStack_.push_back(std::move(notifPacket));

            //then signal extra requestors
            for (const auto& extraBDV : extraRequestors) {
               auto notifPacket = std::make_shared<BDV_Notification_Packet>();
               notifPacket->bdvPtr = extraBDV;
               notifPacket->notifPtr = std::make_shared<BDV_Notification_Error>(
                  extraBDV->getID(),
                  (int)fallbackStruct.err, fallbackStruct.txHash, std::string{}
               );
               innerBDVNotifStack_.push_back(std::move(notifPacket));
            }

            //finally, skip RPC fallback
            continue;
         }

         //tally timed out zc
         RpcBroadcastPacket packet;
         packet.rawTx_ = fallbackStruct.rawTxPtr;
         packet.bdvPtr_ = bdvPtr;
         packet.extraRequestors_ = std::move(extraRequestors);
         rpcPackets.emplace_back(std::move(packet));
      }

      if (rpcPackets.empty()) {
         return;
      }

      //push through rpc
      for (auto& packet : rpcPackets) {
         rpcBroadcastQueue_.push_back(std::move(packet));
      }
   };

   //broadcast
   bdm_->zeroConfCont_->broadcastZC(
      rawZCs, 5000, errorCallback, bdvId);
}

///////////////////////////////////////////////////////////////////////////////
// Callback
Callback::~Callback()
{}

///////////////////////////////////////////////////////////////////////////////
void WS_Callback::push(std::unique_ptr<Network::Socket_WritePayload> payload)
{
   //write to socket
   WebSocketServer::write(bdvID_, WEBSOCKET_CALLBACK_ID, std::move(payload));
}

///////////////////////////////////////////////////////////////////////////////
void UnitTest_Callback::push(
   std::unique_ptr<Network::Socket_WritePayload> payload)
{
   //stash the notification, unit test will pull it as needed
   notifQueue_.push_back(std::move(payload));
}

///////////////////////////////////////////////////////////////////////////////
BinaryData UnitTest_Callback::getNotification()
{
   try {
      auto notifPtr = notifQueue_.pop_front();
      std::vector<uint8_t> flat;
      notifPtr->serialize(flat);
      return BinaryData{flat.data(), flat.size()};
   }
   catch (const Threading::StopBlockingLoop&) {}
   return {};
}

///////////////////////////////////////////////////////////////////////////////
// BDVMap
void BDVMap::add(std::shared_ptr<BDV_Server_Object> bdvObj)
{
   std::unique_lock<std::mutex> lock(mu);
   bdvs.emplace(bdvObj->getID(), bdvObj);
}

void BDVMap::del(Types::BdvId bdvId)
{
   std::unique_lock<std::mutex> lock(mu);
   bdvs.erase(bdvId);
}

std::shared_ptr<BDV_Server_Object> BDVMap::get(Types::BdvId bdvId) const
{
   std::unique_lock<std::mutex> lock(mu);
   auto iter = bdvs.find(bdvId);
   if (iter == bdvs.end()) {
      return nullptr;
   }
   return iter->second;
}

std::map<Types::BdvId, std::shared_ptr<BDV_Server_Object>> BDVMap::get() const
{
   std::unique_lock<std::mutex> lock(mu);
   return bdvs;
}
