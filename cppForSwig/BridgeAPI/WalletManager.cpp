////////////////////////////////////////////////////////////////////////////////
//                                                                            //
//  Copyright (C) 2016-2023, goatpig                                          //
//  Distributed under the MIT license                                         //
//  See LICENSE-MIT or https://opensource.org/licenses/MIT                    //
//                                                                            //
////////////////////////////////////////////////////////////////////////////////

#include "WalletManager.h"
#include "Wallets/Seeds/Backups.h"
#include "PassphrasePrompt.h"
#include "../Wallets/Seeds/Seeds.h"

#ifdef _WIN32
#include "leveldb_windows_port\win32_posix\dirent_win32.h"
#else
#include "dirent.h"
#endif

using namespace std;
using namespace Armory::Signer;
using namespace Armory::Accounts;
using namespace Armory::Wallets;
using namespace Armory::Seeds;

#define WALLET_135_HEADER "\xbaWALLET\x00"
#define PYBTC_ADDRESS_SIZE 237

////////////////////////////////////////////////////////////////////////////////
////
//// WalletManager
////
////////////////////////////////////////////////////////////////////////////////
map<string, set<AddressAccountId>> WalletManager::getAccountIdMap() const
{
   map<string, set<AddressAccountId>> result;
   for (const auto& wltIt : wallets_)
   {
      auto wltIter = result.emplace(wltIt.first, set<AddressAccountId>());
      for (const auto& accIt : wltIt.second)
         wltIter.first->second.emplace(accIt.first);
   }

   return result;
}

////////////////////////////////////////////////////////////////////////////////
shared_ptr<WalletContainer> WalletManager::getWalletContainer(
   const string& wltId) const
{
   auto iter = wallets_.find(wltId);
   if (iter == wallets_.end())
      throw runtime_error("[WalletManager::getWalletContainer]");

   return iter->second.begin()->second;
}

////////////////////////////////////////////////////////////////////////////////
shared_ptr<WalletContainer> WalletManager::getWalletContainer(
   const string& wltId, const AddressAccountId& accId) const
{
   auto wltIter = wallets_.find(wltId);
   if (wltIter == wallets_.end())
      throw runtime_error("[WalletManager::getWalletContainer]");

   auto accIter = wltIter->second.find(accId);
   if (accIter == wltIter->second.end())
      throw runtime_error("[WalletManager::getWalletContainer]");

   return accIter->second;
}

////////////////////////////////////////////////////////////////////////////////
void WalletManager::setBdvPtr(shared_ptr<AsyncClient::BlockDataViewer> bdvPtr)
{
   bdvPtr_ = bdvPtr;
   for (auto& wltIt : wallets_)
   {
      for (auto& accIt : wltIt.second)
         accIt.second->setBdvPtr(bdvPtr);
   }
}

////////////////////////////////////////////////////////////////////////////////
set<string> WalletManager::registerWallets()
{
   set<string> idSet;
   for (auto& wltIt : wallets_)
   {
      for (auto& accIt : wltIt.second)
         idSet.insert(accIt.second->registerWithBDV(false));
   }

   return idSet;
}

////////////////////////////////////////////////////////////////////////////////
string WalletManager::registerWallet(const string& wltId,
   const AddressAccountId& accId, bool isNew)
{
   auto wltIter = wallets_.find(wltId);
   if (wltIter == wallets_.end())
      throw runtime_error("[WalletManager::registerWallet]");

   auto accIter = wltIter->second.find(accId);
   if (accIter == wltIter->second.end())
      throw runtime_error("[WalletManager::registerWallet]");

   return accIter->second->registerWithBDV(isNew);
}

////////////////////////////////////////////////////////////////////////////////
shared_ptr<WalletContainer> WalletManager::addWallet(
   shared_ptr<AssetWallet> wltPtr, const AddressAccountId& accId)
{
   ReentrantLock lock(this);

   //check we dont have this wallet
   auto wltIter = wallets_.find(wltPtr->getID());
   if (wltIter == wallets_.end())
   {
      wltIter = wallets_.emplace(wltPtr->getID(),
         map<AddressAccountId, shared_ptr<WalletContainer>>()).first;
   }

   auto accIter = wltIter->second.find(accId);
   if (accIter != wltIter->second.end())
      return accIter->second;

   //create wrapper object
   auto wltContPtr = new WalletContainer(wltPtr->getID(), accId);
   shared_ptr<WalletContainer> wltCont;
   wltCont.reset(wltContPtr);

   //set bdvPtr if we have it
   if (bdvPtr_ != nullptr)
      wltCont->setBdvPtr(bdvPtr_);

   //set & add to map
   wltCont->setWalletPtr(wltPtr, accId);
   wltIter->second.emplace(accId, wltCont);

   //return it
   return wltCont;
}

