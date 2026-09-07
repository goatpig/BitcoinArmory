////////////////////////////////////////////////////////////////////////////////
//                                                                            //
//                                                                            //
//  Copyright (C) 2016-2026, goatpig                                          //
//  Distributed under the MIT license                                         //
//  See LICENSE-MIT or https://opensource.org/licenses/MIT                    //
//                                                                            //
////////////////////////////////////////////////////////////////////////////////

#include "TestUtils.h"
#include <reorgTest/blkdata.h>
#include <Utils/BIP15x_Handshake.h>
#include <Utils/FileUtils.h>
#include <Wallets/Accounts/AddressAccounts.h>
#include <Ledgers/LedgerEntry.h>
#include <BDM_mainthread.h>
#include <BlockchainDatabase/BlockchainData.h>
#include <ZeroConf/Parser.h>
#include <ZeroConf/Utils.h>

using namespace Armory;
using namespace Armory::Assets;
using namespace Armory::Wallets;

#include <capnp/message.h>
#include <capnp/serialize.h>
#include "capnp/BDV.capnp.h"
#include "capnp/Types.capnp.h"

namespace {
   BinaryData serializeCapnp(capnp::MallocMessageBuilder& builder)
   {
      auto flat = capnp::messageToFlatArray(builder);
      auto bytes = flat.asBytes();
      return BinaryData(bytes.begin(), bytes.end());
   }

   std::vector<TxIOPair> capnToTxios(
      const capnp::List<Codec::Types::TxioPair, capnp::Kind::STRUCT>::Reader& capnTxios)
   {
      std::vector<TxIOPair> txios;
      txios.reserve(capnTxios.size());

      for (auto capnTxio : capnTxios) {
         auto capnScrAddr = capnTxio.getScrAddr();
         BinaryDataRef scrAddr{capnScrAddr.begin(), capnScrAddr.end()};
         TxIOPair txio{capnTxio.getTxOut(), capnTxio.getAmount(), scrAddr};
         txio.setTxIn(capnTxio.getTxIn());

         txio.setTxTime(capnTxio.getTxTime());
         txio.setRBF(capnTxio.getRbf());
         txio.setChained(capnTxio.getChained());
         txios.emplace_back(std::move(txio));
      }
      return txios;
   }

   std::vector<Types::TxIOKey> toKeyVector(
      const std::map<Types::TxIOKey, TxOutData>& txOutData)
   {
      std::vector<Types::TxIOKey> txOutKeys;
      txOutKeys.reserve(txOutData.size());
      for (const auto& txOutPair : txOutData) {
         txOutKeys.emplace_back(txOutPair.first);
      }
      return txOutKeys;
   }

   UTXO getUTXO(Types::TxIOKey txIOKey, std::shared_ptr<BlockDataManager> bdm)
   {
      if (Types::isThisAZCKey(txIOKey)) {
         auto ss = bdm->zeroConfCont()->getSnapshot();
         auto tx = ss->getTxByKey(Types::getTxKeyFromTxIOKey(txIOKey));
         auto txOutId = Types::getTxIOIndexFromTxIOKey(txIOKey);
         auto txOut = tx->getTxObj().getTxOutCopy(txOutId);
         return UTXO{txOut.getAmount(),
            UINT32_MAX, UINT16_MAX, txOutId,
            tx->getTxHash(), txOut.getScript()};
      } else {
         auto bc = bdm->blockchain();
         auto bcData = bdm->blockchainData();

         auto header = bc->getHeaderById(Types::getBlockIDFromTxKey(txIOKey));
         auto txOutId = Types::getTxIOIndexFromTxIOKey(txIOKey);
         auto tx = bcData->getTx(txIOKey);
         auto txOut = tx.getTxOutCopy(txOutId);
         return UTXO{txOut.getAmount(),
            header->getBlockHeight(), tx.getTxIndex(), txOutId,
            tx.getThisHash(), txOut.getScript()};
      }
   }
}

////////////////////////////////////////////////////////////////////////////////
namespace TestUtils
{
   /////////////////////////////////////////////////////////////////////////////
   bool searchFile(const std::filesystem::path& filename, BinaryData& data)
   {
      //create mmap of file
      auto filemap = FileUtils::FileMap(filename);

      if (data.getSize() < 8) {
         throw std::runtime_error("only for buffers 8 bytes and larger");
      }

      //search it
      uint64_t sample;
      uint64_t* data_head = (uint64_t*)data.getPtr();

      bool result = false;
      for (unsigned i = 0; i < filemap.size() - data.getSize(); i++) {
         memcpy(&sample, filemap.ptr() + i, 8);
         if (sample == *data_head) {
            BinaryDataRef bdr(filemap.ptr() + i, data.getSize());
            if (bdr == data.getRef())
            {
               result = true;
               break;
            }
         }
      }
      return result;
   }

