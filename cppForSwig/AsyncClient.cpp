
////////////////////////////////////////////////////////////////////////////////
//                                                                            //
//  Copyright (C) 2016-2024, goatpig.                                         //
//  Distributed under the MIT license                                         //
//  See LICENSE-MIT or https://opensource.org/licenses/MIT                    //
//                                                                            //
////////////////////////////////////////////////////////////////////////////////

#include "AsyncClient.h"
#include "EncryptionUtils.h"
#include "ArmoryErrors.h"
#include "WebSocketMessage.h"

#include <capnp/message.h>
#include <capnp/serialize.h>
#include "capnp/BDV.capnp.h"

using namespace Armory;
using namespace AsyncClient;

#define REQUEST_ERROR      -1
#define INVALID_COUNT      -2
#define WRONG_REPLY_CLASS  -10
#define WRONG_REPLY_TYPE   -11
#define MISSING_LEDGER_ID  -12

///////////////////////////////////////////////////////////////////////////////
//
// BlockDataViewer
//
///////////////////////////////////////////////////////////////////////////////
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

   // deser helpers
   std::vector<UTXO> capnToUtxoVec(
      capnp::List<Codec::Types::Output, capnp::Kind::STRUCT>::Reader outputs)
   {
      std::vector<UTXO> result;
      result.reserve(outputs.size());
      for (auto output : outputs) {
         auto hashCapn = output.getTxHash();
         BinaryDataRef txHash(hashCapn.begin(), hashCapn.end());

         auto scriptCapn = output.getScript();
         BinaryDataRef script(scriptCapn.begin(), scriptCapn.end());

         result.emplace_back(UTXO(
            output.getValue(), output.getTxHeight(), output.getTxIndex(),
            output.getTxOutIndex(), txHash, script
         ));
      }
      return result;
   }

   std::vector<AddressBookEntry> capnToAddrBook(
      Codec::BDV::AddressBook::Reader addrBook)
   {
      auto entries = addrBook.getEntries();
      std::vector<AddressBookEntry> result;
      result.reserve(entries.size());
      for (auto entry : entries) {
         auto addrCapn = entry.getScrAddr();
         BinaryDataRef addr(addrCapn.begin(), addrCapn.end());
         AddressBookEntry abe(addr);

         auto hashList = entry.getTxHashes();
         for (const auto& hashCapn : hashList) {
            BinaryDataRef hash(hashCapn.begin(), hashCapn.end());
            abe.addTxHash(hash);
         }
         result.emplace_back(std::move(abe));
      }
      return result;
   }

   std::vector<DBClientClasses::LedgerEntry> capnToHistoryPage(
      capnp::List<Codec::BDV::HistoryPage, capnp::Kind::STRUCT>::Reader pages)
   {
      std::vector<DBClientClasses::LedgerEntry> result;
      for (auto page : pages) {
         auto ledgers = page.getLedgers();

         for (const auto& ledger : ledgers) {
            //tx hash
            auto capnTxHash = ledger.getTxHash();
            auto hashBytes = capnTxHash.asBytes();
            BinaryData txHash(hashBytes.begin(), hashBytes.end());

            //scrAddr list
            auto capnScrAddrs = ledger.getScrAddrs();
            std::vector<BinaryData> scrAddrList;
            scrAddrList.reserve(capnScrAddrs.size());
            for (const auto& scrAddr : capnScrAddrs) {
               auto asBytes = scrAddr.asBytes();
               scrAddrList.emplace_back(BinaryData(
                  scrAddr.begin(), scrAddr.end()
               ));
            }

            //instantiate ledger entry
            result.emplace_back(DBClientClasses::LedgerEntry(
               ledger.getWalletId(), ledger.getBalance(), ledger.getTxHeight(),
               txHash, ledger.getTxOutIndex(), ledger.getTxTime(),
               ledger.getIsCoinbase(), ledger.getIsSTS(),
               ledger.getIsChangeBack(), ledger.getIsOptInRBF(),
               ledger.getIsChainedZC(), ledger.getIsWitness(),
               scrAddrList
            ));
         }
      }
      return result;
   }

   std::vector<uint64_t> capnToBalanceVec(
      Codec::BDV::BalanceAndCount::Reader balances)
   {
      std::vector<uint64_t> result(4);
      result[0] = balances.getFull();
      result[1] = balances.getSpendable();
      result[2] = balances.getUnconfirmed();
      result[3] = balances.getTxnCount();
      return result;
   }

   std::map<unsigned, DBClientClasses::FeeEstimateStruct> capnToFeeSchedules(
      capnp::List<Codec::BDV::FeeSchedule, capnp::Kind::STRUCT>::Reader fees)
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
      Codec::BDV::NodeStatus::Reader nodeStatus)
   {
      if (nodeStatus.hasChain()) {
         auto chainCapn = nodeStatus.getChain();
         DBClientClasses::NodeChainStatus ncs(
            CoreRPC::ChainState(chainCapn.getChainState()),
            chainCapn.getBlockSpeed(), chainCapn.getProgress(),
            chainCapn.getEta(), chainCapn.getBlocksLeft()
         );

         auto result = std::make_shared<DBClientClasses::NodeStatus>(
            CoreRPC::NodeState(nodeStatus.getNode()),
            CoreRPC::RpcState(nodeStatus.getRpc()),
            nodeStatus.getIsSW(), ncs
         );

         return result;
      } else {
         DBClientClasses::NodeChainStatus ncs;
         auto result = std::make_shared<DBClientClasses::NodeStatus>(
            CoreRPC::NodeState(nodeStatus.getNode()),
            CoreRPC::RpcState(nodeStatus.getRpc()),
            nodeStatus.getIsSW(), ncs
         );

         return result;
      }
   }

   TxBatchResult capnToTxBatch(
      capnp::List<capnp::Data, capnp::Kind::BLOB>::Reader txs)
   {
      std::map<BinaryData, TxResult> result;
      for (auto capnTx : txs) {
         BinaryDataRef rawTx(capnTx.begin(), capnTx.end());
         auto txObj = std::make_shared<const Tx>(rawTx);
         result.emplace(txObj->getThisHash(), std::move(txObj));
      }

      return result;
   }
}

