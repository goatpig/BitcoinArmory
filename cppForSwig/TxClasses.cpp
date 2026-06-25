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

#include <assert.h>
#include <cstring>
#include <cmath>

#include "TxClasses.h"
#include <Utils/varint.h>
#include <Utils/BtcUtils.h>
#include <Utils/DBUtils.h>

using namespace Armory;

/////////////////////////////////////////////////////////////////////////////
// RecipientReuseException
RecipientReuseException::RecipientReuseException(
   const std::vector<BinaryData>& scrAddrVec, uint64_t total, uint64_t val) :
   total_(total), value_(val)
{
   for (auto& scrAddr : scrAddrVec) {
      addrVec_.emplace_back(BtcUtils::scrAddrToBase58(scrAddr));
   }
}

const std::vector<std::string>& RecipientReuseException::getAddresses() const
{
   return addrVec_;
}

uint64_t RecipientReuseException::total() const
{
   return total_;
}

uint64_t RecipientReuseException::value() const
{
   return value_;
}

/////////////////////////////////////////////////////////////////////////////
// Outpoint
Outpoint::Outpoint(const uint8_t* ptr, size_t remaining)
{
   unserialize(ptr, remaining);
}

Outpoint::Outpoint(const BinaryData& txHash, uint32_t txOutIndex) :
   txHash_(txHash), txOutIndex_(txOutIndex)
{}

const BinaryData& Outpoint::getTxHash() const
{
   return txHash_;
}

BinaryDataRef Outpoint::getTxHashRef() const
{
   return {txHash_};
}

uint32_t Outpoint::getTxOutIndex() const
{
   return txOutIndex_;
}

bool Outpoint::isCoinbase() const
{
   return txHash_ == BtcUtils::EmptyHash;
}

////////
bool Outpoint::operator<(const Outpoint& op2) const
{
   if (txHash_ == op2.txHash_) {
      return txOutIndex_ < op2.txOutIndex_;
   } else {
      return txHash_ < op2.txHash_;
   }
}

bool Outpoint::operator==(const Outpoint& op2) const
{
   return txHash_ == op2.txHash_ &&
      txOutIndex_ == op2.txOutIndex_;
}

////////
void Outpoint::serialize(BinaryWriter& bw) const
{
   bw.put_BinaryData(txHash_);
   bw.put_uint32_t(txOutIndex_);
}

BinaryData Outpoint::serialize() const
{
   BinaryWriter bw(36);
   serialize(bw);
   return bw.getData();
}

////////
void Outpoint::unserialize(const uint8_t* ptr, size_t size)
{
   if (size < 32) {
      throw BtcUtils::BlockDeserializingException{};
   }
   txHash_.copyFrom(ptr, 32);
   txOutIndex_ = READ_UINT32_LE(ptr + 32);
}

void Outpoint::unserialize(BinaryReader& br)
{
   if (br.getSizeRemaining() < 32) {
      throw BtcUtils::BlockDeserializingException();
   }
   br.get_BinaryData(txHash_, 32);
   txOutIndex_ = br.get_uint32_t();
}

void Outpoint::unserialize(BinaryRefReader& brr)
{
   if (brr.getSizeRemaining() < 32) {
      throw BtcUtils::BlockDeserializingException();
   }
   brr.get_BinaryData(txHash_, 32);
   txOutIndex_ = brr.get_uint32_t();
}

void Outpoint::unserialize(const BinaryData& bd)
{
   unserialize(bd.getPtr(), bd.getSize());
}

void Outpoint::unserialize(const BinaryDataRef& bdRef)
{
   unserialize(bdRef.getPtr(), bdRef.getSize());
}

////////////////////////////////////////////////////////////////////////////////
// TxIn
TxIn::TxIn(const uint8_t* ptr, size_t size, size_t nbytes, uint32_t idx) :
   dataCopy_(getRawData(ptr, size, nbytes)),
   index_(idx)
{
   unserialize_checked();
}

TxIn::TxIn(BinaryDataRef dataRef, size_t nbytes, uint32_t idx) :
   dataCopy_(getRawData(dataRef.getPtr(), dataRef.getSize(), nbytes)),
   index_(idx)
{
   unserialize_checked();
}