   /////////////////////////////////////////////////////////////////////////////
   uint32_t getTopBlockHeightInDB(BlockDataManager* bdm, DB_SELECT db)
   {
      auto sdbi = bdm->getIFace()->getStoredDBInfo(db, 0xFFFF);
      auto header = bdm->blockchain()->getHeaderByHash(sdbi.topScannedBlkHash);
      return header->getBlockHeight();
   }

   /////////////////////////////////////////////////////////////////////////////
   int char2int(char input)
   {
      if (input >= '0' && input <= '9')
         return input - '0';
      if (input >= 'A' && input <= 'F')
         return input - 'A' + 10;
      if (input >= 'a' && input <= 'f')
         return input - 'a' + 10;
      return 0;
   }

   /////////////////////////////////////////////////////////////////////////////
   void hex2bin(const char* src, unsigned char* target)
   {
      while (*src && src[1])
      {
         *(target++) = char2int(*src) * 16 + char2int(src[1]);
         src += 2;
      }
   }

   /////////////////////////////////////////////////////////////////////////////
   void concatFile(const std::vector<std::filesystem::path> &from,
      const std::filesystem::path &to)
   {
      std::ofstream o(to, std::ios::app | std::ios::binary);

      for (const auto& fname : from) {
         std::ifstream i(fname, std::ios::binary);
         o << i.rdbuf();
      }

      o.flush();
      o.close();
   }

   /////////////////////////////////////////////////////////////////////////////
   void appendBlocks(const std::vector<std::string> &files,
      const std::filesystem::path &to)
   {
      std::vector<std::filesystem::path> fullFileNames;
      for (const std::string &f : files) {
         std::filesystem::path filename{"blk_" + f + ".dat"};
         fullFileNames.emplace_back(dataDir / filename);
      }
      concatFile(fullFileNames, to);
   }

   /////////////////////////////////////////////////////////////////////////////
   void setBlocks(const std::vector<std::string> &files,
      const std::filesystem::path &to)
   {
      std::ofstream o(to, std::ios::trunc | std::ios::binary);
      o.close();

      std::vector<std::filesystem::path> fullFileNames;
      for (const std::string &f : files) {
         std::filesystem::path filename{"blk_" + f + ".dat"};
         fullFileNames.emplace_back(dataDir / filename);
      }
      concatFile(fullFileNames, to);
   }

   /////////////////////////////////////////////////////////////////////////////
   BinaryData getTx(unsigned height, unsigned id)
   {
      auto path = dataDir / std::filesystem::path{
         "blk_" + std::to_string(height) + ".dat"
      };

      std::ifstream blkfile(path, std::ios::binary);
      blkfile.seekg(0, std::ios::end);
      auto size = blkfile.tellg();
      blkfile.seekg(0, std::ios::beg);

      std::vector<char> vec;
      vec.resize(size);
      blkfile.read(&vec[0], size);
      blkfile.close();

      BinaryRefReader brr((uint8_t*)&vec[0], size);
      StoredHeader sbh;
      sbh.unserializeFullBlock(brr, false, true);

      auto iter = sbh.stxMap.find(id);
      if (iter == sbh.stxMap.end()) {
         throw std::range_error("invalid tx id");
      }
      return iter->second.dataCopy;
   }

   ////////////////////////////////////////////////////////////////////////////////
   std::shared_ptr<AssetEntry> getMainAccountAssetForIndex(
      std::shared_ptr<AssetWallet> wlt, Armory::Wallets::AssetKeyType index)
   {
      auto mainAcc = wlt->getAccountForID(wlt->getMainAccountID());
      auto outerAcc = mainAcc->getOuterAccount();
      return outerAcc->getAssetForKey(index);
   }

   ////////////////////////////////////////////////////////////////////////////////
   size_t getMainAccountAssetCount(std::shared_ptr<AssetWallet> wlt)
   {
      auto mainAcc = wlt->getAccountForID(wlt->getMainAccountID());
      auto outerAcc = mainAcc->getOuterAccount();
      return outerAcc->getAssetCount();
   }
}

