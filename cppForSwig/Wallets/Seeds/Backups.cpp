////////////////////////////////////////////////////////////////////////////////
//                                                                            //
//  Copyright (C) 2020 - 2023, goatpig                                        //
//  Distributed under the MIT license                                         //
//  See LICENSE-MIT or https://opensource.org/licenses/MIT                    //
//                                                                            //
////////////////////////////////////////////////////////////////////////////////

#include "Backups.h"
#include "EncryptionUtils.h"
#include "BtcUtils.h"
#include "../WalletIdTypes.h"
#include "Seeds.h"
#include "Wallets.h"
#include "protobuf/BridgeProto.pb.h"
extern "C" {
#include <trezor-crypto/bip39.h>
}

#define EASY16_CHECKSUM_LEN 2
#define EASY16_INDEX_MAX   15
#define EASY16_LINE_LENGTH 16

#define WALLET_RESTORE_LOOKUP 1000

using namespace std;
using namespace Armory::Seeds;
using namespace Armory::Assets;
using namespace Armory::Wallets;

////////////////////////////////////////////////////////////////////////////////
const vector<char> Easy16Codec::e16chars_ =
{
   'a', 's', 'd', 'f',
   'g', 'h', 'j', 'k',
   'w', 'e', 'r', 't',
   'u', 'i', 'o', 'n'
};

////////////////////////////////////////////////////////////////////////////////
const set<BackupType> Easy16Codec::eligibleIndexes_ =
{
   BackupType::Armory135,
   BackupType::Armory200a,
   BackupType::Armory200b,
   BackupType::Armory200c,
   BackupType::Armory200d
};

////////////////////////////////////////////////////////////////////////////////

/* - comment from etotheipi: -
Nothing up my sleeve!  Need some hardcoded random numbers to use for
encryption IV and salt.  Using the first 256 digits of Pi for the
the IV, and first 256 digits of e for the salt (hashed)
*/

const string SecurePrint::digits_pi_ =
{
   "ARMORY_ENCRYPTION_INITIALIZATION_VECTOR_"
   "1415926535897932384626433832795028841971693993751058209749445923"
   "0781640628620899862803482534211706798214808651328230664709384460"
   "9550582231725359408128481117450284102701938521105559644622948954"
   "9303819644288109756659334461284756482337867831652712019091456485"
};
   
const string SecurePrint::digits_e_ =
{
   "ARMORY_KEY_DERIVATION_FUNCTION_SALT_"
   "7182818284590452353602874713526624977572470936999595749669676277"
   "2407663035354759457138217852516642742746639193200305992181741359"
   "6629043572900334295260595630738132328627943490763233829880753195"
   "2510190115738341879307021540891499348841675092447614606680822648"
};

const uint32_t SecurePrint::kdfBytes_ = 16 * 1024 * 1024;

////////////////////////////////////////////////////////////////////////////////
////
//// Easy16Codec
////
////////////////////////////////////////////////////////////////////////////////
BinaryData Easy16Codec::getHash(const BinaryDataRef& data, uint8_t hint)
{
   if (hint == 0)
   {
      return BtcUtils::getHash256(data);
   }
   else
   {
      SecureBinaryData dataCopy(data.getSize() + 1);
      memcpy(dataCopy.getPtr(), data.getPtr(), data.getSize());
      dataCopy.getPtr()[data.getSize()] = hint;

      return BtcUtils::getHash256(dataCopy);
   }
}

////////////////////////////////////////////////////////////////////////////////
uint8_t Easy16Codec::verifyChecksum(
   const BinaryDataRef& data, const BinaryDataRef& checksum)
{
   for (const auto& indexCandidate : eligibleIndexes_)
   {
      auto hash = getHash(data, (uint8_t)indexCandidate);
      if (hash.getSliceRef(0, EASY16_CHECKSUM_LEN) == checksum)
         return (uint8_t)indexCandidate;
   }

   return EASY16_INVALID_CHECKSUM_INDEX;
}

////////////////////////////////////////////////////////////////////////////////
vector<SecureBinaryData> Easy16Codec::encode(
   const BinaryDataRef data, BackupType bType)
{
   //TODO: use index pairs for a given backup type instead (one index per line)
   uint8_t index = (uint8_t)bType;

   if (index > EASY16_INDEX_MAX)
   {
      LOGERR << "index is too large";
      throw runtime_error("index is too large");
   }

   auto encodeByte = [](char* ptr, uint8_t c)->void
   {
      uint8_t val1 = c >> 4;
      uint8_t val2 = c & 0x0F;
      ptr[0] = e16chars_[val1];
      ptr[1] = e16chars_[val2];
   };

   auto encodeValue = [&encodeByte, &index](
      const BinaryDataRef& chunk16)->SecureBinaryData
   {
      //get hash
      auto h256 = getHash(chunk16, index);
      SecureBinaryData result(46);

      //encode the chunk
      unsigned charCount = 0;
      unsigned offset = 0;
      auto ptr = chunk16.getPtr();
      for (unsigned i=0; i<chunk16.getSize(); i++)
      {
         encodeByte(result.toCharPtr() + offset, ptr[i]);
         offset += 2;
         ++charCount;

         if (charCount % 2 == 0)
         {
            result.toCharPtr()[offset] = ' ';
            ++offset;
         }

         if (charCount % 8 == 0)
         {
            result.toCharPtr()[offset] = ' ';
            ++offset;
         }
      }

      //append first 2 bytes of the hash as its checksum
      auto hashPtr = h256.getPtr();
      for (unsigned i = 0; i < EASY16_CHECKSUM_LEN; i++)
      {
         encodeByte(result.toCharPtr() + offset, hashPtr[i]);
         offset += 2;
      }

      return result;
   };

   BinaryRefReader brr(data);
   uint32_t count = (data.getSize() + EASY16_LINE_LENGTH - 1) /
      EASY16_LINE_LENGTH;
   vector<SecureBinaryData> result;
   result.reserve(count);

   for (unsigned i=0; i<count; i++)
   {
      size_t len =
         std::min(size_t(EASY16_LINE_LENGTH), brr.getSizeRemaining());
      auto chunk = brr.get_BinaryDataRef(len);
      result.emplace_back(encodeValue(chunk));
   }
   return result;
}

