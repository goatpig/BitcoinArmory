
////////////////////////////////////////////////////////////////////////////////
//                                                                            //
//  Copyright (C) 2016-2026, goatpig.                                         //
//  Distributed under the MIT license                                         //
//  See LICENSE-MIT or https://opensource.org/licenses/MIT                    //
//                                                                            //
////////////////////////////////////////////////////////////////////////////////

#include "AsyncClient.h"
#include <Utils/BtcUtils.h>
#include <Utils/varint.h>
#include <Utils/Cryptography.h>
#include <Utils/ArmoryErrors.h>
#include <BlockchainDatabase/txio.h>
#include <Network/WebSocketMessage.h>

#include <capnp/message.h>
#include <capnp/serialize.h>
#include "capnp/BDV.capnp.h"

using namespace Armory;
using namespace Armory::Network;
using namespace AsyncClient;

#define REQUEST_ERROR      -1
#define INVALID_COUNT      -2
#define WRONG_REPLY_CLASS  -10
#define WRONG_REPLY_TYPE   -11

namespace {
   class ClientCallback : public CallbackReturn_WebSocket
   {
      std::function<void(const WebSocketMessagePartial&)> callback_;

   public:
      ClientCallback(std::function<void(const WebSocketMessagePartial&)> callback)
         : callback_(callback)
      {}

      void callback(const WebSocketMessagePartial& msg) override {
         callback_(msg);
      }
   };

   //ser helper
   std::unique_ptr<WritePayload_Raw> toWritePayload(
      capnp::MallocMessageBuilder& builder)
   {
      auto flat = capnp::messageToFlatArray(builder);
      auto bytes = flat.asBytes();
      auto vec = std::vector<uint8_t>(bytes.begin(), bytes.end());
      return std::make_unique<WritePayload_Raw>(vec);
   }

   std::unique_ptr<WritePayload_Capnp> initLargePayload()
   {
      //4096 - overhead, 8 bytes aligned
      static uint32_t segmentSize = 4048 / sizeof(capnp::word);
      std::vector<uint8_t> firstSegment(4048);
      kj::ArrayPtr<capnp::word> arrayPtr(
         reinterpret_cast<capnp::word*>(firstSegment.data()),
         segmentSize
      );

      auto builder = std::make_unique<capnp::MallocMessageBuilder>(
         arrayPtr, capnp::AllocationStrategy::FIXED_SIZE);
      return std::make_unique<WritePayload_Capnp>(
         std::move(builder), std::move(firstSegment));
   }

   // deser helpers
   std::map<unsigned, DBClientClasses::FeeEstimateStruct> capnToFeeSchedules(
      capnp::List<Codec::Types::FeeSchedule, capnp::Kind::STRUCT>::Reader fees)
   {
      //      FeeEstimateStruct(float val, bool isSmart, const std::string& error) :
      std::map<unsigned, DBClientClasses::FeeEstimateStruct> result;
      for (auto fee : fees) {
         result.emplace(fee.getTarget(), DBClientClasses::FeeEstimateStruct{
            fee.getFeeByte(), fee.getSmartFee(), {}});
      }
      return result;
   }

   std::shared_ptr<DBClientClasses::NodeStatus> capnToNodeStatus(
      Codec::Types::NodeStatus::Reader nodeStatus)
   {
      if (nodeStatus.hasChain()) {
         auto chainCapn = nodeStatus.getChain();
         DBClientClasses::NodeChainStatus ncs(
            Node::ChainState(chainCapn.getChainState()),
            chainCapn.getBlockSpeed(), chainCapn.getProgress(),
            chainCapn.getEta(), chainCapn.getBlocksLeft()
         );

         auto result = std::make_shared<DBClientClasses::NodeStatus>(
            Node::NodeState(nodeStatus.getNode()),
            Node::RpcState(nodeStatus.getRpc()),
            nodeStatus.getIsSW(), ncs
         );

         return result;
      } else {
         DBClientClasses::NodeChainStatus ncs;
         auto result = std::make_shared<DBClientClasses::NodeStatus>(
            Node::NodeState(nodeStatus.getNode()),
            Node::RpcState(nodeStatus.getRpc()),
            nodeStatus.getIsSW(), ncs
         );

         return result;
      }
   }