///////////////////////////////////////////////////////////////////////////////
bool BlockDataViewer::hasRemoteDB(void)
{
   return sock_->testConnection();
}

///////////////////////////////////////////////////////////////////////////////
bool BlockDataViewer::connectToRemote(void)
{
   return sock_->connectToRemote();
}

///////////////////////////////////////////////////////////////////////////////
void BlockDataViewer::addPublicKey(const SecureBinaryData& pubkey)
{
   auto wsSock = std::dynamic_pointer_cast<WebSocketClient>(sock_);
   if (wsSock == nullptr)
   {
      LOGERR << "invalid socket type for auth peer management";
      return;
   }

   wsSock->addPublicKey(pubkey);
}

///////////////////////////////////////////////////////////////////////////////
std::shared_ptr<BlockDataViewer> BlockDataViewer::getNewBDV(
   const std::string& addr, const std::string& port, const std::string& datadir,
   const PassphraseLambda& passLbd, bool ephemeralPeers, bool oneWayAuth,
   std::shared_ptr<RemoteCallback> callbackPtr)
{
   //create socket object
   auto sockptr = std::make_shared<WebSocketClient>(addr, port, datadir, passLbd,
      ephemeralPeers, oneWayAuth, callbackPtr);

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
   if (!bdvID_.empty())
      throw BDVAlreadyRegistered();

   //create capnp request
   capnp::MallocMessageBuilder message;
   auto payload = message.initRoot<Codec::BDV::Request>();

   auto staticRequest = payload.initStatic();
   staticRequest.setRegister();
   staticRequest.setMagicWord(magicWord);

   //serialize and add to payload
   auto write_payload = toWritePayload(message);

   //registration is always blocking as it needs to guarantee the bdvID
   auto promPtr = std::make_shared<std::promise<std::string>>();
   auto fut = promPtr->get_future();
   auto read_payload = std::make_shared<Socket_ReadPayload>();
   read_payload->callbackReturn_ = std::make_unique<ClientCallback>(
      [promPtr](const WebSocketMessagePartial& msg) {
      try
      {
         //deser capnp reply
         auto reader = msg.getReader();
         auto reply = reader->getRoot<Codec::BDV::Reply>();

         //sanity checks
         if (!reply.getSuccess()) {
            throw ClientMessageError(reply.getError(), -1);
         }

         if (!reply.isStatic()) {
            throw ClientMessageError("expected static reply", WRONG_REPLY_CLASS);
         }

         auto staticReply = reply.getStatic();
         if (!staticReply.isRegister()) {
            throw ClientMessageError(
               "expected register reply", WRONG_REPLY_TYPE);
         }
         promPtr->set_value(staticReply.getRegister());
      }
      catch (const std::exception& e)
      {
         promPtr->set_exception(std::make_exception_ptr(e));
      }
   });
   sock_->pushPayload(std::move(write_payload), read_payload);

   bdvID_ = fut.get();
}

///////////////////////////////////////////////////////////////////////////////
void BlockDataViewer::unregisterFromDB()
{
   if (sock_ == nullptr)
      return;

   if (sock_->type() == SocketWS)
   {
      auto sockws = std::dynamic_pointer_cast<WebSocketClient>(sock_);
      if(sockws == nullptr)
         return;

      sockws->shutdown();
      return;
   }

   //create capnp request
   capnp::MallocMessageBuilder message;
   auto payload = message.initRoot<Codec::BDV::Request>();

   auto staticRequest = payload.initStatic();
   staticRequest.setUnregister(bdvID_);

   //serialize and add to payload
   auto write_payload = toWritePayload(message);

   //push to server
   sock_->pushPayload(std::move(write_payload), nullptr);
}

///////////////////////////////////////////////////////////////////////////////
void BlockDataViewer::goOnline()
{
   //create capnp request
   capnp::MallocMessageBuilder message;
   auto payload = message.initRoot<Codec::BDV::Request>();

   auto bdvRequest = payload.initBdv();
   bdvRequest.setBdvId(bdvID_);
   bdvRequest.setGoOnline();

   //serialize and add to payload
   auto write_payload = toWritePayload(message);

   //push to server
   sock_->pushPayload(std::move(write_payload), nullptr);
}

///////////////////////////////////////////////////////////////////////////////
BlockDataViewer::BlockDataViewer(void)
{
   cache_ = std::make_shared<ClientCache>();
}

///////////////////////////////////////////////////////////////////////////////
BlockDataViewer::BlockDataViewer(std::shared_ptr<SocketPrototype> sock) :
   sock_(sock)
{
   cache_ = std::make_shared<ClientCache>();
}

///////////////////////////////////////////////////////////////////////////////
BlockDataViewer::~BlockDataViewer()
{}

///////////////////////////////////////////////////////////////////////////////
void BlockDataViewer::shutdown(const std::string& cookie)
{
   //create capnp request
   capnp::MallocMessageBuilder message;
   auto payload = message.initRoot<Codec::BDV::Request>();

   auto staticRequest = payload.initStatic();
   staticRequest.setCookie(cookie);
   staticRequest.setShutdown();

   //serialize and add to payload
   auto write_payload = toWritePayload(message);

   //push to server
   sock_->pushPayload(std::move(write_payload), nullptr);
}

