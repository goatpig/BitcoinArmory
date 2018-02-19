////////////////////////////////////////////////////////////////////////////////
//                                                                            //
//                                                                            //
//  Copyright (C) 2016-17, goatpig                                            //            
//  Distributed under the MIT license                                         //
//  See LICENSE-MIT or https://opensource.org/licenses/MIT                    //                                   
//                                                                            //
////////////////////////////////////////////////////////////////////////////////
#include "TestUtils.h"

#if ! defined(_MSC_VER) && ! defined(__MINGW32__)
   /////////////////////////////////////////////////////////////////////////////
   void rmdir(string src)
   {
      char* syscmd = new char[4096];
      sprintf(syscmd, "rm -rf %s", src.c_str());
      system(syscmd);
      delete[] syscmd;
   }

   /////////////////////////////////////////////////////////////////////////////
   void mkdir(string newdir)
   {
      char* syscmd = new char[4096];
      sprintf(syscmd, "mkdir -p %s", newdir.c_str());
      system(syscmd);
      delete[] syscmd;
   }
#endif

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
      uint64_t* sample;
      uint64_t* data_head = (uint64_t*)data.getPtr();

      bool result = false;
      for (unsigned i = 0; i < filemap.size_ - data.getSize(); i++)
      {
         sample = (uint64_t*)(filemap.filePtr_ + i);
         if (*sample == *data_head)
         {
            BinaryDataRef bdr(filemap.filePtr_ + i, data.getSize());
            if (bdr == data)
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
   }

   /////////////////////////////////////////////////////////////////////////////
   void appendBlocks(const std::vector<std::string> &files, const std::string &to)
   {
      for (const std::string &f : files)
         concatFile("../reorgTest/blk_" + f + ".dat", to);
   }

   /////////////////////////////////////////////////////////////////////////////
   void setBlocks(const std::vector<std::string> &files, const std::string &to)
   {
      std::ofstream o(to, ios::trunc | ios::binary);
      o.close();

      for (const std::string &f : files)
         concatFile("../reorgTest/blk_" + f + ".dat", to);
   }

   /////////////////////////////////////////////////////////////////////////////
   void nullProgress(unsigned, double, unsigned, unsigned)
   {}

   /////////////////////////////////////////////////////////////////////////////
   BinaryData getTx(unsigned height, unsigned id)
   {
      stringstream ss;
      ss << "../reorgTest/blk_" << height << ".dat";

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
}

////////////////////////////////////////////////////////////////////////////////
namespace DBTestUtils
{
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
   string registerBDV(Clients* clients, const BinaryData& magic_word)
   {
      Command cmd;
      cmd.method_ = "registerBDV";
      BinaryDataObject bdo(magic_word);
      cmd.args_.push_back(move(bdo));
      cmd.serialize();

      auto&& result = clients->runCommand(cmd.command_);

      auto& argVec = result.getArgVector();
      auto bdvId = dynamic_pointer_cast<DataObject<BinaryDataObject>>(argVec[0]);
      return bdvId->getObj().toStr();
   }

   /////////////////////////////////////////////////////////////////////////////
   void goOnline(Clients* clients, const string& id)
   {
      Command cmd;
      cmd.method_ = "goOnline";
      cmd.ids_.push_back(id);
      cmd.serialize();
      clients->runCommand(cmd.command_);
   }

   /////////////////////////////////////////////////////////////////////////////
   const shared_ptr<BDV_Server_Object> getBDV(Clients* clients, const string& id)
   {
      return clients->get(id);
   }

   /////////////////////////////////////////////////////////////////////////////
   void regWallet(Clients* clients, const string& bdvId,
      const vector<BinaryData>& scrAddrs, const string& wltName)
   {
      Command cmd;

      BinaryDataObject bdo(wltName);
      cmd.args_.push_back(move(bdo));
      cmd.args_.push_back(move(BinaryDataVector(scrAddrs)));
      cmd.args_.push_back(move(IntType(false)));

      cmd.method_ = "registerWallet";
      cmd.ids_.push_back(bdvId);
      cmd.serialize();

      auto&& result = clients->runCommand(cmd.command_);

      //check result
      auto& argVec = result.getArgVector();
      auto retint = dynamic_pointer_cast<DataObject<IntType>>(argVec[0]);
      if (retint->getObj().getVal() == 0)
      {
         BinaryData wltId = (wltName);
         waitOnWalletRefresh(clients, bdvId, wltId);
      }
   }

   /////////////////////////////////////////////////////////////////////////////
   vector<uint64_t> getBalanceAndCount(Clients* clients,
      const string& bdvId, const string& walletId, unsigned blockheight)
   {
      Command cmd;
      cmd.method_ = "getBalancesAndCount";
      cmd.ids_.push_back(bdvId);
      cmd.ids_.push_back(walletId);

      cmd.args_.push_back(move(IntType(blockheight)));

      cmd.serialize();

      auto&& result = clients->runCommand(cmd.command_);
      auto& argVec = result.getArgVector();

      auto&& balance_full =
         dynamic_pointer_cast<DataObject<IntType>>(argVec[0])->getObj().getVal();
      auto&& balance_spen =
         dynamic_pointer_cast<DataObject<IntType>>(argVec[1])->getObj().getVal();
      auto&& balance_unco =
         dynamic_pointer_cast<DataObject<IntType>>(argVec[2])->getObj().getVal();
      auto&& count =
         dynamic_pointer_cast<DataObject<IntType>>(argVec[3])->getObj().getVal();

      vector<uint64_t> balanceVec;
      balanceVec.push_back(balance_full);
      balanceVec.push_back(balance_spen);
      balanceVec.push_back(balance_unco);
      balanceVec.push_back(count);

      return balanceVec;
   }

   /////////////////////////////////////////////////////////////////////////////
   void regLockbox(Clients* clients, const string& bdvId,
      const vector<BinaryData>& scrAddrs, const string& wltName)
   {
      Command cmd;

      BinaryDataObject bdo(wltName);
      cmd.args_.push_back(move(bdo));
      cmd.args_.push_back(move(BinaryDataVector(scrAddrs)));
      cmd.args_.push_back(move(IntType(false)));

      cmd.method_ = "registerLockbox";
      cmd.ids_.push_back(bdvId);
      cmd.serialize();

      auto&& result = clients->runCommand(cmd.command_);

      //check result
      auto& argVec = result.getArgVector();
      auto retint = dynamic_pointer_cast<DataObject<IntType>>(argVec[0]);
      if (retint->getObj().getVal() == 0)
      {
         BinaryData wltId = (wltName);
         waitOnWalletRefresh(clients, bdvId, wltId);
      }
   }

   /////////////////////////////////////////////////////////////////////////////
   string getLedgerDelegate(Clients* clients, const string& bdvId)
   {
      Command cmd;

      cmd.method_ = "getLedgerDelegateForWallets";
      cmd.ids_.push_back(bdvId);
      cmd.serialize();

      auto&& result = clients->runCommand(cmd.command_);

      //check result
      auto& argVec = result.getArgVector();
      auto delegateid = dynamic_pointer_cast<DataObject<BinaryDataObject>>(argVec[0]);
      return delegateid->getObj().toStr();
   }

   /////////////////////////////////////////////////////////////////////////////
   vector<LedgerEntryData> getHistoryPage(Clients* clients, const string& bdvId,
      const string& delegateId, uint32_t pageId)
   {
      Command cmd;
      cmd.method_ = "getHistoryPage";
      cmd.ids_.push_back(bdvId);
      cmd.ids_.push_back(delegateId);

      cmd.args_.push_back(move(IntType(pageId)));

      cmd.serialize();

      auto&& result = clients->runCommand(cmd.command_);
      auto& argVec = result.getArgVector();

      auto lev = dynamic_pointer_cast<DataObject<LedgerEntryVector>>(argVec[0]);

      auto levData = lev->getObj().toVector();
      return levData;
   }

   /////////////////////////////////////////////////////////////////////////////
   vector<shared_ptr<DataMeta>> waitOnSignal(
      Clients* clients, const string& bdvId,
      string command, const string& signal)
   {
      Command cmd;
      cmd.method_ = "registerCallback";
      cmd.ids_.push_back(bdvId);

      BinaryDataObject bdo(command);
      cmd.args_.push_back(move(bdo));
      cmd.serialize();

      vector<shared_ptr<DataMeta>> resultVec;

      auto processCallback = [&](Arguments args)->bool
      {
         auto& argVec = args.getArgVector();

         for (auto arg : argVec)
         {
            auto argstr = dynamic_pointer_cast<DataObject<BinaryDataObject>>(arg);
            if (argstr == nullptr)
               continue;

            auto&& cb = argstr->getObj().toStr();
            if (cb == signal)
            {
               resultVec = argVec;
               return true;
            }
         }

         return false;
      };

      while (1)
      {
         auto&& result = clients->runCommand(cmd.command_);

         if (processCallback(move(result)))
            return resultVec;
      }
   }

   /////////////////////////////////////////////////////////////////////////////
   void waitOnBDMReady(Clients* clients, const string& bdvId)
   {
      waitOnSignal(clients, bdvId, "waitOnBDV", "BDM_Ready");
   }

   /////////////////////////////////////////////////////////////////////////////
   void waitOnNewBlockSignal(Clients* clients, const string& bdvId)
   {
      waitOnSignal(clients, bdvId, "getStatus", "NewBlock");
   }

   /////////////////////////////////////////////////////////////////////////////
   vector<LedgerEntryData> waitOnNewZcSignal(Clients* clients, const string& bdvId)
   {
      auto&& result = waitOnSignal(clients, bdvId, "getStatus", "BDV_ZC");

      if (result.size() != 2)
      {
         cout << "invalid result vector size in waitOnNewZcSignal";
         throw runtime_error("");
      }

      auto arg_bdov = 
         dynamic_pointer_cast<DataObject<LedgerEntryVector>>(result[1]);
      if (arg_bdov == nullptr)
      {
         cout << "invalid result entry type in waitOnNewBlockSignal";
         throw runtime_error("");
      }

      auto&& levec = arg_bdov->getObj();
      return levec.toVector();
   }

   /////////////////////////////////////////////////////////////////////////////
   void waitOnWalletRefresh(Clients* clients, const string& bdvId,
      const BinaryData& wltId)
   {
      while (1)
      {
         auto&& result = waitOnSignal(clients, bdvId, "getStatus", "BDV_Refresh");

         if (wltId.getSize() == 0)
            return;

         if (result.size() != 3)
         {
            cout << "invalid result vector size in waitOnWalletRefresh";
            throw runtime_error("");
         }

         auto argstr = dynamic_pointer_cast<DataObject<BinaryDataVector>>(result[2]);
         if (argstr == nullptr)
         {
            cout << "invalid result entry type in waitOnWalletRefresh";
            throw runtime_error("");
         }

         if (argstr->getObj().get()[0] == wltId)
            break;
      }
   }

   /////////////////////////////////////////////////////////////////////////////
   void triggerNewBlockNotification(BlockDataManagerThread* bdmt)
   {
      auto nodePtr = bdmt->bdm()->networkNode_;
      auto nodeUnitTest = (NodeUnitTest*)nodePtr.get();

      nodeUnitTest->mockNewBlock();
   }

   /////////////////////////////////////////////////////////////////////////////
   void pushNewZc(BlockDataManagerThread* bdmt, const ZcVector& zcVec)
   {
      auto zcConf = bdmt->bdm()->zeroConfCont_;

      ZeroConfContainer::ZcActionStruct newzcstruct;
      newzcstruct.action_ = Zc_NewTx;

      map<BinaryData, ParsedTx> newzcmap;

      for (auto& newzc : zcVec.zcVec_)
      {
         auto&& zckey = zcConf->getNewZCkey();
         newzcmap[zckey].tx_ = newzc;
      }

      newzcstruct.batch_ = make_shared<ZeroConfBatch>();
      newzcstruct.batch_->txMap_ = move(newzcmap);
      newzcstruct.batch_->isReadyPromise_.set_value(true);
      zcConf->newZcStack_.push_back(move(newzcstruct));
   }

   /////////////////////////////////////////////////////////////////////////////
   pair<BinaryData, BinaryData> getAddrAndPubKeyFromPrivKey(BinaryData privKey)
   {
      auto&& pubkey = CryptoECDSA().ComputePublicKey(privKey);
      auto&& h160 = BtcUtils::getHash160(pubkey);

      pair<BinaryData, BinaryData> result;
      result.second = pubkey;
      result.first = h160;

      return result;
   }

   /////////////////////////////////////////////////////////////////////////////
   BinaryData getTxByHash(Clients* clients, const string bdvId,
      const BinaryData& txHash)
   {
      Command cmd;

      BinaryDataObject bdo(txHash);
      cmd.args_.push_back(move(bdo));

      cmd.method_ = "getTxByHash";
      cmd.ids_.push_back(bdvId);
      cmd.serialize();

      auto&& result = clients->runCommand(cmd.command_);

      //check result
      auto& argVec = result.getArgVector();
      auto tx = dynamic_pointer_cast<DataObject<BinaryDataObject>>(argVec[0]);

      return tx->getObj().get();
   }

   /////////////////////////////////////////////////////////////////////////////
   Tx getTxObjByHash(
      Clients* clients, const string& bdvId, const BinaryData& txHash)
   {
      Command cmd;
      cmd.method_ = "getTxByHash";
      cmd.ids_.push_back(bdvId);


      BinaryDataObject hash(txHash);
      cmd.args_.push_back(move(hash));

      cmd.serialize();

      auto&& result = clients->runCommand(cmd.command_);
      auto& argVec = result.getArgVector();

      Tx tx;
      auto tx_bdo =
         dynamic_pointer_cast<DataObject<BinaryDataObject>>(argVec[0]);
      tx.unserializeWithMetaData(tx_bdo->getObj().get());

      return tx;
   }

   /////////////////////////////////////////////////////////////////////////////
   void addTxioToSsh(
      StoredScriptHistory& ssh, 
      const map<BinaryData, TxIOPair>& txioMap)
   {
      for (auto& txio_pair : txioMap)
      {
         auto subssh_key = txio_pair.first.getSliceRef(0, 4);

         auto& subssh = ssh.subHistMap_[subssh_key];
         subssh.txioMap_[txio_pair.first] = txio_pair.second;

         unsigned txioCount = 1;
         if (txio_pair.second.hasTxIn())
         {
            ssh.totalUnspent_ -= txio_pair.second.getValue();

            auto txinKey_prefix = 
               txio_pair.second.getDBKeyOfInput().getSliceCopy(0, 4);
            if (txio_pair.second.getDBKeyOfOutput().startsWith(txinKey_prefix))
            {
               ssh.totalUnspent_ += txio_pair.second.getValue();
               ++txioCount;
            }
         }
         else
         {
            ssh.totalUnspent_ += txio_pair.second.getValue();
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
      Clients* clients, const string& bdvId, const vector<BinaryData>& idVec)
   {
      Command cmd;

      cmd.method_ = "updateWalletsLedgerFilter";
      cmd.ids_.push_back(bdvId);

      BinaryDataVector bdVec;
      for (auto id : idVec)
         bdVec.push_back(move(id));

      cmd.args_.push_back(move(bdVec));
      cmd.serialize();

      clients->runCommand(cmd.command_);
   }
}
