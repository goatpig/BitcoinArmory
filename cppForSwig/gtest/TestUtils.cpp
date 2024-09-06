////////////////////////////////////////////////////////////////////////////////
//                                                                            //
//                                                                            //
//  Copyright (C) 2016-2021, goatpig                                          //
//  Distributed under the MIT license                                         //
//  See LICENSE-MIT or https://opensource.org/licenses/MIT                    //
//                                                                            //
////////////////////////////////////////////////////////////////////////////////
#include "TestUtils.h"
#include "BIP15x_Handshake.h"

using namespace std;

using namespace Armory::Assets;
using namespace Armory::Wallets;
using namespace Armory;

#include <capnp/message.h>
#include <capnp/serialize.h>
#include "capnp/BDV.capnp.h"
#include "capnp/Types.capnp.h"

#if ! defined(_MSC_VER) && ! defined(__MINGW32__)

/////////////////////////////////////////////////////////////////////////////
void mkdir(string newdir)
{
   char* syscmd = new char[4096];
   sprintf(syscmd, "mkdir -p %s", newdir.c_str());
   system(syscmd);
   delete[] syscmd;
}

#endif

namespace {
   BinaryData serializeCapnp(capnp::MallocMessageBuilder& builder)
   {
      auto flat = capnp::messageToFlatArray(builder);
      auto bytes = flat.asBytes();
      return BinaryData(bytes.begin(), bytes.end());
   }

   DBClientClasses::HistoryPage capnToLedgers(
      capnp::List<Codec::Types::TxLedger::LedgerEntry, capnp::Kind::STRUCT>::Reader& ledgers)
   {
      DBClientClasses::HistoryPage result;
      result.reserve(ledgers.size());
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
      return result;
   }

   std::vector<DBClientClasses::HistoryPage> capnToHistoryPages(
      capnp::List<Codec::Types::TxLedger, capnp::Kind::STRUCT>::Reader& pages)
   {
      std::vector<DBClientClasses::HistoryPage> result;
      result.reserve(pages.size());
      for (auto page : pages) {
         auto ledgers = page.getLedgers();
         auto dbPage = capnToLedgers(ledgers);
         result.emplace_back(std::move(dbPage));
      }
      return result;
   }

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
}

////////////////////////////////////////////////////////////////////////////////
namespace TestUtils
{

   /////////////////////////////////////////////////////////////////////////////
   bool searchFile(const string& filename, BinaryData& data)
   {
      //create mmap of file
      auto filemap = DBUtils::getMmapOfFile(filename);

      if (data.getSize() < 8)
         throw runtime_error("only for buffers 8 bytes and larger");

      //search it
      uint64_t sample;
      uint64_t* data_head = (uint64_t*)data.getPtr();

      bool result = false;
      for (unsigned i = 0; i < filemap.size_ - data.getSize(); i++)
      {
         memcpy(&sample, filemap.filePtr_ + i, 8);
         if (sample == *data_head)
         {
            BinaryDataRef bdr(filemap.filePtr_ + i, data.getSize());
            if (bdr == data.getRef())
            {
               result = true;
               break;
            }
         }
      }

      //clean up
      filemap.unmap();

      //return
      return result;
   }

   /////////////////////////////////////////////////////////////////////////////
   uint32_t getTopBlockHeightInDB(BlockDataManager &bdm, DB_SELECT db)
   {
      StoredDBInfo sdbi;
      bdm.getIFace()->getStoredDBInfo(db, 0);
      return sdbi.topBlkHgt_;
   }

