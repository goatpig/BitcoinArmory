////////////////////////////////////////////////////////////////////////////////
//                                                                            //
//  Copyright (C) 2019, goatpig                                               //
//  Distributed under the MIT license                                         //
//  See LICENSE-MIT or https://opensource.org/licenses/MIT                    //
//                                                                            //
////////////////////////////////////////////////////////////////////////////////

#include "WalletFileInterface.h"
#include "BtcUtils.h"
#include "DBUtils.h"
#include "WalletHeader.h"
#include "DecryptedDataContainer.h"
#include "Seeds/Seeds.h"

using namespace std;
using namespace Armory::Seeds;
using namespace Armory::Wallets::IO;
using namespace Armory::Wallets::Encryption;

#define COMPACT_FILE_SWAP_NAME "swapOld"
#define COMPACT_FILE_COPY_NAME "compactCopy"
#define COMPACT_FILE_FOLDER    "_delete_me"

#ifdef _WIN32
#include "leveldb_windows_port/win32_posix/win32_posix.h"
#define mkdir mkdir_win32
#else
#include <sys/stat.h>
#include <sys/types.h>
#endif

////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////
//// WalletDBInterface
////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////
WalletDBInterface::WalletDBInterface()
{
   fortuna_ = make_unique<PRNG_Fortuna>();
}

////////////////////////////////////////////////////////////////////////////////
WalletDBInterface::~WalletDBInterface()
{
   shutdown();
}

////////////////////////////////////////////////////////////////////////////////
void WalletDBInterface::setupEnv(const string& path, bool fileExists,
   const PassphraseLambda& passLbd, uint32_t lockTime_ms)
{
   auto lock = unique_lock<mutex>(setupMutex_);
   if (dbEnv_ != nullptr)
      return;

   path_ = path;
   dbCount_ = 2;

   //open env for control and meta dbs
   openDbEnv(fileExists);

   //open control db
   openControlDb();

   bool isNew = false;
   shared_ptr<WalletHeader_Control> controlHeader;
   try
   {
      //get control header
      controlHeader = dynamic_pointer_cast<WalletHeader_Control>(
         loadControlHeader());
      if (controlHeader == nullptr)
         throw WalletException("invalid control header");
   }
   catch (NoEntryInWalletException&)
   {
      //no control header, this is a fresh wallet, set it up
      controlHeader = setupControlDB(passLbd, lockTime_ms);
      isNew = true;
   }

   //load control decrypted data container
   loadDataContainer(controlHeader);

   //load control seed
   loadSeed(controlHeader);

   /*
   The passphrase prompt will be called a 3rd time out of 3 in this 
   scope to decrypt the control seed and generate the encrypted 
   header DB.
   */

   //decrypt control seed
   lockControlContainer(passLbd);
   auto& rootEncrKey = 
      decryptedData_->getClearTextAssetData(controlSeed_.get());

   //load wallet header db
   {
      auto headrPtr = make_shared<WalletHeader_Control>();
      headrPtr->walletID_ = WALLETHEADER_DBNAME;
      headrPtr->controlSalt_ = controlHeader->controlSalt_;
      encryptionVersion_ = headrPtr->encryptionVersion_;
      openDB(headrPtr, rootEncrKey, encryptionVersion_);
   }

   //load wallet header objects
   unsigned dbCount;
   if (!isNew)
   {
      loadHeaders();
      dbCount = headerMap_.size() + 2;
   }
   else
   {
      dbCount = 3;
   }

   //set new db count;
   setDbCount(dbCount, false);

   //open all dbs listed in header map
   for (auto& headerPtr : headerMap_)
      openDB(headerPtr.second, rootEncrKey, encryptionVersion_);

   //clean up
   unlockControlContainer();
}

////////////////////////////////////////////////////////////////////////////////
BinaryDataRef WalletDBInterface::getDataRefForKey(
   DBIfaceTransaction* tx, const BinaryData& key)
{
   /** The reference lifetime is tied to the db tx lifetime. The caller has to
   maintain the tx for as long as the data ref needs to be valid **/

   auto ref = tx->getDataRef(key);

   if (ref.getSize() == 0)
      throw NoEntryInWalletException();

   return DBUtils::getDataRefForPacket(ref);
}

////////////////////////////////////////////////////////////////////////////////
void WalletDBInterface::loadHeaders()
{
   auto&& tx = beginReadTransaction(WALLETHEADER_DBNAME);

   //meta map
   auto dbIter = tx->getIterator();

   BinaryWriter bwKey;
   bwKey.put_uint8_t(WALLETHEADER_PREFIX);
   dbIter->seek(bwKey.getDataRef());

   while (dbIter->isValid())
   {
      auto iterkey = dbIter->key();
      auto itervalue = dbIter->value();

      //check value's advertized size is packet size and strip it
      BinaryRefReader brrVal(itervalue);
      auto valsize = brrVal.get_var_int();
      if (valsize != brrVal.getSizeRemaining())
         throw WalletInterfaceException("entry val size mismatch");

      try
      {
         auto headerPtr = WalletHeader::deserialize(
            iterkey, brrVal.get_BinaryDataRef(brrVal.getSizeRemaining()));
         //headerPtr->masterID_ = masterID_;

         if (headerPtr->shouldLoad())
            headerMap_.insert(make_pair(headerPtr->getWalletID(), headerPtr));
      }
      catch (exception& e)
      {
         LOGERR << e.what();
         break;
      }

      dbIter->advance();
   }
}