////////////////////////////////////////////////////////////////////////////////
shared_ptr<WalletContainer> WalletManager::createNewWallet(
   const SecureBinaryData& pass, const SecureBinaryData& controlPass,
   const SecureBinaryData& extraEntropy, unsigned lookup)
{
   auto root = CryptoPRNG::generateRandom(32);
   if (extraEntropy.getSize() >= 32)
      root.XOR(extraEntropy);

   unique_ptr<ClearTextSeed> seed(new ClearTextSeed_Armory135(root,
      ClearTextSeed_Armory135::LegacyType::Armory200));
   auto wallet = AssetWallet_Single::createFromSeed(move(seed),
      WalletCreationParams{
         pass, controlPass, path_, lookup
      });
   return addWallet(wallet, wallet->getMainAccountID());
}

////////////////////////////////////////////////////////////////////////////////
void WalletManager::deleteWallet(const string& wltId,
   const AddressAccountId& accId)
{
   ReentrantLock lock(this);

   shared_ptr<WalletContainer> wltPtr;
   {
      auto wltIter = wallets_.find(wltId);
      if (wltIter == wallets_.end())
         return;

      auto accIter = wltIter->second.find(accId);
      if (accIter == wltIter->second.end())
         return;

      wltPtr = move(accIter->second);
      wltIter->second.erase(accIter);

      if (wltIter->second.empty())
         wallets_.erase(wltIter);
   }

   //delete from disk
   wltPtr->eraseFromDisk();

   try
   {
      //unregister from db
      wltPtr->unregisterFromBDV();
   }
   catch (const exception&)
   {
      //we do not care if the unregister operation fails
   }

   wltPtr.reset();
}

////////////////////////////////////////////////////////////////////////////////
void WalletManager::loadWallets(const PassphraseLambda& passLbd)
{
   //list .lmdb files in folder
   DIR *dir;
   dir = opendir(path_.c_str());
   if (dir == nullptr)
   {
      stringstream ss;
      ss << path_ << "is not a valid datadir";
      LOGERR << ss.str();
      throw runtime_error(ss.str());
   }

   vector<string> walletPaths;
   vector<string> a135Paths;

   struct dirent *ent;
   while ((ent = readdir(dir)) != nullptr)
   {
      auto dirname = ent->d_name;
      if (strlen(dirname) > 5)
      {
         auto endOfPath = ent->d_name + strlen(ent->d_name) - 5;
         if (strcmp(endOfPath, ".lmdb") == 0)
         {
            stringstream ss;
            ss << path_ << "/" << dirname;
            walletPaths.push_back(ss.str());
         }
         else if (strcmp(endOfPath, "allet") == 0)
         {
            stringstream ss;
            ss << path_ << "/" << dirname;
            a135Paths.push_back(ss.str());
         }
      }
   }

   closedir(dir);

   ReentrantLock lock(this);

   //read the files
   for (auto& wltPath : walletPaths)
   {
      try
      {
         auto wltPtr = AssetWallet::loadMainWalletFromFile(wltPath, passLbd);
         const auto& accIds = wltPtr->getAccountIDs();
         for (const auto& accId : accIds)
            addWallet(wltPtr, accId);
      }
      catch (exception& e)
      {
         stringstream ss;
         ss << "Failed to open wallet at " << wltPath <<
            " with error:" << endl << e.what();
         LOGERR << ss.str();
      }
   }

   //parse the potential armory 1.35 wallet files
   for (auto& wltPath : a135Paths)
   {
      Armory135Header a135(wltPath);
      if (!a135.isInitialized())
         continue;

      //an armory v1.35 wallet was loaded, check if we need to
      //migrate it to v3.x
      auto& id = a135.getID();
      auto iter = wallets_.find(id);
      if (iter != wallets_.end())
         continue;

      //no equivalent v3.x wallet loaded, let's migrate it
      auto wltPtr = a135.migrate(passLbd);
      addWallet(wltPtr, wltPtr->getMainAccountID());
   }
}

