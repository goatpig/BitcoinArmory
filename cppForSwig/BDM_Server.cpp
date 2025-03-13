////////////////////////////////////////////////////////////////////////////////
//                                                                            //
//  Copyright (C) 2016-2024, goatpig.                                         //
//  Distributed under the MIT license                                         //
//  See LICENSE-MIT or https://opensource.org/licenses/MIT                    //
//                                                                            //
////////////////////////////////////////////////////////////////////////////////

#include "BDM_Server.h"
#include "ArmoryErrors.h"
#include "SocketWritePayload.h"
#include "BlockchainDatabase/BlockUtils.h"

#include <capnp/message.h>
#include <capnp/serialize.h>
#include "capnp/BDV.capnp.h"

using namespace Armory;
using namespace std::chrono_literals;

#define BDVID_LENGTH    8
#define SCRATCHPAD_SIZE 4048

namespace {
   using namespace Armory::Codec::BDV;

   void utxoToCapn(const UTXO& utxo,
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
         (uint8_t*)hash.getPtr(), hash.getSize()
      ));
   }

   void outputToCapn(const Output& output,
      Codec::Types::Output::Builder& result)
   {
      utxoToCapn(output, result);
      if (output.isSpent()) {
         result.setSpenderHash(capnp::Data::Builder(
            (uint8_t*)output.spenderHash.getPtr(),
            output.spenderHash.getSize()
         ));
      }
   }

   void stxoToCapn(const StoredTxOut& output,
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

      if (!output.spenderHash_.empty()) {
         result.setSpenderHash(capnp::Data::Builder(
            (uint8_t*)output.spenderHash_.getPtr(),
            output.spenderHash_.getSize()
         ));
      }
   }

   void historyPageToCapn(const std::vector<LedgerEntry>& page,
      Codec::Types::TxLedger::Builder& result)
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
            std::string walletId(walletRequest.getWalletId());

            auto capnAddresses = walletRequest.getAddresses();
            std::vector<BinaryData> addresses;
            addresses.reserve(capnAddresses.size());
            for (auto capnAddr : capnAddresses) {
               auto addrBody = capnAddr.getBody();
               addresses.emplace_back(BinaryData(addrBody.begin(), addrBody.end()));
            }

            auto walletType = WalletRegType(walletRequest.getWalletType());
            WalletRegistrationRequest regReq(walletId,
               addresses, walletRequest.getIsNew(), walletType
            );
            bdv->registerWallet(regReq);
            auto builder = ReplyBuilder::getNew(bdv);
            auto bdvReply = prepareReply(builder);
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

         case BdvRequest::Which::GET_LEDGER_DELEGATE:
         {
            auto builder = ReplyBuilder::getNew(bdv);
            auto bdvReply = prepareReply(builder);
            auto delegateId = bdv->getLedgerDelegate();
            bdvReply.setGetLedgerDelegate(delegateId);
            return builder;
         }

         case BdvRequest::Which::GET_TX_BY_HASH:
         {

            auto txHashList = request.getGetTxByHash();
            std::vector<Tx> results;
            results.reserve(txHashList.size());
            for (auto txHash : txHashList) {
               BinaryDataRef hashBd(txHash.begin(), txHash.end());
               auto tx = bdv->getTxByHash(hashBd);
               if (!tx.isInitialized()) {
                  continue;
               }
               results.emplace_back(std::move(tx));
            }

            auto builder = ReplyBuilder::getNew(bdv);
            auto bdvReply = prepareReply(builder);
            auto txHashResults = bdvReply.initGetTxByHash(results.size());
            for (unsigned i=0; i<results.size(); i++) {
               const auto& tx = results[i];
               auto txHashResult = txHashResults[i];
               txHashResult.setBody(capnp::Data::Builder(
                  (uint8_t*)tx.getPtr(), tx.getSize()
               ));
               txHashResult.setHeight(tx.getTxHeight());
               txHashResult.setIndex(tx.getTxIndex());
               txHashResult.setIsChainZc(tx.isChained());
               txHashResult.setIsRbf(tx.isRBF());
            }
            return builder;
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

            auto builder = ReplyBuilder::getNew(bdv);
            auto bdvReply = prepareReply(builder);
            auto capnOutputs = bdvReply.initGetOutputsForOutpoints(
               outputs.size());

            unsigned i=0;
            for (const auto& output : outputs) {
               auto capnOutput = capnOutputs[i++];
               stxoToCapn(output.first, capnOutput);
               capnOutput.setTxHash(capnp::Data::Builder(
                  (uint8_t*)output.second.getPtr(), output.second.getSize()
               ));
            }
            return builder;
         }

         case BdvRequest::Which::GET_OUTPUTS_FOR_ADDRESS:
         {
            auto addrReq = request.getGetOutputsForAddress();
            auto addrList = addrReq.getAddresses();

            std::set<BinaryDataRef> addrSet;
            for (auto addr : addrList) {
               auto addrData = addr.getBody();
               addrSet.emplace(BinaryDataRef(addrData.begin(), addrData.end()));
            }

            auto heightCutoff = addrReq.getHeightCutoff();
            auto zcCutoff = addrReq.getZcCutoff();
            auto result = bdv->getAddressOutpoints(addrSet,
               heightCutoff, zcCutoff);

            auto builder = ReplyBuilder::getNew(bdv);
            auto bdvReply = prepareReply(builder);
            auto reply = bdvReply.initGetOutputsForAddress();
            reply.setHeightCutoff(heightCutoff);
            reply.setZcCutoff(zcCutoff);

            auto capnAddrs = reply.initAddresses(result.size());
            unsigned i=0;
            for (const auto& addrOutputs : result) {
               auto capnAddr = capnAddrs[i++];
               auto addr = capnAddr.getAddr();
               addr.setBody(capnp::Data::Builder(
                  (uint8_t*)addrOutputs.first.getPtr(), addrOutputs.first.getSize()
               ));

               auto capnOutputs = capnAddr.initOutputs(addrOutputs.second.size());
               unsigned y=0;
               for (const auto& output : addrOutputs.second) {
                  auto capnOutput = capnOutputs[y++];
                  outputToCapn(output, capnOutput);
               }
            }
            return builder;
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
            bdv->flagRefresh(BDV_filterChanged, {}, nullptr);
            break;
         }

         case BdvRequest::Which::GET_COMBINED_BALANCES:
         {
            auto bnc = bdv->getCombinedBalances();

            auto builder = ReplyBuilder::getNew(bdv);
            auto bdvReply = prepareReply(builder);
            auto balanceReply = bdvReply.initGetCombinedBalances(bnc.wallets.size());
            unsigned i=0;
            for (const auto& wallet : bnc.wallets) {
               auto capnBnc = balanceReply[i++];
               capnBnc.setId(wallet.first);

               auto capnBalances = capnBnc.getBalances();
               capnBalances.setFull(wallet.second.bnc.full);
               capnBalances.setSpendable(wallet.second.bnc.spendable);
               capnBalances.setUnconfirmed(wallet.second.bnc.unconfirmed);
               capnBalances.setTxnCount(wallet.second.bnc.txnCount);

               auto capnAddrs = capnBnc.initAddresses(wallet.second.addresses.size());
               unsigned y=0;
               for (const auto& addr : wallet.second.addresses) {
                  auto capnAddr = capnAddrs[y++];
                  capnAddr.setScrAddr(capnp::Data::Builder(
                     (uint8_t*)addr.first.getPtr(), addr.first.getSize()
                  ));

                  auto capnBal = capnAddr.getBalances();
                  capnBal.setFull(addr.second.full);
                  capnBal.setSpendable(addr.second.spendable);
                  capnBal.setUnconfirmed(addr.second.unconfirmed);
                  capnBal.setTxnCount(addr.second.txnCount);
               }
            }
            return builder;
         }

         default:
            auto builder = ReplyBuilder::getNew(bdv);
            auto bdvReply = prepareReply(builder);

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
      auto wltPtr = bdv->getWalletOrLockbox(walletId);
      if (wltPtr == nullptr) {
         auto builder = ReplyBuilder::getNew(bdv);
         prepareReply(builder);
         builder.setError("unknown wallet");
         return builder;
      }

      //switch on the method
      switch (request.which())
      {
         case WalletRequest::Which::GET_LEDGER_DELEGATE:
         {
            auto builder = ReplyBuilder::getNew(bdv);
            auto walletReply = prepareReply(builder);
            try {
               auto delegateId = bdv->getLedgerDelegate(walletId);
               walletReply.setGetLedgerDelegate(delegateId);
            } catch (const std::exception& e) {
               builder.setError(e.what());
            }
            return builder;
         }

         case WalletRequest::Which::CREATE_ADDRESS_BOOK:
         {
            auto builder = ReplyBuilder::getNew(bdv);
            auto walletReply = prepareReply(builder);

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
            return builder;
         }

         case WalletRequest::Which::GET_BALANCE_AND_COUNT:
         {
            auto builder = ReplyBuilder::getNew(bdv);
            auto walletReply = prepareReply(builder);

            auto blkHeight = request.getGetBalanceAndCount();
            auto capnBalance = walletReply.initGetBalanceAndCount();
            capnBalance.setFull(wltPtr->getFullBalance());
            capnBalance.setSpendable(wltPtr->getSpendableBalance(blkHeight));
            capnBalance.setUnconfirmed(wltPtr->getUnconfirmedBalance(blkHeight));
            capnBalance.setTxnCount(wltPtr->getWltTotalTxnCount());
            return builder;
         }

         case WalletRequest::Which::GET_OUTPUTS:
         {
            auto builder = ReplyBuilder::getNew(bdv);
            auto walletReply = prepareReply(builder);

            auto opRequest = request.getGetOutputs();
            std::list<std::vector<UTXO>> utxos;
            size_t count = 0;

            auto targetValue = opRequest.getTargetValue();
            if (targetValue > 0) {
               auto wltUtxos = wltPtr->getSpendableTxOutListForValue(
                  targetValue);
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
                  utxoToCapn(utxo, capnOutput);
               }
            }
            return builder;
         }

         case WalletRequest::Which::SET_CONF_TARGET:
         {
            wltPtr->setConfTarget(request.getSetConfTarget());

            //push refersh notif for the wallet
            bdv->flagRefresh(BDV_refreshSkipRescan, walletId, nullptr);
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

            //unregister the addresses
            wltPtr->unregisterAddresses(addresses);

            //push refersh notif for the wallet
            bdv->flagRefresh(BDV_registrationCompleted, walletId, nullptr);
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

   ReplyBuilder parseAddressCommand(
      AddressRequest::Reader request,
      std::shared_ptr<BDV_Server_Object> bdv,
      uint32_t msgId)
   {
      auto prepareReply = [&msgId](ReplyBuilder& rp)
         ->Codec::BDV::AddressReply::Builder
      {
         auto reply = rp.builder->initRoot<Codec::BDV::Reply>();
         reply.setMsgId(msgId);
         reply.setSuccess(true);
         return reply.initAddress();
      };

      //get scrAddr bdref
      auto capnAddr = request.getAddress();
      auto addrBody = capnAddr.getBody();
      BinaryDataRef addrRef(addrBody.begin(), addrBody.end());

      //switch on the method
      switch (request.which())
      {
         case AddressRequest::Which::GET_LEDGER_DELEGATE:
         {
            auto builder = ReplyBuilder::getNew(bdv);
            auto addressReply = prepareReply(builder);
            try {
               std::string walletId = request.getGetLedgerDelegate();
               auto delegateId = bdv->getLedgerDelegate(walletId, addrRef);
               addressReply.setGetLedgerDelegate(delegateId);
            } catch (const std::exception& e) {
               builder.setError(e.what());
            }
            return builder;
         }

         case AddressRequest::Which::GET_BALANCE_AND_COUNT:
         {
            auto balances = bdv->getAddrFullBalance(addrRef);

            auto builder = ReplyBuilder::getNew(bdv);
            auto addressReply = prepareReply(builder);
            auto capnBalance = addressReply.initGetBalanceAndCount();
            capnBalance.setFull(std::get<0>(balances));
            capnBalance.setTxnCount(std::get<1>(balances));
            return builder;
         }

         case AddressRequest::Which::GET_OUTPUTS:
         {
            auto utxoReq = request.getGetOutputs();
            auto utxos = bdv->getUtxosForAddress(
               addrRef, utxoReq.getZc());

            auto builder = ReplyBuilder::getNew(bdv);
            auto addressReply = prepareReply(builder);
            auto outputs = addressReply.initGetOutputs(utxos.size());
            unsigned i=0;
            for (const auto& utxo : utxos) {
               auto capnOutput = outputs[i++];
               utxoToCapn(utxo, capnOutput);
            }
            return builder;
         }

         default:
            auto builder = ReplyBuilder::getNew(bdv);
            prepareReply(builder);
            builder.setError("invalid address request");
      }

      return {};
   }

   ReplyBuilder parseLedgerCommand(
      LedgerRequest::Reader request,
      std::shared_ptr<BDV_Server_Object> bdv,
      uint32_t msgId)
   {
      auto prepareReply = [&msgId](ReplyBuilder& rp)
         ->Codec::BDV::LedgerReply::Builder
      {
         auto reply = rp.builder->initRoot<Codec::BDV::Reply>();
         reply.setMsgId(msgId);
         reply.setSuccess(true);
         return reply.initLedger();
      };

      //get delegate, acts as sanity check
      auto delegateId = std::string(request.getLedgerId());
      auto delegateIter = bdv->delegateMap_.find(delegateId);
      if (delegateIter == bdv->delegateMap_.end()) {
         auto builder = ReplyBuilder::getNew(bdv);
         prepareReply(builder);
         builder.setError("unknown delegate id");
         return builder;
      }

      //switch on the method
      switch (request.which())
      {
         case LedgerRequest::Which::GET_PAGE_COUNT:
         {
            auto builder = ReplyBuilder::getNew(bdv);
            auto ledgerReply = prepareReply(builder);
            ledgerReply.setGetPageCount(delegateIter->second.getPageCount());
            return builder;
         }

         case LedgerRequest::Which::GET_HISTORY_PAGES:
         {
            auto pagesReq = request.getGetHistoryPages();
            std::list<std::vector<LedgerEntry>> pages;
            for (unsigned i=pagesReq.getFirst(); i<=pagesReq.getLast(); i++) {
               try {
                  auto page = delegateIter->second.getHistoryPage(i);
                  if (page.empty()) {
                     break;
                  }
                  pages.emplace_back(std::move(page));
               } catch (const std::range_error&) {
                  break;
               }
            }

            auto builder = ReplyBuilder::getNew(bdv);
            auto ledgerReply = prepareReply(builder);
            auto capnPages = ledgerReply.initGetHistoryPages(pages.size());
            unsigned i=0;
            for (const auto& page : pages) {
               auto capnPage = capnPages[i++];
               historyPageToCapn(page, capnPage);
            }
            return builder;
         }

         default:
            auto builder = ReplyBuilder::getNew(bdv);
            prepareReply(builder);
            builder.setError("invalid bdv request");
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

         case Request::Which::ADDRESS:
            return parseAddressCommand(request.getAddress(), bdv,
               request.getMsgId());

         case Request::Which::LEDGER:
            return parseLedgerCommand(request.getLedger(), bdv,
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
      const std::string& bdvId)
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
            std::string theirCookie = request.getCookie();
            const auto& thisCookie = Armory::Config::NetworkSettings::cookie();
            if (thisCookie.empty() || thisCookie != theirCookie) {
               //we do not inform the caller whether we accept cookies or not
               break;
            }

            //run shutdown sequence in its own thread so that the server listen
            //loop can exit properly.
            std::thread shutdownThr([]{ WebSocketServer::shutdown(); });
            if (shutdownThr.joinable()) {
               shutdownThr.detach();
            }
            break;
         }

         case StaticRequest::Which::SHUTDOWN_NODE:
         {
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
               staticReply.setRegister("sentinel");
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
            nodeReply.setNode((Codec::Types::NodeStatus::NodeState)nodeStatus.state_);
            nodeReply.setIsSW(nodeStatus.SegWitEnabled_);
            nodeReply.setRpc((Codec::Types::NodeStatus::RpcState)nodeStatus.rpcState_);

            auto chainNotif = nodeReply.initChain();
            chainNotif.setChainState((Codec::Types::ChainStatus::ChainState)
               nodeStatus.chainStatus_.state());
            chainNotif.setBlockSpeed(nodeStatus.chainStatus_.getBlockSpeed());
            chainNotif.setEta(nodeStatus.chainStatus_.getETA());
            chainNotif.setProgress(nodeStatus.chainStatus_.getProgressPct());
            chainNotif.setBlocksLeft(nodeStatus.chainStatus_.getBlocksLeft());
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
                  capnFee.setFeeByte(fee.second.feeByte_);
                  capnFee.setSmartFee(fee.second.smartFee_);
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
                  auto header = bcPtr->getHeaderByHeight(height, 0);
                  headers.emplace_back(std::move(header));
               } catch (const std::exception&) {
                  continue;
               }
            }

            auto result = staticReply.initGetHeadersByHeight(headers.size());
            unsigned i=0;
            for (const auto& header : headers) {
               result.set(i++, capnp::Data::Builder(
                  (uint8_t*)header->getPtr(), header->getSize()
               ));
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
std::shared_ptr<BDV_Server_Object> Clients::get(const std::string& id) const
{
   return BDVs_.get(id);
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
         auto bdid = READHEX(getID());
         if (bdid.getSize() != BDVID_LENGTH) {
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

std::vector<uint8_t>& BDV_Server_Object::getScratchPad()
{
   if (scratchPad_.empty()) {
      scratchPad_.resize(SCRATCHPAD_SIZE);
   }
   return scratchPad_;
}

///////////////////////////////////////////////////////////////////////////////
BDV_Server_Object::BDV_Server_Object(
   const std::string& id, std::shared_ptr<BlockDataManager> bdm) :
   BlockDataViewer(bdm), bdvID_(id)
{
   setup();
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

      //fill with addresses from proto payloads
      for (const auto& wlt : wltMap) {
         for (const auto& addr : wlt.second.addresses) {
            batch->scrAddrSet_.insert(addr);
         }
      }

      //callback only serves to wait on the registration event
      auto promPtr = std::make_shared<std::promise<bool>>();
      auto fut = promPtr->get_future();
      auto callback = [promPtr](std::set<BinaryDataRef>, bool success)->void
      {
         promPtr->set_value(success);
      };
      batch->callback_ = callback;

      //register the batch
      auto saf = bdm_->getScrAddrFilter();
      saf->pushAddressBatch(batch);
      if (fut.get() == false) {
         return;
      }

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
      scanWallets(std::move(zcstruct));
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

   //we expect this message to be smaller than our scratchpad
   auto flat = capnp::messageToFlatArray(message);
   auto bytes = flat.asBytes();
   std::vector<uint8_t> replyRaw(bytes.begin(), bytes.end());
   notifications_->push(std::make_unique<WritePayload_Raw>(replyRaw));
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
         auto refreshIds = refreshNotif.initIds(1);
         refreshIds.set(0, payload->refreshID_);
         break;
      }

      case BDV_ZC:
      {
         unsigned notifCount = 1;
         auto payload = std::dynamic_pointer_cast<BDV_Notification_ZC>(notifPtr);
         if (payload->packet_.purgePacket_ != nullptr &&
            !payload->packet_.purgePacket_->invalidatedZcKeys_.empty()) {
            notifCount = 2;
         }

         //new zc legder entries
         auto notifList = notifs.initNotifs(notifCount);
         auto notif = notifList[0];
         auto zcNotif = notif.initZc();
         historyPageToCapn(payload->leVec_, zcNotif);

         if (notifCount == 2) {
            //invalidated zc hashes
            const auto& invalidatedHashes =
               payload->packet_.purgePacket_->invalidatedZcKeys_;
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

         progressNotif.setPhase((uint32_t)payload->phase_);
         progressNotif.setProgress(payload->progress_);
         progressNotif.setTime(payload->time_);
         progressNotif.setNumericProgress(payload->numericProgress_);

         if (payload->walletIDs_.empty()) {
            break;
         }
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

         nodeNotif.setNode((Codec::Types::NodeStatus::NodeState)nodeStatus.state_);
         nodeNotif.setIsSW(nodeStatus.SegWitEnabled_);
         nodeNotif.setRpc((Codec::Types::NodeStatus::RpcState)nodeStatus.rpcState_);

         auto chainNotif = nodeNotif.getChain();
         chainNotif.setChainState((Codec::Types::ChainStatus::ChainState)
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

   notifications_->push(
      std::make_unique<WritePayload_Capnp>(
         std::move(message), std::move(firstSegment)));
}

///////////////////////////////////////////////////////////////////////////////
void BDV_Server_Object::registerWallet(WalletRegistrationRequest& regReq)
{
   if (isReadyFuture_.wait_for(0s) != std::future_status::ready) {
      //only run this code if the bdv maintenance thread hasn't started yet
      std::unique_lock<std::mutex> lock(registerWalletMutex_);

      //save data
      wltRegMap_.emplace(regReq.walletId, std::move(regReq));
      return;
   }

   //set callback to notify of current zc
   regReq.zcCallback = [this, walletId=regReq.walletId](
      const std::set<BinaryDataRef>& addrSet)->void
   {
      auto zcNotifPacket = createZcNotification(addrSet);
      flagRefresh(BDV_refreshAndRescan, walletId, std::move(zcNotifPacket));
   };

   //register wallet with BDV
   registerAWallet(regReq);
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

         auto addrRef = iter->second->scrAddr_.getRef();
         auto addrObj = std::make_shared<ScrAddrObj>(
            db_, &blockchain(), zeroConfCont_.get(), addrRef);
         newAddrMap.emplace(addrRef, addrObj);
      }

      if (newAddrMap.empty()) {
         continue;
      }

      theWallet->scrAddrMap_.update(newAddrMap);
   }
}

////////////////////////////////////////////////////////////////////////////////
void BDV_Server_Object::flagRefresh(
   BDV_refresh refresh, const std::string& refreshID,
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
WebSocketMessagePartial BDV_Server_Object::preparePayload(
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
      auto msgId = WebSocketMessagePartial::readMessageId(packet->packetData_);
      if (msgId != UINT32_MAX) {
         //get the PartialMessage object for this id
         auto msgIter = messageMap_.find(msgId);
         if (msgIter == messageMap_.end()) {
            //create this PartialMessage if it's missing
            msgIter = messageMap_.emplace(msgId, WebSocketMessagePartial()).first;
         }
         auto& msgRef = msgIter->second;

         //try to reconstruct the message
         auto parsed = msgRef.parsePacket(packet->packetData_);
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

   //return the message to be processed
   return msgObj;
}

///////////////////////////////////////////////////////////////////////////////
const std::string& BDV_Server_Object::getLedgerDelegate()
{
   //return ledger delegate for bdv wallets
   const auto& id = getID();
   auto iter = delegateMap_.find(id);
   if (iter == delegateMap_.end()) {
      auto delegate = getLedgerDelegateForWallets();
      iter = delegateMap_.emplace(id, delegate).first;
   }
   return iter->first;
}

////
const std::string& BDV_Server_Object::getLedgerDelegate(
   const std::string& wltId)
{
   auto iter = delegateMap_.find(wltId);
   if (iter == delegateMap_.end()) {
      auto delegate = getLedgerDelegateForWallet(wltId);
      iter = delegateMap_.emplace(wltId, delegate).first;
   }
   return iter->first;
}

////
const std::string& BDV_Server_Object::getLedgerDelegate(
   const std::string& walletId, const BinaryData& scrAddr)
{
   auto id = scrAddr.toHexStr();
   auto iter = delegateMap_.find(id);
   if (iter == delegateMap_.end()) {
      auto delegate = getLedgerDelegateForScrAddr(walletId, scrAddr);
      iter = delegateMap_.emplace(id, delegate).first;
   }
   return iter->first;
}

///////////////////////////////////////////////////////////////////////////////
std::unique_ptr<BDV_Notification_ZC> BDV_Server_Object::createZcNotification(
   const std::set<BinaryDataRef>& addrSet)
{
   ZcNotificationPacket packet(getID());

   //grab zc map
   auto ss = zeroConfCont_->getSnapshot();
   if (ss != nullptr) {
      for (auto& addr : addrSet)
      try {
         const auto& keySet = ss->getTxioKeysForScrAddr(addr);
         auto iter = packet.scrAddrToTxioKeys_.emplace(
            addr, std::set<BinaryData>());

         for (auto& key : keySet) {
            iter.first->second.emplace(key);
         }
      } catch (const std::range_error&) {
         continue;
      }
   }

   packet.ssPtr_ = ss;
   auto notifPtr = std::make_unique<BDV_Notification_ZC>(packet);
   return notifPtr;
}

///////////////////////////////////////////////////////////////////////////////
//
// Clients
//
///////////////////////////////////////////////////////////////////////////////
Clients::Clients(std::shared_ptr<BlockDataManager> bdm) :
   bdm_(bdm)
{}

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
   if (Armory::Config::DBSettings::getDbType() == ARMORY_DB_SUPER &&
      Armory::Config::DBSettings::getServiceType() != SERVICE_UNITTEST) {
      innerThreadCount = std::thread::hardware_concurrency();
   }
   for (unsigned i = 0; i < innerThreadCount; i++) {
      controlThreads_.push_back(std::thread(innerthread));
      controlThreads_.push_back(std::thread(parserThread));
   }

   auto callbackPtr = std::make_unique<ZeroConfCallbacks_BDV>(this);
   bdm_->registerZcCallbacks(std::move(callbackPtr));
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

      const auto& bdvID = notifPtr->bdvID();
      if (bdvID.empty()) {
         //empty bdvID means broadcast notification to all BDVs
         const auto& bdvs = BDVs_.get();
         for (const auto& bdv_pair : bdvs) {
            auto notifPacket = std::make_shared<BDV_Notification_Packet>();
            notifPacket->bdvPtr_ = bdv_pair.second;
            notifPacket->notifPtr_ = notifPtr;
            innerBDVNotifStack_.push_back(std::move(notifPacket));
         }
      } else {
         //grab bdv
         auto bdvPtr = BDVs_.get(bdvID);
         auto notifPacket = std::make_shared<BDV_Notification_Packet>();
         notifPacket->bdvPtr_ = bdvPtr;
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
   bdm_->shutdown();

   for (auto& thr : controlThreads_) {
      if (thr.joinable()) {
         thr.join();
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
bool Clients::registerBDV(const std::string& magicWord,
   const std::string& bdvId)
{
   if (magicWord.empty() || bdvId.empty()) {
      return false;
   }
   auto thisMagicWord =
      Armory::Config::BitcoinSettings::getMagicBytes().toHexStr();
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
void Clients::unregisterBDV(std::string bdvId)
{
   unregBDVQueue_.push_back(move(bdvId));
}

///////////////////////////////////////////////////////////////////////////////
void Clients::unregisterBDVThread()
{
   while (true) {
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
      if (timedout == true || notifPtr->action_type() != BDV_Progress) {
         gcCommands_.push_back(true);
      }

      //grab notif type and move it to bdv notif queue
      if (notifPtr == nullptr) {
         continue;
      }
      auto aType = notifPtr->action_type();
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
   WebSocketMessagePartial msg;
   if (!msg.parsePacket(payloadPtr->packetData_)) {
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
         staticRequest, request.getMsgId(), this, payloadPtr->hexID);
      if (builderPtr != nullptr) {
         WebSocketServer::write(
            payloadPtr->bdvID_, payloadPtr->messageID_,
            std::make_unique<WritePayload_Capnp>(
               std::move(builderPtr), std::vector<uint8_t>{}));
      }
   } catch (const std::runtime_error&) {}
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
         //no bdv, is this a static command?
         parseStandAlonePayload(payloadPtr);
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
      if (result != nullptr) {
         WebSocketServer::write(
            payloadPtr->bdvID_, payloadPtr->messageID_,
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
      notifPacket->bdvPtr_ = bdvPtr;
      notifPacket->notifPtr_ = std::make_shared<BDV_Notification_Error>(
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
         std::set<std::string> extraRequestors;
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
               auto txPtr = batchPtr->zcMap_.begin()->second;
               txPtr->tx_.unserialize(*packet.rawTx_);
               txPtr->tx_.setTxTime(time(0));
               batchPtr->isReadyPromise_->set_value(ArmoryErrorCodes::Success);
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
               if (!watcherEntry->extraRequestors_.empty()) {
                  std::unique_lock<std::mutex> lock(BDVs_.mu);
                  for (auto& extraReq : watcherEntry->extraRequestors_) {
                     auto bdvIter = BDVs_.bdvs.find(extraReq);
                     if (bdvIter == BDVs_.bdvs.end()) {
                        continue;
                     }

                     packet.extraRequestors_.emplace(bdvIter->second);
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
std::unique_ptr<Socket_WritePayload> Clients::processCommand(
   std::shared_ptr<BDV_Payload> payload)
{
   //clear bdvPtr from the payload to avoid circular ownership
   auto bdvPtr = payload->bdvPtr_;
   payload->bdvPtr_.reset();

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
            staticRequest, request.getMsgId(), this, payload->hexID);
         if (builderPtr != nullptr) {
            return std::make_unique<WritePayload_Capnp>(
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
               return std::make_unique<WritePayload_Raw>(firstSegment);
            } else {
               /*
               Message lives across multiple segments, we have to pass it to a
               capnp payload, along with the scratchpad, which contains the
               first segment
               */
               return std::make_unique<WritePayload_Capnp>(
                  std::move(builder.builder),
                  std::move(bdvPtr->getScratchPad())
               );
            }
         }
   }
   return nullptr;
}

void Clients::rpcBroadcast(RpcBroadcastPacket& packet)
{
   rpcBroadcastQueue_.push_back(std::move(packet));
}

void Clients::p2pBroadcast(
   const std::string& bdvId, std::vector<BinaryDataRef>& rawZCs)
{
   //run through submitted ZCs, prune already mined ones
   auto db = bdm_->getIFace();
   for (auto& rawZcRef : rawZCs) {
      Tx tx(rawZcRef);
      auto hash = tx.getThisHash();

      auto dbKey = db->getDBKeyForHash(hash);
      if (!dbKey.empty()) {
         //notify the bdv of the error
         auto notifPacket = std::make_shared<BDV_Notification_Packet>();
         notifPacket->bdvPtr_ = BDVs_.get(bdvId);

         notifPacket->notifPtr_ = std::make_shared<BDV_Notification_Error>(
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
      std::vector<ZeroConfBatchFallbackStruct> zcVec)->void
   {
      std::vector<RpcBroadcastPacket> rpcPackets;
      auto bdvPtr = BDVs_.get(bdvId);
      for (const auto& fallbackStruct : zcVec) {
         std::set<std::shared_ptr<BDV_Server_Object>> extraRequestors;
         for (const auto& extraBdvId : fallbackStruct.extraRequestors_) {
            auto secondBdv = BDVs_.get(extraBdvId);
            if (secondBdv == nullptr) {
               continue;
            }
            extraRequestors.emplace(secondBdv);
         }

         if (fallbackStruct.err_ != ArmoryErrorCodes::ZcBatch_Timeout) {
            //signal error to caller
            auto notifPacket = std::make_shared<BDV_Notification_Packet>();
            notifPacket->bdvPtr_ = bdvPtr;
            notifPacket->notifPtr_ = std::make_shared<BDV_Notification_Error>(
               bdvId,
               (int)fallbackStruct.err_, fallbackStruct.txHash_, std::string{}
            );
            innerBDVNotifStack_.push_back(std::move(notifPacket));

            //then signal extra requestors
            for (const auto& extraBDV : extraRequestors) {
               auto notifPacket = std::make_shared<BDV_Notification_Packet>();
               notifPacket->bdvPtr_ = extraBDV;
               notifPacket->notifPtr_ = std::make_shared<BDV_Notification_Error>(
                  extraBDV->getID(),
                  (int)fallbackStruct.err_, fallbackStruct.txHash_, std::string{}
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
   bdm_->zeroConfCont_->broadcastZC(
      rawZCs, 5000, errorCallback, bdvId);
}

///////////////////////////////////////////////////////////////////////////////
//
// Callback
//
///////////////////////////////////////////////////////////////////////////////
Callback::~Callback()
{}

///////////////////////////////////////////////////////////////////////////////
void WS_Callback::push(std::unique_ptr<Socket_WritePayload> payload)
{
   //write to socket
   WebSocketServer::write(bdvID_, WEBSOCKET_CALLBACK_ID, std::move(payload));
}

///////////////////////////////////////////////////////////////////////////////
void UnitTest_Callback::push(std::unique_ptr<Socket_WritePayload> payload)
{
   //stash the notification, unit test will pull it as needed
   notifQueue_.push_back(std::move(payload));
}

///////////////////////////////////////////////////////////////////////////////
BinaryData UnitTest_Callback::getNotification()
{
   try {
      auto notifPtr = std::move(notifQueue_.pop_front());

      std::vector<uint8_t> flat;
      notifPtr->serialize(flat);
      return BinaryData(flat.data(), flat.size());
   }
   catch (const Threading::StopBlockingLoop&) {}
   return {};
}

///////////////////////////////////////////////////////////////////////////////
//
// BDVMap
//
///////////////////////////////////////////////////////////////////////////////
void BDVMap::add(std::shared_ptr<BDV_Server_Object> bdvObj)
{
   std::unique_lock<std::mutex> lock(mu);
   bdvs.emplace(bdvObj->getID(), bdvObj);
}

void BDVMap::del(const std::string& bdvId)
{
   std::unique_lock<std::mutex> lock(mu);
   bdvs.erase(bdvId);
}

std::shared_ptr<BDV_Server_Object> BDVMap::get(const std::string& bdvId) const
{
   std::unique_lock<std::mutex> lock(mu);
   auto iter = bdvs.find(bdvId);
   if (iter == bdvs.end()) {
      return nullptr;
   }
   return iter->second;
}

std::map<std::string, std::shared_ptr<BDV_Server_Object>> BDVMap::get() const
{
   std::unique_lock<std::mutex> lock(mu);
   return bdvs;
}