////////////////////////////////////////////////////////////////////////////////
namespace DBTestUtils
{
   unsigned commandCtr_ = 0;
   std::deque<unsigned> zcDelays_;

   /////////////////////////////////////////////////////////////////////////////
   Hash32 getTopBlockHash(LMDBBlockDatabase* db, DB_SELECT dbSelect)
   {
      auto sdbi = db->getStoredDBInfo(dbSelect, 0xFFFF);
      return sdbi.topScannedBlkHash;
   }

   /////////////////////////////////////////////////////////////////////////////
   Types::BdvId registerBDV(Clients* clients, const BinaryData& magic_word)
   {
      Types::BdvId bdvID = 123456789;
      if (!clients->registerBDV(magic_word.toHexStr(), bdvID)) {
         return {};
      }
      return bdvID;
   }

   /////////////////////////////////////////////////////////////////////////////
   void goOnline(Clients* clients, Types::BdvId id)
   {
      capnp::MallocMessageBuilder message(128);
      auto payload = message.getRoot<Codec::BDV::Request>();

      auto bdvRequest = payload.initBdv();
      bdvRequest.setGoOnline();
      processCommand(clients, id, serializeCapnp(message));
   }

   /////////////////////////////////////////////////////////////////////////////
   const std::shared_ptr<BDV_Server_Object> getBDV(
      Clients* clients, Types::BdvId id)
   {
      return clients->get(id);
   }

   /////////////////////////////////////////////////////////////////////////////
   void registerWallet(Clients* clients, Types::BdvId bdvId,
      const std::vector<Types::ScrAddr>& scrAddrs, const std::string& wltName,
      bool waitOnReg)
   {
      capnp::MallocMessageBuilder message;
      auto payload = message.initRoot<Codec::BDV::Request>();
      auto bdvRequest = payload.initBdv();

      auto regReq = bdvRequest.initRegisterWallet();
      regReq.setWalletId(wltName);
      regReq.setIsNew(false);
      regReq.initAddresses(scrAddrs.size());
      auto capnAddresses = regReq.getAddresses();
      for (unsigned i=0; i<scrAddrs.size(); i++) {
         const auto& addr = scrAddrs[i];
         auto capnAddr = capnAddresses[i];
         capnAddr.setBody(capnp::Data::Builder(
            (uint8_t*)addr.getPtr(), addr.getSize()));
      }
      processCommand(clients, bdvId, serializeCapnp(message));
      if (!waitOnReg) {
         return;
      }

      while (true) {
         auto cbReply = waitOnSignal(clients, bdvId,
            (int)Codec::BDV::Notification::REFRESH);

         auto rawNotifs = get<0>(cbReply);
         kj::ArrayPtr<const capnp::word> words(
            reinterpret_cast<const capnp::word*>(rawNotifs.getPtr()),
            rawNotifs.getSize() / sizeof(capnp::word));
         capnp::FlatArrayMessageReader reader(words);
         auto notifs = reader.getRoot<Codec::BDV::Notifications>();
         auto notifList = notifs.getNotifs();
         auto capnpNotif = notifList[get<1>(cbReply)];
         if (!capnpNotif.isRefresh()) {
            continue;
         }

         auto refresh = capnpNotif.getRefresh();
         auto refreshIds = refresh.getIds();
         for (auto refreshId : refreshIds) {
            if (refreshId == wltName) {
               return;
            }
         }
      }
   }

   std::map<Armory::Types::TxIOKey, TxOutData> getTxOutHistory(
      const Types::ScrAddr& scrAddr, std::shared_ptr<BlockDataManager> bdm)
   {
      //get scrAddr uniqueID
      Types::BlockId uniqueID = UINT32_MAX;
      {
         auto addrMap = bdm->getScrAddrFilter()->getScanFilterAddrMap();
         auto iter = addrMap->find(scrAddr);
         if (iter != addrMap->end()) {
            uniqueID = iter->second->id;
         }
      }
      if (uniqueID == UINT32_MAX) {
         return {};
      }

      //get txout data from db
      return bdm->getIFace()->getTxOutHistoryForScrAddrKey(
         uniqueID, 0, UINT32_MAX);
   }

   std::map<Types::TxIOKey, std::shared_ptr<const TxIOPair>> getZcHistory(
      const Types::ScrAddr& scrAddr, std::shared_ptr<BlockDataManager> bdm)
   {
      auto ss = bdm->zeroConfCont()->getSnapshot();
      if (ss != nullptr) {
         return ss->getTxioMapForScrAddr(scrAddr);
      } else {
         return {};
      }
   }