////////////////////////////////////////////////////////////////////////////////
void WalletManager::updateStateFromDB(const function<void(void)>& callback)
{
   auto lbd = [this, callback](void)->void
   {
      ReentrantLock lock(this);

      //get wallet ids
      vector<string> walletIds;
      for (auto& wltPair : wallets_)
      {
         for (auto& accId : wltPair.second)
         {
            WalletAccountIdentifier wai(wltPair.first, accId.first);
            walletIds.push_back(wai.serialize());
         }
      }

      //grab wallet balances
      auto promBal = make_shared<promise<map<string, CombinedBalances>>>();
      auto futBal = promBal->get_future();
      auto lbdBal = [promBal]
         (ReturnMessage<map<string, CombinedBalances>> result)->void
      {
         promBal->set_value(result.get());
      };
      bdvPtr_->getCombinedBalances(walletIds, lbdBal);
      auto&& balances = futBal.get();

      //update wallet balances
      for (auto& wltBalance : balances)
      {
         auto wai = WalletAccountIdentifier::deserialize(wltBalance.first);
         auto wltCont = getWalletContainer(wai.walletId, wai.accountId);
         if (wltCont == nullptr)
            continue;

         wltCont->updateWalletBalanceState(wltBalance.second);
      }

      //grab address txio counts
      auto promCnt = make_shared<promise<map<string, CombinedCounts>>>();
      auto futCnt = promCnt->get_future();
      auto lbdCnt = [promCnt]
         (ReturnMessage<map<string, CombinedCounts>> result)->void
      {
         promCnt->set_value(result.get());
      };
      bdvPtr_->getCombinedAddrTxnCounts(walletIds, lbdCnt);
      auto&& counts = futCnt.get();

      //update wallet balances
      for (auto& wltCount : counts)
      {
         auto wai = WalletAccountIdentifier::deserialize(wltCount.first);
         auto wltCont = getWalletContainer(wai.walletId, wai.accountId);
         if (wltCont == nullptr)
            continue;

         wltCont->updateAddressCountState(wltCount.second);
      }

      //fire the lambda
      callback();
   };

   thread thr(lbd);
   if (thr.joinable())
      thr.detach();
}

////////////////////////////////////////////////////////////////////////////////
////
//// WalletContainer
////
////////////////////////////////////////////////////////////////////////////////
void WalletContainer::setWalletPtr(shared_ptr<AssetWallet> wltPtr,
   const Armory::Wallets::AddressAccountId& accId)
{
   wallet_ = wltPtr;
   auto acc = wallet_->getAccountForID(accId);
   auto assetAccountIds = acc->getAccountIdSet();

   for (const auto& aaId : assetAccountIds)
   {
      auto accPtr = acc->getAccountForID(aaId);
      if (accPtr == nullptr)
         throw runtime_error("[setWalletPtr] missing asset account id");

      highestUsedIndex_.emplace(aaId, accPtr->getHighestUsedIndex());
   }
}

////////////////////////////////////////////////////////////////////////////////
shared_ptr<AssetWallet> WalletContainer::getWalletPtr() const
{
   return wallet_;
}

////////////////////////////////////////////////////////////////////////////////
shared_ptr<AddressAccount> WalletContainer::getAddressAccount(void) const
{
   auto accPtr = wallet_->getAccountForID(accountId_);
   return accPtr;
}

////////////////////////////////////////////////////////////////////////////////
void WalletContainer::resetCache(void)
{
   unique_lock<mutex> lock(stateMutex_);

   totalBalance_ = 0;
   spendableBalance_ = 0;
   unconfirmedBalance_ = 0;
   balanceMap_.clear();
   countMap_.clear();
}

////////////////////////////////////////////////////////////////////////////////
string WalletContainer::registerWithBDV(bool isNew)
{
   if (bdvPtr_ == nullptr)
      throw runtime_error("bdvPtr is not set");
   resetCache();

   auto accPtr = wallet_->getAccountForID(accountId_);
   const auto& addrMap = accPtr->getAddressHashMap();

   set<BinaryData> addrSet;
   for (const auto& addrIt : addrMap)
      addrSet.emplace(addrIt.first);

   //convert set to vector
   vector<BinaryData> addrVec;
   addrVec.insert(addrVec.end(), addrSet.begin(), addrSet.end());

   WalletAccountIdentifier wai(wallet_->getID(), accountId_);
   asyncWlt_ = make_shared<AsyncClient::BtcWallet>(
      bdvPtr_->instantiateWallet(wai.serialize()));
   return asyncWlt_->registerAddresses(addrVec, isNew);
}

