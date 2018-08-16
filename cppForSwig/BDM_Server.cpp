////////////////////////////////////////////////////////////////////////////////
//                                                                            //
//  Copyright (C) 2016, goatpig.                                              //
//  Distributed under the MIT license                                         //
//  See LICENSE-MIT or https://opensource.org/licenses/MIT                    //                                      
//                                                                            //
////////////////////////////////////////////////////////////////////////////////

#include "BDM_Server.h"

using namespace ::google::protobuf;
using namespace ::Codec_BDVCommand;

///////////////////////////////////////////////////////////////////////////////
//
// BDV_Server_Object
//
///////////////////////////////////////////////////////////////////////////////
shared_ptr<Message> BDV_Server_Object::processCommand(
   shared_ptr<BDVCommand> command)
{
   /*
   BDV_Command messages using any of the following methods need to carry a 
   valid BDV id
   */

   switch (command->method())
   {
   case Methods::waitOnBDVInit:
   case Methods::waitOnBDVNotification:
   {
      /* in: void
         out: BDVCallback
      */
      if (!cb_->isValid())
         break;

      auto longpoll = dynamic_cast<LongPoll*>(this->cb_.get());
      if (longpoll == nullptr)
         break;

      return longpoll->respond(command);
   }

   case Methods::goOnline:
   {
      /* in: void
         out: void
      */
      this->startThreads();
      return nullptr;
   }

   case Methods::getTopBlockHeight:
   {
      /* in: void
         out: Codec_CommonTypes::OneUnsigned
      */
      auto response = make_shared<::Codec_CommonTypes::OneUnsigned>();
      response->set_value(this->getTopBlockHeight());
      return response;
   }

   case Methods::getHistoryPage:
   {
      /*
         in: delegateID + pageID or
             walletID + pageID
         out: Codec_LedgerEntry::ManyLedgerEntry
      */

      auto toLedgerEntryVector = []
      (vector<LedgerEntry>& leVec)->shared_ptr<Message>
      {
         auto response = make_shared<::Codec_LedgerEntry::ManyLedgerEntry>();

         for (auto& le : leVec)
         {
            auto lePtr = response->add_values();
            le.fillMessage(lePtr);
         }

         return response;
      };


      //is it a ledger from a delegate?
      if (command->has_delegateid())
      {
         auto delegateIter = delegateMap_.find(command->delegateid());
         if (delegateIter != delegateMap_.end())
         {
            if (!command->has_pageid())
               throw runtime_error("invalid command for getHistoryPage");

            auto& delegateObject = delegateIter->second;
            auto pageId = command->pageid();

            auto&& retVal = delegateObject.getHistoryPage(pageId);
            return toLedgerEntryVector(retVal);
         }
      }
      else if(command->has_walletid())
      {
         auto& wltID = command->walletid();
         BinaryDataRef wltIDRef; wltIDRef.setRef(wltID);
         auto theWallet = getWalletOrLockbox(wltIDRef);
         if (theWallet != nullptr)
         {
            unsigned pageId = UINT32_MAX;
            BinaryDataRef txHash;

            if (command->has_pageid())
            {
               auto&& retVal = theWallet->getHistoryPageAsVector(
                  command->pageid());
               return toLedgerEntryVector(retVal);
            }
         }      
      }

      throw runtime_error("invalid command for getHistoryPage");
   }

   case Methods::getPageCountForLedgerDelegate:
   {
      if (!command->has_delegateid())
         throw runtime_error(
            "invalid command for getPageCountForLedgerDelegate");

      auto delegateIter = delegateMap_.find(command->delegateid());
      if (delegateIter != delegateMap_.end())
      {
         auto count = delegateIter->second.getPageCount();
         
         auto response = make_shared<::Codec_CommonTypes::OneUnsigned>();
         response->set_value(count);
         return response;
      }
   }

   case Methods::registerWallet:
   {
      /*
      in: 
         walletid
         flag: set to true if the wallet is new
         hash: registration id. The callback notifying the registation 
               completion will carry this id. If the registration
               id is empty, no callback will be triggered on completion.
         bindata[]: addresses

      out: void, registration completion is signaled by callback
      */
      if (!command->has_walletid())
         throw runtime_error("invalid command for registerWallet");

      this->registerWallet(command);
      break;
   }

   case Methods::registerLockbox:
   {
      /* see registerWallet */

      if (!command->has_walletid())
         throw runtime_error("invalid command for registerLockbox");

      this->registerLockbox(command);
      break;
   }

   case Methods::getLedgerDelegateForWallets:
   {
      /*
      in: void
      out: ledger delegate id as a string wrapped in Codec_CommonTypes::Strings
      */
      auto&& ledgerdelegate = this->getLedgerDelegateForWallets();

      string id = this->getID();
      id.append("_w");

      this->delegateMap_.insert(make_pair(id, ledgerdelegate));

      auto response = make_shared<::Codec_CommonTypes::Strings>();
      response->add_data(id);
      return response;
   }

   case Methods::getLedgerDelegateForLockboxes:
   {
      /* see getLedgerDelegateForWallets */
      auto&& ledgerdelegate = this->getLedgerDelegateForLockboxes();

      string id = this->getID();
      id.append("_l");

      this->delegateMap_.insert(make_pair(id, ledgerdelegate));

      auto response = make_shared<::Codec_CommonTypes::Strings>();
      response->add_data(id);
      return response;
   }

   case Methods::getLedgerDelegateForScrAddr:
   {
      /*
      in:
         walletid
         scraddr
      out: ledger delegate id as a string wrapped in Codec_CommonTypes::Strings
      */
      if (!command->has_walletid() || !command->has_scraddr())
         throw runtime_error("invalid command for getLedgerDelegateForScrAddr");

      auto& walletId = command->walletid();
      BinaryDataRef walletIdRef; walletIdRef.setRef(walletId);

      auto& scrAddr = command->scraddr();
      BinaryData addr; addr.copyFrom(scrAddr);

      auto&& ledgerdelegate =
         this->getLedgerDelegateForScrAddr(walletIdRef, addr);
      string id = addr.toHexStr();

      this->delegateMap_.insert(make_pair(id, ledgerdelegate));
      auto response = make_shared<::Codec_CommonTypes::Strings>();
      response->add_data(id);
      return response;
   }

   case Methods::getBalancesAndCount:
   {
      /*
      in:
         walletid
         height
      out: full, spendable and unconfirmed balance + transaction count
         wrapped in Codec_CommonTypes::ManyUnsigned
      */
      if (! command->has_walletid() || !command->has_height())
         throw runtime_error("invalid command for getBalancesAndCount");

      auto& walletId = command->walletid();
      BinaryDataRef walletIdRef; walletIdRef.setRef(walletId);

      shared_ptr<BtcWallet> wltPtr = nullptr;
      for (auto& group : this->groups_)
      {
         auto wltIter = group.wallets_.find(walletIdRef);
         if (wltIter != group.wallets_.end())
            wltPtr = wltIter->second;
      }

      if (wltPtr == nullptr)
         throw runtime_error("unknown wallet/lockbox ID");

      uint32_t height = command->height();

      auto response = make_shared<::Codec_CommonTypes::ManyUnsigned>();
      response->add_value(wltPtr->getFullBalance());
      response->add_value(wltPtr->getSpendableBalance(height));
      response->add_value(wltPtr->getUnconfirmedBalance(height));
      response->add_value(wltPtr->getWltTotalTxnCount());
      return response;
   }

   case Methods::getSpendableTxOutListForValue:
   {
      /*
      in:
         walletid
         value
      out: enough UTXOs to cover value twice, as Codec_Utxo::ManyUtxo
      */

      if (!command->has_walletid() || !command->has_value())
         throw runtime_error("invalid command for getSpendableTxOutListForValue");

      auto& walletId = command->walletid();
      BinaryDataRef walletIdRef; walletIdRef.setRef(walletId);

      shared_ptr<BtcWallet> wltPtr = nullptr;
      for (auto& group : this->groups_)
      {
         auto wltIter = group.wallets_.find(walletIdRef);
         if (wltIter != group.wallets_.end())
            wltPtr = wltIter->second;
      }

      if (wltPtr == nullptr)
         throw runtime_error("unknown wallet or lockbox ID");

      auto&& utxoVec = wltPtr->getSpendableTxOutListForValue(
         command->value());

      auto response = make_shared<::Codec_Utxo::ManyUtxo>();
      for (auto& utxo : utxoVec)
      {
         auto utxoPtr = response->add_value();
         utxoPtr->set_value(utxo.value_);
         utxoPtr->set_script(utxo.script_.getPtr(), utxo.script_.getSize());
         utxoPtr->set_txheight(utxo.txHeight_);
         utxoPtr->set_txindex(utxo.txIndex_);
         utxoPtr->set_txoutindex(utxo.txOutIndex_);
         utxoPtr->set_txhash(utxo.txHash_.getPtr(), utxo.txHash_.getSize());
      }

      return response;
   }

   case Methods::getSpendableZCList:
   {
      /*
      in:
      walletid
      out: all ZC UTXOs for this wallet, as Codec_Utxo::ManyUtxo
      */

      if (!command->has_walletid())
         throw runtime_error("invalid command for getSpendableZCList");

      auto& walletId = command->walletid();
      BinaryDataRef walletIdRef; walletIdRef.setRef(walletId);

      shared_ptr<BtcWallet> wltPtr = nullptr;
      for (auto& group : this->groups_)
      {
         auto wltIter = group.wallets_.find(walletIdRef);
         if (wltIter != group.wallets_.end())
            wltPtr = wltIter->second;
      }

      if (wltPtr == nullptr)
         throw runtime_error("unknown wallet or lockbox ID");

      auto&& utxoVec = wltPtr->getSpendableTxOutListZC();

      auto response = make_shared<::Codec_Utxo::ManyUtxo>();
      for (auto& utxo : utxoVec)
      {
         auto utxoPtr = response->add_value();
         utxoPtr->set_value(utxo.value_);
         utxoPtr->set_script(utxo.script_.getPtr(), utxo.script_.getSize());
         utxoPtr->set_txheight(utxo.txHeight_);
         utxoPtr->set_txindex(utxo.txIndex_);
         utxoPtr->set_txoutindex(utxo.txOutIndex_);
         utxoPtr->set_txhash(utxo.txHash_.getPtr(), utxo.txHash_.getSize());
      }

      return response;
   }

   case Methods::getRBFTxOutList:
   {
      /*
      in:
      walletid
      out: all RBF UTXOs for this wallet, as Codec_Utxo::ManyUtxo
      */

      if (!command->has_walletid())
         throw runtime_error("invalid command for getSpendableZCList");

      auto& walletId = command->walletid();
      BinaryDataRef walletIdRef; walletIdRef.setRef(walletId);

      shared_ptr<BtcWallet> wltPtr = nullptr;
      for (auto& group : this->groups_)
      {
         auto wltIter = group.wallets_.find(walletIdRef);
         if (wltIter != group.wallets_.end())
            wltPtr = wltIter->second;
      }

      if (wltPtr == nullptr)
         throw runtime_error("unknown wallet or lockbox ID");

      auto&& utxoVec = wltPtr->getRBFTxOutList();

      auto response = make_shared<::Codec_Utxo::ManyUtxo>();
      for (auto& utxo : utxoVec)
      {
         auto utxoPtr = response->add_value();
         utxoPtr->set_value(utxo.value_);
         utxoPtr->set_script(utxo.script_.getPtr(), utxo.script_.getSize());
         utxoPtr->set_txheight(utxo.txHeight_);
         utxoPtr->set_txindex(utxo.txIndex_);
         utxoPtr->set_txoutindex(utxo.txOutIndex_);
         utxoPtr->set_txhash(utxo.txHash_.getPtr(), utxo.txHash_.getSize());
      }

      return response;
   }

   case Methods::getSpendableTxOutListForAddr:
   {
      /*
      in:
      walletid
      scraddr
      out: all UTXOs for this address, as Codec_Utxo::ManyUtxo
      */

      if (!command->has_walletid() || !command->has_scraddr())
         throw runtime_error("invalid command for getSpendableZCList");

      auto& walletId = command->walletid();
      BinaryDataRef walletIdRef; walletIdRef.setRef(walletId);

      shared_ptr<BtcWallet> wltPtr = nullptr;
      for (auto& group : this->groups_)
      {
         auto wltIter = group.wallets_.find(walletIdRef);
         if (wltIter != group.wallets_.end())
            wltPtr = wltIter->second;
      }

      if (wltPtr == nullptr)
         throw runtime_error("unknown wallet or lockbox ID");

      auto& scrAddr = command->scraddr();
      BinaryDataRef scrAddrRef((uint8_t*)scrAddr.data(), scrAddr.size());
      auto addrObj = wltPtr->getScrAddrObjByKey(scrAddrRef);

      auto spentByZC = [this](const BinaryData& dbkey)->bool
      { return this->isTxOutSpentByZC(dbkey); };

      auto&& utxoVec = addrObj->getAllUTXOs(spentByZC);

      auto response = make_shared<::Codec_Utxo::ManyUtxo>();
      for (auto& utxo : utxoVec)
      {
         auto utxoPtr = response->add_value();
         utxoPtr->set_value(utxo.value_);
         utxoPtr->set_script(utxo.script_.getPtr(), utxo.script_.getSize());
         utxoPtr->set_txheight(utxo.txHeight_);
         utxoPtr->set_txindex(utxo.txIndex_);
         utxoPtr->set_txoutindex(utxo.txOutIndex_);
         utxoPtr->set_txhash(utxo.txHash_.getPtr(), utxo.txHash_.getSize());
      }

      return response;
   }

   case Methods::broadcastZC:
   {
      /*
      in: raw tx as bindata[0]
      out: void
      */

      if (command->bindata_size() != 1)
         throw runtime_error("invalid command for broadcastZC");

      auto broadcastLBD = [this](BinaryData bd)->void
      {
         this->zeroConfCont_->broadcastZC(
            bd, this->getID(), 10000);
      };

      auto&& rawTx = command->bindata(0);
      BinaryData rawTxBD((uint8_t*)rawTx.data(), rawTx.size());
      thread thr(broadcastLBD, move(rawTxBD));
      if (thr.joinable())
         thr.detach();

      return nullptr;
   }

   case Methods::getAddrTxnCounts:
   {
      /*
      in: walletid
      out: transaction count for each address in wallet, 
           as Codec_AddressData::ManyAddressData
      */
      if (!command->has_walletid())
         throw runtime_error("invalid command for getSpendableZCList");

      auto& walletId = command->walletid();
      BinaryDataRef walletIdRef; walletIdRef.setRef(walletId);

      shared_ptr<BtcWallet> wltPtr = nullptr;
      for (auto& group : this->groups_)
      {
         auto wltIter = group.wallets_.find(walletIdRef);
         if (wltIter != group.wallets_.end())
            wltPtr = wltIter->second;
      }

      if (wltPtr == nullptr)
         throw runtime_error("unknown wallet or lockbox ID");

      auto&& countMap = wltPtr->getAddrTxnCounts(updateID_);

      auto response = make_shared<::Codec_AddressData::ManyAddressData>();
      for (auto count : countMap)
      {
         auto addrData = response->add_scraddrdata();
         addrData->set_scraddr(count.first.getPtr(), count.first.getSize());
         addrData->add_value(count.second);
      }

      return response;
   }

   case Methods::getAddrBalances:
   {
      /*
      in: walletid
      out: full, spendable and unconfirmed balance for each address in
           wallet, as Codec_AddressData::ManyAddressData
      */

      if (!command->has_walletid())
         throw runtime_error("invalid command for getSpendableZCList");

      auto& walletId = command->walletid();
      BinaryDataRef walletIdRef; walletIdRef.setRef(walletId);

      shared_ptr<BtcWallet> wltPtr = nullptr;
      for (auto& group : this->groups_)
      {
         auto wltIter = group.wallets_.find(walletIdRef);
         if (wltIter != group.wallets_.end())
            wltPtr = wltIter->second;
      }

      if (wltPtr == nullptr)
         throw runtime_error("unknown wallet or lockbox ID");

      auto&& balanceMap = wltPtr->getAddrBalances(
         updateID_, this->getTopBlockHeight());

      auto response = make_shared<::Codec_AddressData::ManyAddressData>();
      for (auto balances : balanceMap)
      {
         auto addrData = response->add_scraddrdata();
         addrData->set_scraddr(balances.first.getPtr(), balances.first.getSize());
         addrData->add_value(get<0>(balances.second));
         addrData->add_value(get<1>(balances.second));
         addrData->add_value(get<2>(balances.second));
      }

      return response;
   }

   case Methods::getTxByHash:
   {
      /*
      in: hash
      out: tx as Codec_CommonTypes::TxWithMetaData
      */

      if (!command->has_hash())
         throw runtime_error("invalid command for getTxByHash");

      auto& txHash = command->hash();
      BinaryDataRef txHashRef; txHashRef.setRef(txHash);
      auto&& retval = this->getTxByHash(txHashRef);
      
      //sanity check
      if (retval.getSize() == 0)
      {
         LOGWARN << "empty raw tx!";
         throw runtime_error("could not find tx");
      }

      auto response = make_shared<::Codec_CommonTypes::TxWithMetaData>();
      response->set_rawtx(retval.getPtr(), retval.getSize());
      response->set_isrbf(retval.isRBF());
      response->set_ischainedzc(retval.isChained());
      return response;
   }

   case Methods::getAddressFullBalance:
   {
      /*
      in: scraddr
      out: current balance in DB (does not cover ZC), 
           as Codec_CommonTypes::OneUnsigned
      */

      if (!command->has_scraddr())
         throw runtime_error("invalid command for getAddressFullBalance");

      auto& scrAddr = command->scraddr();
      BinaryDataRef scrAddrRef; scrAddrRef.setRef(scrAddr);
      auto&& retval = this->getAddrFullBalance(scrAddrRef);

      auto response = make_shared<::Codec_CommonTypes::OneUnsigned>();
      response->set_value(get<0>(retval));
      return response;
   }

   case Methods::getAddressTxioCount:
   {
      /*
      in: scraddr
      out: current transaction count in DB (does not cover ZC), 
           as Codec_CommonTypes::OneUnsigned
      */
      if (!command->has_scraddr())
         throw runtime_error("invalid command for getAddressFullBalance");

      auto& scrAddr = command->scraddr();
      BinaryDataRef scrAddrRef; scrAddrRef.setRef(scrAddr);
      auto&& retval = this->getAddrFullBalance(scrAddrRef);

      auto response = make_shared<::Codec_CommonTypes::OneUnsigned>();
      response->set_value(get<1>(retval));
      return response;
   }

   case Methods::getHeaderByHeight:
   {
      /*
      in: height
      out: raw header, as Codec_CommonTypes::BinaryData
      */

      if (!command->has_height())
         throw runtime_error("invalid command for getHeaderByHeight");

      auto header = blockchain().getHeaderByHeight(command->height());
      auto& headerData = header->serialize();

      auto response = make_shared<::Codec_CommonTypes::BinaryData>();
      response->set_data(headerData.getPtr(), headerData.getSize());
      return response;
   }

   case Methods::createAddressBook:
   {
      /*
      in: walletid
      out: Codec_AddressBook::AddressBook
      */

      if (!command->has_walletid())
         throw runtime_error("invalid command for createAddressBook");

      auto& walletId = command->walletid();
      BinaryDataRef walletIdRef; walletIdRef.setRef(walletId);
      auto wltPtr = getWalletOrLockbox(walletIdRef);
      if (wltPtr == nullptr)
         throw runtime_error("invalid id");

      auto&& abeVec = wltPtr->createAddressBook();

      auto response = make_shared<::Codec_AddressBook::AddressBook>();
      for (auto& abe : abeVec)
      {
         auto entry = response->add_entry();
         auto& scrAddr = abe.getScrAddr();
         entry->set_scraddr(scrAddr.getPtr(), scrAddr.getSize());

         auto& txHashList = abe.getTxHashList();
         for (auto txhash : txHashList)
            entry->add_txhash(txhash.getPtr(), txhash.getSize());
      }

      return response;
   }

   case Methods::updateWalletsLedgerFilter:
   {
      /*
      in: vector of wallet ids to display in wallet ledger delegate, as bindata
      out: void
      */
      vector<BinaryData> bdVec;
      for (unsigned i = 0; i < command->bindata_size(); i++)
      {
         auto& val = command->bindata(i);
         BinaryData valRef((uint8_t*)val.data(), val.size());
         bdVec.push_back(valRef);
      }

      this->updateWalletsLedgerFilter(bdVec);
      return nullptr;
   }

   case Methods::getNodeStatus:
   {
      /*
      in: void
      out: Codec_NodeStatus::NodeStatus
      */
      auto&& nodeStatus = this->bdmPtr_->getNodeStatus();

      auto response = make_shared<::Codec_NodeStatus::NodeStatus>();
      response->set_status((unsigned)nodeStatus.status_);
      response->set_segwitenabled(nodeStatus.SegWitEnabled_);
      response->set_rpcstatus((unsigned)nodeStatus.rpcStatus_);

      auto chainState_proto = new ::Codec_NodeStatus::NodeChainState();
      chainState_proto->set_state((unsigned)nodeStatus.chainState_.state());
      chainState_proto->set_blockspeed(nodeStatus.chainState_.getBlockSpeed());
      chainState_proto->set_eta(nodeStatus.chainState_.getETA());
      chainState_proto->set_pct(nodeStatus.chainState_.getProgressPct());
      chainState_proto->set_blocksleft(nodeStatus.chainState_.getBlocksLeft());
      response->set_allocated_chainstate(chainState_proto);

      return response;
   }

   case Methods::estimateFee:
   {
      /*
      in: 
         value
         startegy as bindata[0]
      out: 
         Codec_FeeEstimate::FeeEstimate
      */
      if (!command->has_value() || command->bindata_size() != 1)
         throw runtime_error("invalid command for estimateFee");

      uint32_t blocksToConfirm = command->value();
      auto strat = command->bindata(0);

      auto feeByte = this->bdmPtr_->nodeRPC_->getFeeByte(
         blocksToConfirm, strat);

      auto response = make_shared<::Codec_FeeEstimate::FeeEstimate>();
      response->set_feebyte(feeByte.feeByte_);
      response->set_smartfee(feeByte.smartFee_);
      response->set_error(feeByte.error_);
      return response;
   }

   case Methods::getHistoryForWalletSelection:
   {
      /*
      in:
         vector of wallet ids to get history for, as bindata
         flag, set to true to order history ascending
      out:
         history for wallet list, as Codec_LedgerEntry::ManyLedgerEntry
      */

      if (!command->has_flag())
         throw runtime_error("invalid command for getHistoryForWalletSelection");

      vector<BinaryData> wltIDs;
      for (unsigned i = 0; i < command->bindata_size(); i++)
      {
         auto& id = command->bindata(i);
         BinaryData idRef((uint8_t*)id.data(), id.size());
         wltIDs.push_back(idRef);
      }

      auto orderingFlag = command->flag();

      HistoryOrdering ordering;
      if (orderingFlag)
         ordering = order_ascending;
      else 
         ordering = order_descending;

      auto&& wltGroup = this->getStandAloneWalletGroup(wltIDs, ordering);

      auto response = make_shared<::Codec_LedgerEntry::ManyLedgerEntry>();
      for (unsigned y = 0; y < wltGroup.getPageCount(); y++)
      {
         auto&& histPage = wltGroup.getHistoryPage(y, false, false, UINT32_MAX);

         for (auto& le : histPage)
         {
            auto lePtr = response->add_values();
            le.fillMessage(lePtr);
         }
      }

      return response;
   }

   case Methods::broadcastThroughRPC:
   {
      /*
      in: raw tx as bindata[0]
      out: rpc response string as Codec_CommonTypes::String
      */

      if (command->bindata_size() != 1)
         throw runtime_error("invalid command for broadcastThroughRPC");

      auto& rawTx = command->bindata(0);
      BinaryDataRef rawTxRef; rawTxRef.setRef(rawTx);

      auto&& response =
         this->bdmPtr_->nodeRPC_->broadcastTx(rawTxRef);

      if (response == "success")
      {
         this->bdmPtr_->zeroConfCont_->pushZcToParser(rawTxRef);
      }

      auto result = make_shared<::Codec_CommonTypes::Strings>();
      result->add_data(response);
      return result;
   }

   case Methods::getUTXOsForAddrList:
   {
      /*
      in: addresses as bindata
      out: Codec_Utxo::ManyUtxo
      */

      vector<BinaryData> addrVec;
      for (unsigned i = 0; i < command->bindata_size(); i++)
      {
         auto& addr = command->bindata(i);
         BinaryData addrRef((uint8_t*)addr.data(), addr.size());
         addrVec.push_back(addrRef);
      }

      auto&& utxoVec = this->getUnspentTxoutsForAddr160List(addrVec, false);

      auto response = make_shared<::Codec_Utxo::ManyUtxo>();
      for (auto& utxo : utxoVec)
      {
         auto utxoPtr = response->add_value();
         utxoPtr->set_value(utxo.value_);
         utxoPtr->set_script(utxo.script_.getPtr(), utxo.script_.getSize());
         utxoPtr->set_txheight(utxo.txHeight_);
         utxoPtr->set_txindex(utxo.txIndex_);
         utxoPtr->set_txoutindex(utxo.txOutIndex_);
         utxoPtr->set_txhash(utxo.txHash_.getPtr(), utxo.txHash_.getSize());
      }

      return response;
   }

   case Methods::getHeaderByHash:
   {
      /*
      in: hash
      out: raw header, as Codec_CommonTypes::BinaryData
      */

      if (!command->has_hash())
         throw runtime_error("invalid command for getHeaderByHash");

      auto& txHash = command->hash();
      BinaryDataRef txHashRef; txHashRef.setRef(txHash);

      auto&& dbKey = this->db_->getDBKeyForHash(txHashRef);

      if (dbKey.getSize() == 0)
         return nullptr;

      unsigned height; uint8_t dup;
      BinaryRefReader key_brr(dbKey.getRef());
      DBUtils::readBlkDataKeyNoPrefix(key_brr, height, dup);

      BinaryData rawHeader;
      try
      {
         auto block = this->blockchain().getHeaderByHeight(height);
         rawHeader = block->serialize();
      }
      catch (exception&)
      {
         return nullptr;
      }

      auto response = make_shared<::Codec_CommonTypes::BinaryData>();
      response->set_data(rawHeader.getPtr(), rawHeader.getSize());
      return response;
   }

   default:
      LOGWARN << "unkonwn method";
   }

   return nullptr;
}