   TxBatchResult capnToTxBatch(
      capnp::List<Codec::Types::Tx, capnp::Kind::STRUCT>::Reader& txs)
   {
      std::map<BinaryData, TxResult> result;
      for (auto capnTx : txs) {
         auto body = capnTx.getRaw();
         BinaryDataRef rawTx(body.begin(), body.end());
         try {
            auto txObj = std::make_shared<Tx>(rawTx);
            txObj->setTxKey(capnTx.getKey());
            txObj->setChainedZC(capnTx.getIsChainedZc());
            txObj->setRBF(capnTx.getIsRbf());
            result.emplace(txObj->getThisHash(), std::move(txObj));
         } catch (const BtcUtils::BlockDeserializingException&) {}
      }
      return result;
   }

   std::vector<Tx>capnToTxVec(
      capnp::List<Codec::Types::Tx, capnp::Kind::STRUCT>::Reader& txs)
   {
      std::vector<Tx> result;
      result.reserve(txs.size());
      for (auto capnTx : txs) {
         auto body = capnTx.getRaw();
         BinaryDataRef rawTx(body.begin(), body.end());
         try {
            auto& txObj = result.emplace_back(Tx{rawTx});
            txObj.setTxKey(capnTx.getKey());
            txObj.setChainedZC(capnTx.getIsChainedZc());
            txObj.setRBF(capnTx.getIsRbf());
         } catch (const BtcUtils::BlockDeserializingException&) {}
      }
      return result;
   }

   HeaderVec capnToHeaderVec(
      capnp::List<Codec::Types::Header, capnp::Kind::STRUCT>::Reader capnHeaders)
   {
      HeaderVec result;
      result.reserve(capnHeaders.size());

      for (auto capnHeader : capnHeaders) {
         auto thisHash = capnHeader.getThisHash();
         auto prevHash = capnHeader.getPrevHash();
         auto headerPtr = result.emplace_back(
            std::make_shared<DBClientClasses::BlockHeader>(
            BinaryDataRef{thisHash.begin(), thisHash.end()},
            BinaryDataRef{prevHash.begin(), prevHash.end()},
            capnHeader.getBlockId(), capnHeader.getHeight(),
            capnHeader.getTimestamp(), capnHeader.getBlockSize(),
            capnHeader.getNumTxs(), capnHeader.getMainBranch()
         ));
      }
      return result;
   }
}

///////////////////////////////////////////////////////////////////////////////
//
// BlockDataViewer
//
///////////////////////////////////////////////////////////////////////////////
BlockDataViewer::BlockDataViewer()
{}

BlockDataViewer::BlockDataViewer(std::shared_ptr<SocketPrototype> sock) :
   sock_(sock)
{}

BlockDataViewer::~BlockDataViewer()
{}

////////
BlockDataViewer& BlockDataViewer::operator=(const BlockDataViewer& rhs)
{
   sock_ = rhs.sock_;
   return *this;
}

////////
bool BlockDataViewer::isValid() const
{
   if (sock_ == nullptr) {
      return false;
   }
   return sock_->running();
}

bool BlockDataViewer::hasRemoteDB()
{
   return sock_->testConnection();
}

///////////////////////////////////////////////////////////////////////////////
bool BlockDataViewer::connectToRemote()
{
   return sock_->connectToRemote();
}

///////////////////////////////////////////////////////////////////////////////
void BlockDataViewer::addPublicKey(const SecureBinaryData& pubkey, bool oneWay)
{
   auto wsSock = std::dynamic_pointer_cast<WebSocketClient>(sock_);
   if (wsSock == nullptr)
   {
      LOGERR << "invalid socket type for auth peer management";
      return;
   }

   wsSock->addPublicKey(pubkey, oneWay);
}

