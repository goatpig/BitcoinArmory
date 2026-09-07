////////////////////////////////////////////////////////////////////////////////
//                                                                            //
//  Copyright (C) 2011-2015, Armory Technologies, Inc.                        //
//  Distributed under the GNU Affero General Public License (AGPL v3)         //
//  See LICENSE-ATI or http://www.gnu.org/licenses/agpl.html                  //
//                                                                            //
//                                                                            //
//  Copyright (C) 2016-2025, goatpig                                          //
//  Distributed under the MIT license                                         //
//  See LICENSE-MIT or https://opensource.org/licenses/MIT                    //
//                                                                            //
////////////////////////////////////////////////////////////////////////////////

#include <algorithm>

#include "BtcUtils.h"
#include "BinaryData.h"
#include "Cryptography.h"
#include "ArmoryConfig.h"
#include "OpCodes.h"
#include "TxOutScrRef.h"
#include "varint.h"

#include "btc/segwit_addr.h"
#include "btc/base58.h"

using namespace Armory;
using namespace std::string_view_literals;

////////////////////////////////////////////////////////////////////////////////
// static members
const BinaryData BtcUtils::BadAddress = BinaryData::CreateFromHex(
   "0000000000000000000000000000000000000000");
const BinaryData BtcUtils::EmptyHash  = BinaryData::CreateFromHex(
   "0000000000000000000000000000000000000000000000000000000000000000");

constexpr char BtcUtils::base64Chars[]{
   "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/"};

const std::map<char, uint8_t> BtcUtils::base64Vals = {
   { 'A', 0 }, { 'B', 1 }, { 'C', 2 }, { 'D', 3 }, { 'E', 4 }, { 'F', 5 },
   { 'G', 6 }, { 'H', 7 }, { 'I', 8 }, { 'J', 9 }, { 'K', 10 }, { 'L', 11 },
   { 'M', 12 }, { 'N', 13 }, { 'O', 14 }, { 'P', 15 }, { 'Q', 16 }, { 'R', 17 },
   { 'S', 18 }, { 'T', 19 }, { 'U', 20 }, { 'V', 21 }, { 'W', 22 }, { 'X', 23 },
   { 'Y', 24 }, { 'Z', 25 }, { 'a', 26 }, { 'b', 27 }, { 'c', 28 }, { 'd', 29 },
   { 'e', 30 }, { 'f', 31 }, { 'g', 32 }, { 'h', 33 }, { 'i', 34 }, { 'j', 35 },
   { 'k', 36 }, { 'l', 37 }, { 'm', 38 }, { 'n', 39 }, { 'o', 40 }, { 'p', 41 },
   { 'q', 42 }, { 'r', 43 }, { 's', 44 }, { 't', 45 }, { 'u', 46 }, { 'v', 47 },
   { 'w', 48 }, { 'x', 49 }, { 'y', 50 }, { 'z', 51 }, { '0', 52 }, { '1', 53 },
   { '2', 54 }, { '3', 55 }, { '4', 56 }, { '5', 57 }, { '6', 58 }, { '7', 59 },
   { '8', 60 }, { '9', 61 }, { '+', 62 }, { '/', 63 }
};

////////////////////////////////////////////////////////////////////////////////
// exceptions
DERException::DERException(const std::string& what) :
   std::runtime_error(what)
{}

////////////////////////////////////////////////////////////////////////////////
// hashes
void BtcUtils::getSha256(const uint8_t* data, size_t len,
   BinaryData& hashOutput)
{
   if (hashOutput.getSize() != 32) {
      hashOutput.resize(32);
   }

   BinaryDataRef dataBdr(data, len);
   Cryptography::Hash::getSha256(dataBdr, hashOutput.getPtr());
}

BinaryData BtcUtils::getSha256(const BinaryData& bd)
{
   BinaryData hashOutput;
   getSha256(bd.getPtr(), bd.getSize(), hashOutput);
   return hashOutput;
}

BinaryData BtcUtils::getHMAC256(BinaryDataRef key, BinaryDataRef message)
{
   BinaryData digest;
   digest.resize(32);
   
   getHMAC256(key.getPtr(), key.getSize(),
      message.toCharPtr(), message.getSize(),
      digest.getPtr());
   return digest;
}

//// hash256
void BtcUtils::getHash256(const uint8_t* strToHash, size_t nBytes,
   BinaryData& hashOutput)
{
   if (hashOutput.getSize() != 32) {
      hashOutput.resize(32);
   }

   BinaryDataRef dataBdr(strToHash, nBytes);
   Cryptography::Hash::getHash256(dataBdr, hashOutput.getPtr());
}

BinaryData BtcUtils::getHash256(const uint8_t* strToHash, size_t nBytes)
{
   BinaryData hashOutput;
   hashOutput.resize(32);
   BinaryDataRef dataBdr(strToHash, nBytes);

   Cryptography::Hash::getHash256(dataBdr, hashOutput.getPtr());
   return hashOutput;
}

void BtcUtils::getHash256(const BinaryData& strToHash, BinaryData& hashOutput)
{
   getHash256(strToHash.getPtr(), strToHash.getSize(), hashOutput);
}

void BtcUtils::getHash256(BinaryDataRef strToHash, BinaryData& hashOutput)
{
   getHash256(strToHash.getPtr(), strToHash.getSize(), hashOutput);
}

BinaryData BtcUtils::getHash256(const BinaryData& strToHash)
{
   BinaryData hashOutput;
   hashOutput.resize(32);
   getHash256(strToHash.getPtr(), strToHash.getSize(), hashOutput);
   return hashOutput;
}

BinaryData BtcUtils::getHash256(const BinaryDataRef& strToHash)
{
   BinaryData hashOutput;
   hashOutput.resize(32);
   getHash256(strToHash.getPtr(), strToHash.getSize(), hashOutput);
   return hashOutput;
}

//// hash160
void BtcUtils::getHash160(const uint8_t* strToHash, size_t nBytes,
   BinaryData& hashOutput)
{
   if (hashOutput.getSize() != 20) {
      hashOutput.resize(20);
   }

   BinaryDataRef bdr(strToHash, nBytes);
   BinaryData sha2_digest;
   sha2_digest.resize(32);

   Cryptography::Hash::getSha256(bdr, sha2_digest.getPtr());
   Cryptography::Hash::getHash160(sha2_digest.getRef(), hashOutput.getPtr());
}

BinaryData BtcUtils::getHash160(const uint8_t* strToHash, size_t nBytes)
{
   BinaryData hashOutput;
   hashOutput.resize(20);
   getHash160(strToHash, nBytes, hashOutput);
   return hashOutput;
}

void BtcUtils::getHash160(BinaryDataRef strToHash, BinaryData& hashOutput)
{
   getHash160(strToHash.getPtr(), strToHash.getSize(), hashOutput);
}

BinaryData BtcUtils::getHash160(const BinaryDataRef& strToHash)
{
   BinaryData hashOutput;
   hashOutput.resize(20);
   getHash160(strToHash.getPtr(), strToHash.getSize(), hashOutput);
   return hashOutput;
}

BinaryData BtcUtils::getHash160(const BinaryData& strToHash)
{
   BinaryData hashOutput;
   hashOutput.resize(20);
   getHash160(strToHash.getPtr(), strToHash.getSize(), hashOutput);
   return hashOutput;
}

BinaryData BtcUtils::ripemd160(const BinaryData& strToHash)
{
   BinaryData bd;
   bd.resize(20);
   Cryptography::Hash::getHash160(strToHash.getRef(), bd.getPtr());
   return bd;
}

//// HMACs
BinaryData BtcUtils::getHMAC512(BinaryDataRef key, BinaryDataRef message)
{
   BinaryData digest;
   digest.resize(64);

   getHMAC512(key.getPtr(), key.getSize(),
      message.toCharPtr(), message.getSize(),
      digest.getPtr());
   return digest;
}