   /////////////////////////////////////////////////////////////////////////////
   uint64_t getDBBalanceForHash160(
      BlockDataManager &bdm,
      BinaryDataRef addr160
      )
   {
      StoredScriptHistory ssh;

      bdm.getIFace()->getStoredScriptHistory(ssh, HASH160PREFIX + addr160);
      if (!ssh.isInitialized())
         return 0;

      return ssh.getScriptBalance();
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
   void concatFile(const string &from, const string &to)
   {
      std::ifstream i(from, ios::binary);
      std::ofstream o(to, ios::app | ios::binary);

      o << i.rdbuf();
      o.flush();
   }

   /////////////////////////////////////////////////////////////////////////////
   void appendBlocks(const std::vector<std::string> &files, const std::string &to)
   {
      for (const std::string &f : files)
         concatFile(dataDir + "/blk_" + f + ".dat", to);
   }

   /////////////////////////////////////////////////////////////////////////////
   void setBlocks(const std::vector<std::string> &files, const std::string &to)
   {
      std::ofstream o(to, ios::trunc | ios::binary);
      o.close();

      for (const std::string &f : files)
         concatFile(dataDir + "/blk_" + f + ".dat", to);
   }

   /////////////////////////////////////////////////////////////////////////////
   void nullProgress(unsigned, double, unsigned, unsigned)
   {}

   /////////////////////////////////////////////////////////////////////////////
   BinaryData getTx(unsigned height, unsigned id)
   {
      stringstream ss;
      ss << dataDir << "/blk_" << height << ".dat";

      ifstream blkfile(ss.str(), ios::binary);
      blkfile.seekg(0, ios::end);
      auto size = blkfile.tellg();
      blkfile.seekg(0, ios::beg);

      vector<char> vec;
      vec.resize(size);
      blkfile.read(&vec[0], size);
      blkfile.close();

      BinaryRefReader brr((uint8_t*)&vec[0], size);
      StoredHeader sbh;
      sbh.unserializeFullBlock(brr, false, true);

      if (sbh.stxMap_.size() - 1 < id)
         throw range_error("invalid tx id");

      auto& stx = sbh.stxMap_[id];
      return stx.dataCopy_;
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
   deque<unsigned> zcDelays_;

   /////////////////////////////////////////////////////////////////////////////
   unsigned getTopBlockHeight(LMDBBlockDatabase* db, DB_SELECT dbSelect)
   {
      auto&& sdbi = db->getStoredDBInfo(dbSelect, 0);
      return sdbi.topBlkHgt_;
   }

   /////////////////////////////////////////////////////////////////////////////
   BinaryData getTopBlockHash(LMDBBlockDatabase* db, DB_SELECT dbSelect)
   {
      auto&& sdbi = db->getStoredDBInfo(dbSelect, 0);
      return sdbi.topScannedBlkHash_;
   }

   /////////////////////////////////////////////////////////////////////////////
   std::string registerBDV(Clients* clients, const BinaryData& magic_word)
   {
      std::string bdvID = "0102030405060708";
      if (!clients->registerBDV(magic_word.toHexStr(), bdvID)) {
         return {};
      }
      return bdvID;
   }

   /////////////////////////////////////////////////////////////////////////////
   void goOnline(Clients* clients, const string& id)
   {
      capnp::MallocMessageBuilder message(128);
      auto payload = message.getRoot<Codec::BDV::Request>();

      auto bdvRequest = payload.initBdv();
      bdvRequest.setBdvId(id);
      bdvRequest.setGoOnline();

      processCommand(clients, id, serializeCapnp(message));
   }

   /////////////////////////////////////////////////////////////////////////////
   const shared_ptr<BDV_Server_Object> getBDV(Clients* clients, const string& id)
   {
      return clients->get(id);
   }

   /////////////////////////////////////////////////////////////////////////////
   void registerWallet(Clients* clients, const string& bdvId,
      const vector<BinaryData>& scrAddrs, const string& wltName,
      bool isLockbox, bool waitOnReg)
   {
      capnp::MallocMessageBuilder message;
      auto payload = message.initRoot<Codec::BDV::Request>();

      auto bdvRequest = payload.initBdv();
      bdvRequest.setBdvId(bdvId);

      auto regReq = bdvRequest.initRegisterWallet();
      regReq.setWalletId(wltName);
      regReq.setIsNew(false);
      if (isLockbox) {
         regReq.setWalletType(Codec::BDV::BdvRequest::WalletType::LOCKBOX);
      } else {
         regReq.setWalletType(Codec::BDV::BdvRequest::WalletType::WALLET);
      }

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

      BinaryDataRef wltNameRef;
      wltNameRef.setRef(wltName);
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
            BinaryData idRef(refreshId.begin(), refreshId.end());
            if (idRef == wltNameRef) {
               return;
            }
         }
      }
   }