///////////////////////////////////////////////////////////////////////////////
void BlockDataViewer::shutdownNode(const std::string& cookie)
{
   //create capnp request
   capnp::MallocMessageBuilder message;
   auto payload = message.initRoot<Codec::BDV::Request>();

   auto staticRequest = payload.initStatic();
   staticRequest.setCookie(cookie);
   staticRequest.setShutdownNode();

   //serialize and add to payload
   auto write_payload = toWritePayload(message);

   //push to server
   sock_->pushPayload(std::move(write_payload), nullptr);
}

///////////////////////////////////////////////////////////////////////////////
AsyncClient::BtcWallet BlockDataViewer::instantiateWallet(const std::string& id)
{
   return std::move(BtcWallet(*this, id));
}

///////////////////////////////////////////////////////////////////////////////
Lockbox BlockDataViewer::instantiateLockbox(const std::string& id)
{
   return std::move(Lockbox(*this, id));
}

///////////////////////////////////////////////////////////////////////////////
AsyncClient::Blockchain BlockDataViewer::blockchain(void)
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
   for (auto& rawTx : rawTxVec)
   {
      auto tx = std::make_shared<Tx>(rawTx);
      cache_->insertTx(tx);

      txList.set(i++, capnp::Data::Builder(
         (uint8_t*)rawTx.getPtr(), rawTx.getSize()));
   }

   //serialize and add to payload
   auto write_payload = toWritePayload(message);

   //push to server
   sock_->pushPayload(std::move(write_payload), nullptr);
}

///////////////////////////////////////////////////////////////////////////////
void BlockDataViewer::broadcastThroughRPC(const BinaryData& rawTx)
{
   auto tx = std::make_shared<Tx>(rawTx);
   cache_->insertTx(tx);

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
void BlockDataViewer::getTxByHash(
   const std::set<BinaryData>& hashes, const TxBatchCallback& callback)
{
   //create capnp request
   capnp::MallocMessageBuilder message;
   auto payload = message.initRoot<Codec::BDV::Request>();

   auto bdvRequest = payload.initBdv();
   bdvRequest.setBdvId(bdvID_);
   auto hashReq = bdvRequest.initGetTxByHash(hashes.size());

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
            auto reader = msg.getReader();
            auto reply = reader->getRoot<Codec::BDV::Reply>();

            //sanity checks
            if (!reply.getSuccess()) {
               throw ClientMessageError(reply.getError(), -1);
            }

            if (!reply.isBdv()) {
               throw ClientMessageError("expected bdv reply", WRONG_REPLY_CLASS);
            }

            auto bdvReply = reply.getBdv();
            if (!bdvReply.isGetTxByHash()) {
               throw ClientMessageError(
                  "expected GetTxByHash reply", WRONG_REPLY_TYPE);
            }

            //convert to utxo vector and fire callback
            auto result = capnToTxBatch(bdvReply.getGetTxByHash());
            callback(ReturnMessage<TxBatchResult>(result));
         } catch (ClientMessageError& e) {
            //something went wrong, set error message and fire callback
            callback(ReturnMessage<TxBatchResult>(e));
         }
      });

   //push to server
   sock_->pushPayload(std::move(write_payload), read_payload);
}

///////////////////////////////////////////////////////////////////////////////
void BlockDataViewer::updateWalletsLedgerFilter(
   const std::vector<std::string>& wltIdVec)
{
   //create capnp request
   capnp::MallocMessageBuilder message;
   auto payload = message.initRoot<Codec::BDV::Request>();

   auto bdvRequest = payload.initBdv();
   bdvRequest.setBdvId(bdvID_);

   auto walletIds = bdvRequest.initUpdateWalletsLedgerFilter(wltIdVec.size());
   for (unsigned i=0; i<wltIdVec.size(); i++) {
      walletIds.set(i, wltIdVec[i]);
   }

   //serialize and add to payload
   auto write_payload = toWritePayload(message);

   //push to server
   sock_->pushPayload(std::move(write_payload), nullptr);
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
            auto reader = msg.getReader();
            auto reply = reader->getRoot<Codec::BDV::Reply>();

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
            auto reader = msg.getReader();
            auto reply = reader->getRoot<Codec::BDV::Reply>();

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
   std::function<bool(const BinaryData&, const std::string&)> lbd)
{
   auto wsSock = std::dynamic_pointer_cast<WebSocketClient>(sock_);
   if (wsSock == nullptr)
      return;

   wsSock->setPubkeyPromptLambda(lbd);
}

///////////////////////////////////////////////////////////////////////////////
void BlockDataViewer::getOutputsForOutpoints(
   const std::map<BinaryData, std::set<unsigned>>& outpoints, bool withZc,
   std::function<void(ReturnMessage<std::vector<UTXO>>)> callback)
{
   //create capnp request
   capnp::MallocMessageBuilder message;
   auto payload = message.initRoot<Codec::BDV::Request>();

   auto bdvRequest = payload.initBdv();
   bdvRequest.setBdvId(bdvID_);
   auto opsReq = bdvRequest.initGetOutputsForOutpoints();
   opsReq.setWithZc(withZc);
   auto capnOps = opsReq.initOutpoints(outpoints.size());

   //populate request data
   unsigned i = 0;
   for (auto& op : outpoints) {
      auto capnOp = capnOps[i++];
      capnOp.setTxHash(
         capnp::Data::Builder((uint8_t*)op.first.getPtr(), op.first.getSize()));

      auto capnIds = capnOp.initOutpointIds(op.second.size());
      unsigned y = 0;
      for (auto& id : op.second) {
         capnIds.set(y++, id);
      }
   }

   //serialize and add to payload
   auto write_payload = toWritePayload(message);

   //reply handling lambda
   auto read_payload = std::make_shared<Socket_ReadPayload>();
   read_payload->callbackReturn_ =
      std::make_unique<ClientCallback>([callback](const WebSocketMessagePartial& msg){
         try {
            //deser capnp reply
            auto reader = msg.getReader();
            auto reply = reader->getRoot<Codec::BDV::Reply>();

            //sanity checks
            if (!reply.getSuccess()) {
               throw ClientMessageError(reply.getError(), -1);
            }

            if (!reply.isBdv()) {
               throw ClientMessageError("expected bdv reply", WRONG_REPLY_CLASS);
            }

            auto bdvReply = reply.getBdv();
            if (!bdvReply.isGetOutputsForOutpoints()) {
               throw ClientMessageError(
                  "expected GetOutputsForOutpoints reply", WRONG_REPLY_TYPE);
            }

            //convert to utxo vector and fire callback
            auto result = capnToUtxoVec(bdvReply.getGetOutputsForOutpoints());
            callback(ReturnMessage<std::vector<UTXO>>(result));
         } catch (ClientMessageError& e) {
            //something went wrong, set error message and fire callback
            callback(ReturnMessage<std::vector<UTXO>>(e));
         }
      });

   //push to server
   sock_->pushPayload(std::move(write_payload), read_payload);
}