TxIn::TxIn(BinaryRefReader brr, size_t nbytes, uint32_t idx) :
   dataCopy_(getRawData(brr.getCurrPtr(), brr.getSizeRemaining(), nbytes)),
   index_(idx)
{
   unserialize_checked();
   brr.advance(getSize());
}

////////
const uint8_t* TxIn::getPtr() const
{
   return dataCopy_.getPtr();
}

size_t TxIn::getSize() const
{
   return dataCopy_.getSize();
}

bool TxIn::isStandard() const
{
   return scriptType_ != TxInScriptType::NONSTANDARD;
}

bool TxIn::isCoinbase() const
{
   return scriptType_ == TxInScriptType::COINBASE;
}

Outpoint TxIn::getOutPoint() const
{
   return Outpoint{getPtr(), getSize()};
}

////////
BinaryData TxIn::getScript(void) const
{
   uint8_t varIntLen;
   size_t scrLen = BtcUtils::readVarInt(
      getPtr() + 36, getSize() - 36, varIntLen);
   return BinaryData{getPtr() + getScriptOffset(), scrLen};
}

BinaryDataRef TxIn::getScriptRef(void) const
{
   uint8_t varIntLen;
   size_t scrLen = BtcUtils::readVarInt(
      getPtr() + 36, getSize() - 36, varIntLen);
   return BinaryDataRef{getPtr() + getScriptOffset(), scrLen};
}

size_t TxIn::getScriptSize() const
{
   return getSize() - scriptOffset_ + 4;
}

TxInScriptType TxIn::getScriptType() const
{
   return scriptType_;
}

uint32_t TxIn::getScriptOffset() const
{
   return scriptOffset_;
}

uint32_t TxIn::getIndex() const
{
   return index_;
}

uint32_t TxIn::getSequence() const
{
   return READ_UINT32_LE(getPtr() + getSize() - 4);
}

////////
const BinaryData& TxIn::serialize() const
{
   return dataCopy_;
}

BinaryDataRef TxIn::getRawData(const uint8_t* ptr,
   size_t size, size_t nbytes)
{
   size_t numBytes = nbytes == 0 ?
      BtcUtils::TxInCalcLength(ptr, size) :
      nbytes;

   if (size < numBytes) {
      throw BtcUtils::BlockDeserializingException();
   }
   return BinaryDataRef{ptr, numBytes};
}

void TxIn::unserialize_checked()
{
   if (dataCopy_.getSize() - 36 < 1) {
      throw BtcUtils::BlockDeserializingException();
   }
   scriptOffset_ = 36 + BtcUtils::readVarIntLength(getPtr() + 36);

   if (dataCopy_.getSize() < 32) {
      throw BtcUtils::BlockDeserializingException();
   }
   scriptType_ = BtcUtils::getTxInScriptType(
      getScriptRef(),
      BinaryDataRef(getPtr(), 32)
   );
}

/////////////////////////////////////////////////////////////////////////////
// Not all TxIns have this information.  Have to go to the Outpoint and get
// the corresponding TxOut to find the sender.  In the case the sender is
// not available, return false and don't write the output
bool TxIn::getSenderScrAddrIfAvail(BinaryData& addrTarget) const
{
   if (scriptType_ == TxInScriptType::NONSTANDARD ||
      scriptType_ == TxInScriptType::COINBASE) {
      addrTarget = BtcUtils::BadAddress;
      return false;
   }

   try {
      addrTarget = BtcUtils::getTxInAddrFromType(getScript(), scriptType_);
   } catch (const BtcUtils::BlockDeserializingException&) {
      return false;
   }
   return true;
}

////////////////////////////////////////////////////////////////////////////////
BinaryData TxIn::getSenderScrAddrIfAvail() const
{
   BinaryData addrTarget;
   getSenderScrAddrIfAvail(addrTarget);
   return addrTarget;
}

