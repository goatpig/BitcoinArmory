////////////////////////////////////////////////////////////////////////////////
//                                                                            //
//  Copyright (C) 2016-2021, goatpig.                                         //
//  Distributed under the MIT license                                         //
//  See LICENSE-MIT or https://opensource.org/licenses/MIT                    //
//                                                                            //
////////////////////////////////////////////////////////////////////////////////

#include "BDM_Server.h"
#include "ArmoryErrors.h"

#include <capnp/message.h>
#include <capnp/serialize.h>
#include "capnp/BDV.capnp.h"

using namespace Armory;
using namespace std::chrono_literals;

namespace {
   using namespace Armory::Codec::BDV;

   void outputToCapn(const UTXO& utxo,
      Codec::Types::Output::Builder& result)
   {
      result.setValue(utxo.getValue());
      result.setTxHeight(utxo.getHeight());
      result.setTxIndex(utxo.getTxIndex());
      result.setTxOutIndex(utxo.getTxOutIndex());

      const auto& script = utxo.getScript();
      result.setScript(capnp::Data::Builder(
         (uint8_t*)script.getPtr(), script.getSize()
      ));

      const auto& hash = utxo.getTxHash();
      result.setTxHash(capnp::Data::Builder(
         (uint8_t*)hash.getPtr(), script.getSize()
      ));
   }

   void outputToCapn(const StoredTxOut& output,
      Codec::Types::Output::Builder& result)
   {
      result.setValue(output.getValue());
      result.setTxHeight(output.blockHeight_);
      result.setTxIndex(output.txIndex_);
      result.setTxOutIndex(output.txOutIndex_);

      auto script = output.getScriptRef();
      result.setScript(capnp::Data::Builder(
         (uint8_t*)script.getPtr(), script.getSize()
      ));
   }

   void historyPageToCapn(const std::vector<LedgerEntry>& page,
      HistoryPage::Builder& result)
   {
      auto capnLes = result.initLedgers(page.size());
      unsigned i=0;
      for (const auto& le : page) {
         auto capnLe = capnLes[i++];

         capnLe.setBalance(le.getValue());
         capnLe.setTxHeight(le.getBlockNum());
         capnLe.setTxOutIndex(le.getIndex());

         capnLe.setIsCoinbase(le.isCoinbase());
         capnLe.setIsChangeBack(le.isChangeBack());
         capnLe.setIsSTS(le.isSentToSelf());
         capnLe.setIsOptInRBF(le.isOptInRBF());
         capnLe.setIsChainedZC(le.isChainedZC());
         capnLe.setIsWitness(le.usesWitness());
         capnLe.setWalletId(le.getWalletID());

         const auto& txHash = le.getTxHash();
         capnLe.setTxHash(capnp::Data::Builder(
            (uint8_t*)txHash.getPtr(), txHash.getSize()
         ));

         const auto& scrAddrList = le.getScrAddrList();
         auto capnScrAddrs = capnLe.initScrAddrs(scrAddrList.size());
         unsigned y=0;
         for (const auto& scrAddr : scrAddrList) {
            capnScrAddrs.set(y++, capnp::Data::Builder(
               (uint8_t*)scrAddr.getPtr(), scrAddr.getSize()
            ));
         }
      }
   }