///////////////////////////////////////////////////////////////////////////////
shared_ptr<BDV_Server_Object> Clients::get(const string& id) const
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
   packetProcess_threadLock_.store(0, memory_order_relaxed);

   isReadyPromise_ = make_shared<promise<bool>>();
   isReadyFuture_ = isReadyPromise_->get_future();
   auto lbdFut = isReadyFuture_;

   //unsafe, should consider creating the blockchain object as a shared_ptr
   auto bc = &blockchain();

   auto isReadyLambda = [lbdFut, bc](void)->unsigned
   {
      if (lbdFut.wait_for(chrono::seconds(0)) == future_status::ready)
      {
         return bc->top()->getBlockHeight();
      }

      return UINT32_MAX;
   };

   if (BlockDataManagerConfig::getServiceType() == SERVICE_FCGI)
   {
      cb_ = make_unique<LongPoll>(isReadyLambda);
   }
   else if (BlockDataManagerConfig::getServiceType() == SERVICE_WEBSOCKET)
   {
      auto&& bdid = READHEX(getID());
      if (bdid.getSize() != 8)
         throw runtime_error("invalid bdv id");

      auto intid = (uint64_t*)bdid.getPtr();
      cb_ = make_unique<WS_Callback>(*intid);
   }
   else
   {
      throw runtime_error("unexpected service type");
   }
}