   /////////////////////////////////////////////////////////////////////////////
   void regLockbox(Clients* clients, const string& bdvId,
      const vector<BinaryData>& scrAddrs, const string& wltName, bool waitOnReg)
   {
      registerWallet(clients, bdvId, scrAddrs, wltName, true, waitOnReg);
   }

   /////////////////////////////////////////////////////////////////////////////
   vector<uint64_t> getBalanceAndCount(Clients* clients,
      const string& bdvId, const string& walletId, unsigned blockheight)
   {
      capnp::MallocMessageBuilder message;
      auto payload = message.initRoot<Codec::BDV::Request>();

      auto walletRequest = payload.initWallet();
      walletRequest.setBdvId(bdvId);
      walletRequest.setWalletId(walletId);
      walletRequest.setGetBalanceAndCount(blockheight);

      auto result = processCommand(clients, bdvId, serializeCapnp(message));

      //parse reply
      kj::ArrayPtr<const capnp::word> words(
         reinterpret_cast<const capnp::word*>(result.getPtr()),
         result.getSize() / sizeof(capnp::word));
      capnp::FlatArrayMessageReader reader(words);
      auto reply = reader.getRoot<Codec::BDV::Reply>();
      auto wltReply = reply.getWallet();
      auto balances = wltReply.getGetBalanceAndCount();

      return vector<uint64_t> {
         balances.getFull(),
         balances.getSpendable(),
         balances.getUnconfirmed(),
         balances.getTxnCount()
      };
   }

   /////////////////////////////////////////////////////////////////////////////
   string getLedgerDelegate(Clients* clients, const string& bdvId)
   {
      capnp::MallocMessageBuilder message;
      auto payload = message.initRoot<Codec::BDV::Request>();

      auto bdvRequest = payload.initBdv();
      bdvRequest.setBdvId(bdvId);
      bdvRequest.setGetLedgerDelegate();

      //parse reply
      auto result = processCommand(clients, bdvId, serializeCapnp(message));
      kj::ArrayPtr<const capnp::word> words(
         reinterpret_cast<const capnp::word*>(result.getPtr()),
         result.getSize() / sizeof(capnp::word));
      capnp::FlatArrayMessageReader reader(words);
      auto reply = reader.getRoot<Codec::BDV::Reply>();
      auto bdvReply = reply.getBdv();
      return bdvReply.getGetLedgerDelegate();
   }

   /////////////////////////////////////////////////////////////////////////////
   vector<DBClientClasses::LedgerEntry> getHistoryPage(
      Clients* clients, const std::string& bdvId,
      const string& delegateId, uint32_t pageId)
   {
      capnp::MallocMessageBuilder message;
      auto payload = message.initRoot<Codec::BDV::Request>();

      auto ledgerRequest = payload.initLedger();
      ledgerRequest.setLedgerId(delegateId);
      auto pageReq = ledgerRequest.initGetHistoryPages();
      pageReq.setFirst(pageId);


      auto result = processCommand(clients, bdvId, serializeCapnp(message));
      kj::ArrayPtr<const capnp::word> words(
         reinterpret_cast<const capnp::word*>(result.getPtr()),
         result.getSize() / sizeof(capnp::word));
      capnp::FlatArrayMessageReader reader(words);
      auto reply = reader.getRoot<Codec::BDV::Reply>();
      auto ledgerReply = reply.getLedger();
      auto pages = ledgerReply.getGetHistoryPages();

      //hard yolo return by index
      auto theResult = capnToHistoryPages(pages);
      return theResult[0];
   }