////////////////////////////////////////////////////////////////////////////////
void WalletContainer::unregisterFromBDV()
{
   if (bdvPtr_ == nullptr)
      throw runtime_error("bdvPtr is not set");
      
   asyncWlt_->unregister();
}

////////////////////////////////////////////////////////////////////////////////
void WalletContainer::updateWalletBalanceState(const CombinedBalances& bal)
{
   unique_lock<mutex> lock(stateMutex_);

   totalBalance_        = bal.walletBalanceAndCount_[0];
   spendableBalance_    = bal.walletBalanceAndCount_[1];
   unconfirmedBalance_  = bal.walletBalanceAndCount_[2];
   txioCount_           = bal.walletBalanceAndCount_[3];

   for (auto& addrPair : bal.addressBalances_)
      balanceMap_[addrPair.first] = addrPair.second;
}

////////////////////////////////////////////////////////////////////////////////
AssetKeyType WalletContainer::getHighestUsedIndex(void) const
{
   auto accPtr = wallet_->getAccountForID(accountId_);
   if (accPtr == nullptr)
      throw runtime_error("[getHighestUsedIndex] invalid acc id");

   auto outerAccId = accPtr->getOuterAccountID();
   if (!outerAccId.isValid())
      throw runtime_error("[getHighestUsedIndex] invalid outer acc id");

   auto indexIter = highestUsedIndex_.find(outerAccId);
   if (indexIter == highestUsedIndex_.end())
      throw runtime_error("[getHighestUsedIndex] missing index for id");

   return indexIter->second;
}

////////////////////////////////////////////////////////////////////////////////
void WalletContainer::updateAddressCountState(const CombinedCounts& cnt)
{
   unique_lock<mutex> lock(stateMutex_);

   map<AssetAccountId, AssetKeyType> topIndexMap;
   shared_ptr<IO::WalletIfaceTransaction> dbtx;
   map<BinaryData, shared_ptr<AddressEntry>> updatedAddressMap;
   map<AssetId, AddressEntryType> addrAndTypeMap;

   for (auto& addrPair : cnt.addressTxnCounts_)
   {
      auto iter = countMap_.find(addrPair.first);
      if (iter != countMap_.end())
      {
         //already tracking count for this address, just update the value
         iter->second = addrPair.second;
         continue;
      }

      const auto& ID = wallet_->getAssetIDForScrAddr(addrPair.first);

      auto topIdIter = topIndexMap.find(ID.first.getAssetAccountId());
      if (topIdIter == topIndexMap.end())
         topIdIter = topIndexMap.emplace(ID.first.getAssetAccountId(), -1).first;

      //track top used index
      auto idKey = ID.first.getAssetKey();
      if (idKey > topIdIter->second)
         topIdIter->second = idKey;

      //mark newly seen addresses for further processing
      addrAndTypeMap.insert(ID);

      //add count to map
      countMap_.emplace(addrPair);
   }

   map<AssetId, AddressEntryType> unpulledAddresses;
   for (auto& idPair : addrAndTypeMap)
   {
      //check scrAddr with on chain data matches scrAddr for
      //address entry in wallet
      if (!wallet_->isAssetUsed(idPair.first))
      {
         //db has history for an address that hasn't been pulled
         //from the wallet yet, save it for further processing
         unpulledAddresses.insert(idPair);
         continue;
      }

      auto addrType = wallet_->getAddrTypeForID(idPair.first);
      if (addrType == idPair.second)
         continue;

      //if we don't have a db tx yet, get one, as we're about to update
      //the address type on disk
      if (dbtx == nullptr)
         dbtx = wallet_->beginSubDBTransaction(wallet_->getID(), true);

      //address type mismatches, update it
      wallet_->updateAddressEntryType(idPair.first, idPair.second);

      auto addrPtr = wallet_->getAddressEntryForID(idPair.first);
      updatedAddressMap.emplace(addrPtr->getPrefixedHash(), addrPtr);
   }

   //split unpulled addresses by their accounts
   map<AssetAccountId, map<AssetId, AddressEntryType>> accIDMap;
   for (auto& idPair : unpulledAddresses)
   {
      auto accID = idPair.first.getAssetAccountId();
      auto iter = accIDMap.find(accID);
      if (iter == accIDMap.end())
         iter = accIDMap.emplace(accID, map<AssetId, AddressEntryType>()).first;
      iter->second.insert(idPair);
   }

   if (dbtx == nullptr)
      dbtx = wallet_->beginSubDBTransaction(wallet_->getID(), true);

   //run through each account, pulling addresses accordingly
   for (auto& accData : accIDMap)
   {
      auto addrAccount = wallet_->getAccountForID(
         accData.first.getAddressAccountId());
      auto assAccount = addrAccount->getAccountForID(accData.first);

      auto currentTop = assAccount->getHighestUsedIndex();
      for (auto& idPair : accData.second)
      {
         auto assetKey = idPair.first.getAssetKey();
         while (assetKey > currentTop + 1)
         {
            auto addrEntry = 
               wallet_->getNewAddress(accData.first, AddressEntryType_Default);
            updatedAddressMap.insert(
               make_pair(addrEntry->getPrefixedHash(), addrEntry));

            ++currentTop;
         }

         auto addrEntry =
            wallet_->getNewAddress(accData.first, idPair.second);
         updatedAddressMap.insert(
            make_pair(addrEntry->getPrefixedHash(), addrEntry));
         
         ++currentTop;
      }
   }

   for (const auto& topIndexIt : topIndexMap)
   {
      auto usedIndexIter = highestUsedIndex_.find(topIndexIt.first);
      if (usedIndexIter == highestUsedIndex_.end())
      {
         LOGWARN << "[updateAddressCountState]" <<
            " missing asset account, skipping";
         continue;
      }

      usedIndexIter->second = std::max(
         topIndexIt.second,
         usedIndexIter->second);
   }

   for (auto& addrPair : updatedAddressMap)
   {
      auto insertIter = updatedAddressMap_.insert(addrPair);
      if (!insertIter.second)
         insertIter.first->second = addrPair.second;
   }
}