////////////////////////////////////////////////////////////////////////////////
void TxIn::pprint(std::ostream& os, int nIndent, bool) const
{
   std::string indent = "";
   for (int i = 0; i<nIndent; i++) {
      indent = indent + "   ";
   }

   os << indent << "TxIn:" << std::endl;
   os << indent << "   Type:    ";
   switch (scriptType_)
   {
      case TxInScriptType::STDUNCOMPR:
         os << "UncomprKey" << std::endl;
         break;

      case TxInScriptType::STDCOMPR:
         os << "ComprKey" << std::endl;
         break;

      case TxInScriptType::COINBASE:
         os << "Coinbase" << std::endl;
         break;

      case TxInScriptType::SPENDPUBKEY:
         os << "SpendPubKey" << std::endl;
         break;

      case TxInScriptType::SPENDP2SH:
         os << "SpendP2sh" << std::endl;
         break;

      case TxInScriptType::NONSTANDARD:
         os << "NonStandard " << std::endl;
         break;

      case TxInScriptType::SPENDMULTI:
         os << "Multi" << std::endl;
         break;

      case TxInScriptType::WITNESS:
         os << "Witness Data" << std::endl;
         break;

      case TxInScriptType::P2WPKH_P2SH:
         os << "Nested Segwit" << std::endl;
         break;

      case TxInScriptType::P2WSH_P2SH:
         os << "Nested P2WSH" << std::endl;
         break;

      default:
         os << "UNKNOWN" << std::endl;
   }
   os << indent << "   Bytes:   " << getSize() << std::endl;
   os << indent << "   Sender:  " <<
      getSenderScrAddrIfAvail().copySwapEndian().toHexStr() << std::endl;
}

////////////////////////////////////////////////////////////////////////////////
// TxOut
TxOut::TxOut(BinaryDataRef data, size_t nbytes, uint32_t idx) :
   dataCopy_(getRawData(data.getPtr(), data.getSize(), nbytes)),
   index_(idx)
{
   unserialize();
}

TxOut::TxOut(BinaryRefReader& brr, size_t nbytes, uint32_t idx) :
   dataCopy_(getRawData(brr.getCurrPtr(), brr.getSizeRemaining(), nbytes)),
   index_(idx)
{
   unserialize();
   brr.advance(getSize());
}

TxOut::TxOut(const uint8_t* ptr, size_t size, uint32_t idx) :
   dataCopy_(getRawData(ptr, size, 0)), index_(idx)
{
   unserialize();
}

BinaryDataRef TxOut::getRawData(const uint8_t* ptr,
   size_t size, size_t nbytes)
{
   uint32_t numBytes = nbytes == 0 ?
      BtcUtils::TxOutCalcLength(ptr, size) :
      nbytes;

   if (size < numBytes) {
      throw BtcUtils::BlockDeserializingException{};
   }
   return {ptr, numBytes};
}

void TxOut::unserialize()
{
   scriptOffset_ = 8 + BtcUtils::readVarIntLength(getPtr() + 8);
   if (dataCopy_.getSize() <= scriptOffset_) {
      throw BtcUtils::BlockDeserializingException{};
   }
   BinaryDataRef scriptRef{
      dataCopy_.getPtr() + scriptOffset_,
      getScriptSize()
   };
   scriptType_ = BtcUtils::getTxOutScriptType(scriptRef);
   uniqueScrAddr_ = BtcUtils::getTxOutScrAddr(scriptRef);
}

////////
const uint8_t* TxOut::getPtr() const
{
   return dataCopy_.getPtr();
}

uint32_t TxOut::getSize() const
{
   return (uint32_t)dataCopy_.getSize();
}

Types::Amount TxOut::getAmount() const
{
   return READ_UINT64_LE(dataCopy_.getPtr());
}

bool TxOut::isStandard() const
{
   return scriptType_ != TxOutScriptType::NONSTANDARD;
}

Types::TxIOId TxOut::getIndex()
{
   return index_;
}

////////
BinaryData TxOut::getScript() const
{
   return BinaryData{
      dataCopy_.getPtr() + scriptOffset_,
      getScriptSize()};
}

BinaryDataRef TxOut::getScriptRef() const
{
   return BinaryDataRef{
      dataCopy_.getPtr() + scriptOffset_,
      getScriptSize()
   };
}

TxOutScriptType TxOut::getScriptType() const
{
   return scriptType_;
}

uint32_t TxOut::getScriptSize() const
{
   return getSize() - scriptOffset_;
}

size_t TxOut::getScriptOffset() const
{
   return scriptOffset_;
}

////////
const Types::ScrAddr& TxOut::getScrAddress() const
{
   return uniqueScrAddr_;
}

////////
BinaryData TxOut::serialize() const
{
   return BinaryData(dataCopy_);
}

BinaryDataRef TxOut::serializeRef() const
{
   return dataCopy_.getRef();
}