   Types::Amount getScrAddrBalance(const Types::ScrAddr& scrAddr,
      std::shared_ptr<BlockDataManager> bdm)
   {
      auto txOutData = getTxOutHistory(scrAddr, bdm);
      auto txInData = bdm->getIFace()->getTxInHistoryForTxOutHistory(
         toKeyVector(txOutData));
      auto zcTxIOs = getZcHistory(scrAddr, bdm);
      auto bc = bdm->blockchain();

      Types::Amount total = 0;
      for (const auto& txOutPair : txOutData) {
         auto header = bc->getHeaderById(txOutPair.second.blockID);
         if (!header->isMainBranch()) {
            continue;
         }

         auto txInIter = txInData.find(txOutPair.first);
         if (txInIter != txInData.end()) {
            auto txInBlockID = Types::getBlockIDFromTxKey(txInIter->second);
            header = bc->getHeaderById(txInBlockID);
            if (header->isMainBranch()) {
               continue;
            }
         }

         auto zcIter = zcTxIOs.find(txOutPair.first);
         if (zcIter != zcTxIOs.end()) {
            continue;
         }
         total += txOutPair.second.amount;
      }

      for (const auto& zcTxio : zcTxIOs) {
         if (!zcTxio.second->hasTxIn()) {
            total += zcTxio.second->getAmount();
         }
      }
      return total;
   }

   Tx getTx(const Types::TxHash& txHash, std::shared_ptr<BlockDataManager> bdm)
   {
      auto db = bdm->getIFace();
      auto blockchainData = bdm->blockchainData();
      auto txKey = db->getDBKeyForHash(txHash);
      return blockchainData->getTx(txKey);
   }

   std::vector<UTXO> getUTXOsForScrAddrs(std::shared_ptr<BlockDataManager> bdm,
      const std::set<Types::ScrAddr>& addrSet)
   {
      std::vector<UTXO> result;
      for (const auto& addr : addrSet) {
         auto txOutData = getTxOutHistory(addr, bdm);
         auto txInData = bdm->getIFace()->getTxInHistoryForTxOutHistory(
            toKeyVector(txOutData));
         auto zcTxIOs = getZcHistory(addr, bdm);

         auto bc = bdm->blockchain();
         auto bcData = bdm->blockchainData();
         for (const auto& txOutPair : txOutData) {
            if (txOutPair.second.txId == 0) {
               continue;
            }
            auto header = bc->getHeaderById(txOutPair.second.blockID);
            if (!header->isMainBranch()) {
               continue;
            }

            auto txInIter = txInData.find(txOutPair.first);
            if (txInIter != txInData.end()) {
               auto txInBlockID = Types::getBlockIDFromTxKey(txInIter->second);
               header = bc->getHeaderById(txInBlockID);
               if (header->isMainBranch()) {
                  continue;
               }
            }

            auto zcIter = zcTxIOs.find(txOutPair.first);
            if (zcIter != zcTxIOs.end()) {
               continue;
            }

            auto txOutId = Types::getTxIOIndexFromTxIOKey(txOutPair.first);
            auto tx = bcData->getTx(txOutPair.first);
            auto txOut = tx.getTxOutCopy(txOutId);
            result.emplace_back(getUTXO(txOutPair.first, bdm));
         }
      }
      //std::sort(result.begin(), result.end());
      return result;
   }

   std::vector<UTXO> getRBFUTXOs(std::shared_ptr<BlockDataManager> bdm,
      const std::set<Types::ScrAddr>& addrSet)
   {
      std::vector<UTXO> result;
      for (const auto& addr : addrSet) {
         auto zcTxIOs = getZcHistory(addr, bdm);
         for (const auto& txio : zcTxIOs) {
            if (!txio.second->isRBF()) {
               continue;
            }
            result.emplace_back(getUTXO(txio.first, bdm));
         }
      }
      //std::sort(result.begin(), result.end());
      return result;
   }

   std::vector<UTXO> getZCUTXOs(std::shared_ptr<BlockDataManager> bdm,
      const std::set<Types::ScrAddr>& addrSet)
   {
      std::vector<UTXO> result;
      for (const auto& addr : addrSet) {
         auto zcTxIOs = getZcHistory(addr, bdm);
         for (const auto& txio : zcTxIOs) {
            if (txio.second->hasTxIn() || !txio.second->hasTxOutZC()) {
               continue;
            }
            result.emplace_back(getUTXO(txio.first, bdm));
         }
      }
      //std::sort(result.begin(), result.end());
      return result;
   }