////////////////////////////////////////////////////////////////////////////////
map<BinaryData, vector<uint64_t>> WalletContainer::getAddrBalanceMap() const
{
   map<BinaryData, vector<uint64_t>> result;

   for (auto& dataPair : countMap_)
   {
      vector<uint64_t> balVec;
      auto iter = balanceMap_.find(dataPair.first);
      if (iter == balanceMap_.end())
         balVec = {0, 0, 0};
      else 
         balVec = iter->second;

      balVec.push_back(dataPair.second);
      result.emplace(make_pair(dataPair.first, balVec));
   }

   return result;
}

////////////////////////////////////////////////////////////////////////////////
void WalletContainer::createAddressBook(
   const function<void(ReturnMessage<vector<AddressBookEntry>>)>& lbd)
{
   if (asyncWlt_ == nullptr)
      throw runtime_error("empty asyncWlt");

   asyncWlt_->createAddressBook(lbd);
}

////////////////////////////////////////////////////////////////////////////////
map<BinaryData, shared_ptr<AddressEntry>> WalletContainer::getUpdatedAddressMap()
{
   auto mapMove = move(updatedAddressMap_);
   updatedAddressMap_.clear();

   return mapMove;
}

////////////////////////////////////////////////////////////////////////////////
unique_ptr<Armory::Seeds::WalletBackup> WalletContainer::getBackupStrings(
   const PassphraseLambda& passLbd) const
{
   auto wltSingle = dynamic_pointer_cast<AssetWallet_Single>(wallet_);
   if (wltSingle == nullptr)
   {
      LOGERR << "WalletContainer::getBackupStrings: unexpected wallet type";
      throw runtime_error(
         "WalletContainer::getBackupStrings: unexpected wallet type");
   }

   wltSingle->setPassphrasePromptLambda(passLbd);
   auto backupStrings = Armory::Seeds::Helpers::getWalletBackup(wltSingle);
   wltSingle->resetPassphrasePromptLambda();

   return backupStrings;
}

////////////////////////////////////////////////////////////////////////////////
void WalletContainer::eraseFromDisk()
{
   auto wltPtr = move(wallet_);
   AssetWallet::eraseFromDisk(wltPtr.get());
   wltPtr.reset();
}

////////////////////////////////////////////////////////////////////////////////
void WalletContainer::setComment(const string& key, const string& val)
{
   auto keyBd = BinaryData::fromString(key);
   wallet_->setComment(keyBd, val);
}

////////////////////////////////////////////////////////////////////////////////
void WalletContainer::setLabels(const string& title, const string& desc)
{
   wallet_->setLabel(title);
   wallet_->setDescription(desc);
}