///////////////////////////////////////////////////////////////////////////////
std::shared_ptr<BlockDataViewer> BlockDataViewer::getNewBDV(
   const std::string& addr, const std::string& port,
   std::shared_ptr<NetworkPeers::ClientStore> peers, bool oneWayAuth,
   std::shared_ptr<RemoteCallback> callbackPtr)
{
   //create socket object
   auto sockptr = std::make_shared<WebSocketClient>(
      addr, port, peers, oneWayAuth, callbackPtr);

   //instantiate bdv object
   BlockDataViewer* bdvPtr = new BlockDataViewer(sockptr);

   //create shared_ptr of bdv object
   std::shared_ptr<BlockDataViewer> bdvSharedPtr;
   bdvSharedPtr.reset(bdvPtr);

   return bdvSharedPtr;
}

///////////////////////////////////////////////////////////////////////////////
void BlockDataViewer::registerWithDB(const std::string& magicWord)
{
   if (registered_) {
      throw BDVAlreadyRegistered();
   }

   //create capnp request
   capnp::MallocMessageBuilder message;
   auto payload = message.initRoot<Codec::BDV::Request>();

   auto staticRequest = payload.initStatic();
   staticRequest.setRegister();
   staticRequest.setMagicWord(magicWord);

   //serialize and add to payload
   auto write_payload = toWritePayload(message);

   //registration is always blocking as it needs to guarantee the bdvID
   auto promPtr = std::make_shared<std::promise<bool>>();
   auto fut = promPtr->get_future();
   auto read_payload = std::make_shared<Socket_ReadPayload>();
   read_payload->callbackReturn_ = std::make_unique<ClientCallback>(
      [promPtr](const WebSocketMessagePartial& msg) {
      try {
         //deser capnp reply
         auto msgReader = msg.getReader();
         auto capnReader = msgReader->getReader();
         auto reply = capnReader->getRoot<Codec::BDV::Reply>();

         //sanity checks
         if (!reply.getSuccess()) {
            throw ClientMessageError(reply.getError(), -1);
         }
         promPtr->set_value(true);
      }
      catch (const std::exception& e) {
         promPtr->set_exception(std::make_exception_ptr(e));
      }
   });
   sock_->pushPayload(std::move(write_payload), read_payload);
   registered_ = fut.get();;
}

///////////////////////////////////////////////////////////////////////////////
void BlockDataViewer::unregisterFromDB()
{
   if (sock_ == nullptr) {
      return;
   }
   sock_.reset();
}

///////////////////////////////////////////////////////////////////////////////
void BlockDataViewer::goOnline()
{
   //create capnp request
   capnp::MallocMessageBuilder message;
   auto payload = message.initRoot<Codec::BDV::Request>();

   auto bdvRequest = payload.initBdv();
   bdvRequest.setGoOnline();

   //serialize and add to payload
   auto write_payload = toWritePayload(message);

   //push to server
   sock_->pushPayload(std::move(write_payload), nullptr);
}

///////////////////////////////////////////////////////////////////////////////
void BlockDataViewer::start()
{
   /* request db synchronization start when automating it */
   capnp::MallocMessageBuilder message;
   auto payload = message.initRoot<Codec::BDV::Request>();
   auto staticRequest = payload.initStatic();
   staticRequest.setStart();

   //serialize and add to payload
   auto write_payload = toWritePayload(message);

   //push to server
   sock_->pushPayload(std::move(write_payload), nullptr);
}

void BlockDataViewer::shutdown()
{
   /* request db shutdown when automating it */
   capnp::MallocMessageBuilder message;
   auto payload = message.initRoot<Codec::BDV::Request>();
   auto staticRequest = payload.initStatic();
   staticRequest.setShutdown();

   //serialize and add to payload
   auto write_payload = toWritePayload(message);

   //push to server
   sock_->pushPayload(std::move(write_payload), nullptr);
}