   ////
   std::unique_ptr<capnp::MessageBuilder> parseBDVCommand(
      BdvRequest::Reader request,
      std::shared_ptr<BDV_Server_Object> bdv,
      uint32_t msgId)
   {
      auto result = std::make_unique<capnp::MallocMessageBuilder>();
      auto reply = result->initRoot<Codec::BDV::Reply>();
      reply.setMsgId(msgId);
      reply.setSuccess(true);
      auto bdvReply = reply.initBdv();

      switch (request.which())
      {
         case BdvRequest::Which::REGISTER_WALLET:
         {
            auto walletRequest = request.getRegisterWallet();

            auto capnAddresses = walletRequest.getAddresses();
            std::vector<BinaryData> addresses;
            addresses.reserve(capnAddresses.size());
            for (auto capnAddr : capnAddresses) {
               auto addrBody = capnAddr.getBody();
               addresses.emplace_back(BinaryData(addrBody.begin(), addrBody.end()));
            }

            auto walletType = WalletRegType(walletRequest.getWalletType());
            WalletRegistrationRequest regReq(
               std::string(walletRequest.getWalletId()),
               addresses, walletRequest.getIsNew(), walletType
            );
            bdv->registerWallet(regReq);
            break;
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

         case BdvRequest::Which::GET_LEDGER_DELEGATE:
         {
            auto delegateId = bdv->getLedgerDelegate();
            bdvReply.setGetLedgerDelegate(delegateId);
            break;
         }

         case BdvRequest::Which::GET_TX_BY_HASH:
         {
            auto txHashList = request.getGetTxByHash();
            auto txHashResults = bdvReply.initGetTxByHash(txHashList.size());
            for (unsigned i=0; i<txHashList.size(); i++) {
               auto txHash = txHashList[i];
               BinaryData hashBd(txHash.begin(), txHash.end());
               auto tx = bdv->getTxByHash(hashBd);

               txHashResults.set(i, capnp::Data::Builder(
                  (uint8_t*)tx.getPtr(), tx.getSize()
               ));
            }
            break;
         }

         case BdvRequest::Which::GET_OUTPUTS_FOR_OUTPOINTS:
         {
            auto opReq = request.getGetOutputsForOutpoints();
            auto outpoints = opReq.getOutpoints();
            std::map<BinaryDataRef, std::set<unsigned>> outpointsMap;

            for (auto op : outpoints) {
               auto txHash = op.getTxHash();
               BinaryDataRef hashRef(txHash.begin(), txHash.end());

               auto idList = op.getOutpointIds();
               std::set<unsigned> ids;
               for (auto id : idList) {
                  ids.emplace(id);
               }
               outpointsMap.emplace(hashRef, std::move(ids));
            }

            auto outputs = bdv->getOutputsForOutpoints(
               outpointsMap, opReq.getWithZc());
            auto capnOutputs = bdvReply.initGetOutputsForOutpoints(
               outputs.size());

            unsigned i=0;
            for (const auto& output : outputs) {
               auto capnOutput = capnOutputs[i++];
               outputToCapn(output.first, capnOutput);
               capnOutput.setTxHash(capnp::Data::Builder(
                  (uint8_t*)output.second.getPtr(), output.second.getSize()
               ));
            }
            break;
         }

         case BdvRequest::Which::GET_OUTPUTS_FOR_ADDRESS:
         {
            auto addrList = request.getGetOutputsForAddress();
            std::list<std::vector<UTXO>> utxos;
            size_t count = 0;

            for (auto addr : addrList) {
               auto addrData = addr.getBody();
               auto addrBd = BinaryDataRef(addrData.begin(), addrData.end());
               auto utxoVec = bdv->getUtxosForAddress(addrBd, true);
               count += utxoVec.size();
               utxos.emplace_back(std::move(utxoVec));
            }

            auto capnOutputs = bdvReply.initGetOutputsForAddress(count);
            unsigned i=0;
            for (const auto& utxoV : utxos) {
               for (const auto& utxo : utxoV) {
                  auto capnOutput = capnOutputs[i++];
                  outputToCapn(utxo, capnOutput);
               }
            }
            break;
         }

         case BdvRequest::Which::UPDATE_WALLETS_LEDGER_FILTER:
         {
            auto walletIdList = request.getUpdateWalletsLedgerFilter();
            std::vector<std::string> idVec;
            idVec.reserve(walletIdList.size());

            for (auto id : walletIdList) {
               idVec.emplace_back(id);
            }

            bdv->updateWalletsLedgerFilter(idVec);
            break;
         }

         default:
            reply.setSuccess(false);
            reply.setError("invalid bdv request");
      }

      return result;
   }

   std::unique_ptr<capnp::MessageBuilder> parseWalletCommand(
      WalletRequest::Reader request,
      std::shared_ptr<BDV_Server_Object> bdv,
      uint32_t msgId)
   {
      //prepare result
      auto result = std::make_unique<capnp::MallocMessageBuilder>();
      auto reply = result->initRoot<Codec::BDV::Reply>();
      reply.setMsgId(msgId);

      //get the wallet ptr, doubles as a sanity check
      std::string walletId(request.getWalletId());
      auto wltPtr = bdv->getWalletOrLockbox(walletId);
      if (wltPtr == nullptr) {
         reply.setError("unknown wallet");
         reply.setSuccess(false);
         return result;
      }

      //more result preparation
      reply.setSuccess(true);
      auto walletReply = reply.initWallet();

      //switch on the method
      switch (request.which())
      {
         case WalletRequest::Which::GET_LEDGER_DELEGATE:
         {
            std::string walletId(request.getWalletId());
            auto delegateId = bdv->getLedgerDelegate(walletId);
            walletReply.setGetLedgerDelegate(delegateId);
            break;
         }

         case WalletRequest::Which::CREATE_ADDRESS_BOOK:
         {
            auto abeVec = wltPtr->createAddressBook();
            auto capnAddrBook = walletReply.initCreateAddressBook();
            auto capnAbes = capnAddrBook.initEntries(abeVec.size());

            unsigned i=0;
            for (const auto& abe : abeVec) {
               auto capnAbe = capnAbes[i++];

               const auto& scrAddr = abe.getScrAddr();
               capnAbe.setScrAddr(capnp::Data::Builder(
                  (uint8_t*)scrAddr.getPtr(), scrAddr.getSize()
               ));

               const auto& txHashList = abe.getTxHashList();
               auto capnHashes = capnAbe.initTxHashes(txHashList.size());

               unsigned y=0;
               for (const auto& hash : txHashList) {
                  capnHashes.set(y++, capnp::Data::Builder(
                     (uint8_t*)hash.getPtr(), hash.getSize()
                  ));
               }
            }
         }

         case WalletRequest::Which::GET_BALANCE_AND_COUNT:
         {
            auto blkHeight = request.getGetBalanceAndCount();
            auto capnBalance = walletReply.initGetBalanceAndCount();
            capnBalance.setFull(wltPtr->getFullBalance());
            capnBalance.setSpendable(wltPtr->getSpendableBalance(blkHeight));
            capnBalance.setUnconfirmed(wltPtr->getUnconfirmedBalance(blkHeight));
            capnBalance.setTxnCount(wltPtr->getWltTotalTxnCount());
            break;
         }

         case WalletRequest::Which::GET_OUTPUTS:
         {
            auto opRequest = request.getGetOutputs();
            std::list<std::vector<UTXO>> utxos;
            size_t count = 0;

            if (opRequest.getSpendable()) {
               auto wltUtxos = wltPtr->getSpendableTxOutListForValue(
                  opRequest.getTargetValue());
               count += wltUtxos.size();
               utxos.emplace_back(std::move(wltUtxos));
            }

            if (opRequest.getZc()) {
               auto zcUtxos = wltPtr->getSpendableTxOutListZC();
               count += zcUtxos.size();
               utxos.emplace_back(std::move(zcUtxos));
            }

            if (opRequest.getRbf()) {
               auto rbfUtxos = wltPtr->getRBFTxOutList();
               count += rbfUtxos.size();
               utxos.emplace_back(std::move(rbfUtxos));
            }

            auto capnOutputs = walletReply.initGetOutputs(count);
            unsigned i=0;
            for (const auto& utxoV : utxos) {
               for (const auto& utxo : utxoV) {
                  auto capnOutput = capnOutputs[i++];
                  outputToCapn(utxo, capnOutput);
               }
            }
            break;
         }

         case WalletRequest::Which::SET_CONF_TARGET:
         {
            wltPtr->setConfTarget(request.getSetConfTarget(), {});
            break;
         }

         case WalletRequest::Which::UNREGISTER_ADDRESSES:
         {
            auto capnAddrs = request.getUnregisterAddresses();
            std::set<BinaryDataRef> addresses;
            for (auto capnAddr : capnAddrs) {
               auto addrBody = capnAddr.getBody();
               addresses.emplace(addrBody.begin(), addrBody.end());
            }

            wltPtr->unregisterAddresses(addresses);
            break;
         }

         default:
            reply.setSuccess(false);
            reply.setError("invalid bdv request");
      }

      return result;
   }

   std::unique_ptr<capnp::MessageBuilder> parseAddressCommand(
      AddressRequest::Reader request,
      std::shared_ptr<BDV_Server_Object> bdv,
      uint32_t msgId)
   {
      //prepare result
      auto result = std::make_unique<capnp::MallocMessageBuilder>();
      auto reply = result->initRoot<Codec::BDV::Reply>();
      reply.setMsgId(msgId);
      reply.setSuccess(true);
      auto addressReply = reply.initAddress();

      //get scrAddr bdref
      auto capnAddr = request.getAddress();
      auto addrBody = capnAddr.getBody();
      BinaryDataRef addrRef(addrBody.begin(), addrBody.end());

      //switch on the method
      switch (request.which())
      {
         case AddressRequest::Which::GET_LEDGER_DELEGATE:
         {
            auto delegateId = bdv->getLedgerDelegate(addrRef);
            addressReply.setGetLedgerDelegate(delegateId);
            break;
         }

         case AddressRequest::Which::GET_BALANCE_AND_COUNT:
         {
            auto balances = bdv->getAddrFullBalance(addrRef);
            auto capnBalance = addressReply.initGetBalanceAndCount();
            capnBalance.setFull(std::get<0>(balances));
            capnBalance.setTxnCount(std::get<1>(balances));
            break;
         }

         case AddressRequest::Which::GET_OUTPUTS:
         {
            auto utxoReq = request.getGetOutputs();
            auto utxos = bdv->getUtxosForAddress(
               addrRef, utxoReq.getZc());

            auto outputs = addressReply.initGetOutputs(utxos.size());
            unsigned i=0;
            for (const auto& utxo : utxos) {
               auto capnOutput = outputs[i++];
               outputToCapn(utxo, capnOutput);
            }
            break;
         }

         default:
            reply.setSuccess(false);
            reply.setError("invalid bdv request");
      }

      return result;
   }

   std::unique_ptr<capnp::MessageBuilder> parseLedgerCommand(
      LedgerRequest::Reader request,
      std::shared_ptr<BDV_Server_Object> bdv,
      uint32_t msgId)
   {
      //prepare result
      auto result = std::make_unique<capnp::MallocMessageBuilder>();
      auto reply = result->initRoot<Codec::BDV::Reply>();
      reply.setMsgId(msgId);

      //get delegate, acts as sanity check
      auto delegateId = std::string(request.getLedgerId());
      auto delegateIter = bdv->delegateMap_.find(delegateId);
      if (delegateIter == bdv->delegateMap_.end()) {
         reply.setError("unknown delegate id");
         reply.setSuccess(false);
         return result;
      }

      //more result preparation
      reply.setSuccess(true);
      auto ledgerReply = reply.initLedger();

      //switch on the method
      switch (request.which())
      {
         case LedgerRequest::Which::GET_PAGE_COUNT:
         {
            ledgerReply.setGetPageCount(delegateIter->second.getPageCount());
            break;
         }

         case LedgerRequest::Which::GET_HISTORY_PAGES:
         {
            auto pagesReq = request.getGetHistoryPages();
            std::list<std::vector<LedgerEntry>> pages;
            for (unsigned i=pagesReq.getFrom(); i<=pagesReq.getTo(); i++) {
               auto page = delegateIter->second.getHistoryPage(i);
               pages.emplace_back(std::move(page));
            }

            auto capnPages = ledgerReply.initGetHistoryPages(pages.size());
            unsigned i=0;
            for (const auto& page : pages) {
               auto capnPage = capnPages[i++];
               historyPageToCapn(page, capnPage);
            }
            break;
         }

         default:
            reply.setSuccess(false);
            reply.setError("invalid bdv request");
      }

      return result;
   }

   ////
   std::unique_ptr<capnp::MessageBuilder> parseRequest(
      Request::Reader& request, unsigned msgId,
      std::shared_ptr<BDV_Server_Object> bdv)
   {
      switch (request.which())
      {
         case Request::Which::BDV:
            return parseBDVCommand(request.getBdv(), bdv,
               request.getMsgId());

         case Request::Which::WALLET:
            return parseWalletCommand(request.getWallet(), bdv,
               request.getMsgId());

         case Request::Which::ADDRESS:
            return parseAddressCommand(request.getAddress(), bdv,
               request.getMsgId());

         case Request::Which::LEDGER:
            return parseLedgerCommand(request.getLedger(), bdv,
               request.getMsgId());

         default:
            auto result = std::make_unique<capnp::MallocMessageBuilder>();
            auto reply = result->initRoot<Codec::BDV::Reply>();
            reply.setMsgId(msgId);
            reply.setSuccess(false);
            reply.setError("invalid request");
            return result;
      }
   }

   ////
   std::unique_ptr<capnp::MessageBuilder> parseStaticRequest(
      StaticRequest::Reader& request, unsigned msgId, Clients* clients,
      std::shared_ptr<BDV_Server_Object> bdv)
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
            const auto& thisCookie = Armory::Config::NetworkSettings::cookie();
            if (thisCookie.empty()) {
               //we do not inform the caller whether we accept cookies or not
               break;
            }

            auto shutdownLambda = [clients](void)->void
            {
               clients->exitRequestLoop();
            };
            //run shutdown sequence in its own thread so that the server listen
            //loop can exit properly.
            std::thread shutdownThr(shutdownLambda);
            if (shutdownThr.joinable()) {
               shutdownThr.detach();
            }
            break;
         }