///////////////////////////////////////////////////////////////////////////////
BDV_Server_Object::BDV_Server_Object(
   const string& id, BlockDataManagerThread *bdmT) :
   bdvID_(id), bdmT_(bdmT), BlockDataViewer(bdmT->bdm())
{
   setup();
}

///////////////////////////////////////////////////////////////////////////////
void BDV_Server_Object::startThreads()
{
   auto initLambda = [this](void)->void
   { this->init(); };

   initT_ = thread(initLambda);
}

///////////////////////////////////////////////////////////////////////////////
void BDV_Server_Object::haltThreads()
{
   if(cb_ != nullptr)
      cb_->shutdown();
   if (initT_.joinable())
      initT_.join();
}

///////////////////////////////////////////////////////////////////////////////
void BDV_Server_Object::init()
{
   bdmPtr_->blockUntilReady();

   while (1)
   {
      bool isNew = false;
      map<string, walletRegStruct> wltMap;

      {
         unique_lock<mutex> lock(registerWalletMutex_);

         if (wltRegMap_.size() == 0)
            break;

         wltMap = move(wltRegMap_);
         wltRegMap_.clear();
      }

      //create address batch
      auto batch = make_shared<AddressBatch>(bdvID_);
      batch->isNew_ = false;

      //fill with addresses from protobuf payloads
      for (auto& wlt : wltMap)
      {
         for (unsigned i = 0; i < wlt.second.command_->bindata_size(); i++)
         {
            auto& addrStr = wlt.second.command_->bindata(i);
            BinaryDataRef addrRef; addrRef.setRef(addrStr);
            batch->scrAddrSet_.insert(move(addrRef));
         }
      }

      //callback only serves to wait on the registration event
      auto promPtr = make_shared<promise<bool>>();
      auto fut = promPtr->get_future();
      auto callback = [promPtr](set<BinaryDataRef>&)->void
      {
         promPtr->set_value(true);
      };

      batch->callback_ = callback;

      //register the batch
      auto saf = bdmPtr_->getScrAddrFilter();
      saf->registerAddressBatch(batch);
      fut.get();

      //addresses are now registered, populate the wallet maps
      populateWallets(wltMap);
   }

   //could a wallet registration event get lost in between the init loop 
   //and setting the promise?

   //init wallets
   auto&& notifPtr = make_unique<BDV_Notification_Init>();
   scanWallets(move(notifPtr));

   //create zc packet and pass to wallets
   auto filterLbd = [this](const BinaryData& scrAddr)->bool
   {
      return hasScrAddress(scrAddr);
   };

   auto zcstruct = createZcNotification(filterLbd);
   scanWallets(move(zcstruct));
   
   //mark bdv object as ready
   isReadyPromise_->set_value(true);

   //callback client with BDM_Ready packet
   auto message = make_shared<BDVCallback>();
   auto notif = message->add_notification();
   notif->set_type(NotificationType::ready);
   notif->set_height(blockchain().top()->getBlockHeight());
   cb_->callback(message);

   DatabaseContainer_Sharded::clearThreadShardTx(this_thread::get_id());
}