   /////////////////////////////////////////////////////////////////////////////
   std::tuple<BinaryData, unsigned> waitOnSignal(
      Clients* clients, Types::BdvId bdvId, int signal)
   {
      auto processCallback = [signal](const BinaryData& packet)->int
      {
         kj::ArrayPtr<const capnp::word> words(
            reinterpret_cast<const capnp::word*>(packet.getPtr()),
            packet.getSize() / sizeof(capnp::word));
         capnp::FlatArrayMessageReader reader(words);
         auto notifs = reader.getRoot<Codec::BDV::Notifications>();
         auto capnNotifs = notifs.getNotifs();

         for (size_t i = 0; i < capnNotifs.size(); i++) {
            auto capnNotif = capnNotifs[i];
            if ((int)capnNotif.which() == signal) {
               return i;
            }
         }
         return -1;
      };

      auto bdv_obj = clients->get(bdvId);
      auto cbPtr = bdv_obj->notifications_.get();
      auto unittest_cbptr = dynamic_cast<UnitTest_Callback*>(cbPtr);
      if (unittest_cbptr == nullptr) {
         throw std::runtime_error("unexpected callback ptr type");
      }

      while (true) {
         auto rawNotif = unittest_cbptr->getNotification();
         auto index = processCallback(rawNotif);
         if (index > -1) {
            return std::make_tuple(rawNotif, index);
         }
      }
   }

   /////////////////////////////////////////////////////////////////////////////
   void waitOnBDMSignal(std::shared_ptr<BlockDataManager> bdm, BDV_Action action)
   {
      while (true) {
         try {
            auto notif = bdm->notificationStack.pop_front();
            if (notif == nullptr) {
               continue;
            } else if (notif->actionType() == action) {
               return;
            }
         } catch (...) {
            return;
         }
      }
   }

   /////////////////////////////////////////////////////////////////////////////
   void waitOnBDVReady(Clients* clients, Types::BdvId bdvId)
   {
      waitOnSignal(clients, bdvId, (int)Codec::BDV::Notification::READY);
   }

   /////////////////////////////////////////////////////////////////////////////
   void waitOnBDMError(std::shared_ptr<BlockDataManager> bdm)
   {
      waitOnBDMSignal(bdm, BDV_Action::BDV_Error);
   }

   /////////////////////////////////////////////////////////////////////////////
   std::tuple<BinaryData, unsigned> waitOnNewBlockSignal(
      Clients* clients, Types::BdvId bdvId)
   {
      return waitOnSignal(clients, bdvId, (int)Codec::BDV::Notification::NEW_BLOCK);
   }

   /////////////////////////////////////////////////////////////////////////////
   // UTCallback
   void UTCallback::run(BdmNotification bdmNotif)
   {
      auto notif = std::make_unique<BdmNotif>();
      notif->action = bdmNotif.action;
      notif->requestID = bdmNotif.requestID;

      if (bdmNotif.action == BDMAction_Refresh) {
         notif->idSet = bdmNotif.ids;
      } else if (bdmNotif.action == BDMAction_ZC) {
         notif->txios = std::move(bdmNotif.txios);
      } else if (bdmNotif.action == BDMAction_NewBlock) {
         if (bdmNotif.newBlock.isReorg()) {
            notif->reorgHeight = bdmNotif.newBlock.getBranchHeight();
         } else {
            notif->reorgHeight = UINT32_MAX;
         }
      } else if (bdmNotif.action == BDMAction_BDV_Error) {
         notif->error = bdmNotif.error;
      }
      actionStack_.push_back(std::move(notif));
   }

   void UTCallback::waitOnZc(
      std::shared_ptr<ZeroConf::ZeroConfContainer> zcPtr,
      const std::set<BinaryData>& hashes)
   {
      auto bdHashes = hashes;
      while (!bdHashes.empty()) {
         auto action = waitOnNotification(BDMAction_ZC);
         auto mempoolSnapshot = zcPtr->getSnapshot();
         for (const auto& txio : action->txios) {
            auto hashOut = mempoolSnapshot->getHashForKey(txio.getTxKeyOfOutput());
            auto iterOut = bdHashes.find(hashOut);
            if (iterOut != bdHashes.end()) {
               bdHashes.erase(iterOut);
            }

            if (!txio.hasTxIn()) {
               continue;
            }
            auto hashIn = mempoolSnapshot->getHashForKey(txio.getTxKeyOfInput());
            auto iterIn = bdHashes.find(hashIn);
            if (iterIn != bdHashes.end()) {
               bdHashes.erase(iterIn);
            }
         }
      }
   }