         case StaticRequest::Which::SHUTDOWN_NODE:
         {
            if (clients->bdmT()->bdm()->nodeRPC_ != nullptr) {
               clients->bdmT()->bdm()->nodeRPC_->shutdown();
            }
            break;
         }

         case StaticRequest::Which::REGISTER:
         {
            std::string magicWord(request.getMagicWord());
            auto bdvId = clients->registerBDV(magicWord);
            if (bdvId.empty()) {
               reply.setError("failed to register new bdv");
               reply.setSuccess(false);
            }
            staticReply.setRegister(bdvId);
         }

         case StaticRequest::Which::RPC_BROADCAST:
         {
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
            clients->p2pBroadcast(bdv->getID(), rawZcVec);
            break;
         }

         default:
            reply.setError("invalid static request");
            reply.setSuccess(false);
      }

      return result;
   }
}

///////////////////////////////////////////////////////////////////////////////
std::shared_ptr<BDV_Server_Object> Clients::get(const std::string& id) const
{
   auto bdvmap = BDVs_.get();
   auto iter = bdvmap->find(id);
   if (iter == bdvmap->end())
      return nullptr;

   return iter->second;
}

///////////////////////////////////////////////////////////////////////////////
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
      if (lbdFut.wait_for(0s) == std::future_status::ready)
      {
         return bc->top()->getBlockHeight();
      }

      return UINT32_MAX;
   };

   switch (Armory::Config::DBSettings::getServiceType())
   {
   case SERVICE_WEBSOCKET:
   case SERVICE_UNITTEST_WITHWS:
   {
      auto&& bdid = READHEX(getID());
      if (bdid.getSize() != 8) {
         throw std::runtime_error("invalid bdv id");
      }

      auto intid = (uint64_t*)bdid.getPtr();
      notifications_ = std::make_unique<WS_Callback>(*intid);
      break;
   }
   
   case SERVICE_UNITTEST:
      notifications_ = std::make_unique<UnitTest_Callback>();
      break;

   default:
      throw std::runtime_error("unexpected service type");
   }
}