////////////////////////////////////////////////////////////////////////////////
void WalletDBInterface::openControlDb(void)
{
   if (controlDb_ != nullptr)
      throw WalletInterfaceException("controlDb is not null");

   controlDb_ = make_unique<LMDB>();
   auto tx = LMDBEnv::Transaction(dbEnv_.get(), LMDB::ReadWrite);
   controlDb_->open(dbEnv_.get(), CONTROL_DB_NAME);
}

////////////////////////////////////////////////////////////////////////////////
void WalletDBInterface::shutdown()
{
   auto lock = unique_lock<mutex>(setupMutex_);
   if (DBIfaceTransaction::hasTx())
      throw WalletInterfaceException("live transactions, cannot shutdown env");

   if (controlDb_ != nullptr)
   {
      controlDb_->close();
      controlDb_.reset();
   }

   controlLock_.reset();
   decryptedData_.reset();
   controlSeed_.reset();

   dbMap_.clear();

   if (dbEnv_ != nullptr)
   {
      dbEnv_->close();
      dbEnv_.reset();
   }

   dbCount_ = 0;
   path_.clear();
}

////////////////////////////////////////////////////////////////////////////////
void WalletDBInterface::openDB(std::shared_ptr<WalletHeader> headerPtr,
   const SecureBinaryData& encrRootKey, unsigned encrVersion)
{
   auto&& dbName = headerPtr->getDbName();
   auto iter = dbMap_.find(dbName);
   if (iter != dbMap_.end())
      return;

   //create db object
   auto dbiPtr = make_unique<DBInterface>(
      dbEnv_.get(), dbName, headerPtr->controlSalt_, encrVersion);
   
   /*
   Load all db entries in RAM. This call also decrypts the on disk data.
   */
   dbiPtr->loadAllEntries(encrRootKey);

   //insert in dbMap
   dbMap_.insert(make_pair(dbName, move(dbiPtr)));

}

////////////////////////////////////////////////////////////////////////////////
const string& WalletDBInterface::getFilename() const
{
   if (dbEnv_ == nullptr)
      throw WalletInterfaceException("null dbEnv");

   return dbEnv_->getFilename();
}

////////////////////////////////////////////////////////////////////////////////
unique_ptr<DBIfaceTransaction> WalletDBInterface::beginWriteTransaction(
   const string& dbName)
{
   auto iter = dbMap_.find(dbName);
   if (iter == dbMap_.end())
   {
      if (dbName == CONTROL_DB_NAME)
      {
         return make_unique<RawIfaceTransaction>(
            dbEnv_.get(), controlDb_.get(), true);
      }

      throw WalletInterfaceException("invalid db name");
   }

   return make_unique<WalletIfaceTransaction>(this, iter->second.get(), true);
}

////////////////////////////////////////////////////////////////////////////////
unique_ptr<DBIfaceTransaction> WalletDBInterface::beginReadTransaction(
   const string& dbName)
{
   auto iter = dbMap_.find(dbName);
   if (iter == dbMap_.end())
   {
      if (dbName == CONTROL_DB_NAME)
      {
         return make_unique<RawIfaceTransaction>(
            dbEnv_.get(), controlDb_.get(), false);
      }

      throw WalletInterfaceException("invalid db name");
   }

   return make_unique<WalletIfaceTransaction>(this, iter->second.get(), false);
}

////////////////////////////////////////////////////////////////////////////////
shared_ptr<WalletHeader> WalletDBInterface::loadControlHeader()
{
   //grab meta object
   BinaryWriter bw;
   bw.put_uint8_t(WALLETHEADER_PREFIX);
   bw.put_String(CONTROL_DB_NAME);
   auto& headerKey = bw.getData();

   auto&& tx = beginReadTransaction(CONTROL_DB_NAME);
   auto headerVal = getDataRefForKey(tx.get(), headerKey);
   if (headerVal.getSize() == 0)
      throw WalletInterfaceException("missing control db entry");

   return WalletHeader::deserialize(headerKey, headerVal);
}

////////////////////////////////////////////////////////////////////////////////
void WalletDBInterface::loadDataContainer(shared_ptr<WalletHeader> headerPtr)
{
   //grab decrypted data object
   auto getWriteTx = [this](const string& name)->unique_ptr<DBIfaceTransaction>
   {
      return this->beginWriteTransaction(name);
   };

   decryptedData_ = make_unique<DecryptedDataContainer>(
      getWriteTx, headerPtr->getDbName(),
      headerPtr->getDefaultEncryptionKey(),
      headerPtr->getDefaultEncryptionKeyId(),
      headerPtr->defaultKdfId_, headerPtr->masterEncryptionKeyId_);

   auto readTx = beginReadTransaction(headerPtr->getDbName());
   decryptedData_->readFromDisk(move(readTx));
}