////////////////////////////////////////////////////////////////////////////////
BackupEasy16DecodeResult Easy16Codec::decode(
   const vector<SecureBinaryData>& lines)
{
   vector<BinaryDataRef> refVec;
   refVec.reserve(lines.size());
   for (const auto& line : lines)
      refVec.emplace_back(line.getRef());

   return decode(refVec);
}

////
BackupEasy16DecodeResult Easy16Codec::decode(const vector<BinaryDataRef>& lines)
{
   if (lines.size() == 0)
      throw runtime_error("empty easy16 code");

   //setup character to value lookup map
   map<char, uint8_t> easy16Vals;
   for (unsigned i=0; i<e16chars_.size(); i++)
      easy16Vals.emplace(e16chars_[i], i);

   auto isSpace = [](const char* str)->bool
   {
      return (*str == ' ');
   };

   auto decodeCharacters = [&easy16Vals](
      uint8_t& result, const char* str)->void
   {
      //convert characters to value, ignore effect of invalid ones
      result = 0;
      auto iter1 = easy16Vals.find(str[0]);
      if (iter1 != easy16Vals.end())
         result = iter1->second << 4;

      auto iter2 = easy16Vals.find(str[1]);
      if (iter2 != easy16Vals.end())
         result += iter2->second;
   };

   /*
   Converts line to binary, appends into result.
   Returns the hash index matching the checksum.
   
   Error values:
    . -1: checksum mismatch
    . -2: invalid checksum data
    . -3: not enough room in  the result buffer
   */
   auto decodeLine = [&isSpace, &decodeCharacters](
      uint8_t* result, size_t& len,
      const BinaryDataRef& line, BinaryData& checksum)->int
   {
      auto maxlen = len;
      len = 0;
      auto ptr = line.toCharPtr();

      unsigned i=0;
      for (; i<line.getSize() - (EASY16_CHECKSUM_LEN * 2); i++)
      {
         //skip spaces
         if (isSpace(ptr + i))
            continue;

         if (len >= maxlen)
            return -3;

         decodeCharacters(result[len], ptr + i);

         //increment result length
         ++len;

         //increment i to skip 2 characters
         ++i;
      }

      //grab checksum
      checksum.resize(EASY16_CHECKSUM_LEN);
      uint8_t* checksumPtr = checksum.getPtr();
      size_t checksumLen = 0;
      for (; i<line.getSize(); i++)
      {
         //skip spaces
         if (isSpace(ptr + i))
            continue;

         if (checksumLen >= EASY16_CHECKSUM_LEN)
            return -2;
         
         decodeCharacters(*(checksumPtr + checksumLen), ptr + i);
         ++checksumLen;
         ++i;
      }

      if (checksumLen != EASY16_CHECKSUM_LEN)
         return -2;

      //hash data
      BinaryDataRef decodedChunk(result, len);
      return verifyChecksum(decodedChunk, checksum);
   };

   size_t fullSize = lines.size() * EASY16_LINE_LENGTH;
   SecureBinaryData data(fullSize);
   vector<int> checksumIndexes;
   vector<BinaryData> checksums(lines.size());

   auto dataPtr = data.getPtr();
   size_t pos = 0;
   for (unsigned i=0; i<lines.size(); i++)
   {
      const auto& line = lines[i];
      size_t len = fullSize - pos;
      auto result = decodeLine(dataPtr + pos, len, line, checksums[i]);

      pos += len;

      switch (result)
      {
      case -1: //could not match checksum
      case -2: //invalid checksum length
      {
         checksumIndexes.push_back(result);
         break;
      }

      case -3:
      {
         //ran out of space in result buffer
         throw runtime_error("easy16 decode buffer is too short");
      }

      default:
         //valid checksum
         checksumIndexes.push_back(result);
      }

      if (len > EASY16_LINE_LENGTH)
      {
         throw runtime_error("easy16 line is too long");
      }
      else if (len < EASY16_LINE_LENGTH)
      {
         if (i != lines.size() - 1)
            throw runtime_error("easy16 line is too short");

         //last line doesn't have to be EASY16_LINE_LENGTH bytes long
         data.resize(pos);
      }
   }

   BackupEasy16DecodeResult result;
   result.checksumIndexes_ = move(checksumIndexes);
   result.checksums_ = move(checksums);
   result.data_ = move(data);
   return result;
}