///////////////////////////////////////////////////////////////////////////////
AsyncClient::BtcWallet BlockDataViewer::getWalletObj(const std::string& id)
{
   return BtcWallet(*this, id);
}

///////////////////////////////////////////////////////////////////////////////
AsyncClient::Blockchain BlockDataViewer::blockchain()
{
   return Blockchain(*this);
}

///////////////////////////////////////////////////////////////////////////////
void BlockDataViewer::broadcastZC(const std::vector<BinaryData>& rawTxVec)
{
   //create capnp request
   capnp::MallocMessageBuilder message;
   auto payload = message.initRoot<Codec::BDV::Request>();

   auto staticRequest = payload.initStatic();
   auto txList = staticRequest.initBroadcast(rawTxVec.size());

   unsigned i=0;
   for (auto& rawTx : rawTxVec) {
      auto tx = std::make_shared<Tx>(rawTx);
      txList.set(i++, capnp::Data::Builder(
         (uint8_t*)rawTx.getPtr(), rawTx.getSize()));
   }

   //serialize and add to payload
   auto write_payload = toWritePayload(message);

   //push to server
   sock_->pushPayload(std::move(write_payload), nullptr);
}

void BlockDataViewer::broadcastThroughRPC(const BinaryData& rawTx)
{
   auto tx = std::make_shared<Tx>(rawTx);

   //create capnp request
   capnp::MallocMessageBuilder message;
   auto payload = message.initRoot<Codec::BDV::Request>();

   auto staticRequest = payload.initStatic();
   staticRequest.setRpcBroadcast(
      capnp::Data::Builder((uint8_t*)rawTx.getPtr(), rawTx.getSize()));

   //serialize and add to payload
   auto write_payload = toWritePayload(message);

   //push to server
   sock_->pushPayload(std::move(write_payload), nullptr);
}

///////////////////////////////////////////////////////////////////////////////
void BlockDataViewer::getTxios(uint32_t from,
   std::function<void(ReturnMessage<std::vector<TxIOPair>>)> callback)
{
   //create capnp request
   capnp::MallocMessageBuilder message;
   auto payload = message.initRoot<Codec::BDV::Request>();

   auto bdvRequest = payload.initBdv();
   bdvRequest.setGetTxios(from);

   //serialize and add to payload
   auto write_payload = toWritePayload(message);

   //reply handling lambda
   auto read_payload = std::make_shared<Socket_ReadPayload>();
   read_payload->callbackReturn_ =
      std::make_unique<ClientCallback>([callback](const WebSocketMessagePartial& msg){
         try {
            //deser capnp reply
            auto msgReader = msg.getReader();
            auto capnReader = msgReader->getReader();
            auto reply = capnReader->getRoot<Codec::BDV::Reply>();

            //sanity checks
            if (!reply.getSuccess()) {
               throw ClientMessageError(reply.getError(), -1);
            }

            if (!reply.isBdv()) {
               throw ClientMessageError("expected bdv reply", WRONG_REPLY_CLASS);
            }

            auto bdvReply = reply.getBdv();
            if (!bdvReply.isGetTxios()) {
               throw ClientMessageError(
                  "expected GetTxios reply", WRONG_REPLY_TYPE);
            }

            //convert to txio vector and fire callback
            auto capnTxios = bdvReply.getGetTxios();
            std::vector<TxIOPair> txios;
            txios.reserve(capnTxios.size());

            for (auto capnTxio : capnTxios) {
               auto capnAddr = capnTxio.getScrAddr();
               BinaryDataRef scrAddr{capnAddr.begin(), capnAddr.end()};

               TxIOPair txio{
                  capnTxio.getTxOut(), capnTxio.getAmount(),
                  scrAddr, capnTxio.getTxIn()};

               txio.setRBF(capnTxio.getRbf());
               txio.setChained(capnTxio.getChained());
               txios.emplace_back(std::move(txio));
            }
            callback(ReturnMessage<std::vector<TxIOPair>>(txios));
         } catch (ClientMessageError& e) {
            //something went wrong, set error message and fire callback
            callback(ReturnMessage<std::vector<TxIOPair>>(e));
         }
      });

   //push to server
   sock_->pushPayload(std::move(write_payload), read_payload);
}