////////////////////////////////////////////////////////////////////////////////
void WalletDBInterface::loadSeed(shared_ptr<WalletHeader> headerPtr)
{
   auto&& tx = beginReadTransaction(headerPtr->getDbName());

   BinaryWriter bwKey;
   bwKey.put_uint32_t(WALLET_SEED_KEY);
   auto rootAssetRef = getDataRefForKey(tx.get(), bwKey.getData());

   controlSeed_ = EncryptedSeed::deserialize(rootAssetRef);
   if (controlSeed_ == nullptr)
      throw WalletException("failed to deser wallet seed");
}

////////////////////////////////////////////////////////////////////////////////
MasterKeyStruct WalletDBInterface::initWalletHeaderObject(
   shared_ptr<WalletHeader> headerPtr,
   const SecureBinaryData& passphrase,
   uint32_t unlockTime_ms)
{
   /*
   Setup master and top encryption key.

   - The master encryption key encrypts entries in the wallet.

   - The top encryption key encrypts the master encryption key.
     If a user passphrase is provided, it is used to generate the top encryption
     key. Otherwise the default encryption key is used.

   - The default encryption key is 32 byte RNG value written in clear text on
     disk. Its purpose is to prevent divergence in implemenation between
     encrypted and unencrypted wallets.
   */

   MasterKeyStruct mks;

   /*
   generate master encryption key, derive id
   */
   mks.kdf_ = make_shared<KeyDerivationFunction_Romix>(unlockTime_ms);
   auto&& masterKeySBD = CryptoPRNG::generateRandom(32);
   mks.decryptedMasterKey_ = make_shared<ClearTextEncryptionKey>(masterKeySBD);
   mks.decryptedMasterKey_->deriveKey(mks.kdf_);
   auto&& masterEncryptionKeyId = mks.decryptedMasterKey_->getId(mks.kdf_->getId());

   /*
   create cipher, tie it to master encryption key
   */
   mks.cipher_ = make_unique<Cipher_AES>(mks.kdf_->getId(),
      masterEncryptionKeyId);

   /*
   setup default encryption key, only ever used if no user passphrase is
   provided
   */
   headerPtr->defaultEncryptionKey_ = move(CryptoPRNG::generateRandom(32));
   auto defaultKey = headerPtr->getDefaultEncryptionKey();
   auto defaultEncryptionKeyPtr = make_unique<ClearTextEncryptionKey>(defaultKey);
   defaultEncryptionKeyPtr->deriveKey(mks.kdf_);
   headerPtr->defaultEncryptionKeyId_ =
      defaultEncryptionKeyPtr->getId(mks.kdf_->getId());

   /*
   encrypt master encryption key with passphrase if present, otherwise use
   default key
   */
   unique_ptr<ClearTextEncryptionKey> topEncryptionKey;
   if (!passphrase.empty())
   {
      //copy passphrase
      auto&& passphraseCopy = passphrase.copy();
      topEncryptionKey = make_unique<ClearTextEncryptionKey>(passphraseCopy);
   }
   else
   {
      LOGWARN << "No control passphrase provided, wallet file will not be encrypted";
      topEncryptionKey = move(defaultEncryptionKeyPtr);
   }

   /*
   derive encryption key id
   */
   topEncryptionKey->deriveKey(mks.kdf_);
   auto&& topEncryptionKeyId = topEncryptionKey->getId(mks.kdf_->getId());

   /*
   create cipher for top encryption key
   */
   auto&& masterKeyCipher = mks.cipher_->getCopy(topEncryptionKeyId);

   /*
   encrypt the master encryption key with the top encryption key
   */
   auto&& encrMasterKey = masterKeyCipher->encrypt(
      topEncryptionKey.get(), mks.kdf_->getId(), 
      mks.decryptedMasterKey_.get());

   /*
   create encryption key object
   */
   mks.masterKey_ = make_shared<EncryptionKey>(masterEncryptionKeyId,
      encrMasterKey, move(masterKeyCipher));

   /*
   set master encryption key relevant ids in the WalletMeta object
   */
   headerPtr->masterEncryptionKeyId_ = mks.masterKey_->getId();
   headerPtr->defaultKdfId_ = mks.kdf_->getId();

   /*
   setup control salt
   */
   headerPtr->controlSalt_ = CryptoPRNG::generateRandom(32);

   return mks;
}