///////////////////////////////////////////////////////////////////////////////
void BDV_Server_Object::processNotification(
   shared_ptr<BDV_Notification> notifPtr)
{
   auto action = notifPtr->action_type();
   if (action < BDV_Progress)
   {
      //skip all but progress notifications if BDV isn't ready
      if (isReadyFuture_.wait_for(chrono::seconds(0)) != future_status::ready)
         return;
   }

   scanWallets(notifPtr);

   auto callbackPtr = make_shared<BDVCallback>();
   auto notif = callbackPtr->add_notification();

   switch (action)
   {
   case BDV_NewBlock:
   {
      notif->set_type(NotificationType::newblock);
      auto&& payload =
         dynamic_pointer_cast<BDV_Notification_NewBlock>(notifPtr);
      uint32_t blocknum =
         payload->reorgState_.newTop_->getBlockHeight();
      notif->set_height(payload->reorgState_.newTop_->getBlockHeight());

      break;
   }

   case BDV_Refresh:
   {
      auto&& payload =
         dynamic_pointer_cast<BDV_Notification_Refresh>(notifPtr);

      auto& bdId = payload->refreshID_;

      notif->set_type(NotificationType::refresh);
      auto refresh = notif->mutable_refresh();
      refresh->set_refreshtype(payload->refresh_);
      refresh->add_id(bdId.getPtr(), bdId.getSize());

      break;
   }

   case BDV_ZC:
   {
      auto&& payload =
         dynamic_pointer_cast<BDV_Notification_ZC>(notifPtr);

      notif->set_type(NotificationType::zc);
      auto ledgers = notif->mutable_ledgers();

      for (auto& lePair : payload->leMap_)
      {
         auto ledger_entry = ledgers->add_values();
         lePair.second.fillMessage(ledger_entry);
      }

      break;
   }

   case BDV_Progress:
   {
      auto&& payload =
         dynamic_pointer_cast<BDV_Notification_Progress>(notifPtr);

      notif->set_type(NotificationType::progress);
      auto pd = notif->mutable_progress();

      pd->set_phase(payload->phase_);
      pd->set_progress(payload->progress_);
      pd->set_time(payload->time_);
      pd->set_numericprogress(payload->numericProgress_);
      for (auto& id : payload->walletIDs_)
         pd->add_id(move(id));
    
      break;
   }

   case BDV_NodeStatus:
   {
      auto&& payload =
         dynamic_pointer_cast<BDV_Notification_NodeStatus>(notifPtr);

      notif->set_type(NotificationType::nodestatus);
      auto status = notif->mutable_nodestatus();

      auto& nodeStatus = payload->status_;

      status->set_status((unsigned)nodeStatus.status_);
      status->set_segwitenabled(nodeStatus.SegWitEnabled_);
      status->set_rpcstatus((unsigned)nodeStatus.rpcStatus_);

      auto chainState_proto = new ::Codec_NodeStatus::NodeChainState();
      chainState_proto->set_state((unsigned)nodeStatus.chainState_.state());
      chainState_proto->set_blockspeed(nodeStatus.chainState_.getBlockSpeed());
      chainState_proto->set_eta(nodeStatus.chainState_.getETA());
      chainState_proto->set_pct(nodeStatus.chainState_.getProgressPct());
      chainState_proto->set_blocksleft(nodeStatus.chainState_.getBlocksLeft());
      status->set_allocated_chainstate(chainState_proto);

      break;
   }

   case BDV_Error:
   {
      auto&& payload =
         dynamic_pointer_cast<BDV_Notification_Error>(notifPtr);

      notif->set_type(NotificationType::error);
      auto error = notif->mutable_error();

      error->set_type((unsigned)payload->errStruct.errType_);
      error->set_error(payload->errStruct.errorStr_);
      error->set_extra(payload->errStruct.extraMsg_);

      break;
   }

   default:
      return;
   }

   cb_->callback(callbackPtr);
}