BinaryData BtcUtils::getHMAC256(BinaryDataRef key,
   const std::string& message)
{
   BinaryData digest;
   digest.resize(32);

   getHMAC256(key.getPtr(), key.getSize(),
      message.c_str(), message.size(),
      digest.getPtr());
   return digest;
}

BinaryData BtcUtils::getHMAC512(BinaryDataRef key,
   const std::string& message)
{
   BinaryData digest;
   digest.resize(64);

   getHMAC512(key.getPtr(), key.getSize(),
      message.c_str(), message.size(),
      digest.getPtr());
   return digest;
}

SecureBinaryData BtcUtils::getHMAC512(const std::string& key,
   BinaryDataRef message)
{
   SecureBinaryData digest;
   digest.resize(64);

   getHMAC512(key.c_str(), key.size(),
      message.getPtr(), message.getSize(),
      digest.getPtr());
   return digest;
}

void BtcUtils::getHMAC256(const uint8_t* keyptr, size_t keylen,
   const char* msgptr, size_t msglen, uint8_t* digest)
{
   BinaryDataRef key_bdr(keyptr, keylen);
   BinaryDataRef msg_bdr((uint8_t*)msgptr, msglen);
   Cryptography::Hash::getHMAC256(key_bdr, msg_bdr, digest);
}

void BtcUtils::getHMAC512(const void* keyptr, size_t keylen,
   const void* msgptr, size_t msglen, void* digest)
{
   BinaryDataRef key_bdr((uint8_t*)keyptr, keylen);
   BinaryDataRef msg_bdr((uint8_t*)msgptr, msglen);
   Cryptography::Hash::getHMAC512(key_bdr, msg_bdr, (uint8_t*)digest);
}

BinaryData BtcUtils::getBotchedArmoryHMAC256(
   BinaryDataRef key, BinaryDataRef msg)
{
   BinaryData hmacKey;
   if (key.getSize() > 32) {
      hmacKey = BtcUtils::getSha256(key);
   } else if (key.getSize() <= 32) {
      hmacKey.resize(32);
      memcpy(hmacKey.getPtr(), key.getPtr(), key.getSize());
      memset(hmacKey.getPtr() + key.getSize(), 0, 32 - key.getSize());
   }

   BinaryData oxor, ixor;
   oxor.resize(32);
   ixor.resize(32);
   for (unsigned i=0; i<32; i++) {
      oxor.getPtr()[i] = hmacKey.getPtr()[i] ^ 0x5c;
      ixor.getPtr()[i] = hmacKey.getPtr()[i] ^ 0x36;
   }

   ixor.append(msg);
   auto iHash = BtcUtils::getSha256(ixor);

   BinaryWriter bw;
   bw.put_BinaryData(oxor);
   bw.put_BinaryData(iHash);

   return BtcUtils::getSha256(bw.getData());
}

BinaryData BtcUtils::getSaltedRpcPass(
   const std::string& salt, const std::string& pass)
{
   BinaryData salted; salted.resize(32);
   Cryptography::Hash::getHMAC256(
      BinaryDataRef::fromString(salt),
      BinaryDataRef::fromString(pass),
      salted.getPtr()
   );
   return salted;
}

////////////////////////////////////////////////////////////////////////////////
// merkle tree
std::vector<BinaryData> BtcUtils::calculateMerkleTree(
   const std::vector<BinaryData>& txhashlist)
{
   // Don't know in advance how big this list will be, make a list too big
   // and copy the result to the right size list afterwards
   size_t numTx = txhashlist.size();
   std::vector<BinaryData> merkleTree(3*numTx);
   BinaryData hashInput;
   hashInput.resize(64);

   for (unsigned i=0; i<numTx; i++) {
      merkleTree[i] = txhashlist[i];
   }

   size_t thisLevelStart = 0;
   size_t nextLevelStart = numTx;
   size_t levelSize = numTx;
   BinaryData hashOutput;
   hashOutput.resize(32);
   while (levelSize>1) {
      for (unsigned j=0; j<(levelSize+1)/2; j++) {
         uint8_t* half1Ptr = hashInput.getPtr();
         uint8_t* half2Ptr = hashInput.getPtr()+32;

         if (j < levelSize/2) {
            merkleTree[thisLevelStart+(2*j)  ].copyTo(half1Ptr, 32);
            merkleTree[thisLevelStart+(2*j)+1].copyTo(half2Ptr, 32);
         } else {
            merkleTree[nextLevelStart-1].copyTo(half1Ptr, 32);
            merkleTree[nextLevelStart-1].copyTo(half2Ptr, 32);
         }

         Cryptography::Hash::getHash256(
            hashInput.getRef(), hashOutput.getPtr());
         merkleTree[nextLevelStart+j] = hashOutput;
      }
      levelSize = (levelSize+1)/2;
      thisLevelStart = nextLevelStart;
      nextLevelStart = nextLevelStart+levelSize;
   }

   // nextLevelStart is the size of the merkle tree
   merkleTree.erase(merkleTree.begin()+nextLevelStart, merkleTree.end());
   return merkleTree;
}

BinaryData BtcUtils::calculateMerkleRoot(
   const std::vector<BinaryData>& txhashlist)
{
   std::vector<BinaryData> mtree = calculateMerkleTree(txhashlist);
   return mtree[mtree.size()-1];
}

////////////////////////////////////////////////////////////////////////////////
// tx length parsing
void BtcUtils::TxInCalcLength(const uint8_t* ptr, size_t size,
   std::vector<size_t>* offsetsIn)
{
   BinaryRefReader brr(ptr, size);
   if (brr.getSizeRemaining() < 4) {
      throw BtcUtils::BlockDeserializingException();
   }

   // Tx Version
   brr.advance(4);

   // TxIn List
   size_t nIn = brr.get_var_int();
   if (offsetsIn != nullptr) {
      offsetsIn->resize(nIn + 1);
      for (size_t i = 0; i < nIn; i++) {
         (*offsetsIn)[i] = brr.getPosition();
         brr.advance(TxInCalcLength(brr.getCurrPtr(), brr.getSizeRemaining()));
      }

      // add in end of the last txin
      (*offsetsIn)[nIn] = brr.getPosition();
   }
}

size_t BtcUtils::TxInCalcLength(const uint8_t* ptr, size_t size)
{
   if (size < 37) {
      throw BtcUtils::BlockDeserializingException();
   }
   uint8_t viLen;
   size_t scrLen = (size_t)readVarInt(ptr+36, size-36, viLen);
   return (36 + viLen + scrLen + 4);
}

size_t BtcUtils::TxOutCalcLength(const uint8_t* ptr, size_t size)
{
   if (size < 9) {
      throw BtcUtils::BlockDeserializingException();
   }

   uint8_t viLen;
   size_t scrLen = (size_t)readVarInt(ptr+8, size-8, viLen);
   return (8 + viLen + scrLen);
}

size_t BtcUtils::TxWitnessCalcLength(const uint8_t* ptr, size_t size)
{
   if (size < 1) {
      throw BtcUtils::BlockDeserializingException();
   }

   size_t witLen = 0;
   uint8_t viStackLen;
   size_t stackLen = readVarInt(ptr, size, viStackLen);
   witLen += viStackLen;
   for (size_t i = 0; i < stackLen; i++) {
      if (witLen >= size) {
         throw BtcUtils::BlockDeserializingException();
      }
      uint8_t viLen;
      witLen += readVarInt(ptr + witLen, size - witLen, viLen);
      witLen += viLen;
      if (witLen > size) {
         throw BtcUtils::BlockDeserializingException();
      }
   }
   return witLen;
}