////////////////////////////////////////////////////////////////////////////////
bool Easy16Codec::repair(BackupEasy16DecodeResult& faultyBackup)
{
   //sanity check
   if (faultyBackup.data_.empty() || faultyBackup.checksums_.empty() ||
      faultyBackup.checksums_.size() != faultyBackup.checksumIndexes_.size())
   {
      throw Easy16RepairError("invalid arugments");
   }

   //is there an error?
   bool hasError = false;
   set<int> validIndexes;
   for (auto index : faultyBackup.checksumIndexes_)
   {
      auto indexIter = eligibleIndexes_.find((BackupType)index);
      if (indexIter == eligibleIndexes_.end())
      {
         if (index == EASY16_INVALID_CHECKSUM_INDEX)
         {
            hasError = true;
            continue;
         }
         else
         {
            //these errors cannot be repaired
            throw Easy16RepairError("fatal checksum error");
         }
      }

      validIndexes.insert(index);
   }

   if (!hasError && validIndexes.size() == 1)
      return true;

   /* checksum search function */
   auto searchChecksum = [](
      const BinaryDataRef& data, const BinaryData& checksum, uint8_t hint)
      ->map<unsigned, map<unsigned, set<uint8_t>>>
   {
      map<unsigned, map<unsigned, set<uint8_t>>> result;

      //copy the data
      SecureBinaryData copied(data);

      //run through each byte of data
      for (unsigned i=0; i<data.getSize(); i++)
      {
         auto& valRef = copied.getPtr()[i];
         auto originalValue = valRef;

         for (unsigned y=0; y<256; y++)
         {
            if (y == originalValue)
               continue;

            //set new value
            valRef = y;

            //check it
            if (hint != EASY16_INVALID_CHECKSUM_INDEX)
            {
               auto hash = getHash(copied, hint);
               if (hash.getSliceRef(0, 2) == checksum)
               {
                  auto& chkVal = result[hint];
                  auto& pos = chkVal[i];
                  pos.insert(y);
               }
            }
            else
            {
               //check all eligible indexes
               for (const auto& indexCandidate : eligibleIndexes_)
               {
                  auto hash = getHash(copied, (uint8_t)indexCandidate);
                  if (hash.getSliceRef(0, 2) == checksum)
                  {
                     auto& chkVal = result[(uint8_t)indexCandidate];
                     auto& pos = chkVal[i];
                     pos.insert(y);
                  }
               }
            }
         }

         //reset value
         valRef = originalValue;
      }

      return result;
   };


   //what kind of error? can it be repaired?
   if (validIndexes.size() > 1)
   {
      //there's more than one checksum index, cannot proceed
      throw Easy16RepairError("checksum results mismatch");
   }
   else if (validIndexes.size() == 1)
   {
      /*
      Some lines are invalid but we have at least one that is valid. This
      allows us to search for the expected checksum index in the invalid
      lines (they should all match)
      */
      unsigned hint = *validIndexes.begin();

      BinaryRefReader brr(faultyBackup.data_);
      for (unsigned i=0; i<faultyBackup.checksumIndexes_.size(); i++)
      {
         if (faultyBackup.checksumIndexes_[i] != EASY16_INVALID_CHECKSUM_INDEX)
         {
            brr.advance(
               std::min(size_t(EASY16_LINE_LENGTH), brr.getSizeRemaining()));
            faultyBackup.repairedIndexes_.push_back(hint);

            continue;
         }

         auto dataRef = brr.get_BinaryDataRef(
            std::min(size_t(EASY16_LINE_LENGTH), brr.getSizeRemaining()));

         auto repairResults = 
            searchChecksum(dataRef, faultyBackup.checksums_[i], hint);

         if (repairResults.size() != 1)
            return false;

         auto repairIter = repairResults.begin();
         if (repairIter->second.size() != 1)
            return false;

         const auto& repairPair = *repairIter->second.begin();
         if (repairPair.second.size() != 1)
            return false;

         //apply repair on the fly
         auto ptr = (uint8_t*)(dataRef.getPtr() + repairPair.first);
         *ptr = *repairPair.second.begin();

         //update the repaired line checksum result
         faultyBackup.repairedIndexes_.push_back(hint);
      }
   }
   else
   {
      /*
      All lines are invalid. There is no indication of what the checksum index
      ought to be. We have to search all lines for a matching index.
      */
      vector<map<unsigned, map<unsigned, set<uint8_t>>>> resultMap;

      BinaryRefReader brr(faultyBackup.data_);
      for (unsigned i=0; i<faultyBackup.checksumIndexes_.size(); i++)
      {
         auto dataRef = brr.get_BinaryDataRef(
            std::min(size_t(EASY16_LINE_LENGTH), brr.getSizeRemaining()));

         auto repairResults = 
            searchChecksum(dataRef, faultyBackup.checksums_[i], -1);

         if (repairResults.empty())
            return false;

         resultMap.emplace_back(move(repairResults));
      }

      //compare results for index matches
      map<unsigned, set<unsigned>> chksumIndexes;
      for (unsigned i=0; i<resultMap.size(); i++)
      {
         const auto& lineResult = resultMap[i];
         for (const auto& lineData : lineResult)
         {
            //skip on multiple solutions
            if (lineData.second.size() != 1)
               continue;

            if (lineData.second.begin()->second.size() != 1)
               continue;
            
            auto& chkValueSet = chksumIndexes[lineData.first];
            chkValueSet.insert(i);
         }
      }

      //only those indexes represented across all lines are eligible
      auto iter = chksumIndexes.begin();
      while (iter != chksumIndexes.end())
      {
         if (iter->second.size() != faultyBackup.checksumIndexes_.size())
         {
            chksumIndexes.erase(iter++);
            continue;
         }

         ++iter;
      }

      //fail if we have several repair candidates
      if (chksumIndexes.size() != 1)
         return false;

      //repair the data
      brr.resetPosition();
      auto repairIndex = chksumIndexes.begin()->first;
      for (unsigned i=0; i<faultyBackup.checksumIndexes_.size(); i++)
      {
         const auto& lineResult = resultMap[i];
         auto lineIter = lineResult.find(repairIndex);
         if (lineIter == lineResult.end())
            return false;

         //do not tolerate multiple solutions
         if (lineIter->second.size() != 1)
            return false;
         
         auto valIter = lineIter->second.begin();
         if (valIter->second.size() != 1)
            return false;

         auto dataRef = brr.get_BinaryDataRef(
            std::min(size_t(EASY16_LINE_LENGTH), brr.getSizeRemaining()));
         
         auto ptr = (uint8_t*)(dataRef.getPtr() + valIter->first);
         *ptr = *valIter->second.begin();

         //update the repaired line checksum result
         faultyBackup.repairedIndexes_.push_back(repairIndex);
      }
   }

   return true;
}

////////////////////////////////////////////////////////////////////////////////
////
//// BackupEasy16DecodeResult
////
////////////////////////////////////////////////////////////////////////////////
bool BackupEasy16DecodeResult::isInitialized() const
{
   return checksumIndexes_.size() == 2;
}