///////////////////////////////////////////////////////////////////////////////
void BDV_Server_Object::registerWallet(
   shared_ptr<::Codec_BDVCommand::BDVCommand> command)
{
   if (isReadyFuture_.wait_for(chrono::seconds(0)) != future_status::ready)
   {
      //only run this code if the bdv maintenance thread hasn't started yet
      unique_lock<mutex> lock(registerWalletMutex_);

      //save data
      auto& wltregstruct = wltRegMap_[command->hash()];
      wltregstruct.command_ = command;
      wltregstruct.type_ = TypeWallet;

      auto notif = make_shared<BDV_Notification_Refresh>(
         getID(), BDV_registrationCompleted, command->hash());
      processNotification(notif);

      return;
   }

   //register wallet with BDV
   auto bdvPtr = (BlockDataViewer*)this;
   bdvPtr->registerWallet(command);
}

///////////////////////////////////////////////////////////////////////////////
void BDV_Server_Object::registerLockbox(
   shared_ptr<::Codec_BDVCommand::BDVCommand> command)
{
   if (isReadyFuture_.wait_for(chrono::seconds(0)) != future_status::ready)
   {
      //only run this code if the bdv maintenance thread hasn't started yet

      unique_lock<mutex> lock(registerWalletMutex_);

      //save data
      auto& wltregstruct = wltRegMap_[command->hash()];
      wltregstruct.command_ = command;
      wltregstruct.type_ = TypeLockbox;

      auto notif = make_shared<BDV_Notification_Refresh>(
         getID(), BDV_registrationCompleted, command->hash());
      processNotification(notif);
      return;
   }

   //register wallet with BDV
   auto bdvPtr = (BlockDataViewer*)this;
   bdvPtr->registerLockbox(command);
}

///////////////////////////////////////////////////////////////////////////////
void BDV_Server_Object::populateWallets(map<string, walletRegStruct>& wltMap)
{
   auto safPtr = getSAF();
   auto addrMap = safPtr->getScrAddrMap();

   for (auto& wlt : wltMap)
   {
      auto& walletId = wlt.second.command_->walletid();
      BinaryDataRef bdr; bdr.setRef(walletId);

      shared_ptr<BtcWallet> theWallet;
      if (wlt.second.type_ == TypeWallet)
         theWallet = groups_[group_wallet].getOrSetWallet(bdr);
      else
         theWallet = groups_[group_lockbox].getOrSetWallet(bdr);

      if (theWallet == nullptr)
      {
         LOGERR << "failed to get or set wallet";
         continue;
      }

      map<BinaryDataRef, shared_ptr<ScrAddrObj>> newAddrMap;
      for (int i = 0; i < wlt.second.command_->bindata_size(); i++)
      {
         auto& addrStr = wlt.second.command_->bindata(i);
         BinaryDataRef addrRef; addrRef.setRef(addrStr);

         if (theWallet->hasScrAddress(addrRef))
            continue;

         auto iter = addrMap->find(addrRef);
         if (iter == addrMap->end())
            throw runtime_error("address missing from saf");

         auto addrObj = make_shared<ScrAddrObj>(
            db_, &blockchain(), iter->first);
         newAddrMap.insert(move(make_pair(iter->first, addrObj)));
      }

      if (newAddrMap.size() == 0)
         continue;

      theWallet->scrAddrMap_.update(newAddrMap);
   }
}