////////////////////////////////////////////////////////////////////////////////
const EncryptionKeyId& WalletContainer::getDefaultEncryptionKeyId() const
{
   return wallet_->getDefaultEncryptionKeyId();
}

////////////////////////////////////////////////////////////////////////////////
////
//// Armory135Header
////
////////////////////////////////////////////////////////////////////////////////
void Armory135Header::verifyChecksum(
   const BinaryDataRef& val, const BinaryDataRef& chkSum)
{
   if (val.isZero() && chkSum.isZero())
      return;

   auto&& computedChkSum = BtcUtils::getHash256(val);
   if (computedChkSum.getSliceRef(0, 4) != chkSum)
      throw runtime_error("failed checksum");
}

////////////////////////////////////////////////////////////////////////////////
void Armory135Header::parseFile()
{
   /*
   Simply return on any failure, the version_ field will not be initialized 
   until the whole header is parsed and checksums pass
   */
   
   uint32_t version = UINT32_MAX;
   try
   {
      //grab root key & address chain length from python wallet
      auto&& fileMap = DBUtils::getMmapOfFile(path_, false);
      BinaryRefReader brr(fileMap.filePtr_, fileMap.size_);

      //file type
      auto fileTypeStr = brr.get_BinaryData(8);
      if (fileTypeStr != BinaryData::fromString(WALLET_135_HEADER, 8))
         return;

      //version
      version = brr.get_uint32_t();

      //magic bytes
      auto magicBytes = brr.get_BinaryData(4);
      if (magicBytes != Armory::Config::BitcoinSettings::getMagicBytes())
         return;

      //flags
      auto flags = brr.get_uint64_t();
      isEncrypted_  = flags & 0x0000000000000001;
      watchingOnly_ = flags & 0x0000000000000002;

      //wallet ID
      auto walletIDbin = brr.get_BinaryData(6);
      walletID_ = BtcUtils::base58_encode(walletIDbin);

      //creation timestamp
      timestamp_ = brr.get_uint64_t();

      //label name & description
      auto&& labelNameBd = brr.get_BinaryData(32);
      auto&& labelDescBd = brr.get_BinaryData(256);

      auto labelNameLen = strnlen(labelNameBd.toCharPtr(), 32);
      labelName_ = string(labelNameBd.toCharPtr(), labelNameLen);

      auto labelDescriptionLen = strnlen(labelNameBd.toCharPtr(), 256);
      labelDescription_ = string(labelDescBd.toCharPtr(), labelDescriptionLen);

      //highest used chain index
      highestUsedIndex_ = brr.get_int64_t();

      {
         /* kdf params */
         auto kdfPayload      = brr.get_BinaryDataRef(256);
         BinaryRefReader brrPayload(kdfPayload);
         auto allKdfData = brrPayload.get_BinaryDataRef(44);
         auto allKdfChecksum  = brrPayload.get_BinaryDataRef(4);

         //skip check if there is wallet is unencrypted
         if (isEncrypted_)
         {
            verifyChecksum(allKdfData, allKdfChecksum);

            BinaryRefReader brrKdf(allKdfData);
            kdfMem_    = brrKdf.get_uint64_t();
            kdfIter_  = brrKdf.get_uint32_t();
            kdfSalt_   = brrKdf.get_BinaryDataRef(32);
         }
      }

      //256 bytes skip
      brr.advance(256);

      /* root address */
      auto rootAddrRef = brr.get_BinaryDataRef(PYBTC_ADDRESS_SIZE);
      Armory135Address rootAddrObj;
      rootAddrObj.parseFromRef(rootAddrRef);
      addrMap_.insert(make_pair(
         BinaryData::fromString("ROOT"), rootAddrObj));

      //1024 bytes skip
      brr.advance(1024);

      {
         /* wallet entries */
         while (brr.getSizeRemaining() > 0)
         {
            auto entryType = brr.get_uint8_t();
            switch (entryType)
            {
               case WLT_DATATYPE_KEYDATA:
               {
                  auto key = brr.get_BinaryDataRef(20);
                  auto val = brr.get_BinaryDataRef(PYBTC_ADDRESS_SIZE);

                  Armory135Address addrObj;
                  addrObj.parseFromRef(val);

                  addrMap_.insert(make_pair(key, addrObj));
                  break;
               }

               case WLT_DATATYPE_ADDRCOMMENT:
               {
                  auto key = brr.get_BinaryDataRef(20);
                  auto len = brr.get_uint16_t();
                  auto val = brr.get_String(len);

                  commentMap_.insert(make_pair(key, val));
                  break;
               }

               case WLT_DATATYPE_TXCOMMENT:
               {
                  auto key = brr.get_BinaryDataRef(32);
                  auto len = brr.get_uint16_t();
                  auto val = brr.get_String(len);

                  commentMap_.insert(make_pair(key, val));
                  break;
               }

               case WLT_DATATYPE_OPEVAL:
                  throw runtime_error("not supported");

               case WLT_DATATYPE_DELETED:
               {
                  auto len = brr.get_uint16_t();
                  brr.advance(len);
                  break;
               }

               default:
                  throw runtime_error("invalid wallet entry");
            }
         }

      }
   }
   catch(exception& e)
   {
      LOGWARN << "failed to load wallet at " << path_ << " with error: ";
      LOGWARN << "   " << e.what();
      return;
   }

   version_ = version;
}