   void UTCallback::waitOnZc_OutOfOrder(
      std::shared_ptr<ZeroConf::ZeroConfContainer> zcPtr,
      const std::set<Types::TxHash>& hashes)
   {
      auto bdHashes = hashes;
      for (auto& pastNotif : zcNotifVec_) {
         auto mempoolSnapshot = zcPtr->getSnapshot();
         for (const auto& txio : pastNotif.txios) {
            auto hashOut = mempoolSnapshot->getHashForKey(
               txio.getTxKeyOfOutput());
            auto iterOut = bdHashes.find(hashOut);
            if (iterOut != bdHashes.end()) {
               bdHashes.erase(iterOut);
            }

            if (!txio.hasTxIn()) {
               continue;
            }
            auto hashIn = mempoolSnapshot->getHashForKey(
               txio.getTxKeyOfInput());
            auto iterIn = bdHashes.find(hashIn);
            if (iterIn != bdHashes.end()) {
               bdHashes.erase(iterIn);
            }
         }

         if (bdHashes.empty()) {
            return;
         }
      }

      while (!bdHashes.empty()) {
         auto action = waitOnNotification(BDMAction_ZC);
         zcNotifVec_.push_back(*action);
         auto mempoolSnapshot = zcPtr->getSnapshot();

         for (const auto& txio : action->txios) {
            auto hashOut = mempoolSnapshot->getHashForKey(
               txio.getTxKeyOfOutput());
            auto iterOut = bdHashes.find(hashOut);
            if (iterOut != bdHashes.end()) {
               bdHashes.erase(iterOut);
            }

            if (!txio.hasTxIn()) {
               continue;
            }
            auto hashIn = mempoolSnapshot->getHashForKey(
               txio.getTxKeyOfInput());
            auto iterIn = bdHashes.find(hashIn);
            if (iterIn != bdHashes.end()) {
               bdHashes.erase(iterIn);
            }
         }
      }
   }

   /////////////////////////////////////////////////////////////////////////////
   std::pair<std::vector<TxIOPair>, std::set<Types::TxHash>> waitOnNewZcSignal(
      Clients* clients, Types::BdvId bdvId)
   {
      auto result = waitOnSignal(clients, bdvId,
         (int)Codec::BDV::Notification::ZC);

      const auto& packet = get<0>(result);
      const auto& index = get<1>(result);

      kj::ArrayPtr<const capnp::word> words(
         reinterpret_cast<const capnp::word*>(packet.getPtr()),
         packet.getSize() / sizeof(capnp::word));
      capnp::FlatArrayMessageReader reader(words);
      auto notifs = reader.getRoot<Codec::BDV::Notifications>();
      auto notifList = notifs.getNotifs();

      auto zcNotif = notifList[index];
      if (zcNotif.which() != Codec::BDV::Notification::ZC) {
         std::cout << "invalid result vector size in waitOnNewZcSignal" << std::endl;
         throw std::runtime_error("");
      }

      auto capnZcs = zcNotif.getZc();

      std::pair<std::vector<TxIOPair>, std::set<Types::TxHash>> txioData;
      txioData.first = capnToTxios(capnZcs);

      if (notifList.size() >= index + 2) {
         auto invalidatedNotif = notifList[index + 1];
         auto invalidatedZCs = invalidatedNotif.getInvalidatedZc();
         for (auto zcHash : invalidatedZCs) {
            txioData.second.emplace(
               Types::TxHash{zcHash.begin(), zcHash.end()});
         }
      }
      return txioData;
   }