////
int BackupEasy16DecodeResult::getIndex() const
{
   if (!isInitialized())
      return -1;

   if (repairedIndexes_.size() == 2)
   {
      if (repairedIndexes_[0] == repairedIndexes_[1])
         return repairedIndexes_[0];
   }
   else
   {
      if (checksumIndexes_[0] == checksumIndexes_[1])
         return checksumIndexes_[0];
   }

   return -1;
}

bool BackupEasy16DecodeResult::isValid() const
{
   if (!isInitialized())
      return false;

   auto iter = Easy16Codec::eligibleIndexes_.find((BackupType)getIndex());
   return (iter != Easy16Codec::eligibleIndexes_.end());
}

////////////////////////////////////////////////////////////////////////////////
////
//// SecurePrint
////
////////////////////////////////////////////////////////////////////////////////
SecurePrint::SecurePrint()
{
   //setup aes IV and kdf
   auto iv32 = BtcUtils::getHash256(
      (const uint8_t*)digits_pi_.c_str(), digits_pi_.size());
   iv16_ = move(iv32.getSliceCopy(0, AES_BLOCK_SIZE));

   salt_ = move(BtcUtils::getHash256(
      (const uint8_t*)digits_e_.c_str(), digits_e_.size()));
   kdf_.usePrecomputedKdfParams(kdfBytes_, 1, salt_);
}

////////////////////////////////////////////////////////////////////////////////
pair<SecureBinaryData, SecureBinaryData> SecurePrint::encrypt(
   BinaryDataRef root, BinaryDataRef chaincode)
{
   /*
   1. generate passphrase from root and chaincode
   */

   //sanity check
   if (root.getSize() != 32)
   {
      LOGERR << "invalid root size for secureprint";
      throw runtime_error("invalid root size for secureprint");
   }

   SecureBinaryData hmacPhrase(64);
   if (chaincode.empty())
   {
      /*
      The passphrase is the hmac of the root and the chaincode. If the 
      chaincode is empty, we only hmac the root.
      */

      auto rootHash = BtcUtils::getHash256(root);
      BtcUtils::getHMAC512(
         rootHash.getPtr(), rootHash.getSize(),
         salt_.getPtr(), salt_.getSize(),
         hmacPhrase.getPtr());
   }
   else
   {
      /*
      Concatenate root and chaincode then hmac
      */

      SecureBinaryData rootCopy(64);
      rootCopy.append(root);
      rootCopy.append(chaincode);

      auto rootHash = BtcUtils::getHash256(rootCopy);
      BtcUtils::getHMAC512(
         rootHash.getPtr(), rootHash.getSize(),
         salt_.getPtr(), salt_.getSize(),
         hmacPhrase.getPtr());
   }

   //passphrase is first 7 bytes of the hmac
   BinaryWriter bw;
   bw.put_BinaryDataRef(hmacPhrase.getSliceRef(0, 7));
   auto passChecksum = BtcUtils::getHash256(bw.getData());
   bw.put_uint8_t(passChecksum[0]);

   passphrase_ = SecureBinaryData::fromString(
      BtcUtils::base58_encode(bw.getData()));

   /*
   2. extend the passphrase
   */

   auto encryptionKey = kdf_.DeriveKey(passphrase_);

   /*
   3. Encrypt the data. We use the libbtc call directly because
      we do not want padding
   */

   auto encrypt = [this, &encryptionKey](
      const SecureBinaryData& cleartext, SecureBinaryData& result)->bool
   {
      //this exclusively encrypt 32 bytes of data
      if (cleartext.getSize() != 32)
         return false;

      //make sure result buffer is large enough
      result.resize(32);

      //encrypt with CBC
      auto encrLen = aes256_cbc_encrypt(
         encryptionKey.getPtr(), iv16_.getPtr(),
         cleartext.getPtr(), cleartext.getSize(),
         0, //no padding
         result.getPtr());

      if (encrLen != 32)
         return false;

      return true;
   };

   pair<SecureBinaryData, SecureBinaryData> result;
   if (!encrypt(root, result.first))
   {
      LOGERR << "SecurePrint encryption failure";
      throw runtime_error("SecurePrint encryption failure");
   }

   if (!chaincode.empty())
   {
      if (!encrypt(chaincode, result.second))
      {
         LOGERR << "SecurePrint encryption failure";
         throw runtime_error("SecurePrint encryption failure");
      }
   }

   return result;
}

////////////////////////////////////////////////////////////////////////////////
SecureBinaryData SecurePrint::decrypt(
   const SecureBinaryData& ciphertext, const BinaryDataRef passphrase) const
{
   //check passphrase checksum
   //TODO: try with std::string_view instead
   string passStr(passphrase.toCharPtr(), passphrase.getSize());
   BinaryData passBin;
   try
   {
      passBin = move(BtcUtils::base58_decode(passStr));
   }
   catch (const exception&)
   {
      LOGERR << "invalid SecurePrint passphrase";
      throw runtime_error("invalid SecurePrint passphrase");
   }

   if (passBin.getSize() != 8)
   {
      LOGERR << "invalid SecurePrint passphrase";
      throw runtime_error("invalid SecurePrint passphrase");
   }

   BinaryRefReader brr(passBin);
   auto passBase = brr.get_BinaryDataRef(7);
   auto checksum = brr.get_uint8_t();

   auto passHash = BtcUtils::getHash256(passBase);
   if (passHash[0] != checksum)
   {
      LOGERR << "invalid SecurePrint passphrase";
      throw runtime_error("invalid SecurePrint passphrase");
   }

   if (ciphertext.getSize() < 32)
   {
      LOGERR << "invalid ciphertext size for SecurePrint";
      throw runtime_error("invalid ciphertext size for SecurePrint");
   }
   
   //kdf the passphrase
   auto encryptionKey = kdf_.DeriveKey(passphrase);

   //
   auto decrypt = [this, &encryptionKey](
      const BinaryDataRef& ciphertext, SecureBinaryData& result)->bool
   {
      //works exclusively on 32 byte packets
      if (ciphertext.getSize() != 32)
         return false;

      result.resize(32);

      auto size = aes256_cbc_decrypt(
         encryptionKey.getPtr(), iv16_.getPtr(),
         ciphertext.getPtr(), ciphertext.getSize(),
         0, //no padding
         result.getPtr());

      if (size != 32)
         return false;

      return true;
   };

   //decrypt the root
   SecureBinaryData result;
   if (!decrypt(ciphertext, result))
   {
      LOGERR << "failed to decrypt SecurePrint string";
      throw runtime_error("failed to decrypt SecurePrint string");
   }

   return result;
}

////////////////////////////////////////////////////////////////////////////////
////
//// Helpers
////
////////////////////////////////////////////////////////////////////////////////

/////////////////////////////// -- backup strings -- ///////////////////////////
unique_ptr<WalletBackup> Helpers::getWalletBackup(
   shared_ptr<AssetWallet_Single> wltPtr, BackupType bType)
{
   std::unique_ptr<ClearTextSeed> clearTextSeed;

   //grab encrypted seed from wallet
   auto lock = wltPtr->lockDecryptedContainer();
   auto wltSeed = wltPtr->getEncryptedSeed();
   if (wltSeed != nullptr)
   {
      const auto& rawClearTextSeed = wltPtr->getDecryptedValue(wltSeed);
      clearTextSeed = ClearTextSeed::deserialize(rawClearTextSeed);
   }
   else
   {
      //wallet has no seed, maybe it's a legacy Armory wallet, where
      //the seed and root are the same
      auto root = wltPtr->getRoot();
      auto root135 = dynamic_pointer_cast<AssetEntry_ArmoryLegacyRoot>(root);
      if (root135 == nullptr)
         return {};

      const auto& rootPrivKey = wltPtr->getDecryptedPrivateKeyForAsset(
         root135);
      clearTextSeed = unique_ptr<ClearTextSeed>(new ClearTextSeed_Armory135(
         rootPrivKey, root135->getChaincode()));
   }

   if (clearTextSeed == nullptr)
      throw runtime_error("[getWalletBackup] could not get seed from wallet");

   //pick default backup type for seed if not set explicitly
   if (bType == BackupType::Invalid)
      bType = clearTextSeed->getPreferedBackupType();

   auto backup = getWalletBackup(move(clearTextSeed), bType);
   backup->wltId_ = wltPtr->getID();
   return backup;
}

////
unique_ptr<WalletBackup> Helpers::getWalletBackup(
   unique_ptr<ClearTextSeed> seed, BackupType bType)
{
   //sanity check
   if (!seed->isBackupTypeEligible(bType))
      throw runtime_error("[getWalletBackup] ineligible backup type");

   switch (bType)
   {
      case BackupType::Armory135:
      case BackupType::Armory200a:
      case BackupType::Armory200b:
      case BackupType::Armory200c:
      case BackupType::Armory200d:
         return getEasy16BackupString(move(seed));

      case BackupType::Base58:
         return getBase58BackupString(move(seed));

      case BackupType::BIP39:
         return getBIP39BackupString(move(seed));

      default:
         throw runtime_error("[getWalletBackup] invalid backup type");
   }
}

////////
unique_ptr<WalletBackup> Helpers::getEasy16BackupString(
   unique_ptr<ClearTextSeed> seed)
{
   BinaryDataRef primaryData;
   BinaryDataRef secondaryData;
   BackupType mode = BackupType::Invalid;

   switch (seed->type())
   {
      case SeedType::Armory135:
      {
         auto seed135   = dynamic_cast<ClearTextSeed_Armory135*>(seed.get());
         primaryData    = seed135->getRoot().getRef();
         secondaryData  = seed135->getChaincode().getRef();
         mode = seed->getPreferedBackupType();
         break;
      }

      case SeedType::BIP32_Structured:
      case SeedType::BIP32_Virgin:
      case SeedType::BIP39:
      {
         auto seedBip32 = dynamic_cast<ClearTextSeed_BIP32*>(seed.get());
         primaryData    = seedBip32->getRawEntropy().getRef();
         mode = seed->getPreferedBackupType();

         switch (seed->type())
         {
            case SeedType::BIP39:
               //force Armory200d for BIP39 seeds
               mode = BackupType::Armory200d;
               break;

            default:
               mode = seed->getPreferedBackupType();
         }
         break;
      }

      default:
         throw runtime_error("[getEasy16BackupString] invalid seed type");
   }

   //apply secureprint to seed data
   SecurePrint sp;
   auto encrRoot = sp.encrypt(primaryData, secondaryData);

   //set cleartext and encrypted root
   auto lines_clear = Easy16Codec::encode(primaryData, mode);
   auto lines_encr  = Easy16Codec::encode(encrRoot.first, mode);

   auto result = make_unique<Backup_Easy16>(mode);
   result->rootClear_ = move(lines_clear);
   result->rootEncr_ = move(lines_encr);
   if (mode == BackupType::Armory135)
   {
      //if there's a chaincode, set it too
      if (!secondaryData.empty())
      {
         result->chaincodeClear_ = move(Easy16Codec::encode(secondaryData, mode));
         result->chaincodeEncr_ = move(Easy16Codec::encode(encrRoot.second, mode));
      }
   }
   result->spPass_ = move(sp.getPassphrase());
   return result;
}

////////
unique_ptr<WalletBackup> Helpers::getBIP39BackupString(
   unique_ptr<ClearTextSeed> seed)
{
   //sanity check
   if (seed->type() != SeedType::BIP39)
      throw runtime_error("[getBIP39BackupString] invalid seed type");

   auto seedBip39 = dynamic_cast<ClearTextSeed_BIP39*>(seed.get());
   unique_ptr<Backup_BIP39> result;
   switch (seedBip39->getDictionnaryId())
   {
      case ClearTextSeed_BIP39::Dictionnary::English_Trezor:
      {
         //clear libbtc/trezor bip39 mnemonic buffer
         mnemonic_clear();

         //convert raw entropy to mnemonic phrase
         auto mnemonicPtr = mnemonic_from_data(
            seedBip39->getRawEntropy().getPtr(),
            seedBip39->getRawEntropy().getSize());
         string_view mnemonicView(mnemonicPtr, strlen(mnemonicPtr));

         //copy mnemonic phrase
         result = Backup_BIP39::fromMnemonicString(mnemonicView);

         //clear libbtc/trezor bip39 mnemonic buffer
         mnemonic_clear();
         break;
      }

      default:
         throw runtime_error("[getBIP39BackupString] invalid dictionnary id");
   }
   return result;
}

////////
unique_ptr<Backup_Base58> Helpers::getBase58BackupString(
   unique_ptr<ClearTextSeed> seed)
{
   auto seedBip32 = dynamic_cast<ClearTextSeed_BIP32*>(seed.get());
   if (seedBip32 == nullptr)
      throw runtime_error("[getBase58BackupString] invalid seed object");

   if (seedBip32->type() != SeedType::BIP32_base58Root)
      throw runtime_error("[getBase58BackupString] invalid seed type");

   auto node = seedBip32->getRootNode();
   auto result = make_unique<Backup_Base58>(move(node->getBase58()));
   return result;
}

////////////////////////////// -- restore methods -- ///////////////////////////
shared_ptr<AssetWallet> Helpers::restoreFromBackup(
   unique_ptr<WalletBackup> backup, const UserPrompt& callback,
   const WalletCreationParams& params)
{
   unique_ptr<ClearTextSeed> seed = nullptr;
   auto bType = backup->type();
   switch (bType)
   {
      //easy16 backups
      case BackupType::Armory135:
      case BackupType::Armory200a:
      case BackupType::Armory200b:
      case BackupType::Armory200d:
      case BackupType::Easy16_Unkonwn:
         seed = restoreFromEasy16(move(backup), callback, bType);
         break;

      case BackupType::Base58:
         seed = restoreFromBase58(move(backup));
         break;

      case BackupType::BIP39:
         seed = restoreFromBIP39(move(backup));
         break;

      default:
         break;
   }

   if (seed == nullptr)
   {
      BridgeProto::RestorePrompt prompt;
      prompt.mutable_type_error()->set_error(
         "failed to create seed from backup");
      callback(move(prompt));
      throw RestoreUserException("failed to create seed from backup");
   }

   //prompt user to verify id
   {
      BridgeProto::RestorePrompt prompt;
      auto checkWltIdMsg = prompt.mutable_check_wallet_id();
      checkWltIdMsg->set_wallet_id(seed->getWalletId());
      checkWltIdMsg->set_backup_type((int)bType);

      auto reply = callback(move(prompt));
      if (!reply.success())
         throw RestoreUserException("user rejected id");
   }

   //prompt for passwords
   BinaryDataRef pass = params.passphrase.getRef();
   BinaryDataRef control = params.controlPassphrase.getRef();
   if (pass.empty())
   {
      BridgeProto::RestorePrompt prompt;
      prompt.set_get_passphrases(true);
      auto reply = callback(move(prompt));

      if (!reply.success())
         throw RestoreUserException("user did not provide a passphrase");

      pass.setRef(reply.passphrases().privkey());
      control.setRef(reply.passphrases().control());
   }

   WalletCreationParams paramsCopy{ pass, control,
      params.folder, params.lookup,
      params.publicUnlockDuration_ms, params.privateUnlockDuration_ms };

   //return wallet
   return AssetWallet_Single::createFromSeed(std::move(seed), paramsCopy);
}

////////
unique_ptr<ClearTextSeed> Helpers::restoreFromEasy16(
   unique_ptr<WalletBackup> backup, const UserPrompt& callback,
   BackupType& bType)
{
   auto backupE16 = dynamic_cast<Backup_Easy16*>(backup.get());
   if (backupE16 == nullptr)
      return nullptr;
   bool isEncrypted = !backupE16->getSpPass().empty();

   /* decode data */

   //root
   vector<BinaryDataRef> first2Lines;
   first2Lines.reserve(2);

   auto firstLine = backupE16->getRoot(
      Backup_Easy16::LineIndex::One, isEncrypted);
   first2Lines.emplace_back(BinaryDataRef(
      (uint8_t*)firstLine.data(), firstLine.size()));

   auto secondLine = backupE16->getRoot(
      Backup_Easy16::LineIndex::Two, isEncrypted);
   first2Lines.emplace_back(BinaryDataRef(
      (uint8_t*)secondLine.data(), secondLine.size()));

   auto primaryData = Easy16Codec::decode(first2Lines);
   if (!primaryData.isInitialized())
      return nullptr;

   //chaincode
   BackupEasy16DecodeResult secondaryData;
   if (backupE16->hasChaincode())
   {
      vector<BinaryDataRef> next2Lines;
      auto thirdLine = backupE16->getChaincode(
         Backup_Easy16::LineIndex::One, isEncrypted);
      next2Lines.emplace_back(BinaryDataRef(
         (uint8_t*)thirdLine.data(), thirdLine.size()));

      auto fourthLine = backupE16->getChaincode(
         Backup_Easy16::LineIndex::Two, isEncrypted);
      next2Lines.emplace_back(BinaryDataRef(
         (uint8_t*)fourthLine.data(), fourthLine.size()));

      secondaryData = Easy16Codec::decode(next2Lines);
      if (!secondaryData.isInitialized())
         return nullptr;
   }

   /* checksums & repair */

   //root
   if (!primaryData.isValid())
   {
      if (!Easy16Codec::repair(primaryData))
      {
         BridgeProto::RestorePrompt prompt;
         auto checksumError = prompt.mutable_checksum_error();
         checksumError->add_index(primaryData.checksumIndexes_[0]);
         checksumError->add_index(primaryData.checksumIndexes_[1]);
         callback(move(prompt));
         return nullptr;
      }

      if (!primaryData.isValid())
      {
         BridgeProto::RestorePrompt prompt;
         auto checksumError = prompt.mutable_checksum_error();
         checksumError->add_index(primaryData.repairedIndexes_[0]);
         checksumError->add_index(primaryData.repairedIndexes_[1]);
         callback(move(prompt));
         return nullptr;
      }
   }

   //chaincode
   if (secondaryData.isInitialized())
   {
      if (!Easy16Codec::repair(secondaryData))
      {
         BridgeProto::RestorePrompt prompt;
         auto checksumError = prompt.mutable_checksum_error();
         checksumError->add_index(secondaryData.checksumIndexes_[0]);
         checksumError->add_index(secondaryData.checksumIndexes_[1]);
         callback(move(prompt));
         return nullptr;
      }

      if (!secondaryData.isValid())
      {
         BridgeProto::RestorePrompt prompt;
         auto checksumError = prompt.mutable_checksum_error();
         checksumError->add_index(secondaryData.repairedIndexes_[0]);
         checksumError->add_index(secondaryData.repairedIndexes_[1]);
         callback(move(prompt));
         return nullptr;
      }

      //check chaincode index matches root index
      if (primaryData.getIndex() != secondaryData.getIndex())
      {
         BridgeProto::RestorePrompt prompt;
         auto checksumError = prompt.mutable_checksum_mismatch();
         checksumError->add_index(primaryData.getIndex());
         checksumError->add_index(secondaryData.getIndex());
         callback(move(prompt));
         return nullptr;
      }
   }

   /* SecurePrint */
   if (isEncrypted)
   try
   {
      SecurePrint sp;
      auto pass = backupE16->getSpPass();
      BinaryDataRef passRef((uint8_t*)pass.data(), pass.size());
      primaryData.data_ = move(sp.decrypt(primaryData.data_, passRef));

      if (secondaryData.isInitialized())
         secondaryData.data_ = move(sp.decrypt(secondaryData.data_, passRef));
   }
   catch (const exception&)
   {
      BridgeProto::RestorePrompt prompt;
      prompt.set_decrypt_error(true);
      callback(move(prompt));
      throw RestoreUserException("invalid SP pass");
   }

   /* backup type */
   if (bType == BackupType::Easy16_Unkonwn)
   {
      bType = (BackupType)primaryData.getIndex();
   }
   else
   {
      if ((BackupType)primaryData.getIndex() != bType)
      {
         //mismatch between easy16 index and backup expected type
         BridgeProto::RestorePrompt prompt;
         auto checksumError = prompt.mutable_checksum_mismatch();
         checksumError->add_index(primaryData.getIndex());
         checksumError->add_index((int)bType);
         callback(move(prompt));
         return nullptr;
      }
   }

   /* create seed */
   unique_ptr<ClearTextSeed> seedPtr = nullptr;
   switch (bType)
   {
      case BackupType::Armory135:
      {
         /*legacy armory wallet, legacy backup string*/
         seedPtr = std::move(std::make_unique<ClearTextSeed_Armory135>(
            primaryData.data_, secondaryData.data_,
            ClearTextSeed_Armory135::LegacyType::Armory135));
         break;
      }

      case BackupType::Armory200a:
      {
         /*legacy armory wallet, indexed backup string*/
         seedPtr = std::move(std::make_unique<ClearTextSeed_Armory135>(
            primaryData.data_, secondaryData.data_,
            ClearTextSeed_Armory135::LegacyType::Armory200));
         break;
      }

      //bip32 wallets
      case BackupType::Armory200b:
      {
         /*BIP32 wallet with BIP44/49/84 accounts*/
         seedPtr = std::move(std::make_unique<ClearTextSeed_BIP32>(
            primaryData.data_, SeedType::BIP32_Structured));
         break;
      }

      case BackupType::Armory200c:
      {
         //empty BIP32 wallet
         seedPtr = std::move(std::make_unique<ClearTextSeed_BIP32>(
            primaryData.data_, SeedType::BIP32_Virgin));
         break;
      }

      case BackupType::Armory200d:
      {
         //empty BIP32 wallet
         seedPtr = std::move(std::make_unique<ClearTextSeed_BIP39>(
            primaryData.data_,
            ClearTextSeed_BIP39::Dictionnary::English_Trezor));
         break;
      }

      default:
         return nullptr;
   }
   return seedPtr;
}

////////
unique_ptr<ClearTextSeed> Helpers::restoreFromBase58(
   unique_ptr<WalletBackup> backup)
{
   auto backupB58 = dynamic_cast<Backup_Base58*>(backup.get());
   if (backupB58 == nullptr)
      return nullptr;

   unique_ptr<ClearTextSeed_BIP32> seed;
   try {
      auto b58StrView = backupB58->getBase58String();
      BinaryData b58Ref(b58StrView.data(), b58StrView.size());
      return ClearTextSeed_BIP32::fromBase58(b58Ref);
   }
   catch (const std::exception&)
   {
      return nullptr;
   }
}

////////
unique_ptr<ClearTextSeed> Helpers::restoreFromBIP39(
   unique_ptr<WalletBackup> backup)
{
   auto backupBIP39 = dynamic_cast<Backup_BIP39*>(backup.get());
   if (backupBIP39 == nullptr)
      return nullptr;

   const char* mnemonic = backupBIP39->getMnemonicString().data();

   //check mnemonic phrase
   if (mnemonic_check(mnemonic) == 0)
      return nullptr;

   //convert mnemonic phrase to raw entropy
   SecureBinaryData rawEntropy(33); //max entropy size + checksum
   auto lenInBits = mnemonic_to_bits(mnemonic, rawEntropy.getPtr());

   if (lenInBits == 0)
      return nullptr;

   //strip out checksum bits
   auto lenInBytes = lenInBits / 8;
   lenInBytes -= lenInBytes % 8;
   rawEntropy.resize(lenInBytes);

   //entropy to seed
   return make_unique<ClearTextSeed_BIP39>(rawEntropy,
      ClearTextSeed_BIP39::Dictionnary::English_Trezor);
}

////////////////////////////////////////////////////////////////////////////////
//
//// WalletBackup
//
////////////////////////////////////////////////////////////////////////////////
WalletBackup::WalletBackup(BackupType bType) :
   type_(bType)
{}

WalletBackup::~WalletBackup()
{}

const string& WalletBackup::getWalletId() const
{
   return wltId_;
}

const BackupType& WalletBackup::type() const
{
   return type_;
}

///////////////////////////////// Backup_Easy16 ////////////////////////////////
Backup_Easy16::Backup_Easy16(BackupType bType) :
   WalletBackup(bType)
{}

Backup_Easy16::~Backup_Easy16()
{}

bool Backup_Easy16::hasChaincode() const
{
   if (type() != BackupType::Armory135 && type() != BackupType::Easy16_Unkonwn)
      return false;

   return !chaincodeClear_.empty() || !chaincodeEncr_.empty() ;
}

////
string_view Backup_Easy16::getRoot(LineIndex li, bool encrypted) const
{
   auto lineIndex = (int)li;
   std::vector<SecureBinaryData>::const_iterator iter;
   if (!encrypted)
   {
      iter = rootClear_.begin() + lineIndex;
      if (iter == rootClear_.end())
      {
         throw runtime_error("[Backup_Easy16::getRoot]"
         " missing cleartext line");
      }
   }
   else
   {
      iter = rootEncr_.begin() + lineIndex;
      if (iter == rootEncr_.end())
      {
         throw runtime_error("[Backup_Easy16::getRoot]"
         " missing encrypted line");
      }
   }

   return string_view(iter->toCharPtr(), iter->getSize());
}

string_view Backup_Easy16::getChaincode(LineIndex li, bool encrypted) const
{
   auto lineIndex = (int)li;
   std::vector<SecureBinaryData>::const_iterator iter;
   if (!encrypted)
   {
      iter = chaincodeClear_.begin() + lineIndex;
      if (iter == chaincodeClear_.end())
      {
         throw runtime_error("[Backup_Easy16::getChaincode]"
            " missing cleartext line");
      }
   }
   else
   {
      iter = chaincodeEncr_.begin() + lineIndex;
      if (iter == chaincodeEncr_.end())
      {
         throw runtime_error("[Backup_Easy16::getChaincode]"
            " missing encrypted line");
      }
   }

   return string_view(iter->toCharPtr(), iter->getSize());
}

string_view Backup_Easy16::getSpPass() const
{
   return string_view(spPass_.toCharPtr(), spPass_.getSize());
}

////
unique_ptr<Backup_Easy16> Backup_Easy16::fromLines(
   const vector<string_view>& lines, string_view spPass)
{
   if (lines.size() % 2 != 0)
      throw runtime_error("[Backup_Easy16::fromLines] invalid line count");

   auto result = make_unique<Backup_Easy16>(BackupType::Easy16_Unkonwn);
   unsigned i=0;

   if (spPass.empty())
   {
      for (const auto& line : lines)
      {
         auto lineSBD = SecureBinaryData::fromStringView(line);
         if (i<2)
            result->rootClear_.emplace_back(move(lineSBD));
         else
            result->chaincodeClear_.emplace_back(move(lineSBD));
         ++i;
      }
   }
   else
   {
      for (const auto& line : lines)
      {
         auto lineSBD = SecureBinaryData::fromStringView(line);
         if (i<2)
            result->rootEncr_.emplace_back(move(lineSBD));
         else
            result->chaincodeEncr_.emplace_back(move(lineSBD));
         ++i;
      }
      result->spPass_ = SecureBinaryData::fromStringView(spPass);
   }

   return result;
}

///////////////////////////////// Backup_Base58 ////////////////////////////////
Backup_Base58::Backup_Base58(SecureBinaryData b58String) :
   WalletBackup(BackupType::Base58), b58String_(move(b58String))
{}

Backup_Base58::~Backup_Base58()
{}

string_view Backup_Base58::getBase58String() const
{
   return string_view(b58String_.toCharPtr(), b58String_.getSize());
}

unique_ptr<Backup_Base58> Backup_Base58::fromString(const string_view& strV)
{
   return make_unique<Backup_Base58>(SecureBinaryData::fromStringView(strV));
}

///////////////////////////////// Backup_BIP39 /////////////////////////////////
Backup_BIP39::Backup_BIP39() :
   WalletBackup(BackupType::BIP39), mnemonicString_()
{}

Backup_BIP39::~Backup_BIP39()
{}

unique_ptr<Backup_BIP39> Backup_BIP39::fromMnemonicString(string_view strV)
{
   //create a SBD with 1 extra byte to account for terminating 0,
   //as trezor-crypto expects null terminate strings
   SecureBinaryData mnemonicSBD(strV.size() + 1);
   memset(mnemonicSBD.getPtr(), 0, strV.size() + 1);
   memcpy(mnemonicSBD.getPtr(), strV.data(), strV.size());

   unique_ptr<Backup_BIP39> result(new Backup_BIP39());
   result->mnemonicString_ = move(mnemonicSBD);
   return result;
}

string_view Backup_BIP39::getMnemonicString() const
{
   return string_view(mnemonicString_.toCharPtr(), mnemonicString_.getSize());
}