bool BtcUtils::checkSwMarker(const uint8_t* ptr)
{
   return ptr[0] == 0x00 && ptr[1] == 0x01;
}

size_t BtcUtils::TxCalcLength(const uint8_t* ptr, size_t size,
   std::vector<size_t>* offsetsIn,
   std::vector<size_t>* offsetsOut,
   std::vector<size_t>* offsetsWitness)
{
   BinaryRefReader brr(ptr, size);

   if (brr.getSizeRemaining() < 4) {
      throw BtcUtils::BlockDeserializingException();
   }

   // Tx Version;
   brr.advance(4);

   // Get marker and flag if transaction uses segwit
   auto usesWitness = checkSwMarker(brr.getCurrPtr());
   if (usesWitness) {
      brr.advance(2);
   }

   // TxIn List
   size_t nIn = brr.get_var_int();
   if (offsetsIn != nullptr) {
      offsetsIn->resize(nIn+1);
      for (size_t i = 0; i < nIn; i++) {
         (*offsetsIn)[i] = brr.getPosition();
         brr.advance(TxInCalcLength(
            brr.getCurrPtr(),
            brr.getSizeRemaining())
         );
      }
      (*offsetsIn)[nIn] = brr.getPosition(); // Get the end of the last
   } else {
      // Don't need to track the offsets, just leap over everything
      for (size_t i = 0; i < nIn; i++) {
         brr.advance(TxInCalcLength(
            brr.getCurrPtr(),
            brr.getSizeRemaining())
         );
      }
   }

   // Now extract the TxOut list
   size_t nOut = brr.get_var_int();
   if (offsetsOut != nullptr) {
      offsetsOut->resize(nOut+1);
      for (size_t i = 0; i<nOut; i++) {
         (*offsetsOut)[i] = brr.getPosition();
         brr.advance(TxOutCalcLength(
            brr.getCurrPtr(),
            brr.getSizeRemaining())
         );
      }
      (*offsetsOut)[nOut] = brr.getPosition();
   } else {
      for (size_t i = 0; i<nOut; i++) {
         brr.advance(TxOutCalcLength(
            brr.getCurrPtr(),
            brr.getSizeRemaining())
         );
      }
   }

   // Now extract the witnesses
   if (usesWitness) {
      if (offsetsWitness != nullptr) {
         offsetsWitness->resize(nIn + 1);
         for (size_t i = 0; i < nIn; i++) {
            (*offsetsWitness)[i] = brr.getPosition();
            brr.advance(TxWitnessCalcLength(
               brr.getCurrPtr(),
               brr.getSizeRemaining())
            );
         }
         (*offsetsWitness)[nIn] = brr.getPosition();
      } else {
         for (size_t i = 0; i < nIn; i++) {
            brr.advance(TxWitnessCalcLength(
               brr.getCurrPtr(),
               brr.getSizeRemaining())
            );
         }
      }
   } else {
      if (offsetsWitness != nullptr) {
         offsetsWitness->resize(1);
         (*offsetsWitness)[0] = brr.getPosition();
      }
   }

   brr.advance(4);
   return brr.getPosition();
}

size_t BtcUtils::StoredTxCalcLength(const uint8_t* ptr,
   size_t len, bool fragged,
   std::vector<size_t>* offsetsIn,
   std::vector<size_t>* offsetsOut,
   std::vector<size_t>* offsetsWitness)
{
   BinaryRefReader brr(ptr, len);

   // Tx Version;
   brr.advance(4);

   // Get marker and flag if transaction uses segwit
   auto usesWitness = checkSwMarker(brr.getCurrPtr());
   if (usesWitness) {
      brr.advance(2);
   }

   // TxIn List
   size_t nIn = brr.get_var_int();
   if (offsetsIn != nullptr) {
      offsetsIn->resize(nIn+1);
      for (size_t i = 0; i < nIn; i++) {
         (*offsetsIn)[i] = brr.getPosition();
         brr.advance(TxInCalcLength(
            brr.getCurrPtr(),
            brr.getSizeRemaining())
         );
      }
      (*offsetsIn)[nIn] = brr.getPosition(); // Get the end of the last
   } else {
      // Don't need to track the offsets, just leap over everything
      for (size_t i = 0; i < nIn; i++) {
         brr.advance(TxInCalcLength(
            brr.getCurrPtr(),
            brr.getSizeRemaining())
         );
      }
   }

   // Now extract the TxOut list
   size_t nOut = brr.get_var_int();
   if (fragged) {
      offsetsOut->resize(nOut+1);
      for (size_t i = 0; i < nOut + 1; i++) {
         (*offsetsOut)[i] = brr.getPosition();
      }
   } else {
      // Now extract the TxOut list
      if (offsetsOut != nullptr) {
         offsetsOut->resize(nOut+1);
         for (size_t i = 0; i < nOut; i++) {
            (*offsetsOut)[i] = brr.getPosition();
            brr.advance(TxOutCalcLength(
               brr.getCurrPtr(),
               brr.getSizeRemaining())
            );
         }
         (*offsetsOut)[nOut] = brr.getPosition();
      } else {
         for (size_t i = 0;  i < nOut; i++) {
            brr.advance(TxOutCalcLength(
               brr.getCurrPtr(),
               brr.getSizeRemaining())
            );
         }
      }
   }

   // Now extract the witnesses
   if (usesWitness) {
      if (offsetsWitness != nullptr) {
         offsetsWitness->resize(nIn + 1);
         for (size_t i = 0; i < nIn; i++) {
            (*offsetsWitness)[i] = brr.getPosition();
            brr.advance(TxWitnessCalcLength(brr.getCurrPtr(), brr.getSizeRemaining()));
         }
         (*offsetsWitness)[nIn] = brr.getPosition();
      } else {
         for (size_t i = 0; i < nIn; i++) {
            brr.advance(TxWitnessCalcLength(brr.getCurrPtr(), brr.getSizeRemaining()));
         }
      }
   } else {
      if (offsetsWitness != nullptr) {
         offsetsWitness->resize(1);
         (*offsetsWitness)[0] = brr.getPosition();
      }
   }

   brr.advance(4);
   return brr.getPosition();
}

////////////////////////////////////////////////////////////////////////////////
// script type parsing
TxOutScriptType BtcUtils::getTxOutScriptType(BinaryDataRef s)
{
   size_t sz = s.getSize();
   if (sz > 0 && sz < 81 && s[0] == 0x6a) {
      return TxOutScriptType::OPRETURN;
   } else if (sz < 21) {
      return TxOutScriptType::NONSTANDARD;
   } else if (sz == 22 && s[0] == 0x00 && s[1] == 0x14) {
      return TxOutScriptType::P2WPKH;
   } else if (sz == 34 && s[0] == 0x00 && s[1] == 0x20) {
      return TxOutScriptType::P2WSH;
   } else if (sz == 25 &&
      s[0] == 0x76 &&
      s[1] == 0xa9 &&
      s[2] == 0x14 &&
      s[-2] == 0x88 &&
      s[-1] == 0xac) {
      return TxOutScriptType::STDHASH160;
   } else if (sz == 67 && s[0] == 0x41 && s[1] == 0x04 && s[-1] == 0xac) {
      return TxOutScriptType::STDPUBKEY65;
   } else if (sz == 35 &&
      s[0] == 0x21 &&
      (s[1] == 0x02 || s[1] == 0x03) &&
      s[-1] == 0xac) {
      return TxOutScriptType::STDPUBKEY33;
   } else if (sz == 23 && s[0] == 0xa9 && s[1] == 0x14 && s[-1] == 0x87) {
      return TxOutScriptType::P2SH;
   } else if (s[-1] == 0xae && isMultisigScript(s)) {
      return TxOutScriptType::MULTISIG;
   } else {
      return TxOutScriptType::NONSTANDARD;
   }
}