////////////////////////////////////////////////////////////////////////////////
shared_ptr<WalletHeader_Control> WalletDBInterface::setupControlDB(
   const PassphraseLambda& passLbd, uint32_t unlockTime_ms)
{
   //prompt for passphrase
   SecureBinaryData passphrase;
   if (passLbd)
      passphrase = passLbd({});

   //create control meta object
   auto headerPtr = make_shared<WalletHeader_Control>();
   headerPtr->walletID_ = CONTROL_DB_NAME;
   auto keyStruct = initWalletHeaderObject(headerPtr, passphrase, unlockTime_ms);

   //setup controlDB decrypted data container
   auto decryptedData = make_shared<DecryptedDataContainer>(
      nullptr, CONTROL_DB_NAME,
      headerPtr->defaultEncryptionKey_,
      headerPtr->defaultEncryptionKeyId_,
      headerPtr->defaultKdfId_,
      headerPtr->masterEncryptionKeyId_);
   decryptedData->addEncryptionKey(keyStruct.masterKey_);
   decryptedData->addKdf(keyStruct.kdf_);

   /*
   The lambda will be called to trigger the encryption of the control seed.
   This will be the second out of 3 calls to the passphrase lambda during
   wallet creation.
   */
   decryptedData->setPassphrasePromptLambda(passLbd);

   {
      //create encrypted seed object
      auto&& seed = CryptoPRNG::generateRandom(32);
      auto&& lock = ReentrantLock(decryptedData.get());

      auto cipherCopy = keyStruct.cipher_->getCopy();
      auto cipherText = decryptedData->encryptData(cipherCopy.get(), seed);
      auto cipherData = make_unique<CipherData>(cipherText, move(cipherCopy));
      auto encrSeed = make_shared<EncryptedSeed>(
         move(cipherData), SeedType::Raw);

      //write seed to disk
      auto&& tx = beginWriteTransaction(CONTROL_DB_NAME);

      BinaryWriter seedKey;
      seedKey.put_uint32_t(WALLET_SEED_KEY);
      auto&& seedVal = encrSeed->serialize();
      tx->insert(seedKey.getData(), seedVal);

      //write meta ptr to disk
      auto&& metaKey = headerPtr->getDbKey();
      auto&& metaVal = headerPtr->serialize();
      tx->insert(metaKey, metaVal);

      //write decrypted data container to disk
      decryptedData->updateOnDisk(move(tx));
   }

   return headerPtr;
}

////////////////////////////////////////////////////////////////////////////////
void WalletDBInterface::putHeader(shared_ptr<WalletHeader> headerPtr)
{
   auto&& key = headerPtr->getDbKey();
   auto&& val = headerPtr->serialize();

   auto&& tx = beginWriteTransaction(WALLETHEADER_DBNAME);
   tx->insert(key, val);
}

////////////////////////////////////////////////////////////////////////////////
void WalletDBInterface::addHeader(std::shared_ptr<WalletHeader> headerPtr)
{
   auto lock = unique_lock<mutex>(setupMutex_);

   auto iter = headerMap_.find(headerPtr->walletID_);
   if (iter != headerMap_.end())
      throw WalletInterfaceException("header already in map");

   if (dbMap_.size() + 2 > dbCount_)
      throw WalletInterfaceException("dbCount is too low");

   auto&& dbName = headerPtr->getDbName();
   if (dbName.size() == 0)
      throw WalletInterfaceException("empty dbname");

   auto& rootEncrKey =
      decryptedData_->getClearTextAssetData(controlSeed_.get());
   auto dbiPtr = make_unique<DBInterface>(
      dbEnv_.get(), dbName, headerPtr->controlSalt_, encryptionVersion_);
   dbiPtr->loadAllEntries(rootEncrKey);

   putHeader(headerPtr);
   dbMap_.insert(make_pair(dbName, move(dbiPtr)));
   headerMap_.insert(make_pair(headerPtr->walletID_, headerPtr));
}

////////////////////////////////////////////////////////////////////////////////
shared_ptr<WalletHeader> WalletDBInterface::getWalletHeader(
   const string& name) const
{
   auto iter = headerMap_.find(name);
   if (iter == headerMap_.end())
      throw WalletException("missing main wallet header");
   return iter->second;
}

////////////////////////////////////////////////////////////////////////////////
const map<string, shared_ptr<WalletHeader>>& 
   WalletDBInterface::getHeaderMap() const
{
   return headerMap_;
}

////////////////////////////////////////////////////////////////////////////////
unsigned WalletDBInterface::getDbCount() const
{
   auto lock = unique_lock<mutex>(setupMutex_);
   return headerMap_.size();
}

////////////////////////////////////////////////////////////////////////////////
unsigned WalletDBInterface::getFreeDbCount() const
{
   auto lock = unique_lock<mutex>(setupMutex_);
   auto count = headerMap_.size() + 2;
   if (count >= dbCount_)
      return 0;

   return dbCount_ - count;
}

////////////////////////////////////////////////////////////////////////////////
void WalletDBInterface::setDbCount(unsigned count)
{
   //add 2 for the control and headers db
   setDbCount(count + 2, true);
}

////////////////////////////////////////////////////////////////////////////////
void WalletDBInterface::openDbEnv(bool fileExists)
{
   if (DBUtils::fileExists(path_, 0) != fileExists)
      throw WalletInterfaceException("[openEnv] file flag mismatch");

   if (dbEnv_ != nullptr)
      throw WalletInterfaceException("[openEnv] dbEnv already instantiated");

   dbEnv_ = make_unique<LMDBEnv>(dbCount_);
   dbEnv_->open(path_, MDB_NOTLS);
   dbEnv_->setMapSize(100*1024*1024ULL);
}

////////////////////////////////////////////////////////////////////////////////
void WalletDBInterface::openEnv()
{
   openDbEnv(true);

   for (auto& dbPtr : dbMap_)
      dbPtr.second->reset(dbEnv_.get());
}