////////
void BlockDataViewer::getTxsByHash(
   const std::set<BinaryData>& hashes, const TxBatchCallback& callback)
{
   //create capnp request
   capnp::MallocMessageBuilder message;
   auto payload = message.initRoot<Codec::BDV::Request>();

   auto bdvRequest = payload.initBdv();
   auto hashReq = bdvRequest.initGetTxsByHash(hashes.size());

   unsigned i=0;
   for (auto& hash : hashes) {
      hashReq.set(i++, capnp::Data::Builder(
         (uint8_t*)hash.getPtr(), hash.getSize()));
   }

   //serialize and add to payload
   auto write_payload = toWritePayload(message);

   //reply handling lambda
   auto read_payload = std::make_shared<Socket_ReadPayload>();
   read_payload->callbackReturn_ =
      std::make_unique<ClientCallback>([callback](const WebSocketMessagePartial& msg){
         try {
            //deser capnp reply
            auto msgReader = msg.getReader();
            auto capnReader = msgReader->getReader();
            auto reply = capnReader->getRoot<Codec::BDV::Reply>();

            //sanity checks
            if (!reply.getSuccess()) {
               throw ClientMessageError(reply.getError(), -1);
            }

            if (!reply.isBdv()) {
               throw ClientMessageError("expected bdv reply", WRONG_REPLY_CLASS);
            }

            auto bdvReply = reply.getBdv();
            if (!bdvReply.isGetTxsByHash()) {
               throw ClientMessageError(
                  "expected GetTxByHash reply", WRONG_REPLY_TYPE);
            }

            //convert to utxo vector and fire callback
            auto txns = bdvReply.getGetTxsByHash();
            auto result = capnToTxBatch(txns);
            callback(ReturnMessage<TxBatchResult>(result));
         } catch (ClientMessageError& e) {
            //something went wrong, set error message and fire callback
            callback(ReturnMessage<TxBatchResult>(e));
         }
      });

   //push to server
   sock_->pushPayload(std::move(write_payload), read_payload);
}

void BlockDataViewer::getTxsByKey(const std::set<Types::TxKey>& txkeys,
   const std::function<void(ReturnMessage<std::vector<Tx>>)>& callback)
{
   //create capnp request
   capnp::MallocMessageBuilder message;
   auto payload = message.initRoot<Codec::BDV::Request>();

   auto bdvRequest = payload.initBdv();
   auto txsReq = bdvRequest.initGetTxsByKey(txkeys.size());

   unsigned i=0;
   for (auto& key : txkeys) {
      txsReq.set(i++, key);
   }

   //serialize and add to payload
   auto write_payload = toWritePayload(message);

   //reply handling lambda
   auto read_payload = std::make_shared<Socket_ReadPayload>();
   read_payload->callbackReturn_ =
      std::make_unique<ClientCallback>([callback](const WebSocketMessagePartial& msg){
         try {
            //deser capnp reply
            auto msgReader = msg.getReader();
            auto capnReader = msgReader->getReader();
            auto reply = capnReader->getRoot<Codec::BDV::Reply>();

            //sanity checks
            if (!reply.getSuccess()) {
               throw ClientMessageError(reply.getError(), -1);
            }

            if (!reply.isBdv()) {
               throw ClientMessageError("expected bdv reply", WRONG_REPLY_CLASS);
            }

            auto bdvReply = reply.getBdv();
            if (!bdvReply.isGetTxsByKey()) {
               throw ClientMessageError(
                  "expected GetTxByHash reply", WRONG_REPLY_TYPE);
            }

            //convert to utxo vector and fire callback
            auto txns = bdvReply.getGetTxsByKey();
            auto result = capnToTxVec(txns);
            callback(ReturnMessage<std::vector<Tx>>(result));
         } catch (ClientMessageError& e) {
            //something went wrong, set error message and fire callback
            callback(ReturnMessage<std::vector<Tx>>(e));
         }
      });

   //push to server
   sock_->pushPayload(std::move(write_payload), read_payload);
}