////////////////////////////////////////////////////////////////////////////////
void BDV_Server_Object::flagRefresh(
   BDV_refresh refresh, const BinaryData& refreshID,
   unique_ptr<BDV_Notification_ZC> zcPtr)
{
   auto notif = make_unique<BDV_Notification_Refresh>(
      getID(), refresh, refreshID);
   if (zcPtr != nullptr)
      notif->zcPacket_ = move(zcPtr->packet_);

   if (notifLambda_)
      notifLambda_(move(notif));
}

////////////////////////////////////////////////////////////////////////////////
bool BDV_Server_Object::processPayload(shared_ptr<BDV_Payload>& packet, 
   shared_ptr<Message>& result)
{
   //only ever one thread gets this far at any given time, therefor none of the
   //underlying objects need to be thread safe
   if (packet == nullptr)
      return false;

   shared_ptr<BDV_Payload> currentPacket = packet;
   do //loop over packetMap to feed unparsed messages back in
   {  
      auto parsed = currentMessage_.parsePacket(currentPacket);
      if (!parsed)
      {
         //packet did not extend current message, save for later
         packetMap_.insert(make_pair(packet->packetID_, currentPacket));
         return true;
      }

      if (currentMessage_.isReady())
      {
         //message is complete, time to process it
         break;
      }

      if (packetMap_.size() == 0)
      {
         //out of packets to feed the current message, return
         return true;
      }

      //look for the next consecutive id following current message's 
      //top id
      auto nextId = currentMessage_.topId() + 1;
      auto iter = packetMap_.find(nextId);
      if (iter == packetMap_.end())
      {
         //no such packet, return
         return true;
      }

      //have the next packet, iterate over it
      currentPacket = iter->second;
      packetMap_.erase(iter);
   } while (1);

   packet->messageID_ = currentMessage_.partialMessage_.getId();
   auto message = make_shared<BDVCommand>();
   if (!currentMessage_.getMessage(message))
   {
      auto staticCommand = make_shared<StaticCommand>();
      if (currentMessage_.getMessage(staticCommand))
         result = staticCommand;

      //reset the current message as it resulted in a full payload
      resetCurrentMessage();
      return false;
   }

   result = processCommand(message);

   //reset the current message as it resulted in a full payload
   resetCurrentMessage();
   return true;
}

///////////////////////////////////////////////////////////////////////////////
void BDV_Server_Object::resetCurrentMessage()
{
   //remove packet ids current message is using from packetMap
   auto& messagePacketMap = currentMessage_.partialMessage_.getPacketMap();

   for (auto& packetPair : messagePacketMap)
      packetMap_.erase(packetPair.first);

   currentMessage_.reset();
   if (packetMap_.size() != 0)
   {
      auto iter = packetMap_.begin();
      packetToReinject_ = iter->second;
      packetMap_.erase(iter);
   }
}

///////////////////////////////////////////////////////////////////////////////
//
// Clients
//
///////////////////////////////////////////////////////////////////////////////
void Clients::init(BlockDataManagerThread* bdmT,
   function<void(void)> shutdownLambda)
{
   bdmT_ = bdmT;
   shutdownCallback_ = shutdownLambda;

   run_.store(true, memory_order_relaxed);

   auto mainthread = [this](void)->void
   {
      notificationThread();
   };

   auto outerthread = [this](void)->void
   {
      bdvMaintenanceLoop();
   };

   auto innerthread = [this](void)->void
   {
      bdvMaintenanceThread();
   };

   auto gcThread = [this](void)->void
   {
      garbageCollectorThread();
   };

   auto parserThread = [this](void)->void
   {
      this->messageParserThread();
   };


   controlThreads_.push_back(thread(mainthread));
   controlThreads_.push_back(thread(outerthread));

   unsigned innerThreadCount = 2;
   if (BlockDataManagerConfig::getDbType() == ARMORY_DB_SUPER &&
      bdmT_->bdm()->config().nodeType_ != Node_UnitTest)
      innerThreadCount = thread::hardware_concurrency();
   for (unsigned i = 0; i < innerThreadCount; i++)
   {
      controlThreads_.push_back(thread(innerthread));
      controlThreads_.push_back(thread(parserThread));
   }

   auto callbackPtr = make_unique<ZeroConfCallbacks_BDV>(this);
   bdmT_->bdm()->registerZcCallbacks(move(callbackPtr));

   //no gc for unit tests
   if (bdmT_->bdm()->config().nodeType_ == Node_UnitTest)
      return;

   controlThreads_.push_back(thread(gcThread));
}

///////////////////////////////////////////////////////////////////////////////
void Clients::bdvMaintenanceLoop()
{
   while (1)
   {
      shared_ptr<BDV_Notification> notifPtr;
      try
      {
         notifPtr = move(outerBDVNotifStack_.pop_front());
      }
      catch (StopBlockingLoop&)
      {
         LOGINFO << "Shutting down BDV event loop";
         break;
      }

      auto bdvMap = BDVs_.get();
      auto& bdvID = notifPtr->bdvID();
      if (bdvID.size() == 0)
      {
         //empty bdvID means broadcast notification to all BDVs
         for (auto& bdv_pair : *bdvMap)
         {
            auto notifPacket = make_shared<BDV_Notification_Packet>();
            notifPacket->bdvPtr_ = bdv_pair.second;
            notifPacket->notifPtr_ = notifPtr;
            innerBDVNotifStack_.push_back(move(notifPacket));
         }
      }
      else
      {
         //grab bdv
         auto iter = bdvMap->find(bdvID);
         if (iter == bdvMap->end())
            continue;

         auto notifPacket = make_shared<BDV_Notification_Packet>();
         notifPacket->bdvPtr_ = iter->second;
         notifPacket->notifPtr_ = notifPtr;
         innerBDVNotifStack_.push_back(move(notifPacket));
      }
   }
}

///////////////////////////////////////////////////////////////////////////////
void Clients::bdvMaintenanceThread()
{
   while (1)
   {
      shared_ptr<BDV_Notification_Packet> notifPtr;
      try
      {
         notifPtr = move(innerBDVNotifStack_.pop_front());
      }
      catch (StopBlockingLoop&)
      {
         break;
      }

      if (notifPtr->bdvPtr_ == nullptr)
      {
         LOGWARN << "null bdvPtr in notification";
         continue;
      }

      notifPtr->bdvPtr_->processNotification(notifPtr->notifPtr_);
   }
}

///////////////////////////////////////////////////////////////////////////////
void Clients::processShutdownCommand(shared_ptr<StaticCommand> command)
{
   auto& thisCookie = bdmT_->bdm()->config().cookie_;
   if (thisCookie.size() == 0)
      return;

   try
   {
      if (!command->has_cookie())
         throw runtime_error("malformed command for processShutdownCommand");
      auto& cookie = command->cookie();

      if ((cookie.size() == 0) || (cookie != thisCookie))
         throw runtime_error("spawnId mismatch");
   }
   catch (...)
   {
      return;
   }

   switch (command->method())
   {
   case StaticMethods::shutdown:
   {
      auto shutdownLambda = [this](void)->void
      {
         this->exitRequestLoop();
      };

      //run shutdown sequence in its own thread so that the fcgi listen
      //loop can exit properly.
      thread shutdownThr(shutdownLambda);
      if (shutdownThr.joinable())
         shutdownThr.detach();
      break;
   }

   case StaticMethods::shutdownNode:
   {
      if (bdmT_->bdm()->nodeRPC_ != nullptr)
         bdmT_->bdm()->nodeRPC_->shutdown();
   }

   default:
      LOGWARN << "unexpected command in processShutdownCommand";
   }
}