TxInScriptType BtcUtils::getTxInScriptType(BinaryDataRef script,
   BinaryDataRef prevTxHash)
{
   if (prevTxHash == EmptyHash) {
      return TxInScriptType::COINBASE;
   }

   if (script.empty()) {
      return TxInScriptType::WITNESS;
   }
   if (script.getSize() == 23 && script[1] == 0x00 && script[2] == 0x14) {
      return TxInScriptType::P2WPKH_P2SH;
   }
   if (script.getSize() == 35 && script[1] == 0x00 && script[2] == 0x20) {
      return TxInScriptType::P2WSH_P2SH;
   }

   // Technically, this doesn't recognize all P2SH spends. Only
   // spends of P2SH scripts that are, themselves, standard
   BinaryData lastPush = getLastPushDataInScript(script);
   if (getTxOutScriptType(lastPush) != TxOutScriptType::NONSTANDARD) {
      return TxInScriptType::SPENDP2SH;
   }

   if (script[0]==0x00) {
      // TODO: All this complexity to check TxIn type may be too slow when
      //       scanning the blockchain...will need to investigate later
      std::vector<BinaryDataRef> splitScr = splitPushOnlyScriptRefs(script);

      if (splitScr.empty()) {
         return TxInScriptType::NONSTANDARD;
      }

      // TODO: Maybe should identify whether the other pushed data
      //       in the script is a potential solution for the
      //       subscript... meh?
      if (script[2]==0x30 && script[4]==0x02) {
         return TxInScriptType::SPENDMULTI;
      }
   }

   if (!(script[1]==0x30 && script[3]==0x02)) {
      return TxInScriptType::NONSTANDARD;
   }

   uint32_t sigSize = script[2] + 4;
   if (script.getSize() == sigSize) {
      return TxInScriptType::SPENDPUBKEY;
   }

   uint32_t keySizeFull = 66;  // \x41 \x04 [X32] [Y32]
   uint32_t keySizeCompr= 34;  // \x41 \x02 [X32]

   if (script.getSize() == sigSize + keySizeFull) {
      return TxInScriptType::STDUNCOMPR;
   } else if (script.getSize() == sigSize + keySizeCompr) {
      return TxInScriptType::STDCOMPR;
   }
   return TxInScriptType::NONSTANDARD;
}

////////////////////////////////////////////////////////////////////////////////
// txin address helpers
BinaryData BtcUtils::getTxInAddr(BinaryDataRef script,
   BinaryDataRef prevTxHash, TxInScriptType type)
{
   if (type==TxInScriptType::NONSTANDARD) {
      type = getTxInScriptType(script, prevTxHash);
   }
   return getTxInAddrFromType(script, type);
}

BinaryData BtcUtils::getTxInAddrFromType(BinaryDataRef script,
   TxInScriptType type)
{
   switch(type)
   {
      case TxInScriptType::STDUNCOMPR:
      {
         if (script.getSize() < 65) {
            throw BtcUtils::BlockDeserializingException();
         }
         return getHash160(script.getSliceRef(-65, 65));
      }

      case TxInScriptType::STDCOMPR:
      {
         if (script.getSize() < 33) {
            throw BtcUtils::BlockDeserializingException();
         }
         return getHash160(script.getSliceRef(-33, 33));
      }

      case TxInScriptType::SPENDP2SH:
      {
         auto pushVect = splitPushOnlyScriptRefs(script);
         return getHash160(pushVect[pushVect.size()-1]);
      }

      case TxInScriptType::COINBASE:
      case TxInScriptType::SPENDPUBKEY:
      case TxInScriptType::SPENDMULTI:
      case TxInScriptType::NONSTANDARD:
         return BadAddress;

      default:
         LOGERR << "What kind of TxIn script did we get?";
         return BadAddress;
   }
}

BinaryData BtcUtils::getTxInAddrFromTypeInt(const BinaryData& script,
   uint32_t typeInt)
{
   return getTxInAddrFromType(script.getRef(), (TxInScriptType)typeInt);
}

////////////////////////////////////////////////////////////////////////////////
// multisig helpers
uint8_t BtcUtils::getMultisigPubKeyList(const BinaryData& script,
   std::vector<BinaryData>& pkList)
{
   if (script[-1] != 0xae) {
      return 0;
   }

   uint8_t M = script[0];
   uint8_t N = script[-2];
   if (M<81 || M>96|| N<81 || N>96) {
      return 0;
   }

   M -= 80;
   N -= 80;

   BinaryRefReader brr(script);
   brr.advance(1); // Skip over M-value
   pkList.resize(N);
   for (uint8_t i=0; i<N; i++) {
      uint8_t nextSz = brr.get_uint8_t();
      if (nextSz != 0x41 && nextSz != 0x21) {
         return 0;
      }

      try {
         pkList[i] = brr.get_BinaryDataRef(nextSz);
      } catch (const std::exception& e) {
         LOGERR << "Failed to decode pub keys for multisig script," <<
            " with error: " << e.what();
         LOGERR << script.toHexStr();
         return 0;
      }
   }
   return M;
}

uint8_t BtcUtils::getMultisigAddrList(const BinaryData& script,
   std::vector<BinaryData>& addr160List)
{
   std::vector<BinaryData> pkList;
   uint32_t M = getMultisigPubKeyList(script, pkList);
   size_t   N = pkList.size();

   if (M==0) {
      return 0;
   }

   addr160List.resize(N);
   for (uint32_t i=0; i<N; i++) {
      addr160List[i] = getHash160(pkList[i]);
   }
   return M;
}

BinaryData BtcUtils::getMultisigUniqueKey(const BinaryData& script)
{
   std::vector<BinaryData> a160List;
   uint8_t M = getMultisigAddrList(script, a160List);
   size_t  N = a160List.size();

   if (M==0) {
      return {};
   }

   BinaryWriter bw(2 + N*20); // reserve enough space for header + N addr
   bw.put_uint8_t((uint8_t)M);
   bw.put_uint8_t((uint8_t)N);

   std::sort(a160List.begin(), a160List.end());
   for (size_t i = 0; i < a160List.size(); i++) {
      bw.put_BinaryData(a160List[i]);
   }
   return bw.getData();
}

bool BtcUtils::isMultisigScript(BinaryDataRef script)
{
   std::vector<BinaryData> a160List;
   return getMultisigPubKeyList(script, a160List) != 0;
}

////////////////////////////////////////////////////////////////////////////////
// push data
std::vector<BinaryDataRef> BtcUtils::splitPushOnlyScriptRefs(
   BinaryDataRef script)
{
   std::vector<BinaryDataRef> opList;
   opList.reserve(4);

   BinaryRefReader brr(script);
   uint8_t nextOp;
   while (brr.getSizeRemaining() > 0) {
      nextOp = brr.get_uint8_t();
      if (nextOp == 0) {
         // Implicit pushdata
         brr.rewind(1);
         opList.emplace_back(brr.get_BinaryDataRef(1));
      } else if (nextOp < 76) {
         // Implicit pushdata
         opList.emplace_back(brr.get_BinaryDataRef(nextOp));
      } else if (nextOp == 76) {
         uint8_t nb = brr.get_uint8_t();
         opList.emplace_back(brr.get_BinaryDataRef(nb));
      } else if( nextOp == 77) {
         uint16_t nb = brr.get_uint16_t();
         opList.emplace_back(brr.get_BinaryDataRef(nb));
      } else if (nextOp == 78) {
         uint16_t nb = brr.get_uint32_t();
         opList.emplace_back(brr.get_BinaryDataRef(nb));
      }
      else if (nextOp > 78 && nextOp < 97 && nextOp !=80) {
         brr.rewind(1);
         opList.emplace_back(brr.get_BinaryDataRef(1));
      } else {
         return {};
      }
   }
   return opList;
}