////////
void TxOut::pprint(std::ostream& os, int nIndent, bool pBigendian)
{
   std::string indent = "";
   for (int i = 0; i<nIndent; i++) {
      indent = indent + "   ";
   }

   os << indent << "TxOut:" << std::endl;
   os << indent << "   Type:   ";
   switch (scriptType_)
   {
      case TxOutScriptType::STDHASH160:
         os << "StdHash160" << std::endl;
         break;

      case TxOutScriptType::STDPUBKEY65:
         os << "StdPubKey65" << std::endl;
         break;

      case TxOutScriptType::STDPUBKEY33:
         os << "StdPubKey65" << std::endl;
         break;

      case TxOutScriptType::P2SH:
         os << "Pay2ScrHash" << std::endl;
         break;

      case TxOutScriptType::MULTISIG:
         os << "Multi" << std::endl;
         break;

      case TxOutScriptType::NONSTANDARD:
         os << "NonStandard" << std::endl;
         break;

      case TxOutScriptType::P2WSH:
         os << "P2WSH" << std::endl;
         break;

      case TxOutScriptType::OPRETURN:
         os << "OP_return" << std::endl;
         break;

      default:
         os << "UNKONWN" << std::endl;
   }

   os << indent << "   Recip:  "
      << uniqueScrAddr_.toHexStr(pBigendian).c_str()
      << (pBigendian ? " (BE)" : " (LE)") << std::endl;
   os << indent << "   Amount:  " << getAmount() << std::endl;
}

////////////////////////////////////////////////////////////////////////////////
// Tx methods
Tx::Tx(const uint8_t* ptr, size_t size) :
   Tx{unserialize(ptr, size)}
{}

Tx::Tx(BinaryDataRef str) :
   Tx{unserialize(str.getPtr(), str.getSize())}
{}

Tx::Tx(BinaryRefReader& brr) :
   Tx{brr.getCurrPtr(), brr.getSizeRemaining()}
{
   brr.advance(getSize());
}

Tx::Tx(BinaryDataRef data,
   std::vector<size_t>& offsetsTxIn,
   std::vector<size_t>& offsetsTxOut,
   std::vector<size_t>& offsetsWitness,
   uint32_t version, uint32_t lockTime, bool useWitness) :
   dataCopy_(data),
   offsetsTxIn_{std::move(offsetsTxIn)},
   offsetsTxOut_{std::move(offsetsTxOut)},
   offsetsWitness_{std::move(offsetsWitness)},
   version_{version}, lockTime_{lockTime}, usesWitness_{useWitness}
{}

Tx Tx::unserialize(const uint8_t* ptr, size_t size)
{
   std::vector<size_t> txins, txouts, witnesses;
   uint32_t nBytes = BtcUtils::TxCalcLength(ptr, size,
      &txins, &txouts, &witnesses);
   if (size < 8 || nBytes > size) {
      throw BtcUtils::BlockDeserializingException();
   }
   BinaryDataRef data{ptr, nBytes};

   uint32_t version = READ_UINT32_LE(ptr);
   bool usesWitness = BtcUtils::checkSwMarker(ptr + 4);
   uint32_t numWitness = witnesses.size() - 1;
   if (4 > nBytes - witnesses[numWitness]) {
      throw BtcUtils::BlockDeserializingException();
   }
   uint32_t lockTime = READ_UINT32_LE(ptr + witnesses[numWitness]);

   return Tx{data, txins, txouts, witnesses,
      version, lockTime, usesWitness};
}

////////
bool Tx::operator==(const Tx& rhs) const
{
   return this->thisHash_ == rhs.thisHash_;
}

////////
const uint8_t* Tx::getPtr() const
{
   return dataCopy_.getPtr();
}

size_t Tx::getSize() const
{
   return dataCopy_.getSize();
}

////////
bool Tx::isCoinbase() const
{
   BinaryDataRef bdr{dataCopy_.getPtr() + offsetsTxIn_[0], 32};
   return bdr == BtcUtils::EmptyHash;
}

uint32_t Tx::getVersion() const
{
   return READ_UINT32_LE(dataCopy_.getPtr());
}

size_t Tx::getNumTxIn() const
{
   return offsetsTxIn_.size() - 1;
}

size_t Tx::getNumTxOut() const
{
   return offsetsTxOut_.size() - 1;
}

