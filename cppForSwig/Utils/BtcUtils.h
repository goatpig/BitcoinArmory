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

#pragma once

#include <vector>
#include <list>
#include <string>
#include <map>
#include <stdexcept>
#include <stdint.h>

#define HEADER_SIZE 80
#define COIN 100000000ULL
#define NBLOCKS_REGARDED_AS_RESCAN 144
#define MIN_CONFIRMATIONS   6

#define COINBASE_MATURITY 100

#define TX_0_UNCONFIRMED    0
#define TX_NOT_EXIST       -1
#define TX_OFF_MAIN_BRANCH -2

class BinaryData;
class BinaryDataRef;
class SecureBinaryData;
class BinaryRefReader;

enum class TxOutScriptType : int
{
   STDHASH160=0,
   STDPUBKEY65,
   STDPUBKEY33,
   MULTISIG,
   P2SH,
   NONSTANDARD,
   P2WPKH,
   P2WSH,
   OPRETURN
};

enum class TxInScriptType : int
{
   STDUNCOMPR=0,
   STDCOMPR,
   COINBASE,
   SPENDPUBKEY,
   SPENDMULTI,
   SPENDP2SH,
   NONSTANDARD,
   WITNESS,
   P2WPKH_P2SH,
   P2WSH_P2SH
};

class DERException : public std::runtime_error
{
public:
   DERException(const std::string& = "");
};

#define BIP32_SER_VERSION_MAIN_PRV 0x0488ADE4
#define BIP32_SER_VERSION_MAIN_PUB 0x0488B21E
#define BIP32_SER_VERSION_TEST_PRV 0x043587CF
#define BIP32_SER_VERSION_TEST_PUB 0x04358394

// This class holds only static methods.
// NOTE:  added default ctor and a few non-static, to support SWIG
//        (-classic SWIG doesn't support static methods)

class TxOutScriptRef;

namespace Armory
{
   namespace BtcUtils
   {
      extern const BinaryData BadAddress;
      extern const char base64Chars[];
      extern const std::map<char, uint8_t> base64Vals;
      extern const BinaryData EmptyHash;

      /////////////////////////////////////////////////////////////////////////
      std::string numToStrWCommas(int64_t);
      BinaryData PackBits(const std::list<bool>&);
      std::list<bool> UnpackBits(const BinaryData&, uint32_t);

      //////////////////////////////////////////////////////////////////////////
      void getSha256(const uint8_t*, size_t, BinaryData&);
      BinaryData getSha256(const BinaryData&);

      void getHash256(const uint8_t* , size_t, BinaryData&);
      BinaryData getHash256(const uint8_t*, size_t);
      void getHash256(const BinaryData&, BinaryData&);
      void getHash256(BinaryDataRef, BinaryData&);
      BinaryData getHash256(const BinaryData&);
      BinaryData getHash256(const BinaryDataRef&);

      void getHash160(const uint8_t*, size_t, BinaryData&);
      BinaryData getHash160(const uint8_t*, size_t);
      void getHash160(BinaryDataRef, BinaryData&);
      BinaryData getHash160(const BinaryDataRef&);
      BinaryData getHash160(const BinaryData&);
      BinaryData ripemd160(const BinaryData&);

      //////////////////////////////////////////////////////////////////////////
      BinaryData calculateMerkleRoot(const std::vector<BinaryData>&);
      std::vector<BinaryData> calculateMerkleTree(
         const std::vector<BinaryData>&);

      //////////////////////////////////////////////////////////////////////////
      // ALL THESE METHODS ASSUME THERE IS A FULL TX/TXIN/TXOUT BEHIND THE PTR
      // The point of these methods is to calculate the length of the object,
      // hence we don't know in advance how big the object actually will be, so
      // we can't provide it as an input for safety checking...
      void TxInCalcLength(const uint8_t*, size_t, std::vector<size_t>*);
      size_t TxInCalcLength(const uint8_t*, size_t);
      size_t TxOutCalcLength(const uint8_t*, size_t);
      size_t TxWitnessCalcLength(const uint8_t*, size_t);
      bool checkSwMarker(const uint8_t*);
      size_t TxCalcLength(const uint8_t*, size_t,
         std::vector<size_t>*, std::vector<size_t>*, std::vector<size_t>*);
      size_t StoredTxCalcLength(const uint8_t*, size_t, bool,
         std::vector<size_t>*, std::vector<size_t>*, std::vector<size_t>*);

      //////////////////////////////////////////////////////////////////////////
      TxOutScriptType getTxOutScriptType(BinaryDataRef);
      TxInScriptType getTxInScriptType(BinaryDataRef, BinaryDataRef);

      //////////////////////////////////////////////////////////////////////////
      BinaryData getTxOutRecipientAddr(const BinaryDataRef&,
         TxOutScriptType=TxOutScriptType::NONSTANDARD);