   /////////////////////////////////////////////////////////////////////////////
   std::tuple<BinaryData, unsigned> waitOnSignal(
      Clients* clients, const string& bdvId, int signal)
   {
      auto processCallback = [signal](const BinaryData& packet)->int
      {
         kj::ArrayPtr<const capnp::word> words(
            reinterpret_cast<const capnp::word*>(packet.getPtr()),
            packet.getSize() / sizeof(capnp::word));
         capnp::FlatArrayMessageReader reader(words);
         auto notifs = reader.getRoot<Codec::BDV::Notifications>();
         auto capnNotifs = notifs.getNotifs();

         for (int i = 0; i < capnNotifs.size(); i++) {
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
         throw runtime_error("unexpected callback ptr type");
      }

      while (true) {
         auto rawNotif = std::move(unittest_cbptr->getNotification());
         auto index = processCallback(rawNotif);
         if (index > -1) {
            return make_tuple(rawNotif, index);
         }
      }
   }

   /////////////////////////////////////////////////////////////////////////////
   void waitOnBDMReady(Clients* clients, const string& bdvId)
   {
      waitOnSignal(clients, bdvId, (int)Codec::BDV::Notification::READY);
   }

   /////////////////////////////////////////////////////////////////////////////
   std::tuple<BinaryData, unsigned> waitOnNewBlockSignal(
      Clients* clients, const string& bdvId)
   {
      return waitOnSignal(clients, bdvId, (int)Codec::BDV::Notification::NEW_BLOCK);
   }

   /////////////////////////////////////////////////////////////////////////////
   pair<vector<DBClientClasses::LedgerEntry>, set<BinaryData>> waitOnNewZcSignal(
      Clients* clients, const string& bdvId)
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
         cout << "invalid result vector size in waitOnNewZcSignal";
         throw runtime_error("");
      }

      auto capnPage = zcNotif.getZc();
      auto capnLedgers = capnPage.getLedgers();

      pair<vector<DBClientClasses::LedgerEntry>, set<BinaryData>> levData;
      levData.first = capnToLedgers(capnLedgers);

