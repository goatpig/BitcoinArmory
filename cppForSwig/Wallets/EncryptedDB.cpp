////////////////////////////////////////////////////////////////////////////////
//                                                                            //
//  Copyright (C) 2019-2021, goatpig                                          //
//  Distributed under the MIT license                                         //
//  See LICENSE-MIT or https://opensource.org/licenses/MIT                    //
//                                                                            //
////////////////////////////////////////////////////////////////////////////////

#include "Utils/BtcUtils.h"
#include "Utils/Cryptography.h"
#include "EncryptedDB.h"
#include "AssetEncryption.h"

using namespace std;
using namespace Armory::Wallets::IO;

////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////
//// IfaceDataMap
////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////
void IfaceDataMap::update(const std::vector<std::shared_ptr<InsertData>>& vec)
{
   for (auto& dataPtr : vec) {
      if (!dataPtr->write_) {
         dataMap_.erase(dataPtr->key_);
         continue;
      }

      auto insertIter = dataMap_.emplace(dataPtr->key_, dataPtr->value_);
      if (!insertIter.second) {
         insertIter.first->second = dataPtr->value_;
      }
   }
}

////////////////////////////////////////////////////////////////////////////////
bool IfaceDataMap::resolveDataKey(const BinaryData& dataKey,
   BinaryData& dbKey)
{
   /*
   Return the dbKey for the data key if it exists, otherwise increment the
   dbKeyCounter and construct a key from that.
   */

   auto iter = dataKeyToDbKey_.find(dataKey);
   if (iter != dataKeyToDbKey_.end())
   {
      dbKey = iter->second;
      return true;
   }

   dbKey = getNewDbKey();
   return false;
}

////////////////////////////////////////////////////////////////////////////////
BinaryData IfaceDataMap::getNewDbKey()
{
   auto dbKeyUint = dbKeyCounter_++;
   return WRITE_UINT32_BE(dbKeyUint);
}

////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////
//// DBInterface
////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////
const BinaryData DBInterface::erasurePlaceHolder_ =
   BinaryData::fromString(ERASURE_PLACE_HOLDER);

const BinaryData DBInterface::keyCycleFlag_ =
   BinaryData::fromString(KEY_CYCLE_FLAG);

////////////////////////////////////////////////////////////////////////////////
DBInterface::DBInterface(
   LMDBEnv* dbEnv, const std::string& dbName,
   const SecureBinaryData& controlSalt, unsigned encrVersion) :
   dbEnv_(dbEnv), dbName_(dbName), controlSalt_(controlSalt),
   encrVersion_(encrVersion)
{
   auto tx = LMDBEnv::Transaction(dbEnv_, LMDB::Mode::ReadWrite);
   db_.open(dbEnv_, dbName_);
   dataMapPtr_.store(std::make_shared<IfaceDataMap>());
}

////////////////////////////////////////////////////////////////////////////////
DBInterface::~DBInterface()
{
   db_.close();
}

////////////////////////////////////////////////////////////////////////////////
void DBInterface::reset(LMDBEnv* envPtr)
{
   if (db_.isOpen()) {
      db_.close();
   }
   dbEnv_ = envPtr;
   auto tx = LMDBEnv::Transaction(dbEnv_, LMDB::Mode::ReadWrite);
   db_.open(dbEnv_, dbName_);
}