std::vector<BinaryData> BtcUtils::splitPushOnlyScript(const BinaryData& script)
{
   auto refs = splitPushOnlyScriptRefs(script);
   std::vector<BinaryData> out(refs.size());
   for (uint32_t i=0; i<refs.size(); i++) {
      out[i].copyFrom(refs[i]);
   }
   return out;
}

BinaryData BtcUtils::getLastPushDataInScript(const BinaryData& script)
{
   auto refs = splitPushOnlyScriptRefs(script);
   if (refs.empty()) {
      return {};
   }
   return refs[refs.size() - 1];
}

BinaryData BtcUtils::getPushDataHeader(const BinaryData& data)
{
   BinaryWriter bw;

   if (data.getSize() <= 75) {
       bw.put_uint8_t((uint8_t)data.getSize());
   } else if (data.getSize() < UINT8_MAX) {
      bw.put_uint8_t(OP_PUSHDATA1);
      bw.put_uint8_t((uint8_t)data.getSize());
   } else if (data.getSize() < UINT16_MAX) {
      bw.put_uint8_t(OP_PUSHDATA2);
      bw.put_uint16_t((uint16_t)data.getSize());
   } else if (data.getSize() < UINT32_MAX) {
      bw.put_uint8_t(OP_PUSHDATA4);
      bw.put_uint32_t((uint32_t)data.getSize());
   } else {
      throw std::runtime_error("pushdata exceeds size limit");
   }
   return bw.getData();
}

////////////////////////////////////////////////////////////////////////////////
// txout script helpers
BinaryData BtcUtils::getP2PKHScript(const BinaryData& scriptHash)
{
   if (scriptHash.getSize() != 20) {
      throw std::runtime_error("invalid P2PKH hash size");
   }

   BinaryWriter bw;
   bw.put_uint8_t(OP_DUP);
   bw.put_uint8_t(OP_HASH160);
   bw.put_uint8_t(20);
   bw.put_BinaryData(scriptHash);
   bw.put_uint8_t(OP_EQUALVERIFY);
   bw.put_uint8_t(OP_CHECKSIG);
   return bw.getData();
}

BinaryData BtcUtils::getP2PKScript(const BinaryData& pubkey)
{
   if (pubkey.getSize() != 33 && pubkey.getSize() != 65) {
      throw std::runtime_error("invalid pubkey size");
   }

   BinaryWriter bw;
   bw.put_var_int(pubkey.getSize());
   bw.put_BinaryData(pubkey);
   bw.put_uint8_t(OP_CHECKSIG);
   return bw.getData();
}

BinaryData BtcUtils::getP2SHScript(const BinaryData& scriptHash)
{
   if (scriptHash.getSize() != 20) {
      throw std::runtime_error("invalid P2SH hash size");
   }

   BinaryWriter bw;
   bw.put_uint8_t(OP_HASH160);
   bw.put_uint8_t(20);
   bw.put_BinaryData(scriptHash);
   bw.put_uint8_t(OP_EQUAL);
   return bw.getData();
}

BinaryData BtcUtils::getP2WPKHOutputScript(const BinaryData& scriptHash)
{
   if (scriptHash.getSize() != 20) {
      throw std::runtime_error("invalid P2WPKH hash size");
   }

   BinaryWriter bw;
   bw.put_uint8_t(0);
   bw.put_uint8_t(20);
   bw.put_BinaryData(scriptHash);
   return bw.getData();
}

BinaryData BtcUtils::getP2WPKHWitnessScript(const BinaryData& scriptHash)
{
   if (scriptHash.getSize() != 20) {
      throw std::runtime_error("invalid P2WPKH hash size");
   }

   BinaryWriter bw;
   bw.put_uint8_t(OP_DUP);
   bw.put_uint8_t(OP_HASH160);
   bw.put_uint8_t(20);
   bw.put_BinaryData(scriptHash);
   bw.put_uint8_t(OP_EQUALVERIFY);
   bw.put_uint8_t(OP_CHECKSIG);
   return bw.getData();
}

BinaryData BtcUtils::getP2WSHOutputScript(const BinaryData& scriptHash)
{
   if (scriptHash.getSize() != 32) {
      throw std::runtime_error("invalid P2WSH hash size");
   }

   BinaryWriter bw;
   bw.put_uint8_t(0);
   bw.put_uint8_t(32);
   bw.put_BinaryData(scriptHash);
   return bw.getData();
}

BinaryData BtcUtils::getP2WSHWitnessScript(const BinaryData& scriptHash)
{
   if (scriptHash.getSize() != 32) {
      throw std::runtime_error("invalid P2WSH hash size");
   }

   BinaryWriter bw;
   bw.put_uint8_t(OP_SHA256);
   bw.put_uint8_t(32);
   bw.put_BinaryData(scriptHash);
   bw.put_uint8_t(OP_EQUAL);
   return bw.getData();
}

////////////////////////////////////////////////////////////////////////////////
SecureBinaryData BtcUtils::computeChainCode_ArmoryLegacy(
   const SecureBinaryData& privateRoot)
{
   /*
   Armory 1.35c defines the chaincode as HMAC<SHA256> with:
   key: double SHA256 of the root key
   message: 'Derive Chaincode from Root Key'

   TODO: The Armory Python code uses a botched self implemented HMAC256,
   reproduce it here.
   */

   auto hmacKey = getHash256(privateRoot);
   auto hmacMsg = BinaryData::fromString("Derive Chaincode from Root Key"sv);

   //use key as is for invalid armory hmac256: armory erroneously uses the
   //output size for sha256 (32 bytes) instead of the block size (64 bytes)
   return getBotchedArmoryHMAC256(hmacKey, hmacMsg);
}

////////////////////////////////////////////////////////////////////////////////
BinaryData BtcUtils::computeDataId(const SecureBinaryData& data,
   const std::string& message)
{
   if (data.empty()) {
      throw std::runtime_error("cannot compute id for empty data");
   }

   if (message.empty()) {
      throw std::runtime_error("cannot compute id for empty message");
   }

   //hmac the hash256 of the data with message
   auto hmacKey = getHash256(data);
   BinaryData id;
   id.resize(32);

   getHMAC256(hmacKey.getPtr(), hmacKey.getSize(),
      message.c_str(), message.size(), id.getPtr());

   //return last 16 bytes
   return id.getSliceCopy(16, 16);
}

////////////////////////////////////////////////////////////////////////////////
// address helpers
BinaryData BtcUtils::getScrAddrForAddrStr(const std::string& addrStr)
{
   BinaryData scrAddr;
   try {
      scrAddr = base58toScrAddr(addrStr);
   } catch (const std::exception&) {
      auto scrAddrPair = BtcUtils::segWitAddressToScrAddr(addrStr);
      if (scrAddrPair.second != 0) {
         throw std::runtime_error(
            "[getScrAddrForAddrStr] unsupported sw version");
      }

      switch (scrAddrPair.first.getSize())
      {
         case 20:
         {
            scrAddr.resize(21);
            memset(scrAddr.getPtr(), (uint8_t)ScriptPrefix::P2WPKH, 1);
            memcpy(scrAddr.getPtr() + 1, scrAddrPair.first.getPtr(), 20);
            break;
         }

         case 32:
         {
            scrAddr.resize(33);
            memset(scrAddr.getPtr(), (uint8_t)ScriptPrefix::P2WSH, 1);
            memcpy(scrAddr.getPtr() + 1, scrAddrPair.first.getPtr(), 32);
            break;
         }

         default:
            break;
      }
   }

   if (scrAddr.empty()) {
      throw std::runtime_error(
         "[getScrAddrForAddrStr] failed to create recipient");
   }
   return scrAddr;
}