////////////////////////////////////////////////////////////////////////////////
shared_ptr<AssetWallet_Single> Armory135Header::migrate(
   const PassphraseLambda& passLbd) const
{
   auto rootKey = BinaryData::fromString("ROOT");
   auto rootAddrIter = addrMap_.find(rootKey);
   if (rootAddrIter == addrMap_.end())
      throw runtime_error("no root entry");

   auto& rootAddrObj = rootAddrIter->second;
   auto chaincodeCopy = rootAddrObj.chaincode();

   SecureBinaryData controlPass;
   SecureBinaryData privKeyPass;

   auto folder = DBUtils::getBaseDir(path_);

   auto highestIndex = highestUsedIndex_;
   for (auto& addrPair : addrMap_)
   {
      if (highestIndex < addrPair.second.chainIndex())
         highestIndex = addrPair.second.chainIndex();
   }
   ++highestIndex;

   //try to decrypt the private root
   SecureBinaryData decryptedRoot;
   {
      if (isEncrypted_ && rootAddrObj.hasPrivKey() &&
         rootAddrObj.isEncrypted())
      {
         //decrypt lbd
         auto decryptPrivKey = [this, &privKeyPass](
            const PassphraseLambda& passLbd,
            const Armory135Address& rootAddrObj)->SecureBinaryData
         {
            set<EncryptionKeyId> idSet = { BinaryData::fromString(walletID_) };

            while (true)
            {
               //prompt for passphrase
               auto&& passphrase = passLbd(idSet);
               if (passphrase.getSize() == 0)
                  return {};

               //kdf it
               KdfRomix myKdf(kdfMem_, kdfIter_, kdfSalt_);
               auto&& derivedPass = myKdf.DeriveKey(passphrase);

               //decrypt the privkey
               auto&& decryptedKey = CryptoAES::DecryptCFB(
                     rootAddrObj.privKey(), derivedPass, rootAddrObj.iv());

               //generate pubkey
               auto computedPubKey = 
                  CryptoECDSA().ComputePublicKey(decryptedKey, false);

               if (rootAddrObj.pubKey() != computedPubKey)
                  continue;

               //compare pubkeys
               privKeyPass = move(passphrase);
               return decryptedKey;
            }
         };

         decryptedRoot = move(decryptPrivKey(passLbd, rootAddrObj));
      }

      //cleanup
      passLbd({});
   }

   //create wallet
   shared_ptr<AssetWallet_Single> wallet;
   if (decryptedRoot.empty())
   {
      auto pubKeyCopy = rootAddrObj.pubKey();
      wallet = AssetWallet_Single::createFromPublicRoot_Armory135(
         folder, pubKeyCopy, chaincodeCopy,
         controlPass, highestIndex);
   }
   else
   {
      unique_ptr<ClearTextSeed> seed(new ClearTextSeed_Armory135(
         decryptedRoot, chaincodeCopy));
      wallet = AssetWallet_Single::createFromSeed(move(seed),
         WalletCreationParams{
            privKeyPass, controlPass, folder, (uint32_t)highestIndex
         });
   }

   //main account id, check it matches armory wallet id
   if (wallet->getID() != walletID_)
      throw runtime_error("wallet id mismatch");

   //run through addresses, figure out script types
   auto accID = wallet->getMainAccountID();
   auto mainAccPtr = wallet->getAccountForID(accID);

   //TODO: deal with imports

   map<AssetId, AddressEntryType> typeMap;
   for (auto& addrPair : addrMap_)
   {
      if (addrPair.second.chainIndex() < 0 ||
         addrPair.second.chainIndex() > highestUsedIndex_)
         continue;

      const auto& addrTypePair = mainAccPtr->getAssetIDPairForAddrUnprefixed(
         addrPair.second.scrAddr());

      if (addrTypePair.second != mainAccPtr->getDefaultAddressType())
         typeMap.emplace(addrTypePair);
   }

   {
      //set script types
      auto dbtx = wallet->beginSubDBTransaction(walletID_, true);
      AssetKeyType lastIndex = -1;
      for (auto& typePair : typeMap)
      {
         //get int index for pair
         while (typePair.first.getAssetKey() != lastIndex)
         {
            wallet->getNewAddress();
            ++lastIndex;
         }

         wallet->getNewAddress(typePair.second);
         ++lastIndex;
      }

      while (lastIndex < highestUsedIndex_)
      {
         wallet->getNewAddress();
         ++lastIndex;
      }
   }

   //set name & desc
   if (labelName_.size() > 0)
      wallet->setLabel(labelName_);

   if (labelDescription_.size() > 0)
      wallet->setDescription(labelDescription_);

   {
      //add comments
      auto dbtx = wallet->beginSubDBTransaction(walletID_, true);
      for (auto& commentPair : commentMap_)
         wallet->setComment(commentPair.first, commentPair.second);
   }

   return wallet;
}