///////////////////////////////////////////////////////////////////////////////
void BlockDataViewer::getNodeStatus(std::function<
   void(ReturnMessage<std::shared_ptr<DBClientClasses::NodeStatus>>)> callback)
{
   //create capnp request
   capnp::MallocMessageBuilder message;
   auto payload = message.initRoot<Codec::BDV::Request>();

   auto staticRequest = payload.initStatic();
   staticRequest.setGetNodeStatus();

   //serialize and add to payload
   auto write_payload = toWritePayload(message);

   //reply handling lambda
   auto read_payload = std::make_shared<Socket_ReadPayload>();
   read_payload->callbackReturn_ =
      std::make_unique<ClientCallback>([callback](
         const WebSocketMessagePartial& msg){
         try {
            //deser capnp reply
            auto msgReader = msg.getReader();
            auto capnReader = msgReader->getReader();
            auto reply = capnReader->getRoot<Codec::BDV::Reply>();

            //sanity checks
            if (!reply.getSuccess()) {
               throw ClientMessageError(reply.getError(), -1);
            }

            if (!reply.isStatic()) {
               throw ClientMessageError("expected static reply", WRONG_REPLY_CLASS);
            }

            auto staticReply = reply.getStatic();
            if (!staticReply.isGetNodeStatus()) {
               throw ClientMessageError(
                  "expected GetNodeStatus reply", WRONG_REPLY_TYPE);
            }

            //convert to header
            auto result = capnToNodeStatus(staticReply.getGetNodeStatus());
            callback(ReturnMessage<std::shared_ptr<
               DBClientClasses::NodeStatus>>(result));
         } catch (ClientMessageError& e) {
            //something went wrong, set error message and fire callback
            callback(ReturnMessage<std::shared_ptr<
               DBClientClasses::NodeStatus>>(e));
         }
      });

   //push to server
   sock_->pushPayload(std::move(write_payload), read_payload);
}

///////////////////////////////////////////////////////////////////////////////
void BlockDataViewer::getFeeSchedule(const std::string& strategy,
   std::function<void(ReturnMessage<
      std::map<unsigned, DBClientClasses::FeeEstimateStruct>>)> callback)
{
   //create capnp request
   capnp::MallocMessageBuilder message;
   auto payload = message.initRoot<Codec::BDV::Request>();

   auto staticRequest = payload.initStatic();
   staticRequest.setGetFeeSchedule(strategy);

   //serialize and add to payload
   auto write_payload = toWritePayload(message);

   //reply handling lambda
   auto read_payload = std::make_shared<Socket_ReadPayload>();
   read_payload->callbackReturn_ =
      std::make_unique<ClientCallback>([callback](
         const WebSocketMessagePartial& msg){
         try {
            //deser capnp reply
            auto msgReader = msg.getReader();
            auto capnReader = msgReader->getReader();
            auto reply = capnReader->getRoot<Codec::BDV::Reply>();

            //sanity checks
            if (!reply.getSuccess()) {
               throw ClientMessageError(reply.getError(), -1);
            }

            if (!reply.isStatic()) {
               throw ClientMessageError("expected static reply", WRONG_REPLY_CLASS);
            }

            auto staticReply = reply.getStatic();
            if (!staticReply.isGetFeeSchedule()) {
               throw ClientMessageError(
                  "expected GetFeeSchedule reply", WRONG_REPLY_TYPE);
            }

            //convert to header
            auto result = capnToFeeSchedules(staticReply.getGetFeeSchedule());
            callback(ReturnMessage<std::map<
               unsigned, DBClientClasses::FeeEstimateStruct>>(result));
         } catch (ClientMessageError& e) {
            //something went wrong, set error message and fire callback
            callback(ReturnMessage<std::map<
               unsigned, DBClientClasses::FeeEstimateStruct>>(e));
         }
      });

   //push to server
   sock_->pushPayload(std::move(write_payload), read_payload);
}