BinaryData BtcUtils::getTxOutScrAddr(BinaryDataRef script,
   TxOutScriptType type)
{
   BinaryWriter bw;
   if (type == TxOutScriptType::NONSTANDARD) {
      type = getTxOutScriptType(script);
   }

   auto h160Prefix = Armory::Config::BitcoinSettings::getPubkeyHashPrefix();
   auto scriptPrefix = Armory::Config::BitcoinSettings::getScriptHashPrefix();

   switch (type)
   {
      case TxOutScriptType::STDHASH160:
      {
         bw.put_uint8_t(h160Prefix);
         bw.put_BinaryData(script.getSliceCopy(3, 20));
         return bw.getData();
      }

      case TxOutScriptType::P2WPKH:
      {
         bw.put_uint8_t((uint8_t)ScriptPrefix::P2WPKH);
         bw.put_BinaryData(script.getSliceCopy(2, 20));
         return bw.getData();
      }

      case TxOutScriptType::P2WSH:
      {
         bw.put_uint8_t((uint8_t)ScriptPrefix::P2WSH);
         bw.put_BinaryData(script.getSliceCopy(2, 32));
         return bw.getData();
      }

      case TxOutScriptType::STDPUBKEY65:
      {
         bw.put_uint8_t(h160Prefix);
         bw.put_BinaryData(getHash160(script.getSliceRef(1, 65)));
         return bw.getData();
      }

      case TxOutScriptType::STDPUBKEY33:
      {
         bw.put_uint8_t(h160Prefix);
         bw.put_BinaryData(getHash160(script.getSliceRef(1, 33)));
         return bw.getData();
      }

      case TxOutScriptType::P2SH:
      {
         bw.put_uint8_t(scriptPrefix);
         bw.put_BinaryData(script.getSliceCopy(2, 20));
         return bw.getData();
      }

      case TxOutScriptType::NONSTANDARD:
      {
         bw.put_uint8_t((uint8_t)ScriptPrefix::NONSTD);
         bw.put_BinaryData(getHash160(script));
         return bw.getData();
      }

      case TxOutScriptType::MULTISIG:
      {
         bw.put_uint8_t((uint8_t)ScriptPrefix::MULTISIG);
         bw.put_BinaryData(getMultisigUniqueKey(script));
         return bw.getData();
      }

      case TxOutScriptType::OPRETURN:
      {
         bw.put_uint8_t((uint8_t)ScriptPrefix::OPRETURN);
         unsigned msg_pos = 1;
         if (script.getSize() > 77) {
            msg_pos += 2;
         } else if (script.getSize() > 1) {
            ++msg_pos;
         }

         bw.put_BinaryData(
            script.getSliceRef(msg_pos, script.getSize() - msg_pos));
         return bw.getData();
      }

      default:
         LOGERR << "What kind of TxOutScript did we get?";
         return {};
   }
}

BinaryData BtcUtils::getTxOutScriptForScrAddr(BinaryDataRef scrAddr)
{
   if (scrAddr.empty()) {
      throw std::runtime_error("invalid scrAddr size");
   }

   BinaryRefReader brr(scrAddr);
   auto prefix = brr.get_uint8_t();
   switch (prefix)
   {
      case (uint8_t)ScriptPrefix::HASH160:
      case (uint8_t)ScriptPrefix::HASH160_TESTNET:
         return getP2PKHScript(brr.get_BinaryData(brr.getSizeRemaining()));

      case (uint8_t)ScriptPrefix::P2SH:
      case (uint8_t)ScriptPrefix::P2SH_TESTNET:
         return getP2SHScript(brr.get_BinaryData(brr.getSizeRemaining()));

      case (uint8_t)ScriptPrefix::P2WPKH:
         return getP2WPKHOutputScript(brr.get_BinaryData(brr.getSizeRemaining()));
      
      case (uint8_t)ScriptPrefix::P2WSH:
         return getP2WSHOutputScript(brr.get_BinaryData(brr.getSizeRemaining()));

      default:
         throw std::runtime_error("unsupported scrAddr");
   }
}

TxOutScriptType BtcUtils::getScriptTypeForScrAddr(BinaryDataRef scrAddr)
{
   if (scrAddr.getSize() == 21) {
      auto h160Prefix = Armory::Config::BitcoinSettings::getPubkeyHashPrefix();
      auto scriptPrefix = Armory::Config::BitcoinSettings::getScriptHashPrefix();

      auto prefix = *scrAddr.getPtr();
      if (prefix == h160Prefix) {
         return TxOutScriptType::STDHASH160;
      } else if (prefix == (uint8_t)ScriptPrefix::P2WPKH) {
         return TxOutScriptType::P2WPKH;
      } else if (prefix == scriptPrefix) {
         return TxOutScriptType::P2SH;
      }
   } else if (scrAddr.getSize() == 32) {
      auto prefix = *scrAddr.getPtr();
      if (prefix == (uint8_t)ScriptPrefix::P2WSH) {
         return TxOutScriptType::P2WSH;
      }
   }
   return TxOutScriptType::NONSTANDARD;
}

std::string BtcUtils::getAddressStrFromScrAddr(BinaryDataRef scrAddrRef)
{
   auto scrType = getScriptTypeForScrAddr(scrAddrRef);
   switch (scrType)
   {
      case TxOutScriptType::P2WPKH:
      case TxOutScriptType::P2WSH:
      {
         auto scrAddrNoPrefix = scrAddrRef.getSliceRef(
            1, scrAddrRef.getSize() -1);
         return BtcUtils::scrAddrToSegWitAddress(scrAddrNoPrefix);
      }

      case TxOutScriptType::STDHASH160:
      case TxOutScriptType::P2SH:
      {
         return BtcUtils::scrAddrToBase58(scrAddrRef);
      }

      default:
         throw std::runtime_error("unsupported address type");
   }
}

TxOutScriptRef BtcUtils::getTxOutScrAddrNoCopy(BinaryDataRef script)
{
   auto p2pkh_prefix = (ScriptPrefix)
      Armory::Config::BitcoinSettings::getPubkeyHashPrefix();

   auto type = getTxOutScriptType(script);
   switch (type)
   {
      case TxOutScriptType::STDHASH160:
         return TxOutScriptRef::fromRef(
            p2pkh_prefix, script.getSliceRef(3, 20));

      case TxOutScriptType::P2WPKH:
         return TxOutScriptRef::fromRef(
            ScriptPrefix::P2WPKH, script.getSliceRef(2, 20));

      case TxOutScriptType::P2WSH:
         return TxOutScriptRef::fromRef(
            ScriptPrefix::P2WSH, script.getSliceRef(2, 32));

      case TxOutScriptType::STDPUBKEY65:
      {
         auto hash = getHash160(script.getSliceRef(1, 65));
         return TxOutScriptRef{p2pkh_prefix, hash};
      }

      case TxOutScriptType::STDPUBKEY33:
      {
         auto hash = getHash160(script.getSliceRef(1, 33));
         return TxOutScriptRef{p2pkh_prefix, hash};
      }

      case TxOutScriptType::P2SH:
         return TxOutScriptRef::fromRef(
            (ScriptPrefix)Config::BitcoinSettings::getScriptHashPrefix(),
            script.getSliceRef(2, 20));

      case TxOutScriptType::NONSTANDARD:
      {
         auto hash = getHash160(script);
         return TxOutScriptRef{ScriptPrefix::NONSTD, hash};
      }

      case TxOutScriptType::MULTISIG:
      {
         auto msKey = getMultisigUniqueKey(script);
         return TxOutScriptRef{ScriptPrefix::MULTISIG, msKey};
      }

      case TxOutScriptType::OPRETURN:
      {
         auto size = script.getSize();
         size_t pos = 1;
         if (size > 77) {
            pos += 2;
         }
         if (size > 1) {
            ++pos;
         }
         return TxOutScriptRef::fromRef(
            ScriptPrefix::OPRETURN,
            script.getSliceRef(pos, size - pos)
         );
      }

      default:
         throw std::runtime_error("What kind of TxOutScript did we get?");
   }
}