///////////////////////////////////////////////////////////////////////////////
void Clients::shutdown()
{
   unique_lock<mutex> lock(shutdownMutex_, defer_lock);
   if (!lock.try_lock())
      return;
   
   /*shutdown sequence*/
   if (!run_.load(memory_order_relaxed))
      return;

   //prevent all new commands from running
   run_.store(false, memory_order_relaxed);

   //shutdown Clients gc thread
   gcCommands_.completed();

   //cleanup all BDVs
   unregisterAllBDVs();

   //shutdown maintenance threads
   outerBDVNotifStack_.completed();
   innerBDVNotifStack_.completed();
   packetQueue_.terminate();

   //exit BDM maintenance thread
   if (!bdmT_->shutdown())
      return;

   vector<thread::id> idVec;
   for (auto& thr : controlThreads_)
   {
      idVec.push_back(thr.get_id());
      if (thr.joinable())
         thr.join();
   }

   //shutdown ZC container
   bdmT_->bdm()->disableZeroConf();
   bdmT_->bdm()->getScrAddrFilter()->shutdown();
   bdmT_->cleanUp();

   DatabaseContainer_Sharded::clearThreadShardTx(idVec);
}

///////////////////////////////////////////////////////////////////////////////
void Clients::exitRequestLoop()
{
   /*terminate request processing loop*/
   LOGINFO << "proceeding to shutdown";

   //shutdown loop on FcgiServer side
   if (shutdownCallback_)
      shutdownCallback_();
}

///////////////////////////////////////////////////////////////////////////////
void Clients::unregisterAllBDVs()
{
   auto bdvs = BDVs_.get();
   BDVs_.clear();

   for (auto& bdv : *bdvs)
      bdv.second->haltThreads();
}

///////////////////////////////////////////////////////////////////////////////
shared_ptr<Message> Clients::registerBDV(
   shared_ptr<StaticCommand> command, string bdvID)
{
   try
   {
      if (!command->has_magicword())
         throw runtime_error("invalid command for registerBDV");
      auto& magic_word = command->magicword();
      BinaryDataRef magic_word_ref; magic_word_ref.setRef(magic_word);
      auto& thisMagicWord = bdmT_->bdm()->config().magicBytes_;

      if (thisMagicWord != magic_word_ref)
         throw runtime_error("magic word mismatch");
   }
   catch (runtime_error& e)
   {
      auto response = make_shared<::Codec_NodeStatus::BDV_Error>();
      response->set_type(Error_BDV);
      response->set_error(e.what());
      return response;
   }

   if (bdvID.size() == 0)
      bdvID = SecureBinaryData().GenerateRandom(10).toHexStr();
   auto newBDV = make_shared<BDV_Server_Object>(bdvID, bdmT_);

   auto notiflbd = [this](unique_ptr<BDV_Notification> notifPtr)
   {
      this->outerBDVNotifStack_.push_back(move(notifPtr));
   };

   newBDV->notifLambda_ = notiflbd;

   //add to BDVs map
   string newID(newBDV->getID());
   BDVs_.insert(move(make_pair(newID, newBDV)));

   LOGINFO << "registered bdv: " << newID;

   auto response = make_shared<::Codec_CommonTypes::BinaryData>();
   response->set_data(newID);
   return response;
}

///////////////////////////////////////////////////////////////////////////////
void Clients::unregisterBDV(const string& bdvId)
{
   shared_ptr<BDV_Server_Object> bdvPtr;

   //shutdown bdv threads
   {
      auto bdvMap = BDVs_.get();
      auto bdvIter = bdvMap->find(bdvId);
      if (bdvIter == bdvMap->end())
         return;

      //copy shared_ptr and unregister from bdv map
      bdvPtr = bdvIter->second;
      BDVs_.erase(bdvId);
   }

   bdvPtr->haltThreads();

   //we are done
   bdvPtr.reset();
   LOGINFO << "unregistered bdv: " << bdvId;
}

///////////////////////////////////////////////////////////////////////////////
void Clients::notificationThread(void) const
{
   if (bdmT_ == nullptr)
      throw runtime_error("invalid BDM thread ptr");

   while (1)
   {
      bool timedout = true;
      shared_ptr<BDV_Notification> notifPtr;

      try
      {
         notifPtr = move(bdmT_->bdm()->notificationStack_.pop_front(
            chrono::seconds(60)));
         if (notifPtr == nullptr)
            continue;
         timedout = false;
      }
      catch (StackTimedOutException&)
      {
         //nothing to do
      }
      catch (StopBlockingLoop&)
      {
         return;
      }
      catch (IsEmpty&)
      {
         LOGERR << "caught isEmpty in Clients maintenance loop";
         continue;
      }

      //trigger gc thread
      if (timedout == true || notifPtr->action_type() != BDV_Progress)
         gcCommands_.push_back(true);

      //don't go any futher if there is no new top
      if (timedout)
         continue;

      outerBDVNotifStack_.push_back(move(notifPtr));
   }
}

///////////////////////////////////////////////////////////////////////////////
void Clients::garbageCollectorThread(void)
{
   while (1)
   {
      try
      {
         bool command = gcCommands_.pop_front();
         if (!command)
            return;
      }
      catch (StopBlockingLoop&)
      {
         return;
      }

      vector<string> bdvToDelete;

      {
         auto bdvmap = BDVs_.get();

         for (auto& bdvPair : *bdvmap)
         {
            if (!bdvPair.second->cb_->isValid())
               bdvToDelete.push_back(bdvPair.first);
         }
      }

      for (auto& bdvID : bdvToDelete)
      {
         unregisterBDV(bdvID);
      }
   }
}

///////////////////////////////////////////////////////////////////////////////
void Clients::messageParserThread(void)
{
   while (1)
   {
      shared_ptr<BDV_Payload> payloadPtr;
      
      try
      {
         payloadPtr = move(packetQueue_.pop_front());
      }
      catch (StopBlockingLoop&)
      {
         break;
      }

      //sanity check
      if (payloadPtr == nullptr)
      {
         LOGERR << "????????? empty payload";
         continue;
      }

      if (payloadPtr->bdvPtr_ == nullptr)
      {
         LOGERR << "???????? empty bdv ptr";
         continue;
      }

      auto bdvPtr = payloadPtr->bdvPtr_;
      unsigned zero = 0;
      if (!bdvPtr->packetProcess_threadLock_.compare_exchange_weak(zero, 1))
      {
         //Failed to grab lock, there's already a thread processing a payload
         //for this bdv. Insert the payload back into the queue. Another 
         //thread will eventually pick it up and successfully grab the lock 
         if(payloadPtr == nullptr)
            LOGERR << "!!!!!! empty payload at reinsertion";

         packetQueue_.push_back(move(payloadPtr));
         continue;
      }

      //grabbed the thread lock, time to process the payload
      auto result = processCommand(payloadPtr);
      if (bdvPtr->packetToReinject_ != nullptr)
      {
         bdvPtr->packetToReinject_->bdvPtr_ = bdvPtr;
         packetQueue_.push_back(move(bdvPtr->packetToReinject_));
         bdvPtr->packetToReinject_ = nullptr;
      }

      //release lock
      bdvPtr->packetProcess_threadLock_.store(0);

      //write return value if any
      if (result != nullptr)
         WebSocketServer::write(
            payloadPtr->packet_->bdvID_, payloadPtr->messageID_, result);
   }
}

///////////////////////////////////////////////////////////////////////////////
shared_ptr<Message> Clients::processCommand(shared_ptr<BDV_Payload>
   payloadPtr)
{
   //clear bdvPtr from the payload to avoid circular ownership
   auto bdvPtr = payloadPtr->bdvPtr_;
   payloadPtr->bdvPtr_.reset();

   //process payload
   shared_ptr<Message> result = nullptr;
   if (!bdvPtr->processPayload(payloadPtr, result))
   {
      auto staticCommand = dynamic_pointer_cast<StaticCommand>(result);
      if (staticCommand == nullptr)
         return nullptr;

      result = processUnregisteredCommand(
         payloadPtr->packet_->bdvID_, staticCommand);
   }

   return result;
}