////////////////////////////////////////////////////////////////////////////////
void WalletDBInterface::closeEnv()
{
   if (controlDb_ != nullptr)
   {
      controlDb_->close();
      controlDb_.reset();
   }

   for (auto& dbPtr : dbMap_)
      dbPtr.second->close();

   dbEnv_->close();
   dbEnv_.reset();
}

////////////////////////////////////////////////////////////////////////////////
void WalletDBInterface::setDbCount(unsigned count, bool doLock)
{
   if (DBIfaceTransaction::hasTx())
   {
      throw WalletInterfaceException(
         "live transactions, cannot change dbCount");
   }

   if (count <= dbCount_)
      return;

   auto lock = unique_lock<mutex>(setupMutex_, defer_lock);
   if (doLock)
      lock.lock();

   //close env
   closeEnv();

   //reopen with new dbCount
   dbCount_ = count;
   openEnv();
}

////////////////////////////////////////////////////////////////////////////////
void WalletDBInterface::lockControlContainer(const PassphraseLambda& passLbd)
{
   if (controlLock_ != nullptr)
      throw WalletInterfaceException("control container already locked");
   
   controlLock_ = make_unique<ReentrantLock>(decryptedData_.get());
   decryptedData_->setPassphrasePromptLambda(passLbd);
}

////////////////////////////////////////////////////////////////////////////////
void WalletDBInterface::unlockControlContainer()
{
   if (controlLock_ == nullptr)
      throw WalletInterfaceException("control container isn't locked");

   decryptedData_->resetPassphraseLambda();
   controlLock_.reset();
}

////////////////////////////////////////////////////////////////////////////////
void WalletDBInterface::changeControlPassphrase(
   const function<SecureBinaryData(void)>& newPassLbd, 
   const PassphraseLambda& passLbd)
{
   try
   {
      openControlDb();
      
      /*
      No need to set the control db after opening it, decryptedData_ is 
      instantiated with the db's shared_ptr, which is not cleaned up
      after the controldb is shut down.
      */
   }
   catch(WalletInterfaceException&)
   {
      //control db is already opened, nothing to do
   }
   
   //hold tx write mutex until the file is compacted
   unique_lock<recursive_mutex> lock(DBIfaceTransaction::writeMutex_);

   //set the lambda to unlock the control encryption key
   decryptedData_->setPassphrasePromptLambda(passLbd);

   //change the passphrase
   auto& masterKeyId = decryptedData_->getMasterEncryptionKeyId();
   auto& kdfId = decryptedData_->getDefaultKdfId();
   decryptedData_->encryptEncryptionKey(masterKeyId, kdfId, newPassLbd);

   //clear the lambda
   decryptedData_->resetPassphraseLambda();

   //wipe the db
   compactFile();
}

////////////////////////////////////////////////////////////////////////////////
void WalletDBInterface::eraseControlPassphrase(const PassphraseLambda& passLbd)
{
   try
   {
      openControlDb();

      /*
      No need to set the control db after opening it, decryptedData_ is
      instantiated with the db's shared_ptr, which is not cleaned up
      after the controldb is shut down.
      */
   }
   catch (WalletInterfaceException&)
   {
      //control db is already opened, nothing to do
   }

   //hold tx write mutex until the file is compacted
   unique_lock<recursive_mutex> lock(DBIfaceTransaction::writeMutex_);

   //set the lambda to unlock the control encryption key
   decryptedData_->setPassphrasePromptLambda(passLbd);

   //erase the passphrase
   auto& masterKeyId = decryptedData_->getMasterEncryptionKeyId();
   auto& kdfId = decryptedData_->getDefaultKdfId();
   decryptedData_->eraseEncryptionKey(masterKeyId, kdfId);

   //clear the lambda
   decryptedData_->resetPassphraseLambda();

   //wipe the db
   compactFile();
}

////////////////////////////////////////////////////////////////////////////////
void WalletDBInterface::compactFile()
{
   /*
   To wipe this file of its deleted entries, we perform a LMDB compact copy
   of the dbEnv, which will skip free/loose data pages and only copy the
   currently valid data in the db. We then swap files and delete the
   original.
   */

   //lock the write mutex before alterning the underlying file
   unique_lock<recursive_mutex> lock(DBIfaceTransaction::writeMutex_);

   //create copy name
   auto fullDbPath = getFilename();
   auto basePath = DBUtils::getBaseDir(fullDbPath);

   auto swapFolder = basePath;
   DBUtils::appendPath(swapFolder, string(COMPACT_FILE_FOLDER));
   if (!DBUtils::fileExists(swapFolder, 0))
   {
      #ifdef _WIN32
      if (mkdir(swapFolder) != 0)
         throw WalletInterfaceException("could not create wallet swap folder");
      #else
      if (mkdir(swapFolder.c_str(), S_IWUSR | S_IRUSR | S_IXUSR) != 0)
         throw WalletInterfaceException("could not create wallet swap folder");
      #endif
   }

   string copyName;
   while (true)
   {
      stringstream ss;
      ss << COMPACT_FILE_COPY_NAME << "-" << fortuna_->generateRandom(16).toHexStr();
      auto fullpath = swapFolder;
      DBUtils::appendPath(fullpath, ss.str());
      
      if (!DBUtils::fileExists(fullpath, 0))
      {
         copyName = fullpath;
         break;
      }
   }

   //copy
   dbEnv_->compactCopy(copyName);

   //close current env
   closeEnv();

   //swap files
   string swapPath;


   while (true)
   {
      stringstream ss;
      ss << COMPACT_FILE_SWAP_NAME << "-" << fortuna_->generateRandom(16).toHexStr();
      auto fullpath = swapFolder;
      DBUtils::appendPath(fullpath, ss.str());

      if (DBUtils::fileExists(fullpath, 0))
         continue;

      swapPath = fullpath;

      //rename old file to swap
      if (rename(fullDbPath.c_str(), swapPath.c_str()) != 0)
      {
         throw WalletInterfaceException(
            "failed to swap file during wipe operation");
      }

      //rename new file to old
      if (rename(copyName.c_str(), fullDbPath.c_str()) != 0)
      {
         throw WalletInterfaceException(
            "failed to swap file during wipe operation");
      }

      break;
   }

   //reset dbEnv to new file
   openEnv();

   //wipe old file
   wipeAndDeleteFile(swapPath);

   //TODO: lock sharing rights on wallet files
}