uint64_t Tx::getSumOfOutputs(void) const
{
   uint64_t sumVal = 0;
   for (uint32_t i = 0; i < getNumTxOut(); i++) {
      sumVal += getTxOutCopy(i).getAmount();
   }
   return sumVal;
}

uint32_t Tx::getLockTime() const
{
   return lockTime_;
}

bool Tx::isChained() const
{
   return isChainedZc_;
}

bool Tx::isSegWit() const
{ 
   return usesWitness_;
}

uint32_t Tx::getTxTime() const
{
   return txTime_;
}

////////
void Tx::setTxKey(Types::TxKey key)
{
   txKey_ = key;
}

Types::BlockId Tx::getBlockId() const
{
   if (!Types::isThisAZCKey(txKey_)) {
      return Types::getBlockIDFromTxKey(txKey_);
   } else {
      return Types::INVALID_BLOCK_ID;
   }
}

Types::TxId Tx::getTxIndex() const
{
   if (!Types::isThisAZCKey(txKey_)) {
      return Types::getTxIndexFromTxKey(txKey_);
   } else {
      return UINT16_MAX;
   }
}

////////
size_t Tx::getTxInOffset(uint32_t i) const
{
   return offsetsTxIn_[i];
}

size_t Tx::getTxOutOffset(uint32_t i) const
{
   return offsetsTxOut_[i];
}

size_t Tx::getWitnessOffset(uint32_t i) const
{
   return  offsetsWitness_[i];
}

////////
void Tx::setRBF(bool isTrue)
{
   isRBF_ = isTrue;
}

void Tx::setChainedZC(bool isTrue)
{
   isChainedZc_ = isTrue;
}

void Tx::setTxTime(uint32_t txtime)
{
   txTime_ = txtime;
}

////////
Tx Tx::createFromStr(const BinaryData& bd)
{
   return Tx{bd};
}

BinaryData Tx::serialize() const
{
   return dataCopy_;
}

BinaryData Tx::serializeNoWitness(void) const
{
   BinaryData dataNoWitness;
   dataNoWitness.append(WRITE_UINT32_LE(version_));
   BinaryDataRef txBody(dataCopy_.getPtr() + 6, offsetsTxOut_.back() - 6);
   dataNoWitness.append(txBody);
   dataNoWitness.append(WRITE_UINT32_LE(lockTime_));
   return dataNoWitness;
}

////////
const Types::TxHash& Tx::getThisHash() const
{
   if (thisHash_.empty()) {
      if (usesWitness_) {
         auto dataNoWitness = serializeNoWitness();
         thisHash_ = BtcUtils::getHash256(dataNoWitness);
      } else {
         thisHash_ = BtcUtils::getHash256(dataCopy_);
      }
   }
   return thisHash_;
}

/////////////////////////////////////////////////////////////////////////////
// This is not a pointer to persistent object, this method actually CREATES
// the TxIn. But it's fast and doesn't hold a lot of post-construction
// information, so it can probably just be computed on the fly
TxIn Tx::getTxInCopy(Types::TxIOId i) const
{
   if (offsetsTxIn_.empty() || i >= (ssize_t)offsetsTxIn_.size() - 1) {
      throw std::range_error("index out of bound");
   }

   uint32_t txinSize = offsetsTxIn_[i + 1] - offsetsTxIn_[i];
   return {dataCopy_.getPtr() + offsetsTxIn_[i],
      dataCopy_.getSize() - offsetsTxIn_[i],
      txinSize, i};
}

TxOut Tx::getTxOutCopy(Types::TxIOId i) const
{
   if (offsetsTxOut_.empty() || i >= (ssize_t)offsetsTxOut_.size() - 1) {
      std::string errStr(
         "index out of bound: " + std::to_string(i) + " out of " +
         std::to_string(offsetsTxOut_.size()));
      throw std::range_error(errStr);
   }

   return {
      dataCopy_.getPtr() + offsetsTxOut_[i],
      offsetsTxOut_[i + 1] - offsetsTxOut_[i], i
   };
}

////////
bool Tx::isRBF() const
{
   if (isRBF_) {
      return true;
   }

   for (unsigned i = 0; i < offsetsTxIn_.size() - 1; i++) {
      uint32_t sequenceOffset = offsetsTxIn_[i + 1] - 4;
      uint32_t sequence;
      memcpy(&sequence,
         dataCopy_.getPtr() + sequenceOffset,
         sizeof(uint32_t));

      if (sequence < 0xFFFFFFFF - 1) {
         return true;
      }
   }
   return false;
}