BinaryData BtcUtils::getTxOutRecipientAddr(const BinaryDataRef& script,
   TxOutScriptType type)
{
   if (type==TxOutScriptType::NONSTANDARD) {
      type = getTxOutScriptType(script);
   }
   switch(type)
   {
      case TxOutScriptType::STDHASH160:
         return script.getSliceCopy(3,20);

      case TxOutScriptType::STDPUBKEY65:
         return getHash160(script.getSliceRef(1,65));

      case TxOutScriptType::STDPUBKEY33:
         return getHash160(script.getSliceRef(1,33));

      case TxOutScriptType::P2SH:
         return script.getSliceCopy(2,20);

      case TxOutScriptType::P2WSH:
         return script.getSliceCopy(2,32);

      case TxOutScriptType::P2WPKH:
         return script.getSliceCopy(2,20);

      case TxOutScriptType::MULTISIG:
      case TxOutScriptType::NONSTANDARD:
      default:
         return BadAddress;
   }
}

////////////////////////////////////////////////////////////////////////////////
// base64
std::string BtcUtils::base64_encode(const std::string_view& in)
{
   size_t main_count = in.size() / 3;
   std::string result;
   result.reserve(main_count * 4 + 5);

   auto ptr = (const uint8_t*)in.data();
   for (unsigned i = 0; i < main_count; i++) {
      uint32_t bits24 = ptr[i * 3] << 24 | ptr[i*3+1] << 16 | ptr[i*3+2] << 8;
      for (unsigned y = 0; y < 4; y++) {
         unsigned val = (bits24 & 0xFC000000) >> 26;
         result.append(1, base64Chars[val]);
         bits24 <<= 6;
      }
   }

   //padding
   size_t left_over = in.size() - main_count * 3;
   if (left_over == 0) {
      return result;
   }

   uint32_t bits24;
   if (left_over == 1) {
      bits24 = ptr[main_count * 3] << 24;
   } else {
      bits24 = ptr[main_count * 3] << 24 | ptr[main_count * 3 + 1] << 16;
   }

   for (unsigned i = 0; i <= left_over; i++) {
      unsigned val = (bits24 & 0xFC000000) >> 26;
      result.append(1, base64Chars[val]);
      bits24 <<= 6;
   }

   result.append(3 - left_over, '=');
   return result;
}

std::string BtcUtils::base64_decode(const std::string& in)
{
   size_t count = (in.size() + 3) / 4;
   std::string result;
   result.resize(count * 3);
   auto ptr = in.c_str();
   auto result_ptr = (uint8_t*)result.c_str();

   unsigned y=0;
   {
      unsigned i=0;
      uint32_t val = 0;
      for (; y < in.size(); y++) {
         if (y % 4 == 0 && y != 0) {
            result_ptr[i * 3] = (val & 0xFF000000) >> 24;
            result_ptr[i * 3 + 1] = (val & 0x00FF0000) >> 16;
            result_ptr[i * 3 + 2] = (val & 0x0000FF00) >> 8;
            val = 0;
            ++i;
         }

         auto val8 = ptr[y];
         auto iter = base64Vals.find(val8);
         if (iter == base64Vals.end()) {
            if (val8 == '=' || val8 == '\n') {
               break;
            }
            throw std::runtime_error("invalid b64 character");
         }

         uint32_t bits = iter->second << (26 - (6 * (y % 4)));
         val |= bits;
      }

      result_ptr[i * 3] = (val & 0xFF000000) >> 24;
      result_ptr[i * 3 + 1] = (val & 0x00FF0000) >> 16;
      result_ptr[i * 3 + 2] = (val & 0x0000FF00) >> 8;
   }

   y *= 3;
   auto len = y / 4;
   result.resize(len);
   return result;
}

////////////////////////////////////////////////////////////////////////////////
// base58
std::string BtcUtils::scrAddrToBase58(const BinaryData& scrAddr)
{
   /***
   caller has to make sure the scrAddr is prepended with the version byte
   ***/

   //hash payload
   auto checksum = getHash256(scrAddr);

   //append first 4 bytes of hash to payload
   auto scriptNhash = scrAddr;
   scriptNhash.append(checksum.getSliceRef(0, 4));
   return base58_encode(scriptNhash);
}

BinaryData BtcUtils::base58toScrAddr(const std::string& b58Addr)
{
   //decode
   auto scriptNhash = base58_decode(b58Addr);

   //should be at least 4 bytes checksum + 1 version byte
   if (scriptNhash.getSize() <= 5) {
      throw std::range_error("invalid b58 decoded address length");
   }

   //split last 4 bytes
   auto len = scriptNhash.getSize();
   auto scriptRef = scriptNhash.getSliceRef(0, len - 4);

   auto checksumRef = scriptNhash.getSliceRef(len - 4, 4);
   auto scriptHash = getHash256(scriptRef);
   auto hash4First = scriptHash.getSliceRef(0, 4);

   if (checksumRef != hash4First) {
      throw std::runtime_error("invalid checksum in b58 address");
   }
   return BinaryData{scriptRef};
}

std::string BtcUtils::base58_encode(BinaryDataRef payload)
{
   size_t size = payload.getSize() * 138 / 100 + 2;
   std::string b58_str;
   b58_str.resize(size);
   size_t return_size = b58_str.size();
   if (!btc_base58_encode(
      &b58_str[0], &return_size,
      payload.getPtr(), payload.getSize()) || 
      return_size > size) {
      throw std::runtime_error("failed to encode b58 string");
   }

   if (return_size == 0) {
      throw std::runtime_error("failed to encode b58 string");
   }

   if (return_size - 1 < size) {
      b58_str.resize(return_size - 1);
   }
   return b58_str;
}

BinaryData BtcUtils::base58_decode(const std::string& b58)
{
   //sanity checks
   if (b58.empty()) {
      throw std::range_error("empty b58 string");
   }

   size_t size = b58.size();
   BinaryData result;
   result.resize(size);

   if (!btc_base58_decode(result.getPtr(), &size, b58.c_str()) ||
      size > b58.size()) {
      throw std::runtime_error("failed to decode b58 string");
   }
   return result.getSliceCopy(b58.size() - size, size);
}

////
std::string BtcUtils::encodePrivKeyBase58(const SecureBinaryData& privKey)
{
   BinaryWriter bwPrivKey;
   bwPrivKey.put_uint8_t(Armory::Config::BitcoinSettings::getPrivKeyPrefix());
   bwPrivKey.put_BinaryData(privKey);
   bwPrivKey.put_uint8_t(0x01);

   auto checksum = getHash256(bwPrivKey.getData());
   bwPrivKey.put_BinaryDataRef(checksum.getSliceRef(0, 4));
   return base58_encode(bwPrivKey.getData());
}

SecureBinaryData BtcUtils::decodePrivKeyBase58(const std::string& strPrivKey)
{
   SecureBinaryData decodedKey(base58_decode(strPrivKey));
   BinaryRefReader brr(decodedKey.getRef());

   //prefix
   auto prefix = brr.get_uint8_t();
   if (prefix != Armory::Config::BitcoinSettings::getPrivKeyPrefix()) {
      throw std::runtime_error("network prefix mismatch");
   }
   brr.rewind(1);

   //key
   auto keyRef = brr.get_BinaryDataRef(brr.getSize() - 4);

   //checksum
   auto checksum = brr.get_BinaryDataRef(4);
   auto hash = BtcUtils::getHash256(keyRef);
   if (hash.getSliceRef(0, 4) != checksum) {
      throw std::runtime_error("privkey checksum mismatch");
   }
   return SecureBinaryData{keyRef.getSliceRef(1, 32)};
}