////////////////////////////////////////////////////////////////////////////////
void WalletDBInterface::wipeAndDeleteFile(const string& path)
{
   if (path.empty())
      return;

   {
      auto fileMap = DBUtils::getMmapOfFile(path, true);
      memset(fileMap.filePtr_, 0, fileMap.size_);
      fileMap.unmap();
   }

   int unlinkResult;
#ifdef _WIN32
   unlinkResult = _unlink(path.c_str());
#else
   unlinkResult = unlink(path.c_str());
#endif

   if (unlinkResult != 0)
   {
      throw WalletInterfaceException(
         "failed to delete file during wipe operation");
   }
}

////////////////////////////////////////////////////////////////////////////////
void WalletDBInterface::eraseFromDisk()
{
   auto pathCopy = path_;
   shutdown();

   wipeAndDeleteFile(pathCopy);
}

////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////
//// WalletIfaceIterator
////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////
bool WalletIfaceIterator::isValid() const
{
   return iterator_ != txPtr_->dataMapPtr_->dataMap_.end();
}

////////////////////////////////////////////////////////////////////////////////
void WalletIfaceIterator::seek(const BinaryDataRef& key)
{
   iterator_ = txPtr_->dataMapPtr_->dataMap_.lower_bound(key);
}

////////////////////////////////////////////////////////////////////////////////
void WalletIfaceIterator::advance()
{
   ++iterator_;
}

////////////////////////////////////////////////////////////////////////////////
BinaryDataRef WalletIfaceIterator::key() const
{
   return iterator_->first.getRef();
}

////////////////////////////////////////////////////////////////////////////////
BinaryDataRef WalletIfaceIterator::value() const
{
   return iterator_->second.getRef();
}

////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////
//// WalletIfaceTransaction
////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////

////////////////////////////////////////////////////////////////////////////////
WalletIfaceTransaction::WalletIfaceTransaction(
   WalletDBInterface* ifacePtr, DBInterface* dbPtr, bool mode) :
   DBIfaceTransaction(), ifacePtr_(ifacePtr), dbPtr_(dbPtr), commit_(mode)
{
   if (!insertTx(this))
      throw WalletInterfaceException("failed to create db tx");
}

////////////////////////////////////////////////////////////////////////////////
WalletIfaceTransaction::~WalletIfaceTransaction() noexcept(false)
{
   closeTx();
}

////////////////////////////////////////////////////////////////////////////////
const std::string& WalletIfaceTransaction::getDbName() const
{
   return dbPtr_->getName();
}