   /////////////////////////////////////////////////////////////////////////////
   void waitOnWalletRefresh(Clients* clients, Types::BdvId bdvId,
      const std::string& wltId)
   {
      while (true) {
         auto result = waitOnSignal(clients, bdvId,
            (int)Codec::BDV::Notification::REFRESH);
         if (wltId.empty()) {
            return;
         }

         auto rawNotifs = get<0>(result);
         kj::ArrayPtr<const capnp::word> words(
            reinterpret_cast<const capnp::word*>(rawNotifs.getPtr()),
            rawNotifs.getSize() / sizeof(capnp::word));
         capnp::FlatArrayMessageReader reader(words);
         auto notifs = reader.getRoot<Codec::BDV::Notifications>();
         auto notifList = notifs.getNotifs();
         auto notif = notifList[get<1>(result)];
         if (notif.which() != Codec::BDV::Notification::REFRESH) {
            std::cout << "invalid result vector size in waitOnWalletRefresh" << std::endl;
            throw std::runtime_error("");
         }

         auto refresh = notif.getRefresh();
         auto ids = refresh.getIds();
         for (auto id : ids) {
            if (id == wltId) {
               return;
            }
         }
      }
   }

   /////////////////////////////////////////////////////////////////////////////
   void triggerNewBlockNotification(BlockDataManagerThread* bdmt)
   {
      auto nodePtr = bdmt->bdm()->processNode;
      auto nodeUnitTest = (NodeUnitTest*)nodePtr.get();

      nodeUnitTest->notifyNewBlock();
   }

   /////////////////////////////////////////////////////////////////////////////
   void mineNewBlock(BlockDataManagerThread* bdmt, const BinaryData& h160,
      unsigned count)
   {
      auto nodePtr = bdmt->bdm()->processNode;
      auto nodeUnitTest = (NodeUnitTest*)nodePtr.get();
      nodeUnitTest->mineNewBlock(bdmt->bdm(), count, h160);
   }

   /////////////////////////////////////////////////////////////////////////////
   std::vector<UnitTestBlock> getMinedBlocks(BlockDataManagerThread* bdmt)
   {
      auto nodePtr = bdmt->bdm()->processNode;
      auto nodeUnitTest = (NodeUnitTest*)nodePtr.get();
      return nodeUnitTest->getMinedBlocks();
   }

   /////////////////////////////////////////////////////////////////////////////
   void setReorgBranchingPoint(
      BlockDataManagerThread* bdmt, const BinaryData& hash)
   {
      auto nodePtr = bdmt->bdm()->processNode;
      auto nodeUnitTest = (NodeUnitTest*)nodePtr.get();

      auto headerPtr = bdmt->bdm()->blockchain()->getHeaderByHash(hash);
      nodeUnitTest->setReorgBranchPoint(headerPtr);
   }

   /////////////////////////////////////////////////////////////////////////////
   void pushNewZc(BlockDataManagerThread* bdmt, const ZcVector& zcVec,
      bool stage)
   {
      auto nodePtr = bdmt->bdm()->processNode;
      auto nodeUnitTest = (NodeUnitTest*)nodePtr.get();

      unsigned delay = UINT32_MAX;
      if (!zcDelays_.empty()) {
         delay = zcDelays_.front();
         zcDelays_.pop_front();
      }

      std::vector<std::pair<BinaryData, unsigned>> txVec;
      for (auto& newzc : zcVec.zcVec_) {
         BinaryData bdTx{newzc.first.getPtr(), newzc.first.getSize()};
         auto localDelay = newzc.second;
         if (newzc.second == 0 && delay != UINT32_MAX) {
            localDelay = delay;
         }
         txVec.emplace_back(std::make_pair(bdTx, localDelay));
      }
      nodeUnitTest->pushZC(txVec, stage);
   }

   /////////////////////////////////////////////////////////////////////////////
   void setNextZcPushDelay(unsigned delay)
   {
      zcDelays_.push_back(delay);
   }

   /////////////////////////////////////////////////////////////////////////////
   std::pair<BinaryData, BinaryData> getAddrAndPubKeyFromPrivKey(
      BinaryData privKey, bool compressed)
   {
      auto pubkey = Cryptography::ECDSA::computePublicKey(privKey, compressed);
      auto h160 = BtcUtils::getHash160(pubkey);

      std::pair<BinaryData, BinaryData> result;
      result.second = pubkey;
      result.first = h160;

      return result;
   }

