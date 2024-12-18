////////////////////////////////////////////////////////////////////////////////
//                                                                            //
//  Copyright (C) 2011-2015, Armory Technologies, Inc.                        //
//  Distributed under the GNU Affero General Public License (AGPL v3)         //
//  See LICENSE-ATI or http://www.gnu.org/licenses/agpl.html                  //
//                                                                            //
////////////////////////////////////////////////////////////////////////////////

#include "BtcUtils.h"
#include "EncryptionUtils.h"
#include "ArmoryConfig.h"
#include "btc/segwit_addr.h"
#include "TxOutScrRef.h"

using namespace std;
using namespace Armory::Config;

const BinaryData BtcUtils::BadAddress_ = BinaryData::CreateFromHex("0000000000000000000000000000000000000000");
const BinaryData BtcUtils::EmptyHash_  = BinaryData::CreateFromHex("0000000000000000000000000000000000000000000000000000000000000000");
const string BtcUtils::base64Chars_ = string("ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/");
const PRNG_Fortuna BtcUtils::fortuna_;

////////////////////////////////////////////////////////////////////////////////
const map<char, uint8_t> BtcUtils::base64Vals_ = {
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
uint64_t BtcUtils::readVarInt(uint8_t const * strmPtr, size_t remaining,
   uint32_t* lenOutPtr)
{
   if (remaining < 1)
      throw VarIntException("invalid varint");
   uint8_t firstByte = strmPtr[0];

   if(firstByte < 0xfd)
   {
      if(lenOutPtr != NULL) 
         *lenOutPtr = 1;
      return firstByte;
   }
   if(firstByte == 0xfd)
   {
      if (remaining < 3)
         throw VarIntException("invalid varint");
      if(lenOutPtr != NULL) 
         *lenOutPtr = 3;
      return READ_UINT16_LE(strmPtr+1);
   }
   else if(firstByte == 0xfe)
   {
      if (remaining < 5)
         throw VarIntException("invalid varint");
      if(lenOutPtr != NULL) 
         *lenOutPtr = 5;
      return READ_UINT32_LE(strmPtr+1);
   }
   else //if(firstByte == 0xff)
   {
      if (remaining < 9)
         throw VarIntException("invalid varint");
      if(lenOutPtr != NULL) 
         *lenOutPtr = 9;
      return READ_UINT64_LE(strmPtr+1);
   }
}

////////////////////////////////////////////////////////////////////////////////
string BtcUtils::computeID(const SecureBinaryData& pubkey)
{
   BinaryDataRef bdr(pubkey);
   auto&& h160 = getHash160(bdr);
   
   BinaryWriter bw;
   bw.put_uint8_t(BitcoinSettings::getPubkeyHashPrefix());
   bw.put_BinaryDataRef(h160.getSliceRef(0, 5));

   //now reverse it
   auto& data = bw.getData();
   auto ptr = data.getPtr();
   BinaryWriter bwReverse;
   for (unsigned i = 0; i < data.getSize(); i++)
   {
      bwReverse.put_uint8_t(ptr[data.getSize() - 1 - i]);
   }

   return BtcUtils::base58_encode(bwReverse.getDataRef());
}

////////////////////////////////////////////////////////////////////////////////
BinaryData BtcUtils::getHMAC256(const SecureBinaryData& key,
   const SecureBinaryData& message)
{
   BinaryData digest;
   digest.resize(32);
   
   getHMAC256(key.getPtr(), key.getSize(), 
      message.getCharPtr(), message.getSize(),
      digest.getPtr());

   return digest;
}

////////////////////////////////////////////////////////////////////////////////
BinaryData BtcUtils::getHMAC512(const SecureBinaryData& key,
   const SecureBinaryData& message)
{
   BinaryData digest;
   digest.resize(64);

   getHMAC512(key.getPtr(), key.getSize(),
      message.getCharPtr(), message.getSize(),
      digest.getPtr());

   return digest;
}

////////////////////////////////////////////////////////////////////////////////
BinaryData BtcUtils::getHMAC256(const BinaryData& key,
   const string& message)
{
   BinaryData digest;
   digest.resize(32);
   
   getHMAC256(key.getPtr(), key.getSize(), 
      message.c_str(), message.size(),
      digest.getPtr());

   return digest;
}

////////////////////////////////////////////////////////////////////////////////
BinaryData BtcUtils::getHMAC512(const BinaryData& key,
   const string& message)
{
   BinaryData digest;
   digest.resize(64);

   getHMAC512(key.getPtr(), key.getSize(),
      message.c_str(), message.size(),
      digest.getPtr());

   return digest;
}

////////////////////////////////////////////////////////////////////////////////
SecureBinaryData BtcUtils::getHMAC512(const string& key,
   const SecureBinaryData& message)
{
   SecureBinaryData digest;
   digest.resize(64);

   getHMAC512(key.c_str(), key.size(),
      message.getPtr(), message.getSize(),
      digest.getPtr());

   return digest;
}


////////////////////////////////////////////////////////////////////////////////
void BtcUtils::getHMAC256(const uint8_t* keyptr, size_t keylen,
   const char* msgptr, size_t msglen, uint8_t* digest)
{
   BinaryDataRef key_bdr(keyptr, keylen);
   BinaryDataRef msg_bdr((uint8_t*)msgptr, msglen);

   CryptoSHA2::getHMAC256(key_bdr, msg_bdr, digest);
}

////////////////////////////////////////////////////////////////////////////////
void BtcUtils::getHMAC512(const void* keyptr, size_t keylen,
   const void* msgptr, size_t msglen, void* digest)
{
   BinaryDataRef key_bdr((uint8_t*)keyptr, keylen);
   BinaryDataRef msg_bdr((uint8_t*)msgptr, msglen);

   CryptoSHA2::getHMAC512(key_bdr, msg_bdr, (uint8_t*)digest);
}

////////////////////////////////////////////////////////////////////////////////
BinaryData BtcUtils::getBotchedArmoryHMAC256(
   const BinaryData& key, const BinaryData& msg)
{
   BinaryData hmacKey;
   if (key.getSize() > 32)
   {
      hmacKey = BtcUtils::getSha256(key);
   }
   else if (key.getSize() <= 32)
   {
      hmacKey.resize(32);
      memcpy(hmacKey.getPtr(), key.getPtr(), key.getSize());
      memset(hmacKey.getPtr() + key.getSize(), 0, 32 - key.getSize());
   }

   BinaryData oxor(32), ixor(32);
   for (int i=0; i<32; i++)
   {
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

////////////////////////////////////////////////////////////////////////////////
SecureBinaryData BtcUtils::computeChainCode_Armory135(
   const SecureBinaryData& privateRoot)
{
   /*
   Armory 1.35c defines the chaincode as HMAC<SHA256> with:
   key: double SHA256 of the root key
   message: 'Derive Chaincode from Root Key'

   TODO: The Armory Python code uses a botched self implemented HMAC256,
   reproduce it here.
   */

   auto hmacKey = BtcUtils::hash256(privateRoot);
   auto hmacMsg = BinaryData::fromString("Derive Chaincode from Root Key");

   //use key as is for invalid armory hmac256: armory erroneously uses the
   //output size for sha256 (32 bytes) instead of the block size (64 bytes)
   return getBotchedArmoryHMAC256(hmacKey, hmacMsg);
}

////////////////////////////////////////////////////////////////////////////////
BinaryData BtcUtils::computeDataId(const SecureBinaryData& data,
   const string& message)
{
   if (data.empty())
      throw runtime_error("cannot compute id for empty data");

   if (message.empty())
      throw runtime_error("cannot compute id for empty message");

   //hmac the hash256 of the data with message
   auto&& hmacKey = BtcUtils::hash256(data);
   BinaryData id(32);

   getHMAC256(hmacKey.getPtr(), hmacKey.getSize(),
      message.c_str(), message.size(), id.getPtr());

   //return last 16 bytes
   return id.getSliceCopy(16, 16);
}

////////////////////////////////////////////////////////////////////////////////
BinaryData BtcUtils::getScrAddrForAddrStr(const string& addrStr)
{
   BinaryData scrAddr;

   try
   {
      scrAddr = base58toScrAddr(addrStr);
   }
   catch (const exception&)
   {
      auto scrAddrPair = BtcUtils::segWitAddressToScrAddr(addrStr);
      if (scrAddrPair.second != 0)
         throw runtime_error("[getScrAddrForAddrStr] unsupported sw version");

      switch (scrAddrPair.first.getSize())
      {
      case 20:
         scrAddr.resize(21);
         memset(scrAddr.getPtr(), SCRIPT_PREFIX_P2WPKH, 1);
         memcpy(scrAddr.getPtr() + 1, scrAddrPair.first.getPtr(), 20);
         break;

      case 32:
         scrAddr.resize(33);
         memset(scrAddr.getPtr(), SCRIPT_PREFIX_P2WSH, 1);
         memcpy(scrAddr.getPtr() + 1, scrAddrPair.first.getPtr(), 32);
         break;

      default:
         break;
      }
   }

   if (scrAddr.empty())
   {
      throw runtime_error(
         "[getScrAddrForAddrStr] failed to create recipient");
   }

   return scrAddr;
}

////////////////////////////////////////////////////////////////////////////////
BinaryData BtcUtils::getTxOutScrAddr(BinaryDataRef script,
   TXOUT_SCRIPT_TYPE type)
{
   BinaryWriter bw;
   if (type == TXOUT_SCRIPT_NONSTANDARD)
      type = getTxOutScriptType(script);

   auto h160Prefix = BitcoinSettings::getPubkeyHashPrefix();
   auto scriptPrefix = BitcoinSettings::getScriptHashPrefix();

   switch (type)
   {
      case(TXOUT_SCRIPT_STDHASH160) :
         bw.put_uint8_t(h160Prefix);
         bw.put_BinaryData(script.getSliceCopy(3, 20));
         return bw.getData();
      case(TXOUT_SCRIPT_P2WPKH) :
         bw.put_uint8_t(SCRIPT_PREFIX_P2WPKH);
         bw.put_BinaryData(script.getSliceCopy(2, 20));
         return bw.getData();
      case(TXOUT_SCRIPT_P2WSH) :
         bw.put_uint8_t(SCRIPT_PREFIX_P2WSH);
         bw.put_BinaryData(script.getSliceCopy(2, 32));
         return bw.getData();
      case(TXOUT_SCRIPT_STDPUBKEY65) :
         bw.put_uint8_t(h160Prefix);
         bw.put_BinaryData(getHash160(script.getSliceRef(1, 65)));
         return bw.getData();
      case(TXOUT_SCRIPT_STDPUBKEY33) :
         bw.put_uint8_t(h160Prefix);
         bw.put_BinaryData(getHash160(script.getSliceRef(1, 33)));
         return bw.getData();
      case(TXOUT_SCRIPT_P2SH) :
         bw.put_uint8_t(scriptPrefix);
         bw.put_BinaryData(script.getSliceCopy(2, 20));
         return bw.getData();
      case(TXOUT_SCRIPT_NONSTANDARD) :
         bw.put_uint8_t(SCRIPT_PREFIX_NONSTD);
         bw.put_BinaryData(getHash160(script));
         return bw.getData();
      case(TXOUT_SCRIPT_MULTISIG) :
         bw.put_uint8_t(SCRIPT_PREFIX_MULTISIG);
         bw.put_BinaryData(getMultisigUniqueKey(script));
         return bw.getData();
      case(TXOUT_SCRIPT_OPRETURN) :
      {
         bw.put_uint8_t(SCRIPT_PREFIX_NONSTD);
         unsigned msg_pos = 1;
         if (script.getSize() > 77)
            msg_pos += 2;
         else if (script.getSize() > 1)
            ++msg_pos;

         bw.put_BinaryData(
            script.getSliceRef(msg_pos, script.getSize() - msg_pos));
         return bw.getData();
      }

      default:
         LOGERR << "What kind of TxOutScript did we get?";
         return BinaryData(0);
   }
}

////////////////////////////////////////////////////////////////////////////////
BinaryData BtcUtils::getTxOutScriptForScrAddr(BinaryDataRef scrAddr)
{
   if (scrAddr.getSize() == 0)
      throw runtime_error("invalid scrAddr size");

   BinaryRefReader brr(scrAddr);
   auto prefix = brr.get_uint8_t();

   switch (prefix)
   {
      case SCRIPT_PREFIX_HASH160:
      case SCRIPT_PREFIX_HASH160_TESTNET:
         return getP2PKHScript(brr.get_BinaryData(brr.getSizeRemaining()));

      case SCRIPT_PREFIX_P2SH:
      case SCRIPT_PREFIX_P2SH_TESTNET:
         return getP2SHScript(brr.get_BinaryData(brr.getSizeRemaining()));

      case SCRIPT_PREFIX_P2WPKH:
         return getP2WPKHOutputScript(brr.get_BinaryData(brr.getSizeRemaining()));
      
      case SCRIPT_PREFIX_P2WSH:
         return getP2WSHOutputScript(brr.get_BinaryData(brr.getSizeRemaining()));

      default:
         throw runtime_error("unsupported scrAddr");
   }
}

/////////////////////////////////////////////////////////////////////////////
TXOUT_SCRIPT_TYPE BtcUtils::getScriptTypeForScrAddr(BinaryDataRef scrAddr)
{
   if (scrAddr.getSize() == 21)
   {
      auto h160Prefix = BitcoinSettings::getPubkeyHashPrefix();
      auto scriptPrefix = BitcoinSettings::getScriptHashPrefix();

      auto prefix = *scrAddr.getPtr();
      if (prefix == h160Prefix)
         return TXOUT_SCRIPT_STDHASH160;
      else if (prefix == SCRIPT_PREFIX_P2WPKH)
         return TXOUT_SCRIPT_P2WPKH;
      else if (prefix == scriptPrefix)
         return TXOUT_SCRIPT_P2SH;
   }
   else if (scrAddr.getSize() == 32)
   {
      auto prefix = *scrAddr.getPtr();
      if (prefix == SCRIPT_PREFIX_P2WSH)
         return TXOUT_SCRIPT_P2WSH;
   }

   return TXOUT_SCRIPT_NONSTANDARD;
}

/////////////////////////////////////////////////////////////////////////////
std::string BtcUtils::getAddressStrFromScrAddr(BinaryDataRef scrAddrRef)
{
   auto scrType = getScriptTypeForScrAddr(scrAddrRef);
   switch (scrType) 
   {
   case TXOUT_SCRIPT_P2WPKH:
   case TXOUT_SCRIPT_P2WSH:
   {
      auto scrAddrNoPrefix = 
         scrAddrRef.getSliceRef(1, scrAddrRef.getSize() -1);
      return BtcUtils::scrAddrToSegWitAddress(scrAddrNoPrefix);
   }

   case TXOUT_SCRIPT_STDHASH160:
   case TXOUT_SCRIPT_P2SH:
   {
      return BtcUtils::scrAddrToBase58(scrAddrRef);         
   }

   default:
      throw runtime_error("unsupported address type");
   }
}

/////////////////////////////////////////////////////////////////////////////
//no copy version, the regular one is too slow for scanning operations
TxOutScriptRef BtcUtils::getTxOutScrAddrNoCopy(BinaryDataRef script)
{
   TxOutScriptRef outputRef;

   auto p2pkh_prefix = 
      SCRIPT_PREFIX(BitcoinSettings::getPubkeyHashPrefix());
   auto p2sh_prefix = 
      SCRIPT_PREFIX(BitcoinSettings::getScriptHashPrefix());
   
   auto type = getTxOutScriptType(script);
   switch (type)
   {
   case(TXOUT_SCRIPT_STDHASH160) :
   {
      outputRef.type_ = p2pkh_prefix;
      outputRef.scriptRef_ = script.getSliceRef(3, 20);
      break;
   }

   case(TXOUT_SCRIPT_P2WPKH) :
   {
      outputRef.type_ = SCRIPT_PREFIX_P2WPKH;
      outputRef.scriptRef_ = script.getSliceRef(2, 20);
      break;
   }

   case(TXOUT_SCRIPT_P2WSH) :
   {
      outputRef.type_ = SCRIPT_PREFIX_P2WSH;
      outputRef.scriptRef_ = script.getSliceRef(2, 32);
      break;
   }

   case(TXOUT_SCRIPT_STDPUBKEY65) :
   {
      outputRef.type_ = p2pkh_prefix;
      outputRef.scriptCopy_ = getHash160(script.getSliceRef(1, 65));
      outputRef.scriptRef_.setRef(outputRef.scriptCopy_);
      break;
   }

   case(TXOUT_SCRIPT_STDPUBKEY33) :
   {
      outputRef.type_ = p2pkh_prefix;
      outputRef.scriptCopy_ = getHash160(script.getSliceRef(1, 33));
      outputRef.scriptRef_.setRef(outputRef.scriptCopy_);
      break;
   }

   case(TXOUT_SCRIPT_P2SH) :
   {
      outputRef.type_ = p2sh_prefix;
      outputRef.scriptRef_ = script.getSliceRef(2, 20);
      break;
   }

   case(TXOUT_SCRIPT_NONSTANDARD) :
   {
      outputRef.type_ = SCRIPT_PREFIX_NONSTD;
      outputRef.scriptCopy_ = getHash160(script);
      outputRef.scriptRef_.setRef(outputRef.scriptCopy_);
      break;
   }

   case(TXOUT_SCRIPT_MULTISIG) :
   {
      outputRef.type_ = SCRIPT_PREFIX_MULTISIG;
      outputRef.scriptCopy_ = getMultisigUniqueKey(script);
      outputRef.scriptRef_.setRef(outputRef.scriptCopy_);
      break;
   }

   case(TXOUT_SCRIPT_OPRETURN) :
   {
      outputRef.type_ = SCRIPT_PREFIX_OPRETURN;
      auto size = script.getSize();
      size_t pos = 1;
      if (size > 77)
         pos += 2;
      if (size > 1)
         ++pos;

      outputRef.scriptRef_ = script.getSliceRef(pos, size - pos);
      break;
   }

   default:
      LOGERR << "What kind of TxOutScript did we get?";
   }

   return outputRef;
}

////////////////////////////////////////////////////////////////////////////////
string BtcUtils::base64_encode(const string& in)
{
   size_t main_count = in.size() / 3;
   string result;
   result.reserve(main_count * 4 + 5);

   auto ptr = (const uint8_t*)in.c_str();
   for (unsigned i = 0; i < main_count; i++)
   {
      uint32_t bits24 = ptr[i * 3] << 24 | ptr[i*3+1] << 16 | ptr[i*3+2] << 8;
      for (unsigned y = 0; y < 4; y++)
      {
         unsigned val = (bits24 & 0xFC000000) >> 26;
         result.append(1, base64Chars_.c_str()[val]);
         bits24 <<= 6;
      }
   }

   //padding
   size_t left_over = in.size() - main_count * 3;
   if (left_over == 0)
      return result;

   uint32_t bits24;
   if (left_over == 1)
      bits24 = ptr[main_count * 3] << 24;
   else
      bits24 = ptr[main_count * 3] << 24 | ptr[main_count * 3 + 1] << 16;

   for (unsigned i = 0; i <= left_over; i++)
   {
      unsigned val = (bits24 & 0xFC000000) >> 26;
      result.append(1, base64Chars_.c_str()[val]);
      bits24 <<= 6;
   }

   result.append(3 - left_over, '=');
   
   return result;
}

////////////////////////////////////////////////////////////////////////////////
string BtcUtils::base64_decode(const string& in)
{
   size_t count = (in.size() + 3) / 4;
   string result;
   result.resize(count * 3);
   auto ptr = in.c_str();
   auto result_ptr = (uint8_t*)result.c_str();

   unsigned y=0;
   {
      unsigned i=0;
      uint32_t val = 0;
      for (; y < in.size(); y++)
      {
         if (y % 4 == 0 && y != 0)
         {
            result_ptr[i * 3] = (val & 0xFF000000) >> 24;
            result_ptr[i * 3 + 1] = (val & 0x00FF0000) >> 16;
            result_ptr[i * 3 + 2] = (val & 0x0000FF00) >> 8;
            val = 0;
            ++i;
         }

         auto val8 = ptr[y];
         auto iter = base64Vals_.find(val8);
         if (iter == base64Vals_.end())
         {
            if (val8 == '=')
               break;

            throw runtime_error("invalid b64 character");
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
string BtcUtils::encodePrivKeyBase58(const SecureBinaryData& privKey)
{
   BinaryWriter bwPrivKey;
   bwPrivKey.put_uint8_t(BitcoinSettings::getPrivKeyPrefix());
   bwPrivKey.put_BinaryData(privKey);
   bwPrivKey.put_uint8_t(0x01);

   auto&& checksum = getHash256(bwPrivKey.getData());
   bwPrivKey.put_BinaryDataRef(checksum.getSliceRef(0, 4));

   return base58_encode(bwPrivKey.getData());
}

////////////////////////////////////////////////////////////////////////////////
SecureBinaryData BtcUtils::decodePrivKeyBase58(const string& strPrivKey)
{
   SecureBinaryData decodedKey(base58_decode(strPrivKey));

   BinaryRefReader brr(decodedKey.getRef());

   //prefix
   auto prefix = brr.get_uint8_t();
   if (prefix != BitcoinSettings::getPrivKeyPrefix())
      throw runtime_error("network prefix mismatch");
   brr.rewind(1);

   //key
   auto keyRef = brr.get_BinaryDataRef(brr.getSize() - 4);

   //checksum
   auto checksum = brr.get_BinaryDataRef(4);
   auto hash = BtcUtils::getHash256(keyRef);
   if (hash.getSliceRef(0, 4) != checksum)
      throw runtime_error("privkey checksum mismatch");

   return SecureBinaryData(keyRef.getSliceRef(1, 32));
}

////////////////////////////////////////////////////////////////////////////////
const string BtcUtils::swHeaderMain_ = string(SEGWIT_ADDRESS_MAINNET_HEADER);
const string BtcUtils::swHeaderTest_ = string(SEGWIT_ADDRESS_TESTNET_HEADER);

////////////////////////////////////////////////////////////////////////////////
string BtcUtils::scrAddrToSegWitAddress(const BinaryData& scrAddr)
{
   //hardcoded for version 0 witness programs for now
   const string* headerPtr;

   if (BitcoinSettings::getPubkeyHashPrefix() == SCRIPT_PREFIX_HASH160)
      headerPtr = &swHeaderMain_;
   else if (BitcoinSettings::getPubkeyHashPrefix() == SCRIPT_PREFIX_HASH160_TESTNET)
      headerPtr = &swHeaderTest_;
   else
      throw runtime_error("invalid network for segwit address");

   //73 + header size
   string result;
   result.resize(73 + headerPtr->size());
   if (segwit_addr_encode(
      (char*)result.c_str(), headerPtr->c_str(), 0, 
      scrAddr.getPtr(), scrAddr.getSize()) == 0)
   {
      throw runtime_error("failed to encode to sw address!");
   }

   //adjust result size by looking for the null terminator
   auto len = strnlen(result.c_str(), result.size());
   if (len == 0 || len == result.size())
      throw runtime_error("failed to encode to sw address!");

   result.resize(len);
   return result;
}

////////////////////////////////////////////////////////////////////////////////
pair<BinaryData, int> BtcUtils::segWitAddressToScrAddr(const string& swAddr)
{
   const string* headerPtr;

   if (BitcoinSettings::getPubkeyHashPrefix() == SCRIPT_PREFIX_HASH160)
      headerPtr = &swHeaderMain_;
   else if (BitcoinSettings::getPubkeyHashPrefix() == SCRIPT_PREFIX_HASH160_TESTNET)
      headerPtr = &swHeaderTest_;
   else
      throw runtime_error("invalid network for segwit address");

   int ver;
   size_t len;
   BinaryData result(40);
   if (segwit_addr_decode(&ver, result.getPtr(), &len,
      headerPtr->c_str(), swAddr.c_str()) == 0)
   {
      throw runtime_error("failed to decode sw address!");
   }

   if (len == 0)
      throw runtime_error("empty sw program buffer");

   if (ver != 0)
      throw runtime_error("only supporting sw version 0 for now");

   //resize result
   result.resize(len);
   
   return make_pair(result, ver);
}

////////////////////////////////////////////////////////////////////////////////
int BtcUtils::get_varint_len(const int64_t& value)
{
   if (value < 0xFD)
      return 1;
   else if (value <= 0xFFFF)
      return 3;
   else if (value <= 0xFFFFFFFF)
      return 5;

   return 9;
}

/////////////////////////////////////////////////////////////////////////////
double BtcUtils::convertDiffBitsToDouble(BinaryData const & diffBitsBinary)
{
   uint32_t diffBits = READ_UINT32_LE(diffBitsBinary);
   int nShift = (diffBits >> 24) & 0xff;
   double dDiff = (double)0x0000ffff / (double)(diffBits & 0x00ffffff);
   
   while (nShift < 29)
   {
      dDiff *= 256.0;
      nShift++;
   }
   while (nShift > 29)
   {
      dDiff /= 256.0;
      nShift--;
   }
   return dDiff;
}

/////////////////////////////////////////////////////////////////////////////
BinaryData BtcUtils::convertDoubleToDiffBits(double diff)
{
   //quick and dirty, for unit test reorg purposes
   unsigned nShift = 29;
   while (diff > 16777215.0)
   {
      diff /= 256.0;
      --nShift;
   }

   BinaryData diffBits(4);
   auto ptr = diffBits.getPtr();

   auto val = 65535.0 / diff;
   unsigned* bits = (unsigned*)ptr;
   *bits = ((unsigned)val);
   ptr[3] = nShift;

   return diffBits;
}

/////////////////////////////////////////////////////////////////////////////
map<BinaryDataRef, BinaryDataRef> BtcUtils::getPSBTDataPairs(
   BinaryRefReader& brr)
{
   map<BinaryDataRef, BinaryDataRef> result;
   while (true)
   {
      auto keylen = brr.get_var_int();
      if (keylen == 0)
         break;

      auto key = brr.get_BinaryDataRef(keylen);

      auto vallen = brr.get_var_int();
      auto val = brr.get_BinaryDataRef(vallen);

      result.emplace(key, val);
   }

   return result;
}

/////////////////////////////////////////////////////////////////////////////
std::vector<BinaryDataRef> BtcUtils::splitPushOnlyScriptRefs(
   BinaryDataRef script)
{
   std::vector<BinaryDataRef> opList;
   opList.reserve(4);

   BinaryRefReader brr(script);
   uint8_t nextOp;
   while (brr.getSizeRemaining() > 0) {
      nextOp = brr.get_uint8_t();
      if(nextOp == 0) {
         // Implicit pushdata
         brr.rewind(1);
         opList.emplace_back(brr.get_BinaryDataRef(1));
      } else if(nextOp < 76) {
         // Implicit pushdata
         opList.emplace_back(brr.get_BinaryDataRef(nextOp));
      } else if(nextOp == 76) {
         uint8_t nb = brr.get_uint8_t();
         opList.emplace_back( brr.get_BinaryDataRef(nb));
      } else if(nextOp == 77) {
         uint16_t nb = brr.get_uint16_t();
         opList.emplace_back( brr.get_BinaryDataRef(nb));
      } else if(nextOp == 78) {
         uint16_t nb = brr.get_uint32_t();
         opList.push_back( brr.get_BinaryDataRef(nb));
      }
      else if(nextOp > 78 && nextOp < 97 && nextOp !=80) {
         brr.rewind(1);
         opList.push_back( brr.get_BinaryDataRef(1));
      } else {
         return {};
      }
   }

   return opList;
}