////////////////////////////////////////////////////////////////////////////////
void WalletIfaceTransaction::closeTx()
{
   unique_ptr<LMDBEnv::Transaction> tx;
   unique_ptr<unique_lock<recursive_mutex>> writeTxLock = nullptr;

   {
      auto lock = unique_lock<mutex>(txMutex_);
      writeTxLock = move(eraseTx(this));
         
      if (writeTxLock == nullptr || !commit_)
         return;

      tx = make_unique<LMDBEnv::Transaction>(dbPtr_->dbEnv_, LMDB::ReadWrite);
   }

   auto dataMapCopy = make_shared<IfaceDataMap>(*dataMapPtr_);
   bool needsWiped = false;

   //this is the top tx, need to commit all this data to the db object
   for (unsigned i=0; i < insertVec_.size(); i++)
   {
      auto dataPtr = insertVec_[i];

      //is this operation is the last for this data key?
      auto effectIter = keyToDataMap_.find(dataPtr->key_);
      if (effectIter == keyToDataMap_.end())
      {   
         throw WalletInterfaceException(
            "insert operation is not mapped to data key!");
      }

      //skip if this isn't the last effect
      if (i != effectIter->second)
         continue;

      BinaryData dbKey;
      auto keyExists = dataMapCopy->resolveDataKey(dataPtr->key_, dbKey);
      if (keyExists)
      {
         //erase the key
         CharacterArrayRef carKey(dbKey.getSize(), dbKey.getPtr());
         dbPtr_->db_.erase(carKey);
         needsWiped = true;

         //create erasure place holder packet
         BinaryWriter erasedBw;
         erasedBw.put_String("erased");
         erasedBw.put_var_int(dbKey.getSize());
         erasedBw.put_BinaryData(dbKey);

         //get new key
         dbKey = dataMapCopy->getNewDbKey();

         //commit erasure packet
         auto&& dbVal = DBInterface::createDataPacket(
            dbKey, BinaryData(), erasedBw.getData(), 
            dbPtr_->encrPubKey_, dbPtr_->macKey_, dbPtr_->encrVersion_);

         CharacterArrayRef carData(dbVal.getSize(), dbVal.getPtr());
         CharacterArrayRef carKey2(dbKey.getSize(), dbKey.getPtr());
         dbPtr_->db_.insert(carKey2, carData);

         //move on to next piece of data if there is nothing to write
         if (!dataPtr->write_)
         {
            //update dataKeyToDbKey
            dataMapCopy->dataKeyToDbKey_.erase(dataPtr->key_);
            continue;
         }

         //grab a fresh key for the follow up write
         dbKey = dataMapCopy->getNewDbKey();
      }

      //sanity check
      if (!dataPtr->write_)
         throw WalletInterfaceException("key marked for deletion when it does not exist");

      //update dataKeyToDbKey
      dataMapCopy->dataKeyToDbKey_[dataPtr->key_] = dbKey;

      //bundle key and val together, key by dbkey
      auto&& dbVal = DBInterface::createDataPacket(
         dbKey, dataPtr->key_, dataPtr->value_, 
         dbPtr_->encrPubKey_, dbPtr_->macKey_, dbPtr_->encrVersion_);
      CharacterArrayRef carKey(dbKey.getSize(), dbKey.getPtr());
      CharacterArrayRef carVal(dbVal.getSize(), dbVal.getPtr());

      dbPtr_->db_.insert(carKey, carVal);
   }

   //update db data map
   dataMapCopy->update(insertVec_);

   //swap in the data struct
   atomic_store_explicit(
      &dbPtr_->dataMapPtr_, dataMapCopy, memory_order_release);

   if (!needsWiped)
      return;

   if (ifacePtr_ == nullptr)
      return;

   //close the write tx, we still hold the write mutex
   tx.reset();

   //wipe deleted entries from file
   ifacePtr_->compactFile();
}

////////////////////////////////////////////////////////////////////////////////
bool WalletIfaceTransaction::insertTx(WalletIfaceTransaction* txPtr)
{
   if (txPtr == nullptr)
      throw WalletInterfaceException("null tx ptr");

   auto lock = unique_lock<mutex>(txMutex_);

   auto dbIter = dbMap_.find(txPtr->dbPtr_->getName());
   if (dbIter == dbMap_.end())
   {
      auto structPtr = make_shared<DbTxStruct>();
      dbIter = dbMap_.insert(make_pair(
         txPtr->dbPtr_->getName(), structPtr)).first;
   }

   auto& txStruct = dbIter->second;
   auto& txMap = txStruct->txMap_;

   //save tx by thread id
   auto thrId = this_thread::get_id();
   auto iter = txMap.find(thrId);
   if (iter != txMap.end())
   {
      /*we already have a tx for this thread, we will nest the new one within it*/
      
      //make sure the commit type between parent and nested tx match
      if (iter->second->commit_ != txPtr->commit_)
         return false;

      //set lambdas
      txPtr->insertLbd_ = iter->second->insertLbd_;
      txPtr->eraseLbd_ = iter->second->eraseLbd_;
      txPtr->getDataLbd_ = iter->second->getDataLbd_;
      txPtr->dataMapPtr_ = iter->second->dataMapPtr_;

      //increment counter
      ++txStruct->txCount_;
      ++iter->second->counter_;
      return true;
   }

   //this is the parent tx, create the lambdas and setup the struct
   auto ptx = make_shared<ParentTx>();
   ptx->commit_ = txPtr->commit_;
      
   txMap.insert(make_pair(thrId, ptx));
   ++txStruct->txCount_;

   //release the dbMap lock
   lock.unlock();

   if (txPtr->commit_)
   {
      //write tx, lock db write mutex
      ptx->writeLock_ = make_unique<unique_lock<recursive_mutex>>(writeMutex_);

      auto insertLbd = [thrId, txPtr](const BinaryData& key, BothBinaryDatas& val)
      {
         if (thrId != this_thread::get_id())
            throw WalletInterfaceException("insert operation thread id mismatch");

         auto dataPtr = make_shared<InsertData>();
         dataPtr->key_ = key;
         dataPtr->value_ = move(val);

         unsigned vecSize = txPtr->insertVec_.size();
         txPtr->insertVec_.emplace_back(dataPtr);

         /*
         Insert the index for this data object in the key map.
         Replace the index if it's already there as we want to track
         the final effect for each key.
         */
         auto insertPair = txPtr->keyToDataMap_.insert(make_pair(key, vecSize));
         if (!insertPair.second)
            insertPair.first->second = vecSize;
      };

      auto eraseLbd = [thrId, txPtr](const BinaryData& key)
      {
         if (thrId != this_thread::get_id())
            throw WalletInterfaceException("insert operation thread id mismatch");

         auto dataPtr = make_shared<InsertData>();
         dataPtr->key_ = key;
         dataPtr->write_ = false; //set to false to signal deletion

         unsigned vecSize = txPtr->insertVec_.size();
         txPtr->insertVec_.emplace_back(dataPtr);

         auto insertPair = txPtr->keyToDataMap_.insert(make_pair(key, vecSize));
         if (!insertPair.second)
            insertPair.first->second = vecSize;
      };

      auto getDataLbd = [thrId, txPtr](const BinaryData& key)->
         const shared_ptr<InsertData>&
      {
         auto iter = txPtr->keyToDataMap_.find(key);
         if (iter == txPtr->keyToDataMap_.end())
            throw NoDataInDB();

         return txPtr->insertVec_[iter->second];
      };

      txPtr->insertLbd_ = insertLbd;
      txPtr->eraseLbd_ = eraseLbd;
      txPtr->getDataLbd_ = getDataLbd;

      ptx->insertLbd_ = insertLbd;
      ptx->eraseLbd_ = eraseLbd;
      ptx->getDataLbd_ = getDataLbd;
   }

   ptx->dataMapPtr_ = atomic_load_explicit(
      &txPtr->dbPtr_->dataMapPtr_, memory_order_acquire);
   txPtr->dataMapPtr_ = ptx->dataMapPtr_;
   
   return true;
}