////////
size_t Tx::getWeight() const
{
   // from https://github.com/bitcoin/bips/blob/master/bip-0141.mediawiki
   // weight = base transaction size * 3 + total transaction size

   size_t size = getSize();
   if (offsetsWitness_.empty()) {
      // for non segwit base transaction size = total transaction size
      return 4 * size;
   }

   size_t witnessSize = offsetsWitness_.back() - offsetsWitness_.front();
   // Two bytes for marker and flag (see BIP-141)
   size_t baseSize = size - 2 - witnessSize;
   size_t weight = baseSize * 3 + size;
   return weight;
}

size_t Tx::getTxWeight() const
{
   // from https://github.com/bitcoin/bips/blob/master/bip-0141.mediawiki
   // virtual transaction size = weight / 4 (rounded up to the next integer).

   size_t weight = getWeight();
   // divide with rounding up
   size_t vSize = (weight + 3) / 4;
   return vSize;
}

////////
Types::TxKey Tx::getDBKey() const
{
   return txKey_;
}

////////////////////////////////////////////////////////////////////////////////
// UTXO methods
UTXO::UTXO(Types::Amount amt, uint32_t height, Types::TxId txid,
   Types::TxIOId txoutid, Types::TxHash txHash, BinaryData script) :
   amount(amt), txHeight(height), txIndex(txid), txOutIndex(txoutid),
   txHash(std::move(txHash)), script(std::move(script))
{}

UTXO::UTXO()
{}

////////
bool UTXO::operator==(const UTXO& rhs) const
{
   if (rhs.getTxHash() != getTxHash()) {
      return false;
   }
   return rhs.getTxOutIndex() == getTxOutIndex();
}

bool UTXO::operator!=(const UTXO& rhs) const
{
   return !(*this == rhs);
}

bool UTXO::operator<(const UTXO& rhs) const
{
   if (txHash != rhs.txHash) {
      return txHash < rhs.txHash;
   }

   if (txOutIndex != rhs.txOutIndex) {
      return txOutIndex < rhs.txOutIndex;
   }
   return false;
}

////////
bool UTXO::isInitialized() const
{
   return !script.empty();
}

Types::Amount UTXO::getAmount() const
{
   return amount;
}

const Types::TxHash& UTXO::getTxHash() const
{
   return txHash;
}

std::string UTXO::getTxHashStr() const
{
   return txHash.toHexStr();
}

const BinaryData& UTXO::getScript() const
{
   return script;
}

Types::TxId UTXO::getTxIndex() const
{
   return txIndex;
}

Types::TxIOId UTXO::getTxOutIndex() const
{
   return txOutIndex;
}

uint32_t UTXO::getNumConfirm(uint32_t height) const
{
   if (txHeight == UINT32_MAX) {
      return 0;
   }
   return height - txHeight + 1;
}

unsigned UTXO::getPreferredSequence() const
{
   return preferredSequence;
}

uint32_t UTXO::getHeight() const
{
   return txHeight;
}

BinaryData UTXO::getRecipientScrAddr() const
{
   return BtcUtils::getTxOutScrAddr(script);
}

////////
bool UTXO::isSegWit() const
{
   return isInputSW;
}

unsigned UTXO::getInputRedeemSize() const
{
   if (txinRedeemSizeBytes == UINT32_MAX) {
      throw std::runtime_error("redeem size is no set");
   }
   return txinRedeemSizeBytes;
}

unsigned UTXO::getWitnessDataSize() const
{
   if (!isSegWit() || witnessDataSizeBytes == UINT32_MAX) {
      throw std::runtime_error("no witness data size available");
   }
   return witnessDataSizeBytes;
}

////////
BinaryData UTXO::serialize() const
{
   BinaryWriter bw;
   //8 + 4 + 2 + 2 + (1 + hash) + (3 + script) + 4
   bw.reserve(26 + txHash.getSize() + script.getSize());
   bw.put_uint64_t(amount);
   bw.put_uint32_t(txHeight);
   bw.put_uint16_t(txIndex);
   bw.put_uint16_t(txOutIndex);

   bw.put_var_int(txHash.getSize());
   bw.put_BinaryData(txHash);

   bw.put_var_int(script.getSize());
   bw.put_BinaryData(script);
   bw.put_uint32_t(preferredSequence);
   return bw.getData();
}