///////////////////////////////////////////////////////////////////////////////
BDV_Server_Object::BDV_Server_Object(
   const std::string& id, BlockDataManagerThread *bdmT) :
   BlockDataViewer(bdmT->bdm()), bdvID_(id), bdmT_(bdmT)
{
   setup();
}

///////////////////////////////////////////////////////////////////////////////
void BDV_Server_Object::startThreads()
{
   if (started_.fetch_or(1, std::memory_order_relaxed) != 0)
      return;
   
   auto initLambda = [this](void)->void
   { this->init(); };

   initT_ = std::thread(initLambda);
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
   bdmPtr_->blockUntilReady();
   while (true)
   {
      std::map<std::string, WalletRegistrationRequest> wltMap;

      {
         std::unique_lock<std::mutex> lock(registerWalletMutex_);

         if (wltRegMap_.empty()) {
            break;
         }

         wltMap = std::move(wltRegMap_);
         wltRegMap_.clear();
      }

      //create address batch
      auto batch = std::make_shared<RegistrationBatch>();
      batch->isNew_ = false;

      //fill with addresses from protobuf payloads
      for (const auto& wlt : wltMap)
      {
         for (const auto& addr : wlt.second.addresses) {
            batch->scrAddrSet_.insert(addr);
         }
      }

      //callback only serves to wait on the registration event
      auto promPtr = std::make_shared<std::promise<bool>>();
      auto fut = promPtr->get_future();
      auto callback = [promPtr](std::set<BinaryData>&)->void
      {
         promPtr->set_value(true);
      };
      batch->callback_ = callback;

      //register the batch
      auto saf = bdmPtr_->getScrAddrFilter();
      saf->pushAddressBatch(batch);
      fut.get();

      //addresses are now registered, populate the wallet maps
      populateWallets(wltMap);
   }

   //could a wallet registration event get lost in between the init loop 
   //and setting the promise?

   //init wallets
   auto notifPtr = std::make_unique<BDV_Notification_Init>();
   scanWallets(std::move(notifPtr));

   //create zc packet and pass to wallets
   auto addrSet = getAddrSet();
   auto zcstruct = createZcNotification(addrSet);
   auto zcAction = dynamic_cast<BDV_Notification_ZC*>(zcstruct.get());
   if (zcAction != nullptr &&
      !zcAction->packet_.scrAddrToTxioKeys_.empty()) {
      scanWallets(move(zcstruct));
   }

   //mark bdv object as ready
   isReadyPromise_->set_value(true);

   //callback client with BDM_Ready packet
   capnp::MallocMessageBuilder message;
   auto notifs = message.initRoot<Codec::BDV::Notifications>();
   auto notifList = notifs.initNotifs(1);
   auto notif = notifList[0];
   auto readyNotif = notif.initReady();
   readyNotif.setHeight(blockchain().top()->getBlockHeight());

   auto flat = capnp::messageToFlatArray(message);
   auto bytes = flat.asBytes();
   BinaryData payload(bytes.begin(), bytes.end());
   notifications_->push(payload);
}