////////////////////////////////////////////////////////////////////////////////
void DBInterface::loadAllEntries(const SecureBinaryData& rootKey)
{
   //to keep track of dbkey gaps
   set<unsigned> gaps;
   SecureBinaryData decrPrivKey;
   SecureBinaryData macKey;

   auto&& saltedRoot = BtcUtils::getHMAC256(controlSalt_, rootKey);

   //key derivation method
   auto computeKeyPair = [&saltedRoot, &decrPrivKey, &macKey](unsigned hmacKeyInt)
   {
      SecureBinaryData hmacKey((uint8_t*)&hmacKeyInt, 4);
      auto hmacVal = BtcUtils::getHMAC512(hmacKey, saltedRoot);

      //first half is the encryption key, second half is the hmac key
      BinaryRefReader brr(hmacVal.getRef());
      decrPrivKey = SecureBinaryData{brr.get_BinaryDataRef(32)};
      macKey = SecureBinaryData{brr.get_BinaryDataRef(32)};

      //decryption private key sanity check
      if (!Cryptography::ECDSA::checkPrivKeyIsValid(decrPrivKey))
         throw EncryptedDBException("invalid decryption private key");
   };

   //init first decryption key pair
   unsigned decrKeyCounter = 0;
   computeKeyPair(decrKeyCounter);

   //meta data handling lbd
   auto processMetaDataPacket = [&gaps, &computeKeyPair, &decrKeyCounter]
   (const BothBinaryDatas& packet)->bool
   {
      if (packet.getSize() > erasurePlaceHolder_.getSize())
      {
         BinaryRefReader brr(packet.getRef());
         auto placeHolder =
            brr.get_BinaryDataRef(erasurePlaceHolder_.getSize());

         if (placeHolder == erasurePlaceHolder_)
         {
            auto len = brr.get_var_int();
            if (len == 4)
            {
               auto key = brr.get_BinaryData(4);
               auto gapInt = READ_UINT32_BE(key);

               auto gapIter = gaps.find(gapInt);
               if (gapIter == gaps.end())
               {
                  throw EncryptedDBException(
                     "erasure place holder for missing gap");
               }

               gaps.erase(gapIter);
               return true;
            }
         }
      }

      if (packet.getRef() == keyCycleFlag_.getRef())
      {
         //cycle key
         ++decrKeyCounter;
         computeKeyPair(decrKeyCounter);
         return true;
      }

      return false;
   };

   /*****/

   {
      //setup transactional data struct
      auto dataMapPtr = make_shared<IfaceDataMap>();

      //read all db entries
      auto tx = LMDBEnv::Transaction(dbEnv_, LMDB::Mode::ReadOnly);

      int prevDbKey = -1;
      auto iter = db_.begin();
      while (iter.isValid())
      {
         auto key_mval = iter.key();
         if (key_mval.mv_size != 4)
            throw EncryptedDBException("invalid dbkey");

         auto val_mval = iter.value();

         BinaryDataRef key_bdr((const uint8_t*)key_mval.mv_data, key_mval.mv_size);
         BinaryDataRef val_bdr((const uint8_t*)val_mval.mv_data, val_mval.mv_size);

         //dbkeys should be consecutive integers, mark gaps
         uint32_t dbKeyUint = READ_UINT32_BE(key_bdr);
         if (dbKeyUint >= 0x10000000U)
         {
            // dbKey can unlikely be >2^31, so this looks like
            // data corruption
            throw EncryptedDBException("invalid dbkey");
         }

         auto dbKeyInt = (int32_t)dbKeyUint;
         if (dbKeyInt - prevDbKey != 1)
         {
            for (int i = prevDbKey + 1; i < dbKeyInt; i++)
               gaps.insert(i);
         }

         //set lowest seen integer key
         prevDbKey = dbKeyInt;

         //grab the data
         auto dataPair = readDataPacket(
            key_bdr, val_bdr, decrPrivKey, macKey, encrVersion_);

         /*
         Check if packet is meta data.
         Meta data entries have an empty data key.
         */
         if (dataPair.first.getSize() == 0)
         {
            if (!processMetaDataPacket(dataPair.second))
               throw EncryptedDBException("empty data key");

            iter.advance();
            continue;
         }

         auto&& keyPair = make_pair(dataPair.first, move(key_bdr.copy()));
         auto insertIter = dataMapPtr->dataKeyToDbKey_.emplace(keyPair);
         if (!insertIter.second)
            throw EncryptedDBException("duplicated db entry");

         dataMapPtr->dataMap_.emplace(dataPair);
         iter.advance();
      }

      //sanity check
      if (gaps.size() != 0) {
         throw EncryptedDBException("unfilled dbkey gaps!");
      }

      //set dbkey counter
      dataMapPtr->dbKeyCounter_ = prevDbKey + 1;

      //set the data map
      dataMapPtr_.store(dataMapPtr, memory_order_release);
   }

   {
      /*
      Append a key cycling flag to the this DB. All data written during
      this session will use the next key in line. This flag will signify
      the next wallet load to cycle the key accordingly to decrypt this
      new data correctly.
      */
      auto tx = LMDBEnv::Transaction(dbEnv_, LMDB::Mode::ReadWrite);

      auto dataMapPtr = dataMapPtr_.load(memory_order_acquire);
      auto flagKey = dataMapPtr->getNewDbKey();
      BothBinaryDatas keyFlagBd(keyCycleFlag_);
      auto encrPubKey = Cryptography::ECDSA::computePublicKey(
         decrPrivKey, true);
      auto flagPacket = createDataPacket(flagKey, BinaryData(),
         keyFlagBd, encrPubKey, macKey, encrVersion_);

      CharacterArrayRef carKey(flagKey.getSize(), flagKey.getPtr());
      CharacterArrayRef carVal(flagPacket.getSize(), flagPacket.getPtr());
      db_.insert(carKey, carVal);
   }

   //cycle to next key for this session
   ++decrKeyCounter;
   computeKeyPair(decrKeyCounter);

   //set mac key for the current session
   encrPubKey_ = Cryptography::ECDSA::computePublicKey(decrPrivKey, true);
   macKey_ = move(macKey);
}