BinaryData UTXO::serializeTxOut() const
{
   BinaryWriter bw;
   bw.reserve(11 + script.getSize());
   bw.put_uint64_t(amount);
   bw.put_var_int(script.getSize());
   bw.put_BinaryData(script);
   return bw.getData();
}

////////
void UTXO::unserialize(const BinaryData& data)
{
   if (data.getSize() < 18) {
      throw std::runtime_error("invalid raw utxo size");
   }
   BinaryRefReader brr(data.getRef());


   amount = brr.get_uint64_t();
   txHeight = brr.get_uint32_t();
   txIndex = brr.get_uint16_t();
   txOutIndex = brr.get_uint16_t();

   auto hashSize = brr.get_var_int();
   txHash = std::move(brr.get_BinaryData(hashSize));

   auto scriptSize = brr.get_var_int();
   if (scriptSize == 0) {
      throw std::runtime_error("no script data in raw utxo");
   }
   script = std::move(brr.get_BinaryData(scriptSize));
   preferredSequence = brr.get_uint32_t();
}

void UTXO::unserializeRaw(const BinaryData& data)
{
   BinaryRefReader brr(data.getRef());
   amount = brr.get_uint64_t();
   auto scriptSize = brr.get_var_int();
   script = brr.get_BinaryData(scriptSize);
}

////////////////////////////////////////////////////////////////////////////////
// Output
Output::Output(uint64_t value, uint32_t txHeight, uint32_t txIndex,
   uint32_t txOutIndex, BinaryData txHash, BinaryData script,
   BinaryData spender) :
   UTXO(value, txHeight, txIndex, txOutIndex, txHash, script),
   spenderHash(spender)
{}

bool Output::isSpent() const
{
   return !spenderHash.empty();
}

////////////////////////////////////////////////////////////////////////////////
// AddressBookEntry methods
AddressBookEntry::AddressBookEntry() :
   scrAddr_{BtcUtils::BadAddress}
{}

AddressBookEntry::AddressBookEntry(BinaryDataRef scraddr) :
   scrAddr_(scraddr)
{}

bool AddressBookEntry::operator<(const AddressBookEntry& rhs) const
{
   return scrAddr_ < rhs.scrAddr_;
}

////////
const BinaryData& AddressBookEntry::getScrAddr() const
{
   return scrAddr_;
}

const std::vector<BinaryData>& AddressBookEntry::getTxHashList() const
{
   return txHashList_;
}

void AddressBookEntry::addTxHash(const BinaryData& hash)
{
   txHashList_.emplace_back(hash);
}

////////
BinaryData AddressBookEntry::serialize(void) const
{
   BinaryWriter bw;
   bw.reserve(8 + scrAddr_.getSize() + txHashList_.size() * 32);
   bw.put_var_int(scrAddr_.getSize());
   bw.put_BinaryData(scrAddr_);
   bw.put_var_int(txHashList_.size());

   for (const auto& hash : txHashList_) {
      bw.put_BinaryData(hash);
   }
   return bw.getData();
}

void AddressBookEntry::unserialize(const BinaryData& data)
{
   if (data.getSize() < 2) {
      throw std::runtime_error("invalid serialized AddressBookEntry");
   }
   BinaryRefReader brr(data.getRef());
   
   auto addrSize = brr.get_var_int();

   if (brr.getSizeRemaining() < addrSize + 1) {
      throw std::runtime_error("invalid serialized AddressBookEntry");
   }
   scrAddr_ = std::move(brr.get_BinaryData(addrSize));

   auto hashListCount = brr.get_var_int();
   if (brr.getSizeRemaining() != hashListCount * 32) {
      throw std::runtime_error("invalid serialized AddressBookEntry");
   }
   for (unsigned i = 0; i < hashListCount; i++) {
      auto hash = brr.get_BinaryData(32);
      txHashList_.emplace_back(std::move(hash));
   }
}

////////
bool AddressBookEntry::Comparator::operator()
(const AddressBookEntry& lhs, const AddressBookEntry& rhs) const
{
   return lhs.getScrAddr() < rhs.getScrAddr();
}

bool AddressBookEntry::Comparator::operator()
(const AddressBookEntry& lhs, const BinaryData& rhs) const
{
   return lhs.getScrAddr() < rhs;
}