///////////////////////////////////////////////////////////////////////////////
void BDV_Server_Object::processNotification(
   std::shared_ptr<BDV_Notification> notifPtr)
{
   auto action = notifPtr->action_type();
   if (action < BDV_Progress) {
      //skip all but progress notifications if BDV isn't ready
      if (isReadyFuture_.wait_for(0s) != std::future_status::ready) {
         return;
      }
   }

   scanWallets(notifPtr);

   capnp::MallocMessageBuilder message;
   auto notifs = message.initRoot<Codec::BDV::Notifications>();

   switch (action)
   {
      case BDV_NewBlock:
      {
         auto payload =
            std::dynamic_pointer_cast<BDV_Notification_NewBlock>(notifPtr);

         //init notif builder
         bool haveZcs = false;
         if (payload->zcPurgePacket_ != nullptr &&
            !payload->zcPurgePacket_->invalidatedZcKeys_.empty()) {
            notifs.initNotifs(2);
            haveZcs = true;
         } else {
            notifs.initNotifs(1);
         }

         //new block height
         auto notifList = notifs.getNotifs();
         auto heightNotif = notifList[0];
         auto blockData = heightNotif.initNewBlock();

         blockData.setHeight(payload->reorgState_.newTop_->getBlockHeight());
         if (!payload->reorgState_.prevTopStillValid_) {
            blockData.setBranchHeight(
               payload->reorgState_.reorgBranchPoint_->getBlockHeight());
         }

         //invalidated zc ids
         if (haveZcs) {
            auto zcNotif = notifList[1];
            auto zcIdList = zcNotif.initInvalidatedZc(
               payload->zcPurgePacket_->invalidatedZcKeys_.size());

            unsigned i=0;
            for (const auto& zcId : payload->zcPurgePacket_->invalidatedZcKeys_) {
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
         refreshNotif.setType((uint32_t)payload->refresh_);

         const auto& bdId = payload->refreshID_;
         auto refreshIds = refreshNotif.initIds(1);
         refreshIds.set(0, capnp::Data::Builder(
            (uint8_t*)bdId.getPtr(), bdId.getSize()));
         break;
      }

      case BDV_ZC:
      {
         auto notifList = notifs.initNotifs(1);
         auto notif = notifList[0];
         auto zcNotif = notif.initZc();

         auto payload =
            std::dynamic_pointer_cast<BDV_Notification_ZC>(notifPtr);
         historyPageToCapn(payload->leVec_, zcNotif);
         break;
      }

      case BDV_Progress:
      {
         auto notifList = notifs.initNotifs(1);
         auto notif = notifList[0];
         auto progressNotif = notif.initProgress();

         auto payload =
            std::dynamic_pointer_cast<BDV_Notification_Progress>(notifPtr);

         progressNotif.setPhase((uint32_t)payload->phase_);
         progressNotif.setProgress(payload->progress_);
         progressNotif.setTime(payload->time_);
         progressNotif.setNumericProgress(payload->numericProgress_);

         auto progressIds = progressNotif.initIds(payload->walletIDs_.size());
         unsigned i=0;
         for (const auto& id : payload->walletIDs_) {
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

         auto& nodeStatus = payload->status_;

         nodeNotif.setNode((Codec::BDV::NodeStatus::NodeState)nodeStatus.state_);
         nodeNotif.setIsSW(nodeStatus.SegWitEnabled_);
         nodeNotif.setRpc((Codec::BDV::NodeStatus::RpcState)nodeStatus.rpcState_);

         auto chainNotif = nodeNotif.getChain();
         chainNotif.setChainState((Codec::BDV::ChainStatus::ChainState)
            nodeStatus.chainStatus_.state());
         chainNotif.setBlockSpeed(nodeStatus.chainStatus_.getBlockSpeed());
         chainNotif.setEta(nodeStatus.chainStatus_.getETA());
         chainNotif.setProgress(nodeStatus.chainStatus_.getProgressPct());
         chainNotif.setBlocksLeft(nodeStatus.chainStatus_.getBlocksLeft());
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

   auto flat = capnp::messageToFlatArray(message);
   auto bytes = flat.asBytes();
   BinaryData payload(bytes.begin(), bytes.end());
   notifications_->push(payload);
}

///////////////////////////////////////////////////////////////////////////////
void BDV_Server_Object::registerWallet(WalletRegistrationRequest& regReq)
{
   if (isReadyFuture_.wait_for(0s) != std::future_status::ready)
   {
      //only run this code if the bdv maintenance thread hasn't started yet
      std::unique_lock<std::mutex> lock(registerWalletMutex_);

      //save data
      wltRegMap_.emplace(regReq.walletId, std::move(regReq));
      return;
   }

   //register wallet with BDV
   auto bdvPtr = (BlockDataViewer*)this;
   bdvPtr->registerWallet(regReq);
}

///////////////////////////////////////////////////////////////////////////////
void BDV_Server_Object::populateWallets(
   std::map<std::string, WalletRegistrationRequest>& wltMap)
{
   auto safPtr = getSAF();
   auto addrMap = safPtr->getScanFilterAddrMap();

   for (const auto& wlt : wltMap) {
      std::shared_ptr<BtcWallet> theWallet;
      if (wlt.second.type == WalletRegType::WALLET) {
         theWallet = groups_[group_wallet].getOrSetWallet(wlt.first);
      } else {
         theWallet = groups_[group_lockbox].getOrSetWallet(wlt.first);
      }

      if (theWallet == nullptr) {
         LOGERR << "failed to get or set wallet";
         continue;
      }

      std::map<BinaryDataRef, std::shared_ptr<ScrAddrObj>> newAddrMap;
      for (const auto& addr : wlt.second.addresses) {
         if (theWallet->hasScrAddress(addr)) {
            continue;
         }

         auto iter = addrMap->find(addr);
         if (iter == addrMap->end()) {
            throw std::runtime_error("address missing from saf");
         }

         auto addrObj = std::make_shared<ScrAddrObj>(
            db_, &blockchain(), zeroConfCont_.get(), iter->first);
         newAddrMap.emplace(iter->first, addrObj);
      }

      if (newAddrMap.empty()) {
         continue;
      }

      theWallet->scrAddrMap_.update(newAddrMap);
   }
}

////////////////////////////////////////////////////////////////////////////////
void BDV_Server_Object::flagRefresh(
   BDV_refresh refresh, const BinaryData& refreshID,
   std::unique_ptr<BDV_Notification_ZC> zcPtr)
{
   auto notif = std::make_unique<BDV_Notification_Refresh>(
      getID(), refresh, refreshID);
   if (zcPtr != nullptr) {
      notif->zcPacket_ = std::move(zcPtr->packet_);
   }

   if (notifLambda_) {
      notifLambda_(std::move(notif));
   }
}

////////////////////////////////////////////////////////////////////////////////
BDV_PartialMessage BDV_Server_Object::preparePayload(
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
   if (!packet->packetData_.empty()) {
      //grab and check the packet's message id
      auto msgId = BDV_PartialMessage::getMessageId(packet);
      if (msgId != UINT32_MAX) {
         //get the PartialMessage object for this id
         auto msgIter = messageMap_.find(msgId);
         if (msgIter == messageMap_.end()) {
            //create this PartialMessage if it's missing
            msgIter = messageMap_.emplace(msgId, BDV_PartialMessage()).first;
         }
         auto& msgRef = msgIter->second;

         //try to reconstruct the message
         auto parsed = msgRef.parsePacket(packet);
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
   packet->messageID_ = nextId;

   //parse the payload
   return msgObj;
}

///////////////////////////////////////////////////////////////////////////////
//
// Clients
//
///////////////////////////////////////////////////////////////////////////////
void Clients::init(BlockDataManagerThread* bdmT,
   std::function<void(void)> shutdownLambda)
{
   bdmT_ = bdmT;
   shutdownCallback_ = shutdownLambda;

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
   if (Armory::Config::DBSettings::getDbType() == ARMORY_DB_SUPER &&
      Armory::Config::DBSettings::getServiceType() != SERVICE_UNITTEST) {
      innerThreadCount = std::thread::hardware_concurrency();
   }
   for (unsigned i = 0; i < innerThreadCount; i++) {
      controlThreads_.push_back(std::thread(innerthread));
      controlThreads_.push_back(std::thread(parserThread));
   }

   auto callbackPtr = std::make_unique<ZeroConfCallbacks_BDV>(this);
   bdmT_->bdm()->registerZcCallbacks(std::move(callbackPtr));
}

///////////////////////////////////////////////////////////////////////////////
void Clients::bdvMaintenanceLoop()
{
   while (true) {
      std::shared_ptr<BDV_Notification> notifPtr;
      try {
         notifPtr = std::move(outerBDVNotifStack_.pop_front());
      } catch (const Threading::StopBlockingLoop&) {
         LOGINFO << "Shutting down BDV event loop";
         break;
      }

      auto bdvMap = BDVs_.get();
      const auto& bdvID = notifPtr->bdvID();
      if (bdvID.empty()) {
         //empty bdvID means broadcast notification to all BDVs
         for (const auto& bdv_pair : *bdvMap) {
            auto notifPacket = std::make_shared<BDV_Notification_Packet>();
            notifPacket->bdvPtr_ = bdv_pair.second;
            notifPacket->notifPtr_ = notifPtr;
            innerBDVNotifStack_.push_back(std::move(notifPacket));
         }
      } else {
         //grab bdv
         auto iter = bdvMap->find(bdvID);
         if (iter == bdvMap->end()) {
            continue;
         }

         auto notifPacket = std::make_shared<BDV_Notification_Packet>();
         notifPacket->bdvPtr_ = iter->second;
         notifPacket->notifPtr_ = notifPtr;
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

      if (notifPtr->bdvPtr_ == nullptr) {
         LOGWARN << "null bdvPtr in notification";
         continue;
      }

      auto bdvPtr = notifPtr->bdvPtr_;
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

      bdvPtr->processNotification(notifPtr->notifPtr_);
      bdvPtr->notificationProcess_threadLock_.store(0);
   }
}

///////////////////////////////////////////////////////////////////////////////
void Clients::shutdown()
{
   std::unique_lock<std::mutex> lock(shutdownMutex_, std::defer_lock);
   if (!lock.try_lock()) {
      return;
   }
   
   /*shutdown sequence*/
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
   if (!bdmT_->shutdown()) {
      return;
   }

   std::vector<std::thread::id> idVec;
   for (auto& thr : controlThreads_) {
      idVec.push_back(thr.get_id());
      if (thr.joinable()) {
         thr.join();
      }
   }

   //shutdown ZC container
   bdmT_->bdm()->disableZeroConf();
   bdmT_->bdm()->getScrAddrFilter()->shutdown();
}

///////////////////////////////////////////////////////////////////////////////
void Clients::exitRequestLoop()
{
   /*terminate request processing loop*/
   LOGINFO << "proceeding to shutdown";

   //shutdown loop on server side
   if (shutdownCallback_) {
      shutdownCallback_();
   }
}

///////////////////////////////////////////////////////////////////////////////
void Clients::unregisterAllBDVs()
{
   auto bdvs = BDVs_.get();
   BDVs_.clear();

   for (auto& bdv : *bdvs) {
      bdv.second->haltThreads();
   }
}

///////////////////////////////////////////////////////////////////////////////
std::string Clients::registerBDV(const std::string& magicWord)
{
   if (magicWord.empty()) {
      return {};
   }
   auto thisMagicWord =
      Armory::Config::BitcoinSettings::getMagicBytes().toHexStr();
   if (thisMagicWord != magicWord) {
      return {};
   }

   auto bdvID = BtcUtils::fortuna_.generateRandom(10).toHexStr();
   auto newBDV = std::make_shared<BDV_Server_Object>(bdvID, bdmT_);
   auto notiflbd = [this](std::unique_ptr<BDV_Notification> notifPtr)
   {
      this->outerBDVNotifStack_.push_back(std::move(notifPtr));
   };
   newBDV->notifLambda_ = notiflbd;

   //add to BDVs map
   BDVs_.insert(std::make_pair(bdvID, newBDV));
   LOGINFO << "registered bdv: " << bdvID;
   return bdvID;
}

///////////////////////////////////////////////////////////////////////////////
void Clients::unregisterBDV(std::string bdvId)
{
   unregBDVQueue_.push_back(move(bdvId));
}

///////////////////////////////////////////////////////////////////////////////
void Clients::unregisterBDVThread()
{
   while(true) {
      //grab bdv id
      std::string bdvId;
      try {
         bdvId = std::move(unregBDVQueue_.pop_front());
      } catch(const Threading::StopBlockingLoop&) {
         break;
      }
      
      //grab bdv ptr
      std::shared_ptr<BDV_Server_Object> bdvPtr;
      {
         auto bdvMap = BDVs_.get();
         auto bdvIter = bdvMap->find(bdvId);
         if (bdvIter == bdvMap->end()) {
            return;
         }

         //copy shared_ptr and erase from bdv map
         bdvPtr = bdvIter->second;
         BDVs_.erase(bdvId);
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
void Clients::notificationThread() const
{
   if (bdmT_ == nullptr) {
      throw std::runtime_error("invalid BDM thread ptr");
   }

   while (true) {
      bool timedout = true;
      std::shared_ptr<BDV_Notification> notifPtr;

      try {
         notifPtr = std::move(bdmT_->bdm()->notificationStack_.pop_front(60s));
         if (notifPtr == nullptr) {
            continue;
         }
         timedout = false;
      }
      catch (const Threading::StackTimedOutException&) {
         //nothing to do
      }
      catch (const Threading::StopBlockingLoop&) {
         return;
      }
      catch (const Threading::IsEmpty&) {
         LOGERR << "caught isEmpty in Clients maintenance loop";
         continue;
      }

      //trigger gc thread
      if (timedout == true || notifPtr->action_type() != BDV_Progress) {
         gcCommands_.push_back(true);
      }

      //don't go any futher if there is no new top
      if (timedout) {
         continue;
      }

      outerBDVNotifStack_.push_back(std::move(notifPtr));
   }
}

///////////////////////////////////////////////////////////////////////////////
void Clients::messageParserThread(void)
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

      if (payloadPtr->bdvPtr_ == nullptr) {
         LOGERR << "???????? empty bdv ptr";
         continue;
      }

      auto bdvPtr = payloadPtr->bdvPtr_;
      unsigned zero = 0;
      if (bdvPtr && !bdvPtr->packetProcess_threadLock_.compare_exchange_weak(
         zero, 1, std::memory_order_relaxed, std::memory_order_relaxed)) {
         //Failed to grab lock, there's already a thread processing a payload
         //for this bdv. Insert the payload back into the queue. Another 
         //thread will eventually pick it up and successfully grab the lock 
         if(payloadPtr == nullptr) {
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

      /** NOTE: deal with nullptr bdvPtr **/
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
            auto flagPacket = std::make_shared<BDV_Payload>();
            flagPacket->bdvPtr_ = bdvPtr;
            flagPacket->bdvID_ = payloadPtr->bdvID_;
            packetQueue_.push_back(std::move(flagPacket));
         }
      }
      //release the locks
      lock.unlock();
      bdvPtr->packetProcess_threadLock_.store(0);

      //write return value if any
      if (!result.empty()) {
         WebSocketServer::write(
            payloadPtr->bdvID_, payloadPtr->messageID_, result);
      }
   }
}

///////////////////////////////////////////////////////////////////////////////
void Clients::broadcastThroughRPC()
{
   auto notifyError = [this](
      const BinaryData& hash, std::shared_ptr<BDV_Server_Object> bdvPtr,
      int errCode, const std::string& verbose,
      const std::string& requestID)->void
   {
      auto notifPacket = std::make_shared<BDV_Notification_Packet>();
      notifPacket->bdvPtr_ = bdvPtr;
      notifPacket->notifPtr_ = std::make_shared<BDV_Notification_Error>(
         bdvPtr->getID(), requestID, errCode, hash, verbose);
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
      auto zcPtr = bdmT_->bdm()->zeroConfCont();

      //feed the watcher map with all relevant requestor/bdv ids
      {
         //if this is a RPC fallback from a timed out P2P zc push
         //we may have extra requestors attached to this broadcast
         std::map<std::string, std::string> extraRequestors;
         for (auto& reqPair : packet.extraRequestors_) {
            extraRequestors.emplace(reqPair.first, reqPair.second->getID());
         }

         if (!zcPtr->insertWatcherEntry(
            *hashes.begin(), packet.rawTx_, //tx
            packet.bdvPtr_->getID(), packet.requestID_, //main requestor
            extraRequestors, //extra requestor, in case this is a fallback
            false)) //do not process watcher node invs for this entry
         {
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
         packet.bdvPtr_->getID(),
         packet.requestID_);

      //push to rpc
      std::string verbose;
      auto result = bdmT_->bdm()->nodeRPC_->broadcastTx(
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
               auto txPtr = batchPtr->zcMap_.begin()->second;
               txPtr->tx_.unserialize(*packet.rawTx_);
               txPtr->tx_.setTxTime(time(0));
               batchPtr->isReadyPromise_->set_value(ArmoryErrorCodes::Success);
            } catch (const std::future_error&) {
               LOGWARN << "rpc broadcast promise was already set";
            }

            //signal all extra requestors for an already-in-mempool error
            for (auto& requestor : packet.extraRequestors_) {
               notifyError(*hashes.begin(), requestor.second,
                  (int)ArmoryErrorCodes::ZcBroadcast_AlreadyInMempool, 
                  "Extra requestor RPC broadcast error: Already in mempool",
                  requestor.first);
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
               /*
               The watcher entry may have received extra requestors we
               didn't start with. We need to add those to our RPC packet
               requestor map. Those carry full on BDV objects so we need
               to curate the map first (for our own extra requestors), then
               resolve the IDs to the BDV objects.
               */
               auto extraReqIter = watcherEntry->extraRequestors_.begin();
               while (extraReqIter != watcherEntry->extraRequestors_.end()) {
                  if (packet.extraRequestors_.find(extraReqIter->first) !=
                     packet.extraRequestors_.end()) {
                     watcherEntry->extraRequestors_.erase(extraReqIter++);
                     continue;
                  }
                  ++extraReqIter;
               }

               if (!watcherEntry->extraRequestors_.empty()) {
                  auto bdvMap = BDVs_.get();
                  for (auto& extraReq : watcherEntry->extraRequestors_) {
                     auto bdvIter = bdvMap->find(extraReq.second);
                     if (bdvIter == bdvMap->end()) {
                        continue;
                     }

                     packet.extraRequestors_.emplace(
                        extraReq.first, bdvIter->second);
                  }
               }
            }

            //fail the batch promise
            batchPtr->isReadyPromise_->set_exception(
               std::make_exception_ptr(ZcBatchError()));

            //notify the bdv of the error
            std::stringstream errMsg;
            errMsg << "RPC broadcast error: " << verbose;
            notifyError(*hashes.begin(), packet.bdvPtr_,
               result, errMsg.str(), packet.requestID_);

            //notify extra requestors of the error as well
            for (auto& requestor : packet.extraRequestors_) {
               std::stringstream reqMsg;
               reqMsg << "Extra requestor broadcast error: " << verbose;
               notifyError(*hashes.begin(), requestor.second,
                  result, reqMsg.str(), requestor.first);
            }
      }
   }
}

///////////////////////////////////////////////////////////////////////////////
BinaryData Clients::processCommand(std::shared_ptr<BDV_Payload> payload)
{
   //clear bdvPtr from the payload to avoid circular ownership
   auto bdvPtr = payload->bdvPtr_;
   payload->bdvPtr_.reset();

   //process payload
   auto preparedPayload = bdvPtr->preparePayload(payload);
   if (!preparedPayload.isReady()) {
      return {};
   }

   auto reader = preparedPayload.getReader();
   if (reader == nullptr) {
      return {};
   }

   auto request = reader->getRoot<Codec::BDV::Request>();
   switch (request.which())
   {
      case Codec::BDV::Request::Which::STATIC:
      {
         //process static command
         auto staticRequest = request.getStatic();
         auto builderPtr = parseStaticRequest(
            staticRequest, request.getMsgId(), this, bdvPtr);
         auto flat = capnp::messageToFlatArray(*builderPtr);
         auto bytes = flat.asBytes();
         return BinaryData(bytes.begin(), bytes.end());
      }

      default:
         auto builderPtr = parseRequest(request, request.getMsgId(), bdvPtr);
         auto flat = capnp::messageToFlatArray(*builderPtr);
         auto bytes = flat.asBytes();
         return BinaryData(bytes.begin(), bytes.end());
   }
}

void Clients::rpcBroadcast(RpcBroadcastPacket& packet)
{
   rpcBroadcastQueue_.push_back(std::move(packet));
}

void Clients::p2pBroadcast(
   const std::string& bdvId, std::vector<BinaryDataRef>& rawZCs)
{
   //run through submitted ZCs, prune already mined ones
   auto db = bdmT()->bdm()->getIFace();
   for (auto& rawZcRef : rawZCs) {
      Tx tx(rawZcRef);
      auto hash = tx.getThisHash();

      auto dbKey = db->getDBKeyForHash(hash);
      if (!dbKey.empty()) {
         //notify the bdv of the error
         auto notifPacket = std::make_shared<BDV_Notification_Packet>();
         notifPacket->notifPtr_ = std::make_shared<BDV_Notification_Error>(
            bdvId, "",
            (int)ArmoryErrorCodes::ZcBroadcast_AlreadyInChain,
            hash, "RPC broadcast error: Already in chain"
         );
         innerBDVNotifStack_.push_back(std::move(notifPacket));

         //reset data ref so as to not parse the zc
         rawZcRef.reset();
      }
   }

   auto errorCallback = [this, bdvId](
      std::vector<ZeroConfBatchFallbackStruct> zcVec)->void
   {
      std::vector<RpcBroadcastPacket> rpcPackets;
      auto bdvMap = BDVs_.get();
      auto bdvPtr = bdvMap->at(bdvId);
      for (const auto& fallbackStruct : zcVec) {
         std::map<std::string, std::shared_ptr<BDV_Server_Object>> extraRequestors;
         for (const auto& extraBdvId : fallbackStruct.extraRequestors_) {
            auto iter = bdvMap->find(extraBdvId.second);
            if (iter == bdvMap->end()) {
               continue;
            }
            extraRequestors.emplace(extraBdvId.first, iter->second);
         }

         if (fallbackStruct.err_ != ArmoryErrorCodes::ZcBatch_Timeout) {
            //signal error to caller
            auto notifPacket = std::make_shared<BDV_Notification_Packet>();
            notifPacket->bdvPtr_ = bdvPtr;
            notifPacket->notifPtr_ = std::make_shared<BDV_Notification_Error>(
               bdvId, "", (int)fallbackStruct.err_,
               fallbackStruct.txHash_, ""
            );
            innerBDVNotifStack_.push_back(std::move(notifPacket));

            //then signal extra requestors
            for (const auto& extraBDV : extraRequestors) {
               auto notifPacket = std::make_shared<BDV_Notification_Packet>();
               notifPacket->bdvPtr_ = extraBDV.second;
               notifPacket->notifPtr_ = std::make_shared<BDV_Notification_Error>(
                  extraBDV.second->getID(), extraBDV.first, (int)fallbackStruct.err_,
                  fallbackStruct.txHash_, ""
               );
               innerBDVNotifStack_.push_back(std::move(notifPacket));
            }

            //finally, skip RPC fallback
            continue;
         }

         //tally timed out zc
         RpcBroadcastPacket packet;
         packet.rawTx_ = fallbackStruct.rawTxPtr_;
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
   bdmT()->bdm()->zeroConfCont_->broadcastZC(
      rawZCs, 5000, errorCallback, bdvId, {});
}

///////////////////////////////////////////////////////////////////////////////
//
// Callback
//
///////////////////////////////////////////////////////////////////////////////
Callback::~Callback()
{}

///////////////////////////////////////////////////////////////////////////////
void WS_Callback::push(BinaryData& payload)
{
   //write to socket
   WebSocketServer::write(bdvID_, WEBSOCKET_CALLBACK_ID, payload);
}

///////////////////////////////////////////////////////////////////////////////
void UnitTest_Callback::push(BinaryData& payload)
{
   //stash the notification, unit test will pull it as needed
   notifQueue_.push_back(std::move(payload));
}

///////////////////////////////////////////////////////////////////////////////
BinaryData UnitTest_Callback::getNotification()
{
   try {
      return notifQueue_.pop_front();
   }
   catch (const Threading::StopBlockingLoop&) {}
   return {};
}

///////////////////////////////////////////////////////////////////////////////
//
// BDV_PartialMessage
//
///////////////////////////////////////////////////////////////////////////////
bool BDV_PartialMessage::parsePacket(std::shared_ptr<BDV_Payload> packet)
{
   auto bdr = packet->packetData_.getRef();
   auto result = partialMessage_.parsePacket(bdr);
   if (!result) {
      return false;
   }

   payloads_.emplace_back(packet);
   return true;
}

///////////////////////////////////////////////////////////////////////////////
void BDV_PartialMessage::reset()
{
   partialMessage_.reset();
   payloads_.clear();
}

///////////////////////////////////////////////////////////////////////////////
std::shared_ptr<capnp::MessageReader> BDV_PartialMessage::getReader()
{
   if (!isReady()) {
      return nullptr;
   }

   return partialMessage_.getReader();
}

///////////////////////////////////////////////////////////////////////////////
size_t BDV_PartialMessage::topId() const
{
   auto& packetMap = partialMessage_.getPacketMap();
   if (packetMap.size() == 0)
      return SIZE_MAX;

   return packetMap.rbegin()->first;
}

///////////////////////////////////////////////////////////////////////////////
unsigned BDV_PartialMessage::getMessageId(std::shared_ptr<BDV_Payload> packet)
{
   return WebSocketMessagePartial::getMessageId(packet->packetData_.getRef());
}