///////////////////////////////////////////////////////////////////////////////
//
// LedgerDelegate
//
///////////////////////////////////////////////////////////////////////////////
LedgerDelegate::LedgerDelegate(std::shared_ptr<SocketPrototype> sock,
   const std::string& bdvid, const std::string& ldid) :
   delegateID_(ldid), bdvID_(bdvid), sock_(sock)
{}

///////////////////////////////////////////////////////////////////////////////
void LedgerDelegate::getHistoryPage(uint32_t id,
   std::function<void(ReturnMessage<
      std::vector<DBClientClasses::LedgerEntry>>)> callback)
{
   capnp::MallocMessageBuilder message;
   auto payload = message.initRoot<Codec::BDV::Request>();

   auto ledgerRequest = payload.initLedger();
   ledgerRequest.setLedgerId(bdvID_);

   auto pageReq = ledgerRequest.getGetHistoryPages();
   pageReq.setFrom(id);

   //serialize and add to payload
   auto write_payload = toWritePayload(message);

   //reply handling lambda
   auto read_payload = std::make_shared<Socket_ReadPayload>();
   read_payload->callbackReturn_ =
      std::make_unique<ClientCallback>([callback](const WebSocketMessagePartial& msg){
      try {
         //deser capnp reply
         auto reader = msg.getReader();
         auto reply = reader->getRoot<Codec::BDV::Reply>();

         //sanity checks
         if (!reply.getSuccess()) {
            throw ClientMessageError(reply.getError(), -1);
         }

         if (!reply.isLedger()) {
            throw ClientMessageError("expected ledger reply", WRONG_REPLY_CLASS);
         }

         auto ledgerReply = reply.getLedger();
         if (!ledgerReply.isGetHistoryPages()) {
            throw ClientMessageError(
               "expected history pages", WRONG_REPLY_TYPE);
         }

         //convert to history page
         auto result = capnToHistoryPage(
            ledgerReply.getGetHistoryPages());
         callback(ReturnMessage<std::vector<DBClientClasses::LedgerEntry>>(result));
      } catch (ClientMessageError& e) {
         //something went wrong, set error message and fire callback
         callback(ReturnMessage<std::vector<DBClientClasses::LedgerEntry>>(e));
      }
   });

   //push to server
   sock_->pushPayload(move(write_payload), read_payload);
}

///////////////////////////////////////////////////////////////////////////////
void LedgerDelegate::getPageCount(
   std::function<void(ReturnMessage<uint64_t>)> callback) const
{
   capnp::MallocMessageBuilder message;
   auto payload = message.initRoot<Codec::BDV::Request>();

   auto ledgerRequest = payload.initLedger();
   ledgerRequest.setLedgerId(delegateID_);
   ledgerRequest.setGetPageCount();

   //serialize and add to payload
   auto write_payload = toWritePayload(message);

   //reply handling lambda
   auto read_payload = std::make_shared<Socket_ReadPayload>();
   read_payload->callbackReturn_ =
      std::make_unique<ClientCallback>([callback](const WebSocketMessagePartial& msg){
         try {
            //deser capnp reply
            auto reader = msg.getReader();
            auto reply = reader->getRoot<Codec::BDV::Reply>();

            //sanity checks
            if (!reply.getSuccess()) {
               throw ClientMessageError(reply.getError(), -1);
            }

            if (!reply.isLedger()) {
               throw ClientMessageError("expected ledger reply", WRONG_REPLY_CLASS);
            }

            auto ledgerReply = reply.getLedger();
            if (!ledgerReply.isGetPageCount()) {
               throw ClientMessageError(
                  "expected page count", WRONG_REPLY_TYPE);
            }

            //convert to history page
            callback(ReturnMessage<uint64_t>(ledgerReply.getGetPageCount()));
         } catch (ClientMessageError& e) {
            //something went wrong, set error message and fire callback
            callback(ReturnMessage<uint64_t>(e));
         }
      });

   //push to server
   sock_->pushPayload(move(write_payload), read_payload);
}

///////////////////////////////////////////////////////////////////////////////
//
// BtcWallet
//
///////////////////////////////////////////////////////////////////////////////
AsyncClient::BtcWallet::BtcWallet(const BlockDataViewer& bdv,
   const std::string& id) :
   walletID_(id), bdvID_(bdv.bdvID_), sock_(bdv.sock_)
{}

///////////////////////////////////////////////////////////////////////////////
void AsyncClient::BtcWallet::registerAddresses(
   const std::vector<BinaryData>& addrVec, bool isNew)
{
   //create capnp request
   capnp::MallocMessageBuilder message;
   auto payload = message.initRoot<Codec::BDV::Request>();

   auto bdvRequest = payload.initBdv();
   bdvRequest.setBdvId(bdvID_);

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

   //serialize and add to payload
   auto write_payload = toWritePayload(message);

   //push to server
   sock_->pushPayload(move(write_payload), nullptr);
}