///////////////////////////////////////////////////////////////////////////////
shared_ptr<Message> Clients::processUnregisteredCommand(const uint64_t& bdvId, 
   shared_ptr<StaticCommand> command)
{
   switch (command->method())
   {
   case StaticMethods::shutdown:
   case StaticMethods::shutdownNode:
   {
      /*
      in: cookie
      out: void
      */
      processShutdownCommand(command);
      break;
   }

   case StaticMethods::registerBDV:
   {
      /*
      in: network magic word
      out: bdv id as string
      */
      BinaryDataRef bdr;
      bdr.setRef((uint8_t*)&bdvId, 8);
      return registerBDV(command, bdr.toHexStr());
   }

   case StaticMethods::unregisterBDV:
      break;

   default:
      return nullptr;
   }

   return nullptr;
}

///////////////////////////////////////////////////////////////////////////////
//
// Callback
//
///////////////////////////////////////////////////////////////////////////////
Callback::~Callback()
{}

///////////////////////////////////////////////////////////////////////////////
shared_ptr<Message> LongPoll::respond(shared_ptr<BDVCommand> command)
{
   unique_lock<mutex> lock(mu_, defer_lock);

   if (!lock.try_lock())
   {
      auto response = make_shared<BDVCallback>();
      auto notif = response->add_notification();
      notif->set_type(NotificationType::continue_polling);
      return response;
   }

   count_ = 0;

   switch (command->method())
   {
   case Methods::waitOnBDVInit:
   {
      //check is bdv is ready
      auto topheight = isReady_();
      if (topheight != UINT32_MAX)
      {
         auto response = make_shared<BDVCallback>();
         auto notif = response->add_notification();
         notif->set_type(NotificationType::ready);
         notif->set_height(topheight);

         vector<shared_ptr<BDVCallback>> orderVec;
         orderVec.push_back(response);
         return respond_inner(orderVec);
      }

      //otherwise, fallback to wait on notifications
   }

   case Methods::waitOnBDVNotification:
   {
      vector<shared_ptr<BDVCallback>> orderVec;

      try
      {
         orderVec = move(notificationStack_.pop_all(std::chrono::seconds(50)));
      }
      catch (IsEmpty&)
      {
         auto response = make_shared<BDVCallback>();
         auto notif = response->add_notification();
         notif->set_type(NotificationType::continue_polling);
         orderVec.push_back(response);
      }
      catch (StackTimedOutException&)
      {
         auto response = make_shared<BDVCallback>();
         auto notif = response->add_notification();
         notif->set_type(NotificationType::continue_polling);
         orderVec.push_back(response);
      }
      catch (StopBlockingLoop&)
      {
         count_ = 5;

         //return terminate packet
         auto response = make_shared<BDVCallback>();
         auto notif = response->add_notification();
         notif->set_type(NotificationType::terminate);
         orderVec.push_back(response);
      }

      return respond_inner(orderVec);
   }

   default:
      LOGWARN << "invalid command for longpoll callback";
   }

   return nullptr;
}

///////////////////////////////////////////////////////////////////////////////
void WS_Callback::callback(shared_ptr<BDVCallback> command)
{
   //write to socket
   WebSocketServer::write(bdvID_, WEBSOCKET_CALLBACK_ID, command);
}

///////////////////////////////////////////////////////////////////////////////
shared_ptr<Message> LongPoll::respond_inner(
   vector<shared_ptr<BDVCallback>>& orderVec)
{
   if (orderVec.size() == 0)
      return nullptr;

   //consolidate NewBlock and Progress notifications
   shared_ptr<BDVCallback> order_newblock;
   shared_ptr<BDVCallback> order_progress;

   auto response = make_shared<::Codec_BDVCommand::BDVCallback>();

   for (auto& order : orderVec)
   {
      if (order->notification_size() == 0)
         continue;

      auto& notif = order->notification(0);
      switch (notif.type())
      {

      case NotificationType::newblock:
         order_newblock = order;
         break;

      case NotificationType::progress:
         order_progress = order;
         break;

      default:
         response->MergeFrom(*order);
      }
   }

   if (order_newblock != nullptr)
      response->MergeFrom(*order_newblock);

   if (order_progress != nullptr)
      response->MergeFrom(*order_progress);
    
   return move(response);
}

///////////////////////////////////////////////////////////////////////////////
//
// ZeroConfCallbacks
//
///////////////////////////////////////////////////////////////////////////////
set<string> ZeroConfCallbacks_BDV::hasScrAddr(const BinaryDataRef& addr) const
{
   set<string> result;
   auto bdvPtr = clientsPtr_->BDVs_.get();

   for (auto& bdv_pair : *bdvPtr)
   {
      if (bdv_pair.second->hasScrAddress(addr))
         result.insert(bdv_pair.first);
   }

   return result;
}

///////////////////////////////////////////////////////////////////////////////
void ZeroConfCallbacks_BDV::pushZcNotification(
   ZeroConfContainer::NotificationPacket& packet)
{
   auto bdvPtr = clientsPtr_->BDVs_.get();
   auto iter = bdvPtr->find(packet.bdvID_);
   if (iter == bdvPtr->end())
   {
      LOGWARN << "pushed zc notification with invalid bdvid";
      return;
   }

   auto notifPacket = make_shared<BDV_Notification_Packet>();
   notifPacket->bdvPtr_ = iter->second;
   notifPacket->notifPtr_ = make_shared<BDV_Notification_ZC>(packet);
   clientsPtr_->innerBDVNotifStack_.push_back(move(notifPacket));
}

///////////////////////////////////////////////////////////////////////////////
void ZeroConfCallbacks_BDV::errorCallback(
   const string& bdvId, string& errorStr, const string& txHash)
{
   auto bdvPtr = clientsPtr_->BDVs_.get();
   auto iter = bdvPtr->find(bdvId);
   if (iter == bdvPtr->end())
   {
      LOGWARN << "pushed zc error for missing bdv";
      return;
   }

   auto notifPacket = make_shared<BDV_Notification_Packet>();
   notifPacket->bdvPtr_ = iter->second;
   notifPacket->notifPtr_ = make_shared<BDV_Notification_Error>(
      bdvId, Error_ZC, move(errorStr), txHash);
   clientsPtr_->innerBDVNotifStack_.push_back(move(notifPacket));
}

///////////////////////////////////////////////////////////////////////////////
//
// BDV_PartialMessage
//
///////////////////////////////////////////////////////////////////////////////
bool BDV_PartialMessage::parsePacket(shared_ptr<BDV_Payload> packet)
{
   auto&& bdr = packet->packet_->data_.getRef();
   auto result = partialMessage_.parsePacket(packet->packetID_, bdr);
   if (!result)
      return false;

   payloads_.push_back(packet);
   return true;
}

///////////////////////////////////////////////////////////////////////////////
void BDV_PartialMessage::reset()
{
   partialMessage_.reset();
   payloads_.clear();
}

///////////////////////////////////////////////////////////////////////////////
bool BDV_PartialMessage::getMessage(shared_ptr<Message> msgPtr)
{
   if (!isReady())
      return false;

   return partialMessage_.getMessage(msgPtr.get());
}

///////////////////////////////////////////////////////////////////////////////
size_t BDV_PartialMessage::topId() const
{
   auto& packetMap = partialMessage_.getPacketMap();
   if (packetMap.size() == 0)
      return SIZE_MAX;

   return packetMap.rbegin()->first;
}