////////////////////////////////////////////////////////////////////////////////
BinaryData DBInterface::createDataPacket(const BinaryData& dbKey,
   const BinaryData& dataKey, const BothBinaryDatas& dataVal,
   const SecureBinaryData& encrPubKey, const SecureBinaryData& macKey,
   unsigned encrVersion)
{
   BinaryWriter encrPacket;

   switch (encrVersion)
   {
   case 0x00000001:
   {
   /* authentitcation leg */
      //concatenate dataKey and dataVal to create payload
      BinaryWriter bw;
      bw.put_var_int(dataKey.getSize());
      bw.put_BinaryData(dataKey);
      bw.put_var_int(dataVal.getSize());
      bw.put_BinaryDataRef(dataVal.getRef());

      //append dbKey to payload
      BinaryWriter bwHmac;
      bwHmac.put_BinaryData(bw.getData());
      bwHmac.put_BinaryData(dbKey);

      //hmac (payload | dbKey)
      auto&& hmac = BtcUtils::getHMAC256(macKey, bwHmac.getData());

      //append payload to hmac
      BinaryWriter bwData;
      bwData.put_BinaryData(hmac);
      bwData.put_BinaryData(bw.getData());

      //pad payload to modulo blocksize

   /* encryption key generation */
      //generate local encryption private key
      auto localPrivKey = Cryptography::ECDSA::createNewPrivateKey();

      //generate compressed pubkey
      auto localPubKey = Cryptography::ECDSA::computePublicKey(
         localPrivKey, true);

      //ECDH local private key with encryption public key
      auto ecdhPubKey = Cryptography::ECDSA::pubKeyScalarMultiply(
         encrPubKey, localPrivKey);

      //hash256 the key as stand in for KDF
      auto encrKey = BtcUtils::getHash256(ecdhPubKey);

   /* encryption leg */
      //generate IV
      auto iv = Cryptography::PRNG::fortuna.generateRandom(
         Encryption::Cipher::getBlockSize(CipherType_AES));

      //AES_CBC (hmac | payload)
      auto cipherText = Cryptography::Encryption::AES::encryptCBC(
         bwData.getDataRef(), {encrKey}, iv);

      //build IES packet
      encrPacket.put_BinaryData(localPubKey);
      encrPacket.put_BinaryData(iv);
      encrPacket.put_BinaryData(cipherText);

      break;
   }

   default:
      throw EncryptedDBException("unsupported encryption version");
   }

   return encrPacket.getData();
}

////////////////////////////////////////////////////////////////////////////////
pair<BinaryData, BothBinaryDatas> DBInterface::readDataPacket(
   const BinaryData& dbKey, const BinaryData& dataPacket,
   const SecureBinaryData& decrPrivKey, const SecureBinaryData& macKey,
   unsigned encrVersion)
{
   BinaryData dataKey;
   BothBinaryDatas dataVal;

   switch (encrVersion)
   {
   case 0x00000001:
   {
   /* decryption key */
      //recover public key
      BinaryRefReader brrCipher(dataPacket.getRef());

      //public key
      SecureBinaryData localPubKey{brrCipher.get_BinaryDataRef(33)};

      //ECDH with decryption private key
      auto ecdhPubKey = Cryptography::ECDSA::pubKeyScalarMultiply(
         localPubKey, decrPrivKey);

      //kdf
      auto decrKey = BtcUtils::getHash256(ecdhPubKey);

   /* decryption leg */
      //get iv
      SecureBinaryData iv{brrCipher.get_BinaryDataRef(
         Encryption::Cipher::getBlockSize(CipherType_AES))};

      //get cipher text
      SecureBinaryData cipherText{brrCipher.get_BinaryDataRef(
         brrCipher.getSizeRemaining())};

      //decrypt
      auto plainText = Cryptography::Encryption::AES::decryptCBC(
         cipherText, {decrKey}, iv);

   /* authentication leg */
      BinaryRefReader brrPlain(plainText.getRef());

      //grab hmac
      auto hmac = brrPlain.get_BinaryData(32);

      //grab data key
      auto len = brrPlain.get_var_int();
      dataKey = move(brrPlain.get_BinaryData(len));

      //grab data val
      len = brrPlain.get_var_int();
      dataVal = SecureBinaryData{brrPlain.get_BinaryDataRef(len)};

      //mark the position
      auto pos = brrPlain.getPosition() - 32;

      //sanity check
      if (brrPlain.getSizeRemaining() != 0)
         throw EncryptedDBException("loose data entry");

      //reset reader & grab data packet
      brrPlain.resetPosition();
      brrPlain.advance(32);
      auto data = brrPlain.get_BinaryData(pos);

      //append db key
      data.append(dbKey);

      //compute hmac
      auto computedHmac = BtcUtils::getHMAC256(macKey, data);

      //check hmac
      if (computedHmac != hmac)
         throw EncryptedDBException("mac mismatch");

      break;
   }

   default:
      throw EncryptedDBException("unsupported encryption version");
   }

   return make_pair(dataKey, dataVal);
}