///////////////////////////////////////////////////////////////////////////////
void AsyncClient::BtcWallet::setUnconfirmedTarget(unsigned confTarget)
{
   //create capnp request
   capnp::MallocMessageBuilder message;
   auto payload = message.initRoot<Codec::BDV::Request>();

   auto walletRequest = payload.initWallet();
   walletRequest.setBdvId(bdvID_);
   walletRequest.setWalletId(walletID_);
   walletRequest.setSetConfTarget(confTarget);

   //serialize and add to payload
   auto write_payload = toWritePayload(message);

   //push to server
   sock_->pushPayload(move(write_payload), nullptr);
}

///////////////////////////////////////////////////////////////////////////////
void AsyncClient::BtcWallet::unregisterAddresses(
   const std::set<BinaryData>& addrSet)
{
   //create capnp request
   capnp::MallocMessageBuilder message;
   auto payload = message.initRoot<Codec::BDV::Request>();

   auto walletRequest = payload.initWallet();
   walletRequest.setBdvId(bdvID_);
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
void AsyncClient::BtcWallet::getBalancesAndCount(uint32_t blockheight,
   std::function<void(ReturnMessage<std::vector<uint64_t>>)> callback)
{
   //create capnp request
   capnp::MallocMessageBuilder message;
   auto payload = message.initRoot<Codec::BDV::Request>();

   auto walletRequest = payload.initWallet();
   walletRequest.setBdvId(bdvID_);
   walletRequest.setWalletId(walletID_);
   walletRequest.setGetBalanceAndCount();

   //serialize and add to payload
   auto write_payload = toWritePayload(message);

   //reply handling lambda
   auto read_payload = std::make_shared<Socket_ReadPayload>();
   read_payload->callbackReturn_ =
      std::make_unique<ClientCallback>([callback](const WebSocketMessagePartial& msg){
         try {
            //deser capnp reply
            auto reader = msg.getReader();
            auto reply = reader->getRoot<Codec::BDV::Reply>();

            //sanity checks
            if (!reply.getSuccess()) {
               throw ClientMessageError(reply.getError(), -1);
            }

            if (!reply.isWallet()) {
               throw ClientMessageError("expected wallet reply", WRONG_REPLY_CLASS);
            }

            auto walletReply = reply.getWallet();
            if (!walletReply.isGetBalanceAndCount()) {
               throw ClientMessageError(
                  "expected balance and count reply", WRONG_REPLY_TYPE);
            }

            //convert to utxo vector and fire callback
            auto result = capnToBalanceVec(walletReply.getGetBalanceAndCount());
            callback(ReturnMessage<std::vector<uint64_t>>(result));
         } catch (ClientMessageError& e) {
            //something went wrong, set error message and fire callback
            callback(ReturnMessage<std::vector<uint64_t>>(e));
         }
      });

   //push to server
   sock_->pushPayload(move(write_payload), read_payload);
}

///////////////////////////////////////////////////////////////////////////////
void AsyncClient::BtcWallet::getUTXOsForValue(uint64_t val,
   std::function<void(ReturnMessage<std::vector<UTXO>>)> callback)
{
   //create capnp request
   capnp::MallocMessageBuilder message;
   auto payload = message.initRoot<Codec::BDV::Request>();

   auto walletRequest = payload.initWallet();
   walletRequest.setBdvId(bdvID_);
   walletRequest.setWalletId(walletID_);

   auto outputReq = walletRequest.getGetOutputs();
   outputReq.setTargetValue(val);
   outputReq.setSpendable(true);

   //serialize and add to payload
   auto write_payload = toWritePayload(message);

   //reply handling lambda
   auto read_payload = std::make_shared<Socket_ReadPayload>();
   read_payload->callbackReturn_ =
      std::make_unique<ClientCallback>([callback](const WebSocketMessagePartial& msg){
         try {
            //deser capnp reply
            auto reader = msg.getReader();
            auto reply = reader->getRoot<Codec::BDV::Reply>();

            //sanity checks
            if (!reply.getSuccess()) {
               throw ClientMessageError(reply.getError(), -1);
            }

            if (!reply.isWallet()) {
               throw ClientMessageError("expected wallet reply", WRONG_REPLY_CLASS);
            }

            auto walletReply = reply.getWallet();
            if (!walletReply.isGetOutputs()) {
               throw ClientMessageError(
                  "expected getOutputs reply", WRONG_REPLY_TYPE);
            }

            //convert to utxo vector and fire callback
            auto result = capnToUtxoVec(walletReply.getGetOutputs());
            callback(ReturnMessage<std::vector<UTXO>>(result));
         } catch (ClientMessageError& e) {
            //something went wrong, set error message and fire callback
            callback(ReturnMessage<std::vector<UTXO>>(e));
         }
      });

   //push to server
   sock_->pushPayload(move(write_payload), read_payload);
}

///////////////////////////////////////////////////////////////////////////////
void AsyncClient::BtcWallet::getHistoryPage(uint32_t id,
   std::function<void(ReturnMessage<
      std::vector<DBClientClasses::LedgerEntry>>)> callback)
{
   if (ledgerID_.empty()) {
      ClientMessageError err("need a ledgerId!", MISSING_LEDGER_ID);
      callback(ReturnMessage<std::vector<DBClientClasses::LedgerEntry>>(err));
   }

   capnp::MallocMessageBuilder message;
   auto payload = message.initRoot<Codec::BDV::Request>();

   auto ledgerRequest = payload.initLedger();
   ledgerRequest.setLedgerId(bdvID_);

   auto pageReq = ledgerRequest.getGetHistoryPages();
   pageReq.setFrom(id);

   //serialize and add to payload
   auto write_payload = toWritePayload(message);

   //reply handling lambda
   auto read_payload = std::make_shared<Socket_ReadPayload>();
   read_payload->callbackReturn_ =
      std::make_unique<ClientCallback>([callback](const WebSocketMessagePartial& msg){
         try {
            //deser capnp reply
            auto reader = msg.getReader();
            auto reply = reader->getRoot<Codec::BDV::Reply>();

            //sanity checks
            if (!reply.getSuccess()) {
               throw ClientMessageError(reply.getError(), -1);
            }

            if (!reply.isLedger()) {
               throw ClientMessageError("expected ledger reply", WRONG_REPLY_CLASS);
            }

            auto ledgerReply = reply.getLedger();
            if (!ledgerReply.isGetHistoryPages()) {
               throw ClientMessageError(
                  "expected history pages", WRONG_REPLY_TYPE);
            }

            //convert to history page
            auto result = capnToHistoryPage(
               ledgerReply.getGetHistoryPages());
            callback(ReturnMessage<std::vector<
               DBClientClasses::LedgerEntry>>(result));
         } catch (ClientMessageError& e) {
            //something went wrong, set error message and fire callback
            callback(ReturnMessage<std::vector<
               DBClientClasses::LedgerEntry>>(e));
         }
      });

   //push to server
   sock_->pushPayload(move(write_payload), read_payload);
}

///////////////////////////////////////////////////////////////////////////////
ScrAddrObj AsyncClient::BtcWallet::getScrAddrObjByKey(const BinaryData& scrAddr,
   uint64_t full, uint64_t spendable, uint64_t unconf, uint32_t count)
{
   return ScrAddrObj(sock_, bdvID_, walletID_, scrAddr, INT32_MAX,
      full, spendable, unconf, count);
}

///////////////////////////////////////////////////////////////////////////////
void AsyncClient::BtcWallet::createAddressBook(
   std::function<void(ReturnMessage<std::vector<AddressBookEntry>>)> callback) const
{
   //create capnp request
   capnp::MallocMessageBuilder message;
   auto payload = message.initRoot<Codec::BDV::Request>();

   auto walletRequest = payload.initWallet();
   walletRequest.setBdvId(bdvID_);
   walletRequest.setWalletId(walletID_);
   walletRequest.setCreateAddressBook();

   //serialize and add to payload
   auto write_payload = toWritePayload(message);

   //reply handling lambda
   auto read_payload = std::make_shared<Socket_ReadPayload>();
   read_payload->callbackReturn_ =
      std::make_unique<ClientCallback>([callback](const WebSocketMessagePartial& msg){
         try {
            //deser capnp reply
            auto reader = msg.getReader();
            auto reply = reader->getRoot<Codec::BDV::Reply>();

            //sanity checks
            if (!reply.getSuccess()) {
               throw ClientMessageError(reply.getError(), -1);
            }

            if (!reply.isWallet()) {
               throw ClientMessageError("expected wallet reply", WRONG_REPLY_CLASS);
            }

            auto walletReply = reply.getWallet();
            if (!walletReply.isCreateAddressBook()) {
               throw ClientMessageError(
                  "expected createAddressBook reply", WRONG_REPLY_TYPE);
            }

            //convert to address book
            auto result = capnToAddrBook(
               walletReply.getCreateAddressBook());
            callback(ReturnMessage<std::vector<AddressBookEntry>>(result));
         } catch (ClientMessageError& e) {
            //something went wrong, set error message and fire callback
            callback(ReturnMessage<std::vector<AddressBookEntry>>(e));
         }
      });

   //push to server
   sock_->pushPayload(move(write_payload), read_payload);
}

///////////////////////////////////////////////////////////////////////////////
//
// Lockbox
//
///////////////////////////////////////////////////////////////////////////////
void Lockbox::getBalancesAndCountFromDB(uint32_t topBlockHeight)
{
   auto setValue = [this](ReturnMessage<std::vector<uint64_t>> int_vec)->void
   {
      auto v = std::move(int_vec.get());
      if (v.size() != 4)
         throw std::runtime_error("unexpected vector size");

      fullBalance_ = v[0];
      spendableBalance_ = v[1];
      unconfirmedBalance_ = v[2];

      txnCount_ = v[3];
   };

   BtcWallet::getBalancesAndCount(topBlockHeight, setValue);
}

///////////////////////////////////////////////////////////////////////////////
//
// ScrAddrObj
//
///////////////////////////////////////////////////////////////////////////////
ScrAddrObj::ScrAddrObj(std::shared_ptr<SocketPrototype> sock,
   const std::string& bdvId, const std::string& walletID,
   const BinaryData& scrAddr, int index,
   uint64_t full, uint64_t spendabe, uint64_t unconf, uint32_t count) :
   bdvID_(bdvId), walletID_(walletID), scrAddr_(scrAddr), sock_(sock),
   fullBalance_(full), spendableBalance_(spendabe),
   unconfirmedBalance_(unconf), count_(count), index_(index)
{}

///////////////////////////////////////////////////////////////////////////////
ScrAddrObj::ScrAddrObj(AsyncClient::BtcWallet* wlt, const BinaryData& scrAddr,
   int index, uint64_t full, uint64_t spendabe, uint64_t unconf,
   uint32_t count) :
   bdvID_(wlt->bdvID_), walletID_(wlt->walletID_), 
   scrAddr_(scrAddr), sock_(wlt->sock_),
   fullBalance_(full), spendableBalance_(spendabe),
   unconfirmedBalance_(unconf), count_(count), index_(index)
{}

///////////////////////////////////////////////////////////////////////////////
void ScrAddrObj::getOutputs(
   std::function<void(ReturnMessage<std::vector<UTXO>>)> callback)
{
   //create capnp request
   capnp::MallocMessageBuilder message;
   auto payload = message.initRoot<Codec::BDV::Request>();

   auto addrRequest = payload.initAddress();
   addrRequest.setBdvId(bdvID_);
   auto address = addrRequest.getAddress();
   address.setBody(capnp::Data::Builder(
      (uint8_t*)scrAddr_.getPtr(), scrAddr_.getSize()));
   addrRequest.initGetOutputs();

   //serialize and add to payload
   auto write_payload = toWritePayload(message);

   //reply handling lambda
   auto read_payload = std::make_shared<Socket_ReadPayload>();
   read_payload->callbackReturn_ =
      std::make_unique<ClientCallback>([callback](const WebSocketMessagePartial& msg){
         try {
            //deser capnp reply
            auto reader = msg.getReader();
            auto reply = reader->getRoot<Codec::BDV::Reply>();

            //sanity checks
            if (!reply.getSuccess()) {
               throw ClientMessageError(reply.getError(), -1);
            }

            if (!reply.isAddress()) {
               throw ClientMessageError("expected address reply", WRONG_REPLY_CLASS);
            }

            auto addressReply = reply.getAddress();
            if (!addressReply.isGetOutputs()) {
               throw ClientMessageError(
                  "expected getOutputs reply", WRONG_REPLY_TYPE);
            }

            //convert to utxo vector and fire callback
            auto result = capnToUtxoVec(addressReply.getGetOutputs());
            callback(ReturnMessage<std::vector<UTXO>>(result));
         } catch (ClientMessageError& e) {
            //something went wrong, set error message and fire callback
            callback(ReturnMessage<std::vector<UTXO>>(e));
         }
      });

   //push to server
   sock_->pushPayload(move(write_payload), read_payload);
}

///////////////////////////////////////////////////////////////////////////////
//
// Blockchain
//
///////////////////////////////////////////////////////////////////////////////
AsyncClient::Blockchain::Blockchain(const BlockDataViewer& bdv) :
   sock_(bdv.sock_), bdvID_(bdv.bdvID_)
{}

///////////////////////////////////////////////////////////////////////////////
void AsyncClient::Blockchain::getHeaderByHash(const BinaryData& hash,
   std::function<void(ReturnMessage<DBClientClasses::BlockHeader>)> callback)
{
   //create capnp request
   capnp::MallocMessageBuilder message;
   auto payload = message.initRoot<Codec::BDV::Request>();

   auto staticRequest = payload.initStatic();
   auto getHeaders = staticRequest.initGetHeadersByHash(1);
   getHeaders.set(0,
      capnp::Data::Builder((uint8_t*)hash.getPtr(), hash.getSize()));

   //serialize and add to payload
   auto write_payload = toWritePayload(message);

   //reply handling lambda
   auto read_payload = std::make_shared<Socket_ReadPayload>();
   read_payload->callbackReturn_ =
      std::make_unique<ClientCallback>([callback](
         const WebSocketMessagePartial& msg){
         try {
            //deser capnp reply
            auto reader = msg.getReader();
            auto reply = reader->getRoot<Codec::BDV::Reply>();

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
            auto headerReply = staticReply.getGetHeadersByHeight();
            if (headerReply.size() != 1) {
               throw ClientMessageError("expected 1 header", INVALID_COUNT);
            }

            auto headerData = headerReply[0];
            BinaryDataRef headerBdr(headerData.begin(), headerData.size());
            auto result = DBClientClasses::BlockHeader(headerBdr, UINT32_MAX);
            callback(ReturnMessage<DBClientClasses::BlockHeader>(result));
         } catch (ClientMessageError& e) {
            //something went wrong, set error message and fire callback
            callback(ReturnMessage<DBClientClasses::BlockHeader>(e));
         }
      });

   //push to server
   sock_->pushPayload(move(write_payload), read_payload);
}

///////////////////////////////////////////////////////////////////////////////
void AsyncClient::Blockchain::getHeaderByHeight(unsigned height,
   std::function<void(ReturnMessage<DBClientClasses::BlockHeader>)> callback)
{
   //create capnp request
   capnp::MallocMessageBuilder message;
   auto payload = message.initRoot<Codec::BDV::Request>();

   auto staticRequest = payload.initStatic();
   auto getHeaders = staticRequest.initGetHeadersByHeight(1);
   getHeaders.set(0, height);

   //serialize and add to payload
   auto write_payload = toWritePayload(message);

   //reply handling lambda
   auto read_payload = std::make_shared<Socket_ReadPayload>();
   read_payload->callbackReturn_ =
      std::make_unique<ClientCallback>([callback, height](
         const WebSocketMessagePartial& msg){
         try {
            //deser capnp reply
            auto reader = msg.getReader();
            auto reply = reader->getRoot<Codec::BDV::Reply>();

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
            auto headerReply = staticReply.getGetHeadersByHeight();
            if (headerReply.size() != 1) {
               throw ClientMessageError("expected 1 header", INVALID_COUNT);
            }

            auto headerData = headerReply[0];
            BinaryDataRef headerBdr(headerData.begin(), headerData.size());
            auto result = DBClientClasses::BlockHeader(headerBdr, height);
            callback(ReturnMessage<DBClientClasses::BlockHeader>(result));
         } catch (ClientMessageError& e) {
            //something went wrong, set error message and fire callback
            callback(ReturnMessage<DBClientClasses::BlockHeader>(e));
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

///////////////////////////////////////////////////////////////////////////////
void AsyncClient::BlockDataViewer::getUTXOsForAddress(
   std::set<BinaryData>& addrSet,
   std::function<void(ReturnMessage<std::vector<UTXO>>)> callback)
{
   //create capnp request
   capnp::MallocMessageBuilder message;
   auto payload = message.initRoot<Codec::BDV::Request>();

   auto bdvRequest = payload.initBdv();
   bdvRequest.setBdvId(bdvID_);
   auto addrs = bdvRequest.initGetOutputsForAddress(addrSet.size());

   //populate request data
   unsigned i = 0;
   for (auto& addr : addrSet) {
      addrs[i++].setBody(
         capnp::Data::Builder((uint8_t*)addr.getPtr(), addr.getSize()));
   }

   //serialize and add to payload
   auto write_payload = toWritePayload(message);

   //reply handling lambda
   auto read_payload = std::make_shared<Socket_ReadPayload>();
   read_payload->callbackReturn_ =
      std::make_unique<ClientCallback>([callback](const WebSocketMessagePartial& msg){
         try {
            //deser capnp reply
            auto reader = msg.getReader();
            auto reply = reader->getRoot<Codec::BDV::Reply>();

            //sanity checks
            if (!reply.getSuccess()) {
               throw ClientMessageError(reply.getError(), -1);
            }

            if (!reply.isBdv()) {
               throw ClientMessageError("expected bdv reply", WRONG_REPLY_CLASS);
            }

            auto bdvReply = reply.getBdv();
            if (!bdvReply.isGetOutputsForAddress()) {
               throw ClientMessageError(
                  "expected getOutputsForAddress reply", WRONG_REPLY_TYPE);
            }

            //convert to utxo vector and fire callback
            auto result = capnToUtxoVec(bdvReply.getGetOutputsForAddress());
            callback(ReturnMessage<std::vector<UTXO>>(result));
         } catch (ClientMessageError& e) {
            //something went wrong, set error message and fire callback
            callback(ReturnMessage<std::vector<UTXO>>(e));
         }
      });

   //push to server
   sock_->pushPayload(move(write_payload), read_payload);
}

///////////////////////////////////////////////////////////////////////////////
//
// ClientCache
//
///////////////////////////////////////////////////////////////////////////////
void ClientCache::insertTx(std::shared_ptr<Tx> tx)
{
   ReentrantLock(this);
   txMap_.emplace(tx->getThisHash(), tx);
}

///////////////////////////////////////////////////////////////////////////////
void ClientCache::insertTx(const BinaryData& hash, std::shared_ptr<Tx> tx)
{
   ReentrantLock(this);
   txMap_.emplace(hash, tx);
}

///////////////////////////////////////////////////////////////////////////////
void ClientCache::insertRawHeader(unsigned& height, BinaryDataRef header)
{
   ReentrantLock(this);
   rawHeaderMap_.insert(std::make_pair(height, header));
}

///////////////////////////////////////////////////////////////////////////////
void ClientCache::insertHeightForTxHash(BinaryData& hash, unsigned& height)
{
   ReentrantLock(this);
   txHashToHeightMap_.insert(std::make_pair(hash, height));
}

///////////////////////////////////////////////////////////////////////////////
std::shared_ptr<const Tx> ClientCache::getTx(const BinaryDataRef& hashRef) const
{
   ReentrantLock(this);

   auto iter = txMap_.find(hashRef);
   if (iter == txMap_.end())
      throw NoMatch();

   auto constTx = std::const_pointer_cast<const Tx>(iter->second);
   return constTx;
}

///////////////////////////////////////////////////////////////////////////////
std::shared_ptr<Tx> ClientCache::getTx_NoConst(const BinaryDataRef& hashRef)
{
   ReentrantLock(this);

   auto iter = txMap_.find(hashRef);
   if (iter == txMap_.end())
      throw NoMatch();

   return iter->second;
}

///////////////////////////////////////////////////////////////////////////////
const BinaryData& ClientCache::getRawHeader(const unsigned& height) const
{
   ReentrantLock(this);

   auto iter = rawHeaderMap_.find(height);
   if (iter == rawHeaderMap_.end())
      throw NoMatch();

   return iter->second;
}

///////////////////////////////////////////////////////////////////////////////
const unsigned& ClientCache::getHeightForTxHash(const BinaryData& height) const
{
   ReentrantLock(this);

   auto iter = txHashToHeightMap_.find(height);
   if (iter == txHashToHeightMap_.end())
      throw NoMatch();

   return iter->second;
}

///////////////////////////////////////////////////////////////////////////////
//
// OutpointBatch/Data
//
///////////////////////////////////////////////////////////////////////////////
void OutpointBatch::prettyPrint() const
{
   std::stringstream ss;

   ss << " - cutoffs: " << heightCutoff_ << ", " << zcIndexCutoff_ << std::endl;
   ss << " - address count: " << outpoints_.size() << std::endl;
   
   for (const auto& addrPair : outpoints_)
   {
      //convert scrAddr to address string
      auto addrStr = BtcUtils::getAddressStrFromScrAddr(addrPair.first);

      //address & outpoint count
      ss << "  ." << addrStr << ", op count: " << 
         addrPair.second.size() << std::endl;

      //outpoint data
      std::map<unsigned, std::map<BinaryDataRef, std::vector<OutpointData>>> heightHashMap;
      for (const auto& op : addrPair.second)
      {
         auto height = op.txHeight_;
         const auto& hash = op.txHash_;

         auto& hashMap = heightHashMap[height];
         auto& opVec = hashMap[hash.getRef()];
         opVec.emplace_back(op);
      }

      for (const auto& hashMap : heightHashMap)
      {
         ss << "   *height: " << hashMap.first << std::endl;
         
         for (const auto& opMap : hashMap.second)
         {
            ss << "    .hash: " << opMap.first.toHexStr(true) << std::endl;

            for (const auto& op : opMap.second)
               op.prettyPrint(ss);
         }
      }

      ss << std::endl;
   }

   std::cout << ss.str();
}

///////////////////////////////////////////////////////////////////////////////
void OutpointData::prettyPrint(std::ostream& st) const
{
   st << "     _id: " << txOutIndex_ << ", value: " << value_ << std::endl;

   st << "      spender: ";
   if (spenderHash_.empty())
      st << "N/A" << std::endl;
   else
      st << spenderHash_.toHexStr(true) << std::endl;
}