////////////////////////////////////////////////////////////////////////////////
// sw addresses
std::string BtcUtils::scrAddrToSegWitAddress(const BinaryData& scrAddr)
{
   //hardcoded for version 0 witness programs for now
   const char* headerPtr;
   if (Armory::Config::BitcoinSettings::getPubkeyHashPrefix() ==
      (uint8_t)ScriptPrefix::HASH160) {
      headerPtr = SEGWIT_ADDRESS_MAINNET_HEADER;
   } else if (Armory::Config::BitcoinSettings::getPubkeyHashPrefix() ==
      (uint8_t)ScriptPrefix::HASH160_TESTNET) {
      headerPtr = SEGWIT_ADDRESS_TESTNET_HEADER;
   } else {
      throw std::runtime_error("invalid network for segwit address");
   }

   //73 + header size
   std::string result; result.resize(75);
   if (segwit_addr_encode(
      (char*)result.c_str(), headerPtr, 0,
      scrAddr.getPtr(), scrAddr.getSize()) == 0) {
      throw std::runtime_error("failed to encode to sw address!");
   }

   //adjust result size by looking for the null terminator
   auto len = strnlen(result.c_str(), result.size());
   if (len == 0 || len == result.size()) {
      throw std::runtime_error("failed to encode to sw address!");
   }
   result.resize(len);
   return result;
}

std::pair<BinaryData, int> BtcUtils::segWitAddressToScrAddr(
   const std::string& swAddr)
{
   //hardcoded for version 0 witness programs for now
   const char* headerPtr;
   if (Armory::Config::BitcoinSettings::getPubkeyHashPrefix() ==
      (uint8_t)ScriptPrefix::HASH160) {
      headerPtr = SEGWIT_ADDRESS_MAINNET_HEADER;
   } else if (Armory::Config::BitcoinSettings::getPubkeyHashPrefix() ==
      (uint8_t)ScriptPrefix::HASH160_TESTNET) {
      headerPtr = SEGWIT_ADDRESS_TESTNET_HEADER;
   } else {
      throw std::runtime_error("invalid network for segwit address");
   }

   int ver;
   size_t len;
   BinaryData result;
   result.resize(40);
   if (segwit_addr_decode(&ver, result.getPtr(), &len,
      headerPtr, swAddr.c_str()) == 0) {
      throw std::runtime_error("failed to decode sw address!");
   }

   if (len == 0) {
      throw std::runtime_error("empty sw program buffer");
   }
   if (ver != 0) {
      throw std::runtime_error("only supporting sw version 0 for now");
   }

   //resize result
   result.resize(len);
   return std::make_pair(result, ver);
}

/////////////////////////////////////////////////////////////////////////////
// misc
std::string BtcUtils::numToStrWCommas(int64_t fullNum)
{
   uint64_t num = fullNum;
   num *= (fullNum < 0 ? -1 : 1);
   std::vector<uint32_t> triplets;
   do {
      int bottom3 = (num % 1000);
      triplets.emplace_back(bottom3);
      num = (num - bottom3) / 1000;
   } while(num >= 1);

   std::stringstream out;
   out << (fullNum < 0 ? "-" : "");
   size_t nt = triplets.size()-1;
   char t[4];
   for (uint32_t i=0; i<=nt; i++) {
      if (i==0) {
         sprintf(t, "%d", triplets[nt-i]);
      } else {
         sprintf(t, "%03d", triplets[nt-i]);
      }
      out << t;

      if (i != nt) {
         out << ",";
      }
   }
   return out.str();
}

////
BinaryData BtcUtils::PackBits(const std::list<bool>& boolVec)
{
   BinaryData out;
   out.resize((boolVec.size()+7) / 8);
   memset(out.getPtr(), 0, out.getSize());

   unsigned i=0;
   for (const auto& entry : boolVec) {
      if (entry == true) {
         out[i/8] |= (1<<(7-i%8));
      }
      ++i;
   }
   return out;
}

std::list<bool> BtcUtils::UnpackBits(const BinaryData& bits, uint32_t nBits)
{
   std::list<bool> out;
   for (unsigned i=0; i<nBits; i++) {
      uint8_t bit = bits[i/8] & (1 << (7-i%8));
      out.emplace_back(bit>0);
   }
   return out;
}

////
double BtcUtils::convertDiffBitsToDouble(const BinaryData& diffBitsBinary)
{
   uint32_t diffBits = READ_UINT32_LE(diffBitsBinary);
   int nShift = (diffBits >> 24) & 0xff;
   double dDiff = (double)0x0000ffff / (double)(diffBits & 0x00ffffff);

   while (nShift < 29) {
      dDiff *= 256.0;
      nShift++;
   }
   while (nShift > 29) {
      dDiff /= 256.0;
      nShift--;
   }
   return dDiff;
}

BinaryData BtcUtils::convertDoubleToDiffBits(double diff)
{
   //quick and dirty, for unit test reorg purposes
   unsigned nShift = 29;
   while (diff > 16777215.0) {
      diff /= 256.0;
      --nShift;
   }

   BinaryData diffBits;
   diffBits.resize(4);
   auto ptr = diffBits.getPtr();

   auto val = 65535.0 / diff;
   unsigned* bits = (unsigned*)ptr;
   *bits = ((unsigned)val);
   ptr[3] = nShift;

   return diffBits;
}

void BtcUtils::pprintScript(const BinaryData& script)
{
   std::vector<std::string> oplist = convertScriptToOpStrings(script);
   for (uint32_t i = 0; i < oplist.size(); i++) {
      std::cout << "   " << oplist[i] << std::endl;
   }
}

////
BinaryData BtcUtils::extractRSFromDERSig(BinaryDataRef bdr)
{
   auto forceTo32Bytes = [](BinaryDataRef data, BinaryWriter& output)->void
   {
      auto len = data.getSize();
      if (len > 32) {
         output.put_BinaryData(data.getSliceRef(len - 32, 32));
      } else {
         int zeroCount = 32 - len;
         while (zeroCount-- > 0) {
            output.put_uint8_t(0);
         }
         output.put_BinaryData(data);
      }
   };

   BinaryWriter output;
   BinaryRefReader brr(bdr);

   //check code byte
   auto codeByte = brr.get_uint8_t();
   if (codeByte != 0x30) {
      throw DERException("unexpected code byte in DER sig");
   }

   auto len = brr.get_uint8_t();

   //onto R, again check code byte
   codeByte = brr.get_uint8_t();
   len = brr.get_uint8_t();
   if (codeByte != 0x02) {
      throw DERException("unexpected code byte in DER sig");
   }

   //grab R
   auto rRef = brr.get_BinaryDataRef(len);

   //force to 32 bytes length
   forceTo32Bytes(rRef, output);

   //S
   codeByte = brr.get_uint8_t();
   len = brr.get_uint8_t();
   if (codeByte != 0x02) {
      throw DERException("unexpected code byte in DER sig");
   }

   //grab S
   auto sRef = brr.get_BinaryDataRef(len);

   //force to 32 bytes length
   forceTo32Bytes(sRef, output);

   return output.getData();
}

////
std::map<BinaryDataRef, BinaryDataRef> BtcUtils::getPSBTDataPairs(
   BinaryRefReader& brr)
{
   std::map<BinaryDataRef, BinaryDataRef> result;
   while (true) {
      auto keylen = brr.get_var_int();
      if (keylen == 0) {
         break;
      }
      auto key = brr.get_BinaryDataRef(keylen);

      auto vallen = brr.get_var_int();
      auto val = brr.get_BinaryDataRef(vallen);

      result.emplace(key, val);
   }
   return result;
}