bool AddressBookEntry::Comparator::operator()
(const BinaryData& lhs, const AddressBookEntry& rhs) const
{
   return lhs < rhs.getScrAddr();
}

////////////////////////////////////////////////////////////////////////////////
// UnspentTxOut
UnspentTxOut::UnspentTxOut() :
   txHash_(BtcUtils::EmptyHash),
   txOutIndex_(0),
   txHeight_(0),
   value_(0),
   isMultisigRef_(false)
{}

UnspentTxOut::UnspentTxOut(const BinaryData& hash, uint32_t outIndex,
   uint32_t height, uint64_t val, const BinaryData& script) :
   txHash_(hash), txOutIndex_(outIndex), txHeight_(height),
   value_(val), script_(script)
{}

////////
BinaryData UnspentTxOut::getTxHash() const
{
   return txHash_;
}

uint32_t UnspentTxOut::getTxtIndex() const
{
   return txIndex_;
}

uint32_t UnspentTxOut::getTxOutIndex() const
{
   return txOutIndex_;
}

uint64_t UnspentTxOut::getValue() const
{
   return value_;
}

uint64_t UnspentTxOut::getTxHeight() const
{
   return txHeight_;
}

uint32_t UnspentTxOut::isMultisigRef() const
{
   return isMultisigRef_;
}

BinaryData UnspentTxOut::getRecipientScrAddr() const
{
   return BtcUtils::getTxOutScrAddr(getScript());
}

uint32_t UnspentTxOut::getNumConfirm(uint32_t currBlkNum) const
{
   if (txHeight_ == UINT32_MAX) {
      throw std::runtime_error("uninitiliazed UnspentTxOut");
   }
   return currBlkNum - txHeight_ + 1;
}

Outpoint UnspentTxOut::getOutPoint() const
{
   return Outpoint(txHash_, txOutIndex_);
}

const BinaryData& UnspentTxOut::getScript() const
{
   return script_;
}

////////
bool UnspentTxOut::CompareNaive(const UnspentTxOut& uto1,
   const UnspentTxOut& uto2)
{
   float val1 = (float)uto1.getValue();
   float val2 = (float)uto2.getValue();
   return (val1 * uto1.txHeight_ < val2 * uto2.txHeight_);
}

bool UnspentTxOut::CompareTech1(const UnspentTxOut& uto1,
   const UnspentTxOut& uto2)
{
   float val1 = pow((float)uto1.getValue(), 1.0f/3.0f);
   float val2 = pow((float)uto2.getValue(), 1.0f/3.0f);
   return (val1 * uto1.txHeight_ < val2 * uto2.txHeight_);
}

bool UnspentTxOut::CompareTech2(const UnspentTxOut& uto1,
   const UnspentTxOut& uto2)
{
   float val1 = pow(log10((float)uto1.getValue()) + 5, 5);
   float val2 = pow(log10((float)uto2.getValue()) + 5, 5);
   return (val1 * uto1.txHeight_ < val2 * uto2.txHeight_);

}

bool UnspentTxOut::CompareTech3(const UnspentTxOut& uto1,
   const UnspentTxOut& uto2)
{
   float val1 = pow(log10((float)uto1.getValue()) + 5, 4);
   float val2 = pow(log10((float)uto2.getValue()) + 5, 4);
   return (val1 * uto1.txHeight_ < val2 * uto2.txHeight_);
}

void UnspentTxOut::sortTxOutVect(
   std::vector<UnspentTxOut>& utovect, int sortType)
{
   switch (sortType)
   {
      case 0: sort(utovect.begin(), utovect.end(), CompareNaive); break;
      case 1: sort(utovect.begin(), utovect.end(), CompareTech1); break;
      case 2: sort(utovect.begin(), utovect.end(), CompareTech2); break;
      case 3: sort(utovect.begin(), utovect.end(), CompareTech3); break;
      default: break; // do nothing
   }
}

void UnspentTxOut::pprintOneLine(uint32_t currBlk)
{
   printf(" Tx:%s:%02d   BTC:%0.3f   nConf:%04d\n",
      txHash_.copySwapEndian().getSliceCopy(0,8).toHexStr().c_str(),
      txOutIndex_,
      value_/1e8,
      getNumConfirm(currBlk)
   );
}