      if (notifList.size() >= (int)index + 2) {
         auto invalidatedNotif = notifList[index + 1];
         auto invalidatedZCs = invalidatedNotif.getInvalidatedZc();
         for (auto zcHash : invalidatedZCs) {
            BinaryDataRef hashRef(zcHash.begin(), zcHash.end());
            levData.second.insert(hashRef);
         }
      }
      return levData;
   }

   /////////////////////////////////////////////////////////////////////////////
   void waitOnWalletRefresh(Clients* clients, const string& bdvId,
      const BinaryData& wltId)
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
            cout << "invalid result vector size in waitOnWalletRefresh";
            throw runtime_error("");
         }

         auto refresh = notif.getRefresh();
         auto ids = refresh.getIds();
         for (auto id : ids) {
            BinaryDataRef idRef(id.begin(), id.end());
            if (idRef == wltId) {
               return;
            }
         }
      }
   }

   /////////////////////////////////////////////////////////////////////////////
   void triggerNewBlockNotification(BlockDataManagerThread* bdmt)
   {
      auto nodePtr = bdmt->bdm()->processNode_;
      auto nodeUnitTest = (NodeUnitTest*)nodePtr.get();

      nodeUnitTest->notifyNewBlock();
   }

   /////////////////////////////////////////////////////////////////////////////
   void mineNewBlock(BlockDataManagerThread* bdmt, const BinaryData& h160, 
      unsigned count)
   {
      auto nodePtr = bdmt->bdm()->processNode_;
      auto nodeUnitTest = (NodeUnitTest*)nodePtr.get();

      nodeUnitTest->mineNewBlock(bdmt->bdm(), count, h160);
   }

   /////////////////////////////////////////////////////////////////////////////
   std::vector<UnitTestBlock> getMinedBlocks(BlockDataManagerThread* bdmt)
   {
      auto nodePtr = bdmt->bdm()->processNode_;
      auto nodeUnitTest = (NodeUnitTest*)nodePtr.get();

      return nodeUnitTest->getMinedBlocks();
   }

   /////////////////////////////////////////////////////////////////////////////
   void setReorgBranchingPoint(
      BlockDataManagerThread* bdmt, const BinaryData& hash)
   {
      auto nodePtr = bdmt->bdm()->processNode_;
      auto nodeUnitTest = (NodeUnitTest*)nodePtr.get();

      auto headerPtr = bdmt->bdm()->blockchain()->getHeaderByHash(hash);
      nodeUnitTest->setReorgBranchPoint(headerPtr);
   }

   /////////////////////////////////////////////////////////////////////////////
   void pushNewZc(BlockDataManagerThread* bdmt, const ZcVector& zcVec, 
      bool stage)
   {
      auto nodePtr = bdmt->bdm()->processNode_;
      auto nodeUnitTest = (NodeUnitTest*)nodePtr.get();

      unsigned delay = UINT32_MAX;
      if (zcDelays_.size() != 0)
      {
         delay = zcDelays_.front();
         zcDelays_.pop_front();
      }

      std::vector<pair<BinaryData, unsigned>> txVec;
      for (auto& newzc : zcVec.zcVec_)
      {
         BinaryData bdTx(newzc.first.getPtr(), newzc.first.getSize());

         auto localDelay = newzc.second;
         if (newzc.second == 0 && delay != UINT32_MAX)
            localDelay = delay;

         txVec.push_back(make_pair(bdTx, localDelay));
      }

      nodeUnitTest->pushZC(txVec, stage);
   }

   /////////////////////////////////////////////////////////////////////////////
   void setNextZcPushDelay(unsigned delay)
   {
      zcDelays_.push_back(delay);
   }

   /////////////////////////////////////////////////////////////////////////////
   pair<BinaryData, BinaryData> getAddrAndPubKeyFromPrivKey(
      BinaryData privKey, bool compressed)
   {
      auto&& pubkey = CryptoECDSA().ComputePublicKey(privKey, compressed);
      auto&& h160 = BtcUtils::getHash160(pubkey);

      pair<BinaryData, BinaryData> result;
      result.second = pubkey;
      result.first = h160;

      return result;
   }

   /////////////////////////////////////////////////////////////////////////////
   Tx getTxByHash(Clients* clients, const string bdvId,
      const BinaryData& txHash)
   {
      capnp::MallocMessageBuilder message;
      auto payload = message.initRoot<Codec::BDV::Request>();

      auto bdvRequest = payload.initBdv();
      bdvRequest.setBdvId(bdvId);
      auto hashReq = bdvRequest.initGetTxByHash(1);
      hashReq.set(0, capnp::Data::Builder(
         (uint8_t*)txHash.getPtr(), txHash.getSize()));

      auto result = processCommand(clients, bdvId, serializeCapnp(message));
      kj::ArrayPtr<const capnp::word> words(
         reinterpret_cast<const capnp::word*>(result.getPtr()),
         result.getSize() / sizeof(capnp::word));
      capnp::FlatArrayMessageReader reader(words);
      auto reply = reader.getRoot<Codec::BDV::Reply>();
      auto bdvReply = reply.getBdv();
      auto capnTxs = bdvReply.getGetTxByHash();
      auto capnTx = capnTxs[0];
      auto body = capnTx.getBody();
      BinaryDataRef rawTx(body.begin(), body.end());

      Tx txobj(rawTx);
      txobj.setTxHeight(capnTx.getHeight());
      txobj.setTxIndex(capnTx.getIndex());
      txobj.setChainedZC(capnTx.getIsChainZc());
      txobj.setRBF(capnTx.getIsRbf());
      return txobj;
   }

   /////////////////////////////////////////////////////////////////////////////
   std::vector<UTXO> getUtxoForAddress(Clients* clients, const string bdvId,
      const BinaryData& scrAddr, bool withZc)
   {
      //create capnp request
      capnp::MallocMessageBuilder message;
      auto payload = message.initRoot<Codec::BDV::Request>();

      auto addrReq = payload.initAddress();
      addrReq.setBdvId(bdvId);
      auto capnAddr = addrReq.getAddress();
      capnAddr.setBody(capnp::Data::Builder(
         (uint8_t*)scrAddr.getPtr(), scrAddr.getSize()
      ));
      auto outputReq = addrReq.initGetOutputs();
      outputReq.setTargetValue(UINT64_MAX);
      outputReq.setZc(withZc);

      auto result = processCommand(clients, bdvId, serializeCapnp(message));
      kj::ArrayPtr<const capnp::word> words(
         reinterpret_cast<const capnp::word*>(result.getPtr()),
         result.getSize() / sizeof(capnp::word));
      capnp::FlatArrayMessageReader reader(words);
      auto reply = reader.getRoot<Codec::BDV::Reply>();
      auto bdvReply = reply.getAddress();
      auto utxoReply = bdvReply.getGetOutputs();
      return capnToUtxoVec(utxoReply);
   }

   /////////////////////////////////////////////////////////////////////////////
   void addTxioToSsh(StoredScriptHistory& ssh, 
      const map<BinaryDataRef, shared_ptr<const TxIOPair>>& txioMap)
   {
      for (auto& txio_pair : txioMap)
      {
         auto subssh_key = txio_pair.first.getSliceRef(0, 4);

         auto& subssh = ssh.subHistMap_[subssh_key];
         subssh.txioMap_[txio_pair.first] = *txio_pair.second;

         unsigned txioCount = 1;
         if (txio_pair.second->hasTxIn())
         {
            ssh.totalUnspent_ -= txio_pair.second->getValue();

            auto txinKey_prefix = 
               txio_pair.second->getDBKeyOfInput().getSliceCopy(0, 4);
            if (txio_pair.second->getDBKeyOfOutput().startsWith(txinKey_prefix))
            {
               ssh.totalUnspent_ += txio_pair.second->getValue();
               ++txioCount;
            }
         }
         else
         {
            ssh.totalUnspent_ += txio_pair.second->getValue();
         }

         ssh.totalTxioCount_ += txioCount;
      }
   }

   /////////////////////////////////////////////////////////////////////////////
   void prettyPrintSsh(StoredScriptHistory& ssh)
   {
      cout << "balance: " << ssh.totalUnspent_ << endl;
      cout << "txioCount: " << ssh.totalTxioCount_ << endl;

      for(auto& subssh : ssh.subHistMap_)
      {
         cout << "key: " << subssh.first.toHexStr() << ", txCount:" << 
            subssh.second.txioCount_ << endl;
        
         for(auto& txio : subssh.second.txioMap_)
         {
            cout << "   amount: " << txio.second.getValue();
            cout << "   keys: " << txio.second.getDBKeyOfOutput().toHexStr();
            if (txio.second.hasTxIn())
            {
               cout << " to " << txio.second.getDBKeyOfInput().toHexStr();
            }

            cout << ", isUTXO: " << txio.second.isUTXO();
            cout << endl;
         }
      }
   }

   /////////////////////////////////////////////////////////////////////////////
   LedgerEntry getLedgerEntryFromWallet(
      shared_ptr<BtcWallet> wlt, const BinaryData& txHash)
   {
      //get ledgermap from wallet
      auto ledgerMap = wlt->getHistoryPage(0);

      //grab ledger by hash
      for (auto& ledger : *ledgerMap)
      {
         if (ledger.second.getTxHash() == txHash)
            return ledger.second;
      }

      return LedgerEntry();
   }

   /////////////////////////////////////////////////////////////////////////////
   LedgerEntry getLedgerEntryFromAddr(
      ScrAddrObj* scrAddrObj, const BinaryData& txHash)
   {
      //get ledgermap from wallet
      auto&& ledgerMap = scrAddrObj->getHistoryPageById(0);

      //grab ledger by hash
      for (auto& ledger : ledgerMap)
      {
         if (ledger.getTxHash() == txHash)
            return ledger;
      }

      return LedgerEntry();
   }

   /////////////////////////////////////////////////////////////////////////////
   void updateWalletsLedgerFilter(
      Clients* clients, const string& bdvId, const vector<string>& idVec)
   {
      capnp::MallocMessageBuilder message;
      auto payload = message.initRoot<Codec::BDV::Request>();

      auto bdvRequest = payload.initBdv();
      bdvRequest.setBdvId(bdvId);
      auto walletIds = bdvRequest.initUpdateWalletsLedgerFilter(idVec.size());
      for (unsigned i=0; i<idVec.size(); i++) {
         walletIds.set(i, idVec[i]);
      }
      processCommand(clients, bdvId, serializeCapnp(message));
   }

   /////////////////////////////////////////////////////////////////////////////
   void init(void)
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
   BinaryData processCommand(Clients* clients, const std::string& bdvId,
      BinaryData msg)
   {
      auto bdVec = WebSocketMessageCodec::serialize(
         msg, nullptr,
         ArmoryAEAD::BIP151_PayloadType::FragmentHeader, commandCtr_++);

      if (bdVec.size() > 1) {
         LOGWARN << "large message in unit tests";
      }

      auto payload = make_shared<BDV_Payload>();
      payload->messageID_ = 0;
      payload->bdvID_ = 0;

      auto bdRef = bdVec[0].getSliceRef(
         LWS_PRE, bdVec[0].getSize() - LWS_PRE);
      payload->packetData_ = bdRef;

      BinaryData zero;
      zero.resize(8);
      memset(zero.getPtr(), 0, 8);
      payload->bdvPtr_ = clients->get(bdvId);

      auto reply = clients->processCommand(payload);
      if (reply == nullptr) {
         return {};
      }
      std::vector<uint8_t> flat;
      reply->serialize(flat);
      return BinaryData(flat.data(), flat.size());
   }

   /////////////////////////////////////////////////////////////////////////////
   AsyncClient::LedgerDelegate getLedgerDelegate(
      shared_ptr<AsyncClient::BlockDataViewer> bdv)
   {
      auto prom = make_shared<promise<AsyncClient::LedgerDelegate>>();
      auto fut = prom->get_future();
      auto getDelegate = [prom]
         (ReturnMessage<AsyncClient::LedgerDelegate> msg)->void
      {
         prom->set_value(msg.get());
      };
      bdv->getLedgerDelegate(getDelegate);
      return fut.get();
   }

   /////////////////////////////////////////////////////////////////////////////
   AsyncClient::LedgerDelegate getLedgerDelegateForScrAddr(
      shared_ptr<AsyncClient::BlockDataViewer> bdv,
      const string& walletId, const BinaryData& scrAddr)
   {
      auto prom = make_shared<promise<AsyncClient::LedgerDelegate>>();
      auto fut = prom->get_future();
      auto getDelegate = [prom]
         (ReturnMessage<AsyncClient::LedgerDelegate> msg)->void
      {
         prom->set_value(msg.get());
      };
      auto walletObj = bdv->getWalletObj(walletId);
      auto addrObj = walletObj.getScrAddrObj(scrAddr, 0, 0, 0, 0);
      addrObj.getLedgerDelegate(getDelegate);
      return fut.get();
   }

   /////////////////////////////////////////////////////////////////////////////
   vector<DBClientClasses::LedgerEntry> getHistoryPage(
      AsyncClient::LedgerDelegate& del, uint32_t id)
   {
      auto prom = make_shared<promise<vector<DBClientClasses::HistoryPage>>>();
      auto fut = prom->get_future();
      auto lbd = [prom](ReturnMessage<vector<DBClientClasses::HistoryPage>> msg)
      {
         prom->set_value(msg.get());
      };
      del.getHistoryPages(id, 0, lbd);
      auto result = fut.get();
      return result[0];
   }

   /////////////////////////////////////////////////////////////////////////////
   uint64_t getPageCount(AsyncClient::LedgerDelegate& del)
   {
      auto prom = make_shared<promise<uint64_t>>();
      auto fut = prom->get_future();
      auto lbd = [prom](ReturnMessage<uint64_t> msg)
      {
         prom->set_value(msg.get());
      };
      del.getPageCount(lbd);

      return fut.get();
   }

   /////////////////////////////////////////////////////////////////////////////
   map<BinaryData, vector<uint64_t>> getAddrBalancesFromDB(
      shared_ptr<AsyncClient::BlockDataViewer> bdv, const std::string& wltId)
   {
      auto prom = make_shared<promise<map<std::string, AsyncClient::CombinedBalances>>>();
      auto fut = prom->get_future();
      auto lbd = [prom](ReturnMessage<map<std::string, AsyncClient::CombinedBalances>> msg)
      {
         prom->set_value(msg.get());
      };

      bdv->getCombinedBalances(lbd);
      auto combinedBalances = fut.get();

      auto wltData = combinedBalances.at(wltId);
      return wltData.addressBalances;
   }

   /////////////////////////////////////////////////////////////////////////////
   vector<uint64_t> getBalancesAndCount(AsyncClient::BtcWallet& wlt,
      uint32_t blockheight)
   {
      auto prom = make_shared<promise<vector<uint64_t>>>();
      auto fut = prom->get_future();
      auto lbd = [prom](ReturnMessage<vector<uint64_t>> msg)
      {
         prom->set_value(msg.get());
      };

      wlt.getBalancesAndCount(blockheight, lbd);
      return fut.get();
   }

   /////////////////////////////////////////////////////////////////////////////
   AsyncClient::TxResult getTxByHash(
      shared_ptr<AsyncClient::BlockDataViewer> bdv, const BinaryData& hash)
   {
      auto prom = make_shared<promise<AsyncClient::TxBatchResult>>();
      auto fut = prom->get_future();
      auto lbd = [prom](ReturnMessage<AsyncClient::TxBatchResult> msg)->void
      {
         prom->set_value(msg.get());
      };
      bdv->getTxsByHash({hash}, lbd);
      auto result = fut.get();
      return result.at(hash);
   }

   /////////////////////////////////////////////////////////////////////////////
   vector<UTXO> getSpendableTxOutListForValue(
      AsyncClient::BtcWallet& wlt, uint64_t value)
   {
      auto prom = make_shared<promise<vector<UTXO>>>();
      auto fut = prom->get_future();
      auto lbd = [prom](ReturnMessage<vector<UTXO>> msg)->void
      {
         prom->set_value(msg.get());
      };
      wlt.getUTXOs(value, false, false, lbd);

      return fut.get();
   }

   /////////////////////////////////////////////////////////////////////////////
   vector<UTXO> getSpendableZCList(AsyncClient::BtcWallet& wlt)
   {
      auto prom = make_shared<promise<vector<UTXO>>>();
      auto fut = prom->get_future();
      auto lbd = [prom](ReturnMessage<vector<UTXO>> msg)->void
      {
         prom->set_value(msg.get());
      };
      wlt.getUTXOs(0, true, false, lbd);

      return fut.get();
   }
}