////////////////////////////////////////////////////////////////////////////////
unsigned DBInterface::getEntryCount(void) const
{
   auto dbMapPtr = dataMapPtr_.load(memory_order_acquire);
   return dbMapPtr->dataMap_.size();
}

////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////
//// DBIfaceIterator
////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////
DBIfaceIterator::~DBIfaceIterator()
{}

////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////
//// RawIfaceIterator
////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////
bool RawIfaceIterator::isValid() const
{
   return iterator_.isValid();
}

////////////////////////////////////////////////////////////////////////////////
void RawIfaceIterator::seek(const BinaryDataRef& key)
{
   CharacterArrayRef carKey(key.getSize(), key.getPtr());
   iterator_.seek(carKey, LMDB::Iterator::SeekBy::GE);
}

////////////////////////////////////////////////////////////////////////////////
void RawIfaceIterator::advance()
{
   ++iterator_;
}

////////////////////////////////////////////////////////////////////////////////
BinaryDataRef RawIfaceIterator::key() const
{
   auto val = iterator_.key();
   return BinaryDataRef((const uint8_t*)val.mv_data, val.mv_size);
}

////////////////////////////////////////////////////////////////////////////////
BinaryDataRef RawIfaceIterator::value() const
{
   auto val = iterator_.value();
   return BinaryDataRef((const uint8_t*)val.mv_data, val.mv_size);
}

////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////
//// DBIfaceTransaction
////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////
map<string, shared_ptr<DBIfaceTransaction::DbTxStruct>> 
   DBIfaceTransaction::dbMap_;

mutex DBIfaceTransaction::txMutex_;
recursive_mutex DBIfaceTransaction::writeMutex_;

////////////////////////////////////////////////////////////////////////////////
DBIfaceTransaction::~DBIfaceTransaction() noexcept(false)
{}

////////////////////////////////////////////////////////////////////////////////
bool DBIfaceTransaction::hasTx()
{
   auto lock = unique_lock<mutex>(txMutex_);
   for (auto& dbPair : dbMap_)
   {
      if (dbPair.second->txCount() > 0)
         return true;
   }

   return false;
}

////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////
//// RawIfaceTransaction
////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////
void RawIfaceTransaction::insert(const BinaryData& key, BinaryData& val)
{
   CharacterArrayRef carKey(key.getSize(), key.getPtr());
   CharacterArrayRef carVal(val.getSize(), val.getPtr());
   dbPtr_->insert(carKey, carVal);
}

////////////////////////////////////////////////////////////////////////////////
void RawIfaceTransaction::insert(const BinaryData& key, const BinaryData& val)
{
   CharacterArrayRef carKey(key.getSize(), key.getPtr());
   CharacterArrayRef carVal(val.getSize(), val.getPtr());
   dbPtr_->insert(carKey, carVal);
}

////////////////////////////////////////////////////////////////////////////////
void RawIfaceTransaction::insert(const BinaryData& key, SecureBinaryData& val)
{
   CharacterArrayRef carKey(key.getSize(), key.getPtr());
   CharacterArrayRef carVal(val.getSize(), val.getPtr());
   dbPtr_->insert(carKey, carVal);
}

////////////////////////////////////////////////////////////////////////////////
void RawIfaceTransaction::erase(const BinaryData& key)
{
   CharacterArrayRef carKey(key.getSize(), key.getPtr());
   dbPtr_->erase(carKey);
}

////////////////////////////////////////////////////////////////////////////////
const BinaryDataRef RawIfaceTransaction::getDataRef(const BinaryData& key) const
{
   CharacterArrayRef carKey(key.getSize(), key.getPtr());
   auto&& carVal = dbPtr_->get_NoCopy(carKey);

   if (carVal.len == 0)
      return BinaryDataRef();

   BinaryDataRef result((const uint8_t*)carVal.data, carVal.len);
   return result;
}

////////////////////////////////////////////////////////////////////////////////
shared_ptr<DBIfaceIterator> RawIfaceTransaction::getIterator() const
{
   return make_shared<RawIfaceIterator>(dbPtr_);
}