   /////////////////////////////////////////////////////////////////////////////
   Tx getTxByHash(Clients* clients, Types::BdvId bdvId,
      const Types::TxHash& txHash)
   {
      capnp::MallocMessageBuilder message;
      auto payload = message.initRoot<Codec::BDV::Request>();

      auto bdvRequest = payload.initBdv();
      auto hashReq = bdvRequest.initGetTxsByHash(1);
      hashReq.set(0, capnp::Data::Builder(
         (uint8_t*)txHash.getPtr(), txHash.getSize()));

      auto result = processCommand(clients, bdvId, serializeCapnp(message));
      kj::ArrayPtr<const capnp::word> words(
         reinterpret_cast<const capnp::word*>(result.getPtr()),
         result.getSize() / sizeof(capnp::word));
      capnp::FlatArrayMessageReader reader(words);
      auto reply = reader.getRoot<Codec::BDV::Reply>();
      auto bdvReply = reply.getBdv();
      auto capnTxs = bdvReply.getGetTxsByHash();
      auto capnTx = capnTxs[0];
      auto capnRaw = capnTx.getRaw();
      BinaryDataRef rawTx(capnRaw.begin(), capnRaw.end());

      Tx txobj(rawTx);
      txobj.setTxKey(capnTx.getKey());
      txobj.setChainedZC(capnTx.getIsChainedZc());
      txobj.setRBF(capnTx.getIsRbf());
      return txobj;
   }

   /////////////////////////////////////////////////////////////////////////////
   Tx getTxByKey(Clients* clients, Types::BdvId bdvId,
      const Types::TxKey& txKey)
   {
      capnp::MallocMessageBuilder message;
      auto payload = message.initRoot<Codec::BDV::Request>();

      auto bdvRequest = payload.initBdv();
      auto keyReq = bdvRequest.initGetTxsByKey(1);
      keyReq.set(0, txKey);

      auto result = processCommand(clients, bdvId, serializeCapnp(message));
      kj::ArrayPtr<const capnp::word> words(
         reinterpret_cast<const capnp::word*>(result.getPtr()),
         result.getSize() / sizeof(capnp::word));
      capnp::FlatArrayMessageReader reader(words);
      auto reply = reader.getRoot<Codec::BDV::Reply>();
      auto bdvReply = reply.getBdv();
      auto capnTxs = bdvReply.getGetTxsByKey();
      if (capnTxs.size() != 1) {
         throw std::runtime_error(std::format("no tx for key {:x}", txKey));
      }
      auto capnTx = capnTxs[0];
      auto capnRaw = capnTx.getRaw();
      BinaryDataRef rawTx(capnRaw.begin(), capnRaw.end());

      Tx txobj(rawTx);
      txobj.setTxKey(capnTx.getKey());
      txobj.setChainedZC(capnTx.getIsChainedZc());
      txobj.setRBF(capnTx.getIsRbf());
      return txobj;
   }

   /////////////////////////////////////////////////////////////////////////////
   void init()
   {
      /*
      Need a counter to increment the message id of the packets sent to the
      BDV object. This counter has to be reset when the BDM is reset. Since
      the counter is global to the namespace, this means this test interface
      cannot sustain multiple concurent BDVs.

      Use the websocket interface to have multiple clients instead.

      The counter has to start at 1 since the first message is always BDV
      registration, which does not occur when bypassing the websocet interface.
      */
      commandCtr_ = 1;
   }

   /////////////////////////////////////////////////////////////////////////////
   BinaryData processCommand(Clients* clients, Types::BdvId bdvId,
      BinaryData msg)
   {
      auto bdVec = Network::WebSocketMessageCodec::serialize(
         msg, nullptr,
         ArmoryAEAD::BIP151_PayloadType::FragmentHeader, commandCtr_++);

      if (bdVec.size() > 1) {
         LOGWARN << "large message in unit tests";
      }
      auto bdRef = bdVec[0].getSliceRef(
         LWS_PRE, bdVec[0].getSize() - LWS_PRE);

      auto payload = std::make_shared<BDV_Payload>(
         bdRef, clients->get(bdvId), bdvId, BinaryDataRef{});

      auto reply = clients->processCommand(payload);
      if (reply == nullptr) {
         return {};
      }
      std::vector<uint8_t> flat;
      reply->serialize(flat);
      return BinaryData(flat.data(), flat.size());
   }

   /////////////////////////////////////////////////////////////////////////////
   AsyncClient::TxResult getTxByHash(
      std::shared_ptr<AsyncClient::BlockDataViewer> bdv, const BinaryData& hash)
   {
      auto prom = std::make_shared<std::promise<AsyncClient::TxBatchResult>>();
      auto fut = prom->get_future();
      auto lbd = [prom](ReturnMessage<AsyncClient::TxBatchResult> msg)->void
      {
         prom->set_value(msg.get());
      };
      bdv->getTxsByHash({hash}, lbd);
      auto result = fut.get();
      return result.at(hash);
   }
}