///////////////////////////////////////////////////////////////////////////////
void BlockDataViewer::setCheckServerKeyPromptLambda(
   const std::function<bool(const BinaryData&)>& lbd)
{
   auto wsSock = std::dynamic_pointer_cast<WebSocketClient>(sock_);
   if (wsSock == nullptr) {
      return;
   }
   wsSock->setPubkeyPromptLambda(lbd);
}

///////////////////////////////////////////////////////////////////////////////
//
// BtcWallet
//
///////////////////////////////////////////////////////////////////////////////
AsyncClient::BtcWallet::BtcWallet(const BlockDataViewer& bdv,
   const std::string& id) :
   walletID_(id), sock_(bdv.sock_)
{}

///////////////////////////////////////////////////////////////////////////////
bool AsyncClient::BtcWallet::registerAddresses(
   const std::vector<BinaryData>& addrVec, bool isNew)
{
   //create capnp request
   auto writePayload = initLargePayload();
   auto payload = writePayload->builder->initRoot<Codec::BDV::Request>();

   auto bdvRequest = payload.initBdv();
   auto addrReq = bdvRequest.initRegisterWallet();
   addrReq.setWalletId(walletID_);
   addrReq.setIsNew(isNew);

   addrReq.initAddresses(addrVec.size());
   auto capnAddresses = addrReq.getAddresses();
   for (unsigned i=0; i<addrVec.size(); i++) {
      auto& addr = addrVec[i];
      capnAddresses[i].setBody(capnp::Data::Builder(
         (uint8_t*)addr.getPtr(), addr.getSize()));
   }

   auto read_payload = std::make_shared<Socket_ReadPayload>();
   auto prom = std::make_shared<std::promise<bool>>();
   auto fut = prom->get_future();
   read_payload->callbackReturn_ =
      std::make_unique<ClientCallback>([prom](const WebSocketMessagePartial& msg){
         //deser capnp reply
         auto msgReader = msg.getReader();
         auto capnReader = msgReader->getReader();
         auto reply = capnReader->getRoot<Codec::BDV::Reply>();
         prom->set_value(reply.getSuccess());
   });

   //push to server
   sock_->pushPayload(std::move(writePayload), read_payload);
   return fut.get();
}

///////////////////////////////////////////////////////////////////////////////
void AsyncClient::BtcWallet::unregisterAddresses(
   const std::set<BinaryData>& addrSet)
{
   //create capnp request
   capnp::MallocMessageBuilder message;
   auto payload = message.initRoot<Codec::BDV::Request>();

   auto walletRequest = payload.initWallet();
   walletRequest.setWalletId(walletID_);

   auto addrsReq = walletRequest.initUnregisterAddresses(addrSet.size());
   unsigned i=0;
   for (auto& addr : addrSet) {
      addrsReq[i++].setBody(capnp::Data::Builder(
         (uint8_t*)addr.getPtr(), addr.getSize()));
   }

   //serialize and add to payload
   auto write_payload = toWritePayload(message);

   //push to server
   sock_->pushPayload(move(write_payload), nullptr);
}

///////////////////////////////////////////////////////////////////////////////
void AsyncClient::BtcWallet::unregister()
{
   unregisterAddresses({});
}

///////////////////////////////////////////////////////////////////////////////
//
// Blockchain
//
///////////////////////////////////////////////////////////////////////////////
AsyncClient::Blockchain::Blockchain(const BlockDataViewer& bdv) :
   sock_(bdv.sock_)
{}