////////////////////////////////////////////////////////////////////////////////
////
//// Armory135Address
////
////////////////////////////////////////////////////////////////////////////////
void Armory135Address::parseFromRef(const BinaryDataRef& bdr)
{
   BinaryRefReader brrScrAddr(bdr);

   {
      //scrAddr, only to verify the checksum
      scrAddr_ = brrScrAddr.get_BinaryData(20);
      auto scrAddrChecksum = brrScrAddr.get_BinaryData(4);
      Armory135Header::verifyChecksum(scrAddr_, scrAddrChecksum);
   }

   //address version, unused
   brrScrAddr.get_uint32_t();

   //address flags
   auto addrFlags = brrScrAddr.get_uint64_t();
   hasPrivKey_ = addrFlags & 0x0000000000000001;
   hasPubKey_  = addrFlags & 0x0000000000000002;

   isEncrypted_ = addrFlags & 0x0000000000000004;

   //chaincode
   chaincode_ = brrScrAddr.get_BinaryData(32);
   auto chaincodeChecksum = brrScrAddr.get_BinaryDataRef(4);
   Armory135Header::verifyChecksum(chaincode_, chaincodeChecksum);

   //chain index
   chainIndex_       = brrScrAddr.get_int64_t();
   depth_            = brrScrAddr.get_int64_t();

   //iv
   iv_               = brrScrAddr.get_BinaryData(16);
   auto ivChecksum   = brrScrAddr.get_BinaryDataRef(4);
   if (isEncrypted_)
      Armory135Header::verifyChecksum(iv_, ivChecksum);

   //private key
   privKey_             = brrScrAddr.get_BinaryData(32);
   auto privKeyChecksum = brrScrAddr.get_BinaryDataRef(4);
   if (hasPrivKey_)
      Armory135Header::verifyChecksum(privKey_, privKeyChecksum);

   //pub key
   pubKey_              = brrScrAddr.get_BinaryData(65);
   auto pubKeyChecksum  = brrScrAddr.get_BinaryDataRef(4);
   Armory135Header::verifyChecksum(pubKey_, pubKeyChecksum);
}

////////////////////////////////////////////////////////////////////////////////
////
//// WalletAccountIdentifier
////
////////////////////////////////////////////////////////////////////////////////
WalletAccountIdentifier::WalletAccountIdentifier(const string& wId,
   const Armory::Wallets::AddressAccountId& aId) :
   walletId(wId), accountId(aId)
{}

////////////////////////////////////////////////////////////////////////////////
WalletAccountIdentifier WalletAccountIdentifier::deserialize(const string& id)
{
   vector<string> lines;
   istringstream ss(id);

   for (string line; getline(ss, line, ':');)
      lines.emplace_back(move(line));

   if (lines.size() != 2)
      throw runtime_error("[WalletAccountIdentifier::deserialize]");

   auto accId = AddressAccountId::fromHex(lines[1]);
   return WalletAccountIdentifier(lines[0], accId);
}

////////////////////////////////////////////////////////////////////////////////
string WalletAccountIdentifier::serialize() const
{
   string result = walletId + ":" + accountId.toHexStr();
   return result;
}