////////////////////////////////////////////////////////////////////////////////
//                                                                            //
//  Copyright (C) 2020 - 2026, goatpig                                        //
//  Distributed under the MIT license                                         //
//  See LICENSE-MIT or https://opensource.org/licenses/MIT                    //
//                                                                            //
////////////////////////////////////////////////////////////////////////////////

#include "Backups.h"
#include <Utils/Cryptography.h>
#include <Utils/BtcUtils.h>
#include "../KDF.h"
#include "../WalletIdTypes.h"
#include "../Wallets.h"
#include "../IOHeader.h"
#include "../Assets.h"
#include "Seeds.h"


#include <btc/aes256_cbc.h>

extern "C" {
#include <trezor-crypto/bip39.h>
}

#define EASY16_CHECKSUM_LEN 2
#define EASY16_INDEX_MAX   15
#define EASY16_LINE_LENGTH 16

#define WALLET_RESTORE_LOOKUP 1000

using namespace Armory;
using namespace Armory::Seeds;
using namespace std::string_view_literals;

/***
   Checksum indexes are appended as a byte to the 16 bytes line that is
   passed through the hash256 function to generate the checksum. That byte
   value designates the type of wallet this backup was generated from.

   For index 0 (Armory 1.35 wallets), the byte is not appended.
   The indexes for each line in a multiple line easy16 code need to match
   one another.
***/
const std::set<BackupType> Easy16Codec::eligibleIndexes{
   BackupType::Armory135a,
   BackupType::Armory200a,
   BackupType::Armory200b,
   BackupType::Armory200c,
   BackupType::Armory200d
};

constexpr char Easy16Codec::characters[]{"asdfghjkwertuion"};
const std::map<char, uint8_t> Easy16Codec::easy16Vals{
   {'a', 0}, {'s', 1}, {'d', 2}, {'f', 3},
   {'g', 4}, {'h', 5}, {'j', 6}, {'k', 7},
   {'w', 8}, {'e', 9}, {'r', 10}, {'t', 11},
   {'u', 12}, {'i', 13}, {'o', 14}, {'n', 15}
};

namespace
{
   /////////////////////////////////////////////////////////////////////////////
   /* - comment from etotheipi: -
   Nothing up my sleeve!  Need some hardcoded random numbers to use for
   encryption IV and salt.  Using the first 256 digits of Pi for the
   the IV, and first 256 digits of e for the salt (hashed)
   */
   constexpr std::string_view digitsPI{
      "ARMORY_ENCRYPTION_INITIALIZATION_VECTOR_"
      "1415926535897932384626433832795028841971693993751058209749445923"
      "0781640628620899862803482534211706798214808651328230664709384460"
      "9550582231725359408128481117450284102701938521105559644622948954"
      "9303819644288109756659334461284756482337867831652712019091456485"sv
   };

   constexpr std::string_view digitsE{
      "ARMORY_KEY_DERIVATION_FUNCTION_SALT_"
      "7182818284590452353602874713526624977572470936999595749669676277"
      "2407663035354759457138217852516642742746639193200305992181741359"
      "6629043572900334295260595630738132328627943490763233829880753195"
      "2510190115738341879307021540891499348841675092447614606680822648"sv
   };

   constexpr uint32_t spKDFBytes = 16 * 1024 * 1024;

   /////////////////////////////////////////////////////////////////////////////
   // checksum calls
   BinaryData getHash(const BinaryDataRef& data, uint8_t hint)
   {
      if (hint == 0) {
         return BtcUtils::getHash256(data);
      } else {
         SecureBinaryData dataCopy(data.getSize() + 1);
         memcpy(dataCopy.getPtr(), data.getPtr(), data.getSize());
         dataCopy.getPtr()[data.getSize()] = hint;
         return BtcUtils::getHash256(dataCopy);
      }
   }

   uint8_t verifyChecksum(
      const BinaryDataRef& data, const BinaryDataRef& checksum)
   {
      for (const auto& indexCandidate : Easy16Codec::eligibleIndexes) {
         auto hash = getHash(data, (uint8_t)indexCandidate);
         if (hash.getSliceRef(0, EASY16_CHECKSUM_LEN) == checksum) {
            return (uint8_t)indexCandidate;
         }
      }
      return EASY16_INVALID_CHECKSUM_INDEX;
   }

   /////////////////////////////////////////////////////////////////////////////
   // easy16 encode
   void encodeEasy16Byte(uint8_t c, char* ptr)
   {
      uint8_t val1 = c >> 4;
      uint8_t val2 = c & 0x0F;
      ptr[0] = Easy16Codec::characters[val1];
      ptr[1] = Easy16Codec::characters[val2];
   };

   SecureBinaryData encodeEasy16Line(BinaryDataRef chunk, uint8_t index,
      bool isPriv=true)
   {
      //compute checksum
      auto checksum = getHash(chunk, index);

      //2 characters per byte + 2 bytes for the checksum
      size_t charCount = chunk.getSize() * 2 + EASY16_CHECKSUM_LEN * 2;

      //a space after each 4 characters
      size_t spaceCount = charCount / 4;
      if (spaceCount > 0 && spaceCount * 4 == charCount) {
         --spaceCount;
      }

      //a double space every 16 characters
      size_t doubleSpaceCount = isPriv ? charCount / 16 : 0;
      if (doubleSpaceCount > 0 && doubleSpaceCount * 16 == charCount) {
         --doubleSpaceCount;
      }

      //+1 for explicit null byte cause of capnp machinations
      size_t lineLength = charCount + spaceCount + doubleSpaceCount + 1;
      SecureBinaryData result(lineLength);

      size_t characterIndex = 0, spaceIndex = 0, dSpaceIndex = 0;
      auto resultPtr = result.getCharPtr();
      auto encodeChunk = [&resultPtr, &characterIndex,
         &spaceIndex, spaceCount, &dSpaceIndex, doubleSpaceCount]
      (BinaryDataRef chunk)->void
      {
         for (unsigned i=0; i < chunk.getSize(); i++) {
            encodeEasy16Byte(chunk.getPtr()[i], resultPtr);
            resultPtr += 2;
            characterIndex++;

            //spaces, make sure to not add more than we made room for
            if (characterIndex % 2 == 0 && spaceIndex < spaceCount) {
               resultPtr[0] = ' ';
               ++resultPtr;
               ++spaceIndex;
            }

            if (characterIndex % 8 == 0 && dSpaceIndex < doubleSpaceCount) {
               resultPtr[0] = ' ';
               ++resultPtr;
               ++dSpaceIndex;
            }
         }
      };

      //encode to easy16
      encodeChunk(chunk);

      //add the checksum
      encodeChunk(checksum.getSliceRef(0, 2));

      //null terminate and return
      result[lineLength-1] = 0;
      return result;
   }

   /////////////////////////////////////////////////////////////////////////////
   // easy16 decode
   void decodeEasy16Characters(uint8_t& result, const char* str)
   {
      //convert characters to value, ignore effect of invalid ones
      result = 0;
      auto iter1 = Easy16Codec::easy16Vals.find(str[0]);
      if (iter1 != Easy16Codec::easy16Vals.end()) {
         result = iter1->second << 4;
      }

      auto iter2 = Easy16Codec::easy16Vals.find(str[1]);
      if (iter2 != Easy16Codec::easy16Vals.end()) {
         result += iter2->second;
      }
   };

   int decodeEasy16Line(uint8_t* result, size_t& len,
      const BinaryDataRef& line, BinaryData& checksum)
   {
      /*
      Converts line to binary, appends into result.
      Returns the hash index matching the checksum.

      Error values:
      . -1: checksum mismatch
      . -2: invalid checksum data
      . -3: not enough room in the result buffer
      */

      auto maxlen = len;
      len = 0;
      auto ptr = line.toCharPtr();

      //decode the entire line
      SecureBinaryData decodedLine(line.getSize());
      for (unsigned i=0; i<line.getSize(); i++) {
         //skip spaces
         if (*(ptr + i) == ' ') {
            continue;
         } else if (*(ptr + i) == 0) {
            //null char, we're done
            break;
         }

         //this will read the next 2 characters into a single uint8_t
         decodeEasy16Characters(decodedLine.getPtr()[len], ptr + i);

         //increment result length
         ++len;

         //increment i to skip 2 characters
         ++i;
      }

      if (len <= EASY16_CHECKSUM_LEN) {
         //decoded line cannot fit the checksum
         return -2;
      }
      len -= EASY16_CHECKSUM_LEN;

      if (len > maxlen) {
         //not enough room in the result buffer
         return -3;
      }

      //copy decoded line
      memcpy(result, decodedLine.getPtr(), len);

      //copy checksum
      checksum.resize(EASY16_CHECKSUM_LEN);
      memcpy(checksum.getPtr(), decodedLine.getPtr() + len, EASY16_CHECKSUM_LEN);

      //hash data
      BinaryDataRef decodedChunk{result, len};
      return (int)verifyChecksum(decodedChunk, checksum);
   };


   std::unique_ptr<ClearTextSeed> restoreFromEasy16Public(
      Backup_Easy16Public* backup, const Helpers::UserPrompt& callback,
      BackupType& bType)
   {
      //walletId
      size_t idLen = 7;
      BinaryData decodedId, checksum;
      decodedId.resize(idLen);
      auto idRef = BinaryDataRef::fromStringView(backup->getBackupId());
      int decodeChkByte = decodeEasy16Line(decodedId.getPtr(), idLen,
         idRef, checksum);
      if (decodeChkByte < 0 || decodeChkByte == EASY16_INVALID_CHECKSUM_INDEX) {
         return nullptr;
      }

      BinaryRefReader brr(decodedId);
      auto prefix = brr.get_uint8_t();
      auto wltIdRef = brr.get_BinaryDataRef(brr.getSizeRemaining());

      //pubroot
      std::vector<BinaryDataRef> first2Lines;
      first2Lines.reserve(2);

      auto firstLine = backup->getPublicRoot(LineIndex::One);
      first2Lines.emplace_back(BinaryDataRef{
         (uint8_t*)firstLine.data(), firstLine.size()});

      auto secondLine = backup->getPublicRoot(LineIndex::Two);
      first2Lines.emplace_back(BinaryDataRef{
         (uint8_t*)secondLine.data(), secondLine.size()});

      auto primaryData = Easy16Codec::decode(first2Lines);
      if (!primaryData.isInitialized()) {
         return nullptr;
      }

      //check pubkey data integrity
      if (!primaryData.isValid()) {
         if (!Easy16Codec::repair(primaryData)) {
            RestorePrompt prompt{RestorePromptType::ChecksumError};
            for (unsigned i=0; i < primaryData.checksumIndexes.size(); i++) {
               prompt.checksumResult.emplace(i, primaryData.checksumIndexes[i]);
            }
            callback(prompt);
            return nullptr;
         }

         if (!primaryData.isValid()) {
            RestorePrompt prompt{RestorePromptType::ChecksumError};
            for (unsigned i=0; i < primaryData.repairedIndexes.size(); i++) {
               prompt.checksumResult.emplace(i, primaryData.repairedIndexes[i]);
            }
            callback(prompt);
            return nullptr;
         }
      }

      //chaincode
      std::vector<BinaryDataRef> next2Lines;
      auto thirdLine = backup->getChaincode(LineIndex::One);
      next2Lines.emplace_back(BinaryDataRef{
         (uint8_t*)thirdLine.data(), thirdLine.size()});

      auto fourthLine = backup->getChaincode(LineIndex::Two);
      next2Lines.emplace_back(BinaryDataRef{
         (uint8_t*)fourthLine.data(), fourthLine.size()});

      auto secondaryData = Easy16Codec::decode(next2Lines);
      if (!secondaryData.isInitialized()) {
         return nullptr;
      }

      //check chaincode integrity
      if (!secondaryData.isValid()) {
         if (!Easy16Codec::repair(secondaryData)) {
            RestorePrompt prompt{RestorePromptType::ChecksumError};
            for (unsigned i=0; i < primaryData.checksumIndexes.size(); i++) {
               prompt.checksumResult.emplace(i+2, secondaryData.checksumIndexes[i]);
            }
            callback(prompt);
            return nullptr;
         }

         if (!secondaryData.isValid()) {
            RestorePrompt prompt{RestorePromptType::ChecksumError};
            for (unsigned i=0; i < primaryData.repairedIndexes.size(); i++) {
               prompt.checksumResult.emplace(i+2, secondaryData.repairedIndexes[i]);
            }
            callback(prompt);
            return nullptr;
         }
      }

      //check chaincode index matches pubkey index
      if (primaryData.getIndex() != secondaryData.getIndex()) {
         RestorePrompt prompt{RestorePromptType::ChecksumMismatch};
         prompt.checksumResult.emplace(0, primaryData.getIndex());
         prompt.checksumResult.emplace(1, secondaryData.getIndex());
         callback(prompt);
         return nullptr;
      }

      //check pubkey index match wallet index
      if (primaryData.getIndex() != decodeChkByte) {
         RestorePrompt prompt{RestorePromptType::PubkeyChecksumMismatch};
         prompt.checksumResult.emplace(0, primaryData.getIndex());
         prompt.checksumResult.emplace(1, decodeChkByte);
         callback(prompt);
         return nullptr;
      }

      if (bType == BackupType::Easy16_Unkonwn) {
         bType = (BackupType)primaryData.getIndex();
      } else if (bType != (BackupType)primaryData.getIndex()) {
         return nullptr;
      }

      //reconstitute compressed pubkey
      BinaryWriter bw;
      bw.put_uint8_t(prefix == 1 ? 0x02 : 0x03);
      bw.put_BinaryDataRef(primaryData.data);

      //create and return seed
      std::unique_ptr<ClearTextSeed_ArmoryPublic> seed;
      switch (bType)
      {
         case BackupType::Armory135a:
         case BackupType::Armory135c:
         {
            seed = std::make_unique<ClearTextSeed_ArmoryPublic>(
               bw.getDataRef(), secondaryData.data,
               LegacyType::Armory135
            );
            break;
         }

         case BackupType::Armory200a:
         {
            seed = std::make_unique<ClearTextSeed_ArmoryPublic>(
               bw.getDataRef(), secondaryData.data,
               LegacyType::Armory200
            );
            break;
         }

         default:
            return nullptr;
      }

      if (seed->getRawId() != wltIdRef) {
         return nullptr;
      }
      return seed;
   }
}

////////////////////////////////////////////////////////////////////////////////
// Exceptions
RestoreUserException::RestoreUserException(const std::string& errMsg) :
   std::runtime_error(errMsg)
{}

Easy16RepairError::Easy16RepairError(const std::string& errMsg) :
   std::runtime_error(errMsg)
{}

////////////////////////////////////////////////////////////////////////////////
// Easy16Codec
std::vector<SecureBinaryData> Easy16Codec::encode(
   const BinaryDataRef data, BackupType bType, bool isPriv)
{
   uint8_t index = (uint8_t)bType;
   if (bType == BackupType::Armory135c) {
      //index for 135a/c should be 0
      index = 0;
   }
   if (index > EASY16_INDEX_MAX) {
      throw std::runtime_error("index is too large");
   }

   BinaryRefReader brr(data);
   uint32_t count = (data.getSize() + EASY16_LINE_LENGTH - 1) /
      EASY16_LINE_LENGTH;
   std::vector<SecureBinaryData> result;
   result.reserve(count);

   for (unsigned i=0; i<count; i++) {
      size_t len = std::min(
         size_t(EASY16_LINE_LENGTH),
         brr.getSizeRemaining()
      );
      auto chunk = brr.get_BinaryDataRef(len);
      result.emplace_back(encodeEasy16Line(chunk, index, isPriv));
   }
   return result;
}

////////
BackupEasy16DecodeResult Easy16Codec::decode(
   const std::vector<SecureBinaryData>& lines)
{
   std::vector<BinaryDataRef> refVec;
   refVec.reserve(lines.size());
   for (const auto& line : lines) {
      refVec.emplace_back(line.getRef());
   }
   return decode(refVec);
}

BackupEasy16DecodeResult Easy16Codec::decode(const std::vector<BinaryDataRef>& lines)
{
   if (lines.empty()) {
      throw std::runtime_error("empty easy16 code");
   }

   size_t fullSize = lines.size() * EASY16_LINE_LENGTH;
   SecureBinaryData data(fullSize);
   std::vector<int> checksumIndexes;
   std::vector<BinaryData> checksums(lines.size());

   auto dataPtr = data.getPtr();
   size_t pos = 0;
   for (unsigned i=0; i<lines.size(); i++) {
      const auto& line = lines[i];
      size_t len = fullSize - pos;
      auto result = decodeEasy16Line(dataPtr + pos, len, line, checksums[i]);

      pos += len;
      switch (result)
      {
         case -1: //could not match checksum
         case -2: //invalid checksum length
         {
            checksumIndexes.emplace_back(result);
            break;
         }

         case -3:
         {
            //ran out of space in result buffer
            throw std::runtime_error("easy16 decode buffer is too short");
         }

         default:
            //valid checksum
            checksumIndexes.emplace_back(result);
      }

      if (len > EASY16_LINE_LENGTH) {
         throw std::runtime_error("easy16 line is too long");
      } else if (len < EASY16_LINE_LENGTH) {
         if (i != lines.size() - 1) {
            throw std::runtime_error("easy16 line is too short");
         }

         //last line doesn't have to be EASY16_LINE_LENGTH bytes long
         data.resize(pos);
      }
   }

   BackupEasy16DecodeResult result;
   result.checksumIndexes = std::move(checksumIndexes);
   result.checksums = std::move(checksums);
   result.data = std::move(data);
   return result;
}

////////
bool Easy16Codec::repair(BackupEasy16DecodeResult& faultyBackup)
{
   //sanity check
   if (faultyBackup.data.empty() || faultyBackup.checksums.empty() ||
      faultyBackup.checksums.size() != faultyBackup.checksumIndexes.size())
   {
      throw Easy16RepairError("invalid arugments");
   }

   //is there an error?
   bool hasError = false;
   std::set<int> validIndexes;
   for (auto index : faultyBackup.checksumIndexes) {
      auto indexIter = Easy16Codec::eligibleIndexes.find((BackupType)index);
      if (indexIter == Easy16Codec::eligibleIndexes.end()) {
         if (index == EASY16_INVALID_CHECKSUM_INDEX) {
            hasError = true;
            continue;
         } else {
            //these errors cannot be repaired
            throw Easy16RepairError("fatal checksum error");
         }
      }

      validIndexes.insert(index);
   }

   if (!hasError && validIndexes.size() == 1) {
      return true;
   }

   /* checksum search function */
   auto searchChecksum = [](
      const BinaryDataRef& data, const BinaryData& checksum, uint8_t hint)
      ->std::map<unsigned, std::map<unsigned, std::set<uint8_t>>>
   {
      std::map<unsigned, std::map<unsigned, std::set<uint8_t>>> result;

      //copy the data
      SecureBinaryData copied(data);

      //run through each byte of data
      for (unsigned i=0; i<data.getSize(); i++) {
         auto& valRef = copied.getPtr()[i];
         auto originalValue = valRef;

         for (unsigned y=0; y<256; y++) {
            if (y == originalValue) {
               continue;
            }

            //set new value
            valRef = y;

            //check it
            if (hint != EASY16_INVALID_CHECKSUM_INDEX) {
               auto hash = getHash(copied, hint);
               if (hash.getSliceRef(0, 2) == checksum) {
                  auto& chkVal = result[hint];
                  auto& pos = chkVal[i];
                  pos.insert(y);
               }
            } else {
               //check all eligible indexes
               for (const auto& indexCandidate : Easy16Codec::eligibleIndexes) {
                  auto hash = getHash(copied, (uint8_t)indexCandidate);
                  if (hash.getSliceRef(0, 2) == checksum) {
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
   if (validIndexes.size() > 1) {
      //there's more than one checksum index, cannot proceed
      throw Easy16RepairError("checksum results mismatch");
   } else if (validIndexes.size() == 1) {
      /*
      Some lines are invalid but we have at least one that is valid. This
      allows us to search for the expected checksum index in the invalid
      lines (they should all match)
      */
      unsigned hint = *validIndexes.begin();

      BinaryRefReader brr(faultyBackup.data);
      for (unsigned i=0; i<faultyBackup.checksumIndexes.size(); i++) {
         if (faultyBackup.checksumIndexes[i] != EASY16_INVALID_CHECKSUM_INDEX) {
            brr.advance(
               std::min(size_t(EASY16_LINE_LENGTH), brr.getSizeRemaining()));
            faultyBackup.repairedIndexes.push_back(hint);
            continue;
         }

         auto dataRef = brr.get_BinaryDataRef(
            std::min(size_t(EASY16_LINE_LENGTH), brr.getSizeRemaining()));

         auto repairResults =
            searchChecksum(dataRef, faultyBackup.checksums[i], hint);

         if (repairResults.size() != 1) {
            return false;
         }

         auto repairIter = repairResults.begin();
         if (repairIter->second.size() != 1) {
            return false;
         }

         const auto& repairPair = *repairIter->second.begin();
         if (repairPair.second.size() != 1) {
            return false;
         }

         //apply repair on the fly
         auto ptr = (uint8_t*)(dataRef.getPtr() + repairPair.first);
         *ptr = *repairPair.second.begin();

         //update the repaired line checksum result
         faultyBackup.repairedIndexes.push_back(hint);
      }
   } else {
      /*
      All lines are invalid. There is no indication of what the checksum index
      ought to be. We have to search all lines for a matching index.
      */
      std::vector<std::map<unsigned, std::map<unsigned, std::set<uint8_t>>>> resultMap;

      BinaryRefReader brr(faultyBackup.data);
      for (unsigned i=0; i<faultyBackup.checksumIndexes.size(); i++) {
         auto dataRef = brr.get_BinaryDataRef(
            std::min(size_t(EASY16_LINE_LENGTH), brr.getSizeRemaining()));

         auto repairResults = searchChecksum(
            dataRef, faultyBackup.checksums[i], -1);

         if (repairResults.empty()) {
            return false;
         }
         resultMap.emplace_back(std::move(repairResults));
      }

      //compare results for index matches
      std::map<unsigned, std::set<unsigned>> chksumIndexes;
      for (unsigned i=0; i<resultMap.size(); i++) {
         const auto& lineResult = resultMap[i];
         for (const auto& lineData : lineResult) {
            //skip on multiple solutions
            if (lineData.second.size() != 1) {
               continue;
            }
            if (lineData.second.begin()->second.size() != 1) {
               continue;
            }
            auto& chkValueSet = chksumIndexes[lineData.first];
            chkValueSet.insert(i);
         }
      }

      //only those indexes represented across all lines are eligible
      auto iter = chksumIndexes.begin();
      while (iter != chksumIndexes.end()) {
         if (iter->second.size() != faultyBackup.checksumIndexes.size()) {
            chksumIndexes.erase(iter++);
            continue;
         }
         ++iter;
      }

      //fail if we have several repair candidates
      if (chksumIndexes.size() != 1) {
         return false;
      }

      //repair the data
      brr.resetPosition();
      auto repairIndex = chksumIndexes.begin()->first;
      for (unsigned i=0; i<faultyBackup.checksumIndexes.size(); i++) {
         const auto& lineResult = resultMap[i];
         auto lineIter = lineResult.find(repairIndex);
         if (lineIter == lineResult.end()) {
            return false;
         }

         //do not tolerate multiple solutions
         if (lineIter->second.size() != 1) {
            return false;
         }

         auto valIter = lineIter->second.begin();
         if (valIter->second.size() != 1) {
            return false;
         }

         auto dataRef = brr.get_BinaryDataRef(
            std::min(size_t(EASY16_LINE_LENGTH), brr.getSizeRemaining()));

         auto ptr = (uint8_t*)(dataRef.getPtr() + valIter->first);
         *ptr = *valIter->second.begin();

         //update the repaired line checksum result
         faultyBackup.repairedIndexes.push_back(repairIndex);
      }
   }

   return true;
}

////////////////////////////////////////////////////////////////////////////////
// BackupEasy16DecodeResult
bool BackupEasy16DecodeResult::isInitialized() const
{
   return checksumIndexes.size() == 2;
}

////
int BackupEasy16DecodeResult::getIndex() const
{
   if (!isInitialized()) {
      return -1;
   }

   if (repairedIndexes.size() == 2) {
      if (repairedIndexes[0] == repairedIndexes[1]) {
         return repairedIndexes[0];
      }
   } else {
      if (checksumIndexes[0] == checksumIndexes[1]) {
         return checksumIndexes[0];
      }
   }
   return -1;
}

bool BackupEasy16DecodeResult::isValid() const
{
   if (!isInitialized()) {
      return false;
   }

   return
      Easy16Codec::eligibleIndexes.find((BackupType)getIndex()) !=
      Easy16Codec::eligibleIndexes.end();
}

////////////////////////////////////////////////////////////////////////////////
// SecurePrint
SecurePrint::SecurePrint()
{
   //setup aes IV and kdf
   auto iv32 = BtcUtils::getHash256(
      (const uint8_t*)digitsPI.data(), digitsPI.size());
   iv16_ = std::move(iv32.getSliceCopy(
      0, Cryptography::Encryption::AES::BLOCK_SIZE));

   salt_ = std::move(BtcUtils::getHash256(
      (const uint8_t*)digitsE.data(), digitsE.size()));
}

////////
const SecureBinaryData& SecurePrint::getPassphrase() const
{
   return passphrase_;
}

std::pair<SecureBinaryData, SecureBinaryData> SecurePrint::encrypt(
   BinaryDataRef root, BinaryDataRef chaincode)
{
   /*
   1. generate passphrase from root and chaincode
   */

   //sanity check
   if (root.getSize() != 32) {
      LOGERR << "invalid root size for secureprint";
      throw std::runtime_error("invalid root size for secureprint");
   }

   SecureBinaryData hmacPhrase(64);
   if (chaincode.empty()) {
      /*
      The passphrase is the hmac of the root and the chaincode. If the 
      chaincode is empty, we only hmac the root.
      */

      auto rootHash = BtcUtils::getHash256(root);
      BtcUtils::getHMAC512(
         rootHash.getPtr(), rootHash.getSize(),
         salt_.getPtr(), salt_.getSize(),
         hmacPhrase.getPtr());
   } else {
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
   Wallets::Encryption::KdfRomix kdf{spKDFBytes, 1, salt_.getRef()};
   auto encryptionKey = kdf.DeriveKey(passphrase_);

   /*
   3. Encrypt the data. We use the libbtc call directly because
      we do not want padding
   */

   auto encrypt = [this, &encryptionKey](
      const SecureBinaryData& cleartext, SecureBinaryData& result)->bool
   {
      //this exclusively encrypt 32 bytes of data
      if (cleartext.getSize() != 32) {
         return false;
      }

      //make sure result buffer is large enough
      result.resize(32);

      //encrypt with CBC
      auto encrLen = aes256_cbc_encrypt(
         encryptionKey.getPtr(), iv16_.getPtr(),
         cleartext.getPtr(), cleartext.getSize(),
         0, //no padding
         result.getPtr());

      if (encrLen != 32) {
         return false;
      }
      return true;
   };

   std::pair<SecureBinaryData, SecureBinaryData> result;
   if (!encrypt(root, result.first)) {
      LOGERR << "SecurePrint encryption failure";
      throw std::runtime_error("SecurePrint encryption failure");
   }

   if (!chaincode.empty()) {
      if (!encrypt(chaincode, result.second)) {
         LOGERR << "SecurePrint encryption failure";
         throw std::runtime_error("SecurePrint encryption failure");
      }
   }

   return result;
}

SecureBinaryData SecurePrint::decrypt(
   const SecureBinaryData& ciphertext, const BinaryDataRef passphrase) const
{
   //check passphrase checksum
   //TODO: try with std::string_view instead
   std::string passStr(passphrase.toCharPtr(), passphrase.getSize());
   BinaryData passBin;
   try {
      passBin = std::move(BtcUtils::base58_decode(passStr));
   } catch (const std::exception&) {
      LOGERR << "invalid SecurePrint passphrase";
      throw std::runtime_error("invalid SecurePrint passphrase");
   }

   if (passBin.getSize() != 8) {
      LOGERR << "invalid SecurePrint passphrase";
      throw std::runtime_error("invalid SecurePrint passphrase");
   }

   BinaryRefReader brr(passBin);
   auto passBase = brr.get_BinaryDataRef(7);
   auto checksum = brr.get_uint8_t();

   auto passHash = BtcUtils::getHash256(passBase);
   if (passHash[0] != checksum) {
      LOGERR << "invalid SecurePrint passphrase";
      throw std::runtime_error("invalid SecurePrint passphrase");
   }

   if (ciphertext.getSize() < 32) {
      LOGERR << "invalid ciphertext size for SecurePrint";
      throw std::runtime_error("invalid ciphertext size for SecurePrint");
   }

   //kdf the passphrase
   Wallets::Encryption::KdfRomix kdf{spKDFBytes, 1, salt_.getRef()};
   auto encryptionKey = kdf.DeriveKey(passphrase);

   //
   auto decrypt = [this, &encryptionKey](
      const BinaryDataRef& ciphertext, SecureBinaryData& result)->bool
   {
      //works exclusively on 32 byte packets
      if (ciphertext.getSize() != 32) {
         return false;
      }
      result.resize(32);

      auto size = aes256_cbc_decrypt(
         encryptionKey.getPtr(), iv16_.getPtr(),
         ciphertext.getPtr(), ciphertext.getSize(),
         0, //no padding
         result.getPtr());

      if (size != 32) {
         return false;
      }
      return true;
   };

   //decrypt the root
   SecureBinaryData result;
   if (!decrypt(ciphertext, result)) {
      LOGERR << "failed to decrypt SecurePrint string";
      throw std::runtime_error("failed to decrypt SecurePrint string");
   }
   return result;
}

////////////////////////////////////////////////////////////////////////////////
// Helpers
std::unique_ptr<WalletBackup> Helpers::getWalletBackup(
   std::shared_ptr<Wallets::AssetWallet_Single> wltPtr, bool isPriv,
   BackupType bType)
{
   std::unique_ptr<ClearTextSeed> clearTextSeed;

   //grab encrypted seed from wallet
   if (isPriv) {
      auto wltSeed = wltPtr->getEncryptedSeed();
      if (wltSeed != nullptr) {
         const auto& rawClearTextSeed = wltPtr->getDecryptedValue(wltSeed);
         clearTextSeed = ClearTextSeed::deserialize(rawClearTextSeed);
      } else {
         //wallet has no seed, maybe it's a legacy Armory wallet, where
         //the seed and root are the same
         auto root = std::dynamic_pointer_cast<Assets::AssetEntry_ArmoryLegacyRoot>(
            wltPtr->getRoot());
         if (root == nullptr) {
            return nullptr;
         }
         const auto& rootPrivKey =
            wltPtr->getDecryptedPrivateKeyForAsset(root);
         clearTextSeed = std::unique_ptr<ClearTextSeed>(
            new ClearTextSeed_Armory(rootPrivKey, root->getChaincode(),
               root->getSeedType()));
      }
   } else {
      auto root = std::dynamic_pointer_cast<Assets::AssetEntry_ArmoryLegacyRoot>(
         wltPtr->getRoot());
      if (root == nullptr) {
         LOGWARN << "public backups needs implemented for non legacy roots!";
         return nullptr;
      }
      clearTextSeed = std::unique_ptr<ClearTextSeed>(
         new ClearTextSeed_ArmoryPublic(root->getPubKey(), root->getChaincode(),
            root->getSeedType()));
   }

   if (clearTextSeed == nullptr) {
      throw std::runtime_error(
         "[getWalletBackup] could not get seed from wallet");
   }

   //pick default backup type for seed if not set explicitly
   if (bType == BackupType::Invalid) {
      bType = clearTextSeed->getPreferedBackupType();
   }
   auto backup = getWalletBackup(move(clearTextSeed), bType);
   backup->wltId_ = wltPtr->getID();
   return backup;
}

std::unique_ptr<WalletBackup> Helpers::getWalletBackup(
   std::unique_ptr<ClearTextSeed> seed, BackupType bType)
{
   //sanity check
   if (!seed->isBackupTypeEligible(bType)) {
      throw std::runtime_error("[getWalletBackup] ineligible backup type");
   }

   switch (bType)
   {
      case BackupType::Armory135a:
      case BackupType::Armory135c:
      case BackupType::Armory200a:
      case BackupType::Armory200b:
      case BackupType::Armory200c:
      case BackupType::Armory200d:
         return getEasy16BackupString(std::move(seed));

      case BackupType::Base58:
         return getBase58BackupString(std::move(seed));

      case BackupType::BIP39:
         return getBIP39BackupString(std::move(seed));

      default:
         throw std::runtime_error("[getWalletBackup] invalid backup type");
   }
}

////////
std::unique_ptr<WalletBackup> Helpers::getEasy16BackupString(
   std::unique_ptr<ClearTextSeed> seed)
{
   BinaryDataRef primaryData;
   BinaryDataRef secondaryData;
   BackupType mode = BackupType::Invalid;

   switch (seed->type())
   {
      case SeedType::ArmoryLegacy:
      {
         auto seedLegacy   = dynamic_cast<ClearTextSeed_Armory*>(seed.get());
         primaryData       = seedLegacy->getRoot().getRef();
         secondaryData     = seedLegacy->getChaincode().getRef();
         mode              = seed->getPreferedBackupType();
         break;
      }

      case SeedType::ArmoryLegacyPublic:
      {
         auto seedPublic = dynamic_cast<ClearTextSeed_ArmoryPublic*>(seed.get());
         return std::make_unique<Backup_Easy16Public>(
            seedPublic->getPreferedBackupType(),
            seedPublic->getPublicRoot(),
            seedPublic->getChaincode()
         );
      }

      case SeedType::BIP32_Structured:
      case SeedType::BIP32_Virgin:
      case SeedType::BIP39:
      {
         auto seedBip32 = dynamic_cast<ClearTextSeed_BIP32*>(seed.get());
         primaryData    = seedBip32->getRawEntropy().getRef();
         mode           = seed->getPreferedBackupType();

         switch (seed->type())
         {
            case SeedType::BIP39:
            {
               //force Armory200d for BIP39 seeds
               mode = BackupType::Armory200d;
               break;
            }

            default:
               mode = seed->getPreferedBackupType();
         }
         break;
      }

      default:
         throw std::runtime_error("[getEasy16BackupString] invalid seed type");
   }

   //apply secureprint to seed data
   SecurePrint sp;
   auto encrRoot = sp.encrypt(primaryData, secondaryData);

   //set cleartext and encrypted root
   auto lines_clear = Easy16Codec::encode(primaryData, mode);
   auto lines_encr  = Easy16Codec::encode(encrRoot.first, mode);

   auto result = std::make_unique<Backup_Easy16>(mode);
   result->rootClear_ = std::move(lines_clear);
   result->rootEncr_ = std::move(lines_encr);
   if (mode == BackupType::Armory135a) {
      result->chaincodeClear_ = std::move(Easy16Codec::encode(secondaryData, mode));
      result->chaincodeEncr_ = std::move(Easy16Codec::encode(encrRoot.second, mode));
   }
   result->spPass_ = std::move(sp.getPassphrase());
   return result;
}

////////
std::unique_ptr<WalletBackup> Helpers::getBIP39BackupString(
   std::unique_ptr<ClearTextSeed> seed)
{
   //sanity check
   if (seed->type() != SeedType::BIP39) {
      throw std::runtime_error("[getBIP39BackupString] invalid seed type");
   }

   auto seedBip39 = dynamic_cast<ClearTextSeed_BIP39*>(seed.get());
   std::unique_ptr<Backup_BIP39> result;
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
         std::string_view mnemonicView{mnemonicPtr, strlen(mnemonicPtr)};

         //copy mnemonic phrase
         result = Backup_BIP39::fromMnemonicString(mnemonicView);

         //clear libbtc/trezor bip39 mnemonic buffer
         mnemonic_clear();
         break;
      }

      default:
         throw std::runtime_error(
            "[getBIP39BackupString] invalid dictionnary id");
   }
   return result;
}

////////
std::unique_ptr<Backup_Base58> Helpers::getBase58BackupString(
   std::unique_ptr<ClearTextSeed> seed)
{
   auto seedBip32 = dynamic_cast<ClearTextSeed_BIP32*>(seed.get());
   if (seedBip32 == nullptr) {
      throw std::runtime_error("[getBase58BackupString] invalid seed object");
   }
   if (seedBip32->type() != SeedType::BIP32_base58Root) {
      throw std::runtime_error("[getBase58BackupString] invalid seed type");
   }

   auto node = seedBip32->getRootNode();
   auto result = std::make_unique<Backup_Base58>(std::move(node->getBase58()));
   return result;
}

////////////////////////////// -- restore methods -- ///////////////////////////
RestoreResult Helpers::restoreFromBackup(
   std::unique_ptr<WalletBackup> backup, const UserPrompt& callback,
   const Wallets::IO::CreateWalletParams& params)
{
   std::unique_ptr<ClearTextSeed> seed = nullptr;
   auto bType = backup->type();
   switch (bType)
   {
      //easy16 backups
      case BackupType::Armory135a:
      case BackupType::Armory135c:
      case BackupType::Armory200a:
      case BackupType::Armory200b:
      case BackupType::Armory200d:
      case BackupType::Easy16_Unkonwn:
         seed = restoreFromEasy16(std::move(backup), callback, bType);
         break;

      case BackupType::Base58:
         seed = restoreFromBase58(std::move(backup));
         break;

      case BackupType::BIP39:
         seed = restoreFromBIP39(std::move(backup));
         break;

      default:
         break;
   }

   if (seed == nullptr) {
      //could not generate a seed from this backup, halt the call
      throw RestoreUserException(
         std::string{"failed to create seed from backup"sv});
   }

   //prompt user to verify id
   bool merge = false;
   {
      RestorePrompt prompt{RestorePromptType::Id};
      prompt.walletId = seed->getWalletId();
      prompt.backupType = bType;
      auto reply = callback(prompt);
      if (!reply.success) {
         throw RestoreUserException("user rejected id");
      } else if (reply.merge) {
         merge = true;
      }
   }

   //return wallet
   auto wlt = Wallets::AssetWallet_Single::createFromSeed(
      std::move(seed), params);
   return {wlt, merge};
}

////////
std::unique_ptr<ClearTextSeed> Helpers::restoreFromEasy16(
   std::unique_ptr<WalletBackup> backup, const UserPrompt& callback,
   BackupType& bType)
{
   auto backupE16 = dynamic_cast<Backup_Easy16*>(backup.get());
   if (backupE16 == nullptr) {
      auto backupE16Public = dynamic_cast<Backup_Easy16Public*>(backup.get());
      if (backupE16Public != nullptr) {
         return restoreFromEasy16Public(backupE16Public, callback, bType);
      }
      return nullptr;
   }
   bool isEncrypted = !backupE16->getSpPass().empty();

   /* decode data */

   //root
   std::vector<BinaryDataRef> first2Lines;
   first2Lines.reserve(2);

   auto firstLine = backupE16->getRoot(LineIndex::One, isEncrypted);
   first2Lines.emplace_back(BinaryDataRef{
      (uint8_t*)firstLine.data(), firstLine.size()});

   auto secondLine = backupE16->getRoot(LineIndex::Two, isEncrypted);
   first2Lines.emplace_back(BinaryDataRef{
      (uint8_t*)secondLine.data(), secondLine.size()});

   auto primaryData = Easy16Codec::decode(first2Lines);
   if (!primaryData.isInitialized()) {
      return nullptr;
   }

   //chaincode
   BackupEasy16DecodeResult secondaryData;
   if (backupE16->hasChaincode()) {
      std::vector<BinaryDataRef> next2Lines;
      auto thirdLine = backupE16->getChaincode(LineIndex::One, isEncrypted);
      next2Lines.emplace_back(BinaryDataRef{
         (uint8_t*)thirdLine.data(), thirdLine.size()});

      auto fourthLine = backupE16->getChaincode(LineIndex::Two, isEncrypted);
      next2Lines.emplace_back(BinaryDataRef{
         (uint8_t*)fourthLine.data(), fourthLine.size()});

      secondaryData = Easy16Codec::decode(next2Lines);
      if (!secondaryData.isInitialized()) {
         return nullptr;
      }
   }

   /* checksums & repair */

   //root
   if (!primaryData.isValid()) {
      if (!Easy16Codec::repair(primaryData)) {
         RestorePrompt prompt{RestorePromptType::ChecksumError};
         for (unsigned i=0; i < primaryData.checksumIndexes.size(); i++) {
            prompt.checksumResult.emplace(i, primaryData.checksumIndexes[i]);
         }
         callback(prompt);
         return nullptr;
      }

      if (!primaryData.isValid()) {
         RestorePrompt prompt{RestorePromptType::ChecksumError};
         for (unsigned i=0; i < primaryData.repairedIndexes.size(); i++) {
            prompt.checksumResult.emplace(i, primaryData.repairedIndexes[i]);
         }
         callback(prompt);
         return nullptr;
      }
   }

   //chaincode
   if (secondaryData.isInitialized()) {
      if (!Easy16Codec::repair(secondaryData)) {
         RestorePrompt prompt{RestorePromptType::ChecksumError};
         for (unsigned i=0; i < primaryData.checksumIndexes.size(); i++) {
            prompt.checksumResult.emplace(i+2, secondaryData.checksumIndexes[i]);
         }
         callback(prompt);
         return nullptr;
      }

      if (!secondaryData.isValid()) {
         RestorePrompt prompt{RestorePromptType::ChecksumError};
         for (unsigned i=0; i < primaryData.repairedIndexes.size(); i++) {
            prompt.checksumResult.emplace(i+2, secondaryData.repairedIndexes[i]);
         }
         callback(prompt);
         return nullptr;
      }

      //check chaincode index matches root index
      if (primaryData.getIndex() != secondaryData.getIndex()) {
         RestorePrompt prompt{RestorePromptType::ChecksumMismatch};
         prompt.checksumResult.emplace(0, primaryData.getIndex());
         prompt.checksumResult.emplace(1, secondaryData.getIndex());
         callback(prompt);
         return nullptr;
      }
   }

   /* SecurePrint */
   if (isEncrypted) {
      try {
         SecurePrint sp;
         auto pass = backupE16->getSpPass();
         BinaryDataRef passRef{(uint8_t*)pass.data(), pass.size()};
         primaryData.data = std::move(sp.decrypt(primaryData.data, passRef));

         if (secondaryData.isInitialized()) {
            secondaryData.data = std::move(sp.decrypt(secondaryData.data, passRef));
         }
      } catch (const std::exception&) {
         callback(RestorePrompt{RestorePromptType::DecryptError});
         throw RestoreUserException("invalid SP pass");
      }
   }

   /* backup type */
   if (bType == BackupType::Easy16_Unkonwn) {
      bType = (BackupType)primaryData.getIndex();
      if (bType == BackupType::Armory135a && !secondaryData.isInitialized()) {
         bType = BackupType::Armory135c;
      }
   } else {
      if ((BackupType)primaryData.getIndex() != bType) {
         RestorePrompt prompt{RestorePromptType::ChecksumMismatch};
         prompt.checksumResult.emplace(0, primaryData.getIndex());
         prompt.checksumResult.emplace(UINT8_MAX, (int)bType);
         callback(prompt);
         return nullptr;
      }
   }

   /* create seed */
   std::unique_ptr<ClearTextSeed> seedPtr = nullptr;
   switch (bType)
   {
      case BackupType::Armory135a:
      case BackupType::Armory135c:
      {
         /*legacy armory wallet, legacy backup string*/
         seedPtr = std::move(std::make_unique<ClearTextSeed_Armory>(
            primaryData.data, secondaryData.data,
            LegacyType::Armory135));
         break;
      }

      case BackupType::Armory200a:
      {
         /*legacy armory wallet, indexed backup string*/
         seedPtr = std::move(std::make_unique<ClearTextSeed_Armory>(
            primaryData.data, secondaryData.data,
            LegacyType::Armory200));
         break;
      }

      //bip32 wallets
      case BackupType::Armory200b:
      {
         /*BIP32 wallet with BIP44/49/84 accounts*/
         seedPtr = std::move(std::make_unique<ClearTextSeed_BIP32>(
            primaryData.data, SeedType::BIP32_Structured));
         break;
      }

      case BackupType::Armory200c:
      {
         //empty BIP32 wallet
         seedPtr = std::move(std::make_unique<ClearTextSeed_BIP32>(
            primaryData.data, SeedType::BIP32_Virgin));
         break;
      }

      case BackupType::Armory200d:
      {
         //empty BIP32 wallet
         seedPtr = std::move(std::make_unique<ClearTextSeed_BIP39>(
            primaryData.data,
            ClearTextSeed_BIP39::Dictionnary::English_Trezor));
         break;
      }

      default:
         return nullptr;
   }
   return seedPtr;
}

////////
std::unique_ptr<ClearTextSeed> Helpers::restoreFromBase58(
   std::unique_ptr<WalletBackup> backup)
{
   auto backupB58 = dynamic_cast<Backup_Base58*>(backup.get());
   if (backupB58 == nullptr) {
      return nullptr;
   }

   std::unique_ptr<ClearTextSeed_BIP32> seed;
   try {
      auto b58StrView = backupB58->getBase58String();
      BinaryData b58Ref(b58StrView.data(), b58StrView.size());
      return ClearTextSeed_BIP32::fromBase58(b58Ref);
   } catch (const std::exception&) {
      return nullptr;
   }
}

////////
std::unique_ptr<ClearTextSeed> Helpers::restoreFromBIP39(
   std::unique_ptr<WalletBackup> backup)
{
   auto backupBIP39 = dynamic_cast<Backup_BIP39*>(backup.get());
   if (backupBIP39 == nullptr) {
      return nullptr;
   }
   const char* mnemonic = backupBIP39->getMnemonicString().data();

   //check mnemonic phrase
   if (mnemonic_check(mnemonic) == 0) {
      return nullptr;
   }

   //convert mnemonic phrase to raw entropy
   SecureBinaryData rawEntropy(33); //max entropy size + checksum
   auto lenInBits = mnemonic_to_bits(mnemonic, rawEntropy.getPtr());

   if (lenInBits == 0) {
      return nullptr;
   }

   //strip out checksum bits
   auto lenInBytes = lenInBits / 8;
   lenInBytes -= lenInBytes % 8;
   rawEntropy.resize(lenInBytes);

   //entropy to seed
   return std::make_unique<ClearTextSeed_BIP39>(rawEntropy,
      ClearTextSeed_BIP39::Dictionnary::English_Trezor);
}

////////////////////////////////////////////////////////////////////////////////
// WalletBackup
WalletBackup::WalletBackup(BackupType bType) :
   type_(bType)
{}

WalletBackup::~WalletBackup()
{}

const std::string& WalletBackup::getWalletId() const
{
   return wltId_;
}

const BackupType& WalletBackup::type() const
{
   return type_;
}

////////////////////////////////////////////////////////////////////////////////
// Backup_Easy16
Backup_Easy16::Backup_Easy16(BackupType bType) :
   WalletBackup(bType)
{}

Backup_Easy16::~Backup_Easy16()
{}

bool Backup_Easy16::hasChaincode() const
{
   return !chaincodeClear_.empty() || !chaincodeEncr_.empty();
}

////
std::string_view Backup_Easy16::getRoot(LineIndex li, bool encrypted) const
{
   auto lineIndex = (int)li;
   std::vector<SecureBinaryData>::const_iterator iter;
   if (!encrypted) {
      iter = rootClear_.begin() + lineIndex;
      if (iter == rootClear_.end()) {
         throw std::runtime_error("[Backup_Easy16::getRoot]"
         " missing cleartext line");
      }
   } else {
      iter = rootEncr_.begin() + lineIndex;
      if (iter == rootEncr_.end()) {
         throw std::runtime_error("[Backup_Easy16::getRoot]"
         " missing encrypted line");
      }
   }

   //all e16 backup strings come with a padded null byte, capnp expects this
   //byte at buffer[size], so we do not cover it with the string_view
   return {iter->getCharPtr(), iter->getSize() - 1};
}

std::string_view Backup_Easy16::getChaincode(LineIndex li, bool encrypted) const
{
   /*see Backup_Easy16::getRoot comment for the -1*/

   auto lineIndex = (int)li;
   std::vector<SecureBinaryData>::const_iterator iter;
   if (!encrypted) {
      iter = chaincodeClear_.begin() + lineIndex;
      if (iter == chaincodeClear_.end()) {
         throw std::runtime_error("[Backup_Easy16::getChaincode]"
            " missing cleartext line");
      }
   } else {
      iter = chaincodeEncr_.begin() + lineIndex;
      if (iter == chaincodeEncr_.end()) {
         throw std::runtime_error("[Backup_Easy16::getChaincode]"
            " missing encrypted line");
      }
   }
   return {iter->getCharPtr(), iter->getSize() - 1};
}

std::string_view Backup_Easy16::getSpPass() const
{
   if (spPass_.empty()) {
      return {};
   }
   return {spPass_.getCharPtr(), spPass_.getSize()};
}

////
std::unique_ptr<Backup_Easy16> Backup_Easy16::fromLines(
   const std::vector<std::string_view>& lines, std::string_view spPass)
{
   if (lines.size() % 2 != 0) {
      throw std::runtime_error("[Backup_Easy16::fromLines] invalid line count");
   }
   auto result = std::make_unique<Backup_Easy16>(BackupType::Easy16_Unkonwn);
   unsigned i=0;

   if (spPass.empty()) {
      for (const auto& line : lines) {
         auto lineSBD = SecureBinaryData::fromStringView(line);
         if (i<2) {
            result->rootClear_.emplace_back(std::move(lineSBD));
         } else {
            result->chaincodeClear_.emplace_back(std::move(lineSBD));
         }
         ++i;
      }
   } else {
      for (const auto& line : lines) {
         auto lineSBD = SecureBinaryData::fromStringView(line);
         if (i<2) {
            result->rootEncr_.emplace_back(std::move(lineSBD));
         } else {
            result->chaincodeEncr_.emplace_back(std::move(lineSBD));
         }
         ++i;
      }
      result->spPass_ = SecureBinaryData::fromStringView(spPass);
   }
   return result;
}

////////////////////////////////////////////////////////////////////////////////
// Backup_Easy16Public
Backup_Easy16Public::Backup_Easy16Public(BackupType bType) :
   WalletBackup(bType)
{}

Backup_Easy16Public::Backup_Easy16Public(BackupType bType,
   const SecureBinaryData& pubRoot, const SecureBinaryData& chaincode) :
   WalletBackup(bType)
{
   //prepare the pubkey, we need both uncompressed and compressed versions
   SecureBinaryData pubkeyComp, pubkeyUnc;
   if (pubRoot.getSize() == 65) {
      pubkeyComp = Cryptography::ECDSA::compressPoint(pubRoot);
      pubkeyUnc = pubRoot;
   } else {
      pubkeyComp = pubRoot;
      pubkeyUnc = Cryptography::ECDSA::uncompressPoint(pubRoot);
   }

   //prepare backupId for computation
   auto walletId = Wallets::generateWalletIdRaw(pubkeyUnc, chaincode,
      SeedType::ArmoryLegacyPublic);
   uint8_t prefix = 1;
   if (pubkeyComp.getPtr()[0] == 0x03) {
      prefix ^= 0x80;
   }
   BinaryWriter bw;
   bw.put_uint8_t(prefix);
   bw.put_BinaryData(walletId);

   //compute the easy16 line
   uint8_t typeInt = (bType == BackupType::Armory135c) ? 0 : (uint8_t)bType;
   backupId_ = encodeEasy16Line(bw.getDataRef(), typeInt, false);

   //compressed pubkey is turned to easy16 without the leading sign byte
   BinaryDataRef pubkey32{pubkeyComp.getPtr() + 1, 32};
   publicRoot_ = Easy16Codec::encode(pubkey32, bType, false);
   chaincode_ = Easy16Codec::encode(chaincode, bType, false);
}

Backup_Easy16Public::~Backup_Easy16Public()
{}

////////
std::string_view Backup_Easy16Public::getBackupId() const
{
   /*see Backup_Easy16::getRoot comment for the -1*/
   return {backupId_.getCharPtr(), backupId_.getSize() - 1};
}

std::string_view Backup_Easy16Public::getPublicRoot(LineIndex li) const
{
   /*see Backup_Easy16::getRoot comment for the -1*/

   if (publicRoot_.size() != 2) {
      throw std::runtime_error("publicRoot is invalid");
   }

   switch (li)
   {
      case LineIndex::One:
         return std::string_view{
            publicRoot_[0].getCharPtr(),
            publicRoot_[0].getSize() - 1
         };

      case LineIndex::Two:
         return std::string_view{
            publicRoot_[1].getCharPtr(),
            publicRoot_[1].getSize() - 1
         };

      default:
         throw std::runtime_error("invalid line index for public root");
   }
}

std::string_view Backup_Easy16Public::getChaincode(LineIndex li) const
{
   /*see Backup_Easy16::getRoot comment for the -1*/

   if (chaincode_.size() != 2) {
      throw std::runtime_error("chaincode is invalid");
   }

   switch (li)
   {
      case LineIndex::One:
         return std::string_view{
            chaincode_[0].getCharPtr(),
            chaincode_[0].getSize() - 1
         };

      case LineIndex::Two:
         return std::string_view{
            chaincode_[1].getCharPtr(),
            chaincode_[1].getSize() - 1
         };

      default:
         throw std::runtime_error("invalid line index for chaincode");
   }
}

////////
std::unique_ptr<Backup_Easy16Public> Backup_Easy16Public::fromLines(
   const std::vector<std::string_view>& lines)
{
   if (lines.size() != 5) {
      throw std::runtime_error("need 5 lines for Backup_Easy16Public restore");
   }

   std::unique_ptr<Backup_Easy16Public> result(
      new Backup_Easy16Public(BackupType::Easy16_Unkonwn));
   result->publicRoot_.reserve(2);
   result->publicRoot_.emplace_back(SecureBinaryData::fromStringView(lines[1]));
   result->publicRoot_.emplace_back(SecureBinaryData::fromStringView(lines[2]));

   result->chaincode_.reserve(2);
   result->chaincode_.emplace_back(SecureBinaryData::fromStringView(lines[3]));
   result->chaincode_.emplace_back(SecureBinaryData::fromStringView(lines[4]));

   result->backupId_ = SecureBinaryData::fromStringView(lines[0]);
   return result;
}

////////////////////////////////////////////////////////////////////////////////
// Backup_Base58
Backup_Base58::Backup_Base58(SecureBinaryData b58String) :
   WalletBackup(BackupType::Base58), b58String_(std::move(b58String))
{}

Backup_Base58::~Backup_Base58()
{}

std::string_view Backup_Base58::getBase58String() const
{
   return {b58String_.getCharPtr(), b58String_.getSize()};
}

std::unique_ptr<Backup_Base58> Backup_Base58::fromString(
   const std::string_view& strV)
{
   return std::make_unique<Backup_Base58>(
      SecureBinaryData::fromStringView(strV));
}

////////////////////////////////////////////////////////////////////////////////
// Backup_BIP39
Backup_BIP39::Backup_BIP39() :
   WalletBackup(BackupType::BIP39), mnemonicString_()
{}

Backup_BIP39::~Backup_BIP39()
{}

std::unique_ptr<Backup_BIP39> Backup_BIP39::fromMnemonicString(std::string_view strV)
{
   //create a SBD with 1 extra byte to account for terminating 0,
   //as trezor-crypto expects null terminate strings
   SecureBinaryData mnemonicSBD(strV.size() + 1);
   memset(mnemonicSBD.getPtr(), 0, strV.size() + 1);
   memcpy(mnemonicSBD.getPtr(), strV.data(), strV.size());

   std::unique_ptr<Backup_BIP39> result(new Backup_BIP39());
   result->mnemonicString_ = std::move(mnemonicSBD);
   return result;
}

std::string_view Backup_BIP39::getMnemonicString() const
{
   return {mnemonicString_.getCharPtr(), mnemonicString_.getSize()};
}

///////////////////////////////// RestorePrompt ////////////////////////////////
bool RestorePrompt::needsReply() const
{
   switch (promptType)
   {
      case RestorePromptType::ControlPassphrase:
      case RestorePromptType::PrivatePassphrase:
      case RestorePromptType::Id:
         return true;

      default:
         return false;
   }
}