      // We use this for LevelDB keys, to return same key if the same priv/pub
      // pair is used, and also saving a few bytes for common script types
      BinaryData getTxOutScrAddr(BinaryDataRef,
         TxOutScriptType=TxOutScriptType::NONSTANDARD);
      BinaryData getTxOutScriptForScrAddr(BinaryDataRef);
      TxOutScriptRef getTxOutScrAddrNoCopy(BinaryDataRef);
      TxOutScriptType getScriptTypeForScrAddr(BinaryDataRef);
      std::string getAddressStrFromScrAddr(BinaryDataRef);
      BinaryData getScrAddrForAddrStr(const std::string&);

      //////////////////////////////////////////////////////////////////////////
      //        "UniqueKey"=="ScrAddr" minus prefix
      // TODO:  Interesting exercise: is there a non-standard script that could
      //        look like the output of this function operating on a multisig
      //        script (doesn't matter if it's valid or not)?  In other words, is
      //        there a hole where someone could mine a script that would be
      //        forwarded by Bitcoin Core to this code, which would then produce
      //        a non-std-unique-key that would be indistinguishable from the
      //        output of this function? My guess is, no. And my guess is that
      //        it's not a very useful even if it did. But it would be good to
      //        rule it out.
      BinaryData getMultisigUniqueKey(const BinaryData&);
      bool isMultisigScript(BinaryDataRef);

      // Returns M in M-of-N.  Use addr160List.size() for N. Output is sorted.
      uint8_t getMultisigAddrList(const BinaryData&, std::vector<BinaryData>&);

      // Returns M in M-of-N.  Use pkList.size() for N. Output is sorted.
      uint8_t getMultisigPubKeyList(const BinaryData&, std::vector<BinaryData>&);

      BinaryData getTxInAddr(BinaryDataRef, BinaryDataRef,
         TxInScriptType=TxInScriptType::NONSTANDARD);
      BinaryData getTxInAddrFromType(BinaryDataRef, TxInScriptType);
      BinaryData getTxInAddrFromTypeInt(const BinaryData&, uint32_t);

      //////////////////////////////////////////////////////////////////////////
      std::vector<BinaryDataRef> splitPushOnlyScriptRefs(BinaryDataRef);
      std::vector<BinaryData> splitPushOnlyScript(const BinaryData&);
      BinaryData getLastPushDataInScript(const BinaryData&);
      BinaryData getPushDataHeader(const BinaryData&);

      //////////////////////////////////////////////////////////////////////////
      double convertDiffBitsToDouble(const BinaryData&);
      BinaryData convertDoubleToDiffBits(double);

      //////////////////////////////////////////////////////////////////////////
      void pprintScript(const BinaryData&);

      //////////////////////////////////////////////////////////////////////////
      std::string scrAddrToBase58(const BinaryData&);
      BinaryData base58toScrAddr(const std::string&);

      std::string encodePrivKeyBase58(const SecureBinaryData&);
      SecureBinaryData decodePrivKeyBase58(const std::string&);

      std::string base58_encode(BinaryDataRef);
      BinaryData base58_decode(const std::string&);
      std::string base64_encode(const std::string_view&);
      std::string base64_decode(const std::string&);

      //////////////////////////////////////////////////////////////////////////
      BinaryData getHMAC256(BinaryDataRef, BinaryDataRef);
      BinaryData getHMAC512(BinaryDataRef, const BinaryDataRef);
      BinaryData getHMAC256(BinaryDataRef, const std::string&);
      BinaryData getHMAC512(BinaryDataRef, const std::string&);
      SecureBinaryData getHMAC512(const std::string&, BinaryDataRef);
      BinaryData getBotchedArmoryHMAC256(BinaryDataRef, BinaryDataRef);

      void getHMAC256(const uint8_t*, size_t, const char*, size_t, uint8_t*);
      void getHMAC512(const void*, size_t, const void*, size_t, void*);

      //////////////////////////////////////////////////////////////////////////
      SecureBinaryData computeChainCode_ArmoryLegacy(const SecureBinaryData&);
      BinaryData computeDataId(const SecureBinaryData&, const std::string&);

      //////////////////////////////////////////////////////////////////////////
      BinaryData getP2PKHScript(const BinaryData&);
      BinaryData getP2PKScript(const BinaryData&);
      BinaryData getP2SHScript(const BinaryData&);
      BinaryData getP2WPKHOutputScript(const BinaryData&);
      BinaryData getP2WPKHWitnessScript(const BinaryData&);
      BinaryData getP2WSHOutputScript(const BinaryData&);
      BinaryData getP2WSHWitnessScript(const BinaryData&);

      ////
      std::string scrAddrToSegWitAddress(const BinaryData&);
      std::pair<BinaryData, int> segWitAddressToScrAddr(const std::string&);

      BinaryData extractRSFromDERSig(BinaryDataRef);
      std::map<BinaryDataRef, BinaryDataRef> getPSBTDataPairs(BinaryRefReader&);
   }
}