///////////////////////////////////////////////////////////////////////////////
void AsyncClient::Blockchain::getHeadersByHeight(
   const std::set<unsigned>& heights,
   const std::function<void(ReturnMessage<HeaderVec>)>& callback)
{
   //create capnp request
   capnp::MallocMessageBuilder message;
   auto payload = message.initRoot<Codec::BDV::Request>();

   auto staticRequest = payload.initStatic();
   auto getHeaders = staticRequest.initGetHeadersByHeight(heights.size());
   unsigned i = 0;
   for (const auto& height : heights) {
      getHeaders.set(i++, height);
   }

   //serialize and add to payload
   auto write_payload = toWritePayload(message);

   //reply handling lambda
   auto read_payload = std::make_shared<Socket_ReadPayload>();
   read_payload->callbackReturn_ =
      std::make_unique<ClientCallback>([callback, heights](
         const WebSocketMessagePartial& msg){
         try {
            //deser capnp reply
            auto msgReader = msg.getReader();
            auto capnReader = msgReader->getReader();
            auto reply = capnReader->getRoot<Codec::BDV::Reply>();

            //sanity checks
            if (!reply.getSuccess()) {
               throw ClientMessageError(reply.getError(), -1);
            }

            if (!reply.isStatic()) {
               throw ClientMessageError("expected static reply", WRONG_REPLY_CLASS);
            }

            auto staticReply = reply.getStatic();
            if (!staticReply.isGetHeadersByHeight()) {
               throw ClientMessageError(
                  "expected getHeadersByHeight reply", WRONG_REPLY_TYPE);
            }

            //convert to header
            auto result = capnToHeaderVec(staticReply.getGetHeadersByHeight());
            callback(ReturnMessage<HeaderVec>(std::move(result)));
         } catch (ClientMessageError& e) {
            //something went wrong, set error message and fire callback
            callback(ReturnMessage<HeaderVec>(e));
         }
      });

   //push to server
   sock_->pushPayload(move(write_payload), read_payload);
}

void AsyncClient::Blockchain::getHeadersById(
   const std::set<Types::BlockId>& blockIds,
   const std::function<void(ReturnMessage<HeaderVec>)>& callback)
{
   //create capnp request
   capnp::MallocMessageBuilder message;
   auto payload = message.initRoot<Codec::BDV::Request>();

   auto staticRequest = payload.initStatic();
   auto getHeaders = staticRequest.initGetHeadersById(blockIds.size());
   unsigned i = 0;
   for (const auto& blockId : blockIds) {
      getHeaders.set(i++, blockId);
   }

   //serialize and add to payload
   auto write_payload = toWritePayload(message);

   //reply handling lambda
   auto read_payload = std::make_shared<Socket_ReadPayload>();
   read_payload->callbackReturn_ =
      std::make_unique<ClientCallback>([callback](
         const WebSocketMessagePartial& msg){
         try {
            //deser capnp reply
            auto msgReader = msg.getReader();
            auto capnReader = msgReader->getReader();
            auto reply = capnReader->getRoot<Codec::BDV::Reply>();

            //sanity checks
            if (!reply.getSuccess()) {
               throw ClientMessageError(reply.getError(), -1);
            }

            if (!reply.isStatic()) {
               throw ClientMessageError("expected static reply", WRONG_REPLY_CLASS);
            }

            auto staticReply = reply.getStatic();
            if (!staticReply.isGetHeadersById()) {
               throw ClientMessageError(
                  "expected getHeadersById reply", WRONG_REPLY_TYPE);
            }

            //convert to header
            auto result = capnToHeaderVec(staticReply.getGetHeadersById());
            callback(ReturnMessage<HeaderVec>(std::move(result)));
         } catch (ClientMessageError& e) {
            //something went wrong, set error message and fire callback
            callback(ReturnMessage<HeaderVec>(e));
         }
      });

   //push to server
   sock_->pushPayload(move(write_payload), read_payload);
}

///////////////////////////////////////////////////////////////////////////////
std::pair<unsigned, unsigned> AsyncClient::BlockDataViewer::getRekeyCount() const
{
   auto wsSocket = std::dynamic_pointer_cast<WebSocketClient>(sock_);
   if (wsSocket == nullptr)
      return std::make_pair(0, 0);

   return wsSocket->getRekeyCount();
}