////////////////////////////////////////////////////////////////////////////////
unique_ptr<unique_lock<recursive_mutex>> WalletIfaceTransaction::eraseTx(
   WalletIfaceTransaction* txPtr)
{
   if (txPtr == nullptr)
      throw WalletInterfaceException("null tx ptr");
   
   //we should have this db name in the tx map
   auto dbIter = dbMap_.find(txPtr->dbPtr_->getName());
   if (dbIter == dbMap_.end())
      throw WalletInterfaceException("missing db name in tx map");

   auto& txStruct = dbIter->second;
   auto& txMap = txStruct->txMap_;

   //thread id has to be present too
   auto thrId = this_thread::get_id();
   auto iter = txMap.find(thrId);
   if (iter == txMap.end())
      throw WalletInterfaceException("missing thread id in tx map");

   --txStruct->txCount_;
   if (iter->second->counter_ > 1)
   {
      //this is a nested tx, decrement and return false
      --iter->second->counter_;
      return nullptr;
   }

   //counter is 1, this is the parent tx, clean up the entry and return true
   auto lockPtr = move(iter->second->writeLock_);
   txMap.erase(iter);
   return lockPtr;
}

////////////////////////////////////////////////////////////////////////////////
void WalletIfaceTransaction::insert(const BinaryData& key, BinaryData& val)
{
   if (!insertLbd_)
      throw WalletInterfaceException("insert lambda is not set");

   BothBinaryDatas bbdVal(val);
   insertLbd_(key, bbdVal);
}

////////////////////////////////////////////////////////////////////////////////
void WalletIfaceTransaction::insert(
   const BinaryData& key, const BinaryData& val)
{
   if (!insertLbd_)
      throw WalletInterfaceException("insert lambda is not set");

   BothBinaryDatas bbdVal(val);
   insertLbd_(key, bbdVal);
}

////////////////////////////////////////////////////////////////////////////////
void WalletIfaceTransaction::insert(
   const BinaryData& key, SecureBinaryData& val)
{
   if (!insertLbd_)
      throw WalletInterfaceException("insert lambda is not set");

   BothBinaryDatas bbdVal(val);
   insertLbd_(key, bbdVal);
}

////////////////////////////////////////////////////////////////////////////////
void WalletIfaceTransaction::erase(const BinaryData& key)
{
   if (!eraseLbd_)
      throw WalletInterfaceException("erase lambda is not set");

   eraseLbd_(key);
}

////////////////////////////////////////////////////////////////////////////////
std::shared_ptr<DBIfaceIterator> WalletIfaceTransaction::getIterator() const
{
   if (commit_)
      throw WalletInterfaceException("cannot iterate over a write transaction");

   return make_shared<WalletIfaceIterator>(this);
}

////////////////////////////////////////////////////////////////////////////////
const BinaryDataRef WalletIfaceTransaction::getDataRef(
   const BinaryData& key) const
{
   if (commit_)
   {
      /*
      A write transaction may carry data that overwrites the db object data map.
      Check the modification map first.
      */

      try
      {
         auto& dataPtr = getInsertDataForKey(key);
         if (!dataPtr->write_)
            return BinaryDataRef();

         return dataPtr->value_.getRef();
      }
      catch (NoDataInDB&)
      {
         /*
         Will throw if there's no data in the write tx.
         Look for it in the db instead.
         */
      }
   }

   auto iter = dataMapPtr_->dataMap_.find(key);
   if (iter == dataMapPtr_->dataMap_.end())
      return BinaryDataRef();
   return iter->second.getRef();
}

////////////////////////////////////////////////////////////////////////////////
const std::shared_ptr<InsertData>& WalletIfaceTransaction::getInsertDataForKey(
   const BinaryData& key) const
{
   if (!getDataLbd_)
      throw WalletInterfaceException("tx is missing get lbd");

   return getDataLbd_(key);
}