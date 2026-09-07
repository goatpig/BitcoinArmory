////////////////////////////////////////////////////////////////////////////////
//                                                                            //
//  Copyright (C) 2011-2015, Armory Technologies, Inc.                        //
//  Distributed under the GNU Affero General Public License (AGPL v3)         //
//  See LICENSE-ATI or http://www.gnu.org/licenses/agpl.html                  //
//                                                                            //
//                                                                            //
//  Copyright (C) 2016-2026, goatpig                                          //
//  Distributed under the MIT license                                         //
//  See LICENSE-MIT or https://opensource.org/licenses/MIT                    //
//                                                                            //
////////////////////////////////////////////////////////////////////////////////

#include <cstring>

#include "StoredBlockObj.h"
#include <Utils/BtcUtils.h>
#include <Utils/varint.h>
#include <Utils/DBUtils.h>
#include <Utils/ArmoryErrors.h>
#include <Utils/ArmoryConfig.h>
#include <TxClasses.h>
#include "txio.h"

using namespace Armory;

/////////////////////////////////////////////////////////////////////////////
// SDBI
StoredDBInfo::StoredDBInfo() :
   metaHash{BtcUtils::EmptyHash}
{}

bool StoredDBInfo::isInitialized() const
{
   return !magicBytes.empty();
}

BinaryData StoredDBInfo::getDBKey(uint16_t id)
{
   BinaryWriter bw(4);
   bw.put_uint32_t(0xFFFF0000 | id, BE);
   return bw.getData();
}

////////
void StoredDBInfo::unserializeDBValue(BinaryRefReader& brr)
{
   if (brr.getSizeRemaining() != 82) {
      magicBytes.resize(0);
      return;
   }
   armoryVer = brr.get_uint32_t();
   if (armoryVer != (uint32_t)ARMORY_DB_VERSION) {
      std::stringstream ss;
      ss << "DB version mismatch. Use another dbdir or empty the current one!";
      LOGERR << ss.str();
      throw DbErrorMsg(ss.str());
   }
   armoryType = (ARMORY_DB_TYPE)brr.get_uint16_t();
   brr.get_BinaryData(magicBytes, 4);

   auto topHashRef = brr.get_BinaryDataRef(32);
   std::memcpy(topScannedBlkHash.data, topHashRef.getPtr(), 32);
   auto metaRef = brr.get_BinaryDataRef(32);
   std::memcpy(metaHash.data, metaRef.getPtr(), 32);
   metaInt = brr.get_uint64_t();
}

void StoredDBInfo::serializeDBValue(BinaryWriter& bw) const
{
   bw.put_uint32_t((uint32_t)armoryVer);
   bw.put_uint16_t((uint16_t)armoryType);
   bw.put_BinaryData(magicBytes);
   bw.put_BinaryDataRef(topScannedBlkHash.getRef());
   bw.put_BinaryDataRef(metaHash.getRef());
   bw.put_uint64_t(metaInt);
}

void StoredDBInfo::unserializeDBValue(const BinaryData& bd)
{
   BinaryRefReader brr(bd);
   unserializeDBValue(brr);
}

void StoredDBInfo::unserializeDBValue(BinaryDataRef bdr)
{
   BinaryRefReader brr(bdr);
   unserializeDBValue(brr);
}

/////////////////////////////////////////////////////////////////////////////
// StoredHeader
void StoredHeader::setKeyData(uint32_t hgt, uint8_t dupID)
{
   // Set the params for this SBH object
   blockHeight = hgt;
   duplicateID = dupID;

   // Then trickle down to each StoredTx object (if any)
   for (auto& stxPair : stxMap) {
      stxPair.second.setKeyData(hgt, dupID, stxPair.first);
   }
}

////////
bool StoredHeader::haveFullBlock() const
{
   if (dataCopy.getSize() != HEADER_SIZE) {
      return false;
   }

   unsigned count = 0;
   for (const auto& txPair : stxMap) {
      if (txPair.first != count++) {
         return false;
      } else if (!txPair.second.haveAllTxOut()) {
         return false;
      }
   }
   return count == numTx;
}

BinaryData StoredHeader::getSerializedBlock() const
{
   if (!haveFullBlock()) {
      return {};
   }

   BinaryWriter bw;
   if (numBytes > 0) {
      //add extra room for header and var_int
      bw.reserve(numBytes + 100);
   }

   bw.put_BinaryData(dataCopy);
   bw.put_var_int(numTx);
   for (const auto& txPair : stxMap) {
      bw.put_BinaryData(txPair.second.getSerializedTx());
   }
   return bw.getData();
}

////////
Tx StoredHeader::getTxCopy(uint16_t i)
{
   auto iter = stxMap.find(i);
   if (iter == stxMap.end()) {
      throw std::runtime_error("not tx for index: " + std::to_string(i));
   }
   return iter->second.getTxCopy();
}

BinaryData StoredHeader::getSerializedTx(uint16_t i)
{
   auto iter = stxMap.find(i);
   if (iter == stxMap.end()) {
      return {};
   }
   return iter->second.getSerializedTx();
}

////////
void StoredHeader::unserializeFullBlock(BinaryDataRef block, bool doFrag,
   bool withPrefix)
{
   BinaryRefReader brr(block);
   unserializeFullBlock(brr, doFrag, withPrefix);
}

bool StoredHeader::serializeFullBlock(BinaryWriter& bw) const
{
   if (!haveFullBlock()) {
      LOGERR << "Attempted to serialize full block, but only have partial";
      return false;
   }

   if (numTx == UINT32_MAX) {
      LOGERR << "Number of tx not available while serializing full block";
      return false;
   }

   BinaryWriter bwTemp(1024*1024); // preallocate 1 MB which is the limit
   bwTemp.put_BinaryData(dataCopy);
   bwTemp.put_var_int(numTx);
   std::map<uint16_t, StoredTx>::const_iterator iter;
   for (const auto& stxPair : stxMap) {
      if (!stxPair.second.haveAllTxOut()) {
         LOGERR << "Don't have all TxOut in tx during serialize full block";
         return false;
      }
      bwTemp.put_BinaryData(stxPair.second.getSerializedTx());
   }
   bw.put_BinaryData(bwTemp.getDataRef());
   return true;
}

////////
void StoredHeader::addTxToMap(uint16_t txIdx, const Tx& tx)
{
   StoredTx storedTx;
   storedTx.createFromTx(tx);
   addStoredTxToMap(txIdx, storedTx);
}

void StoredHeader::addStoredTxToMap(uint16_t txIdx, const StoredTx& stx)
{
   if (txIdx >= numTx) {
      LOGERR << "TxIdx is greater than numTx of stored header";
      return;
   }
   stxMap[txIdx] = stx;
}

////////
void StoredHeader::unserializeSimple(BinaryRefReader brr)
{
   uint32_t height = blockHeight;
   uint8_t  dupid = duplicateID;

   std::vector<BinaryData> allTxHashes;
   BlockHeader bh(brr.get_BinaryDataRef(HEADER_SIZE));
   uint32_t nTx = (uint32_t)brr.get_var_int();

   createFromBlockHeader(bh);
   numTx = nTx;
   blockHeight = height;
   duplicateID = dupid;

   numBytes = HEADER_SIZE + BtcUtils::calcVarIntSize(numTx);
   if (dataCopy.getSize() != HEADER_SIZE) {
      LOGERR << "Unserializing header did not produce 80-byte object!";
      return;
   }

   if (numBytes > brr.getSize()) {
      LOGERR << "Anticipated size of block header is more than what we have";
      throw BtcUtils::BlockDeserializingException();
   }

   BtcUtils::getHash256(dataCopy, thisHash);
   for (uint32_t tx = 0; tx < nTx; tx++) {
      // gather tx hashes
      Tx thisTx(brr);
      StoredTx stx;
      stx.thisHash = thisTx.getThisHash();
      stxMap[tx] = std::move(stx);
   }
}

void StoredHeader::unserializeFullBlock(BinaryRefReader brr, bool doFrag,
   bool withPrefix)
{
   if (withPrefix) {
      BinaryData magic  = brr.get_BinaryData(4);
      uint32_t   nBytes = brr.get_uint32_t();

      if (brr.getSizeRemaining() < nBytes) {
         LOGERR << "Not enough bytes remaining in BRR to read block";
         return;
      }
   }

   uint32_t height = blockHeight;
   uint8_t  dupid  = duplicateID;

   std::vector<BinaryData> allTxHashes;
   auto rawHeader = brr.get_BinaryData(HEADER_SIZE);
   BlockHeader bh(rawHeader.getRef());
   bh.setRawData(std::move(rawHeader));
   uint32_t nTx = (uint32_t)brr.get_var_int();

   createFromBlockHeader(bh);
   numTx = nTx;
   blockHeight = height;
   duplicateID = dupid;

   numBytes = HEADER_SIZE + BtcUtils::calcVarIntSize(numTx);
   if (dataCopy.getSize() != HEADER_SIZE) {
      LOGERR << "Unserializing header did not produce 80-byte object!";
      return;
   }

   if (numBytes > brr.getSize()) {
      LOGERR << "Anticipated size of block header is more than what we have";
      throw BtcUtils::BlockDeserializingException();
   }

   BtcUtils::getHash256(dataCopy, thisHash);
   for (uint32_t tx = 0; tx < nTx; tx++) {
      // We're going to have to come back to the beginning of the tx, later
      uint32_t txStart = brr.getPosition();

      // Read a regular tx and then convert it
      Tx thisTx(brr);
      numBytes += thisTx.getSize();

      //save the hash for merkle computation
      allTxHashes.push_back(thisTx.getThisHash());

      // Now add stx to the map
      StoredTx stx;

      //copy the appropriate data from the vanilla Tx object
      stx.createFromTx(thisTx, doFrag, true);
      stx.isFragged = doFrag;
      stx.version = thisTx.getVersion();
      stx.txIndex = tx;

      bool isCoinbase = thisTx.getTxInCopy(0).isCoinbase();

      // Regardless of whether the tx is fragged, we still need the STXO map
      // to be updated and consistent
      auto endOfTx = brr.getPosition();
      brr.resetPosition();
      brr.advance(txStart + thisTx.getTxOutOffset(0));
      for (uint32_t txo = 0; txo < thisTx.getNumTxOut(); txo++) {
         StoredTxOut stxo;
         stxo.unserialize(brr);
         stxo.txVersion    = thisTx.getVersion();
         stxo.blockHeight  = UINT32_MAX;
         stxo.txIndex      = tx;
         stxo.txOutIndex   = txo;
         stxo.isCoinbase   = isCoinbase;
         stxo.parentHash   = stx.thisHash;
         stx.stxoMap.emplace(txo, std::move(stxo));
      }
      stxMap.emplace(tx, std::move(stx));

      // Let's skip to the end of the Tx, there may be witness data to parse
      // which this code skips. We need the brr sitting at the next tx.
      brr.resetPosition();
      brr.advance(endOfTx);
   }

   if (nTx == 0 || nTx != allTxHashes.size()) {
      LOGERR << "Mismatch between numtx and allTxHashes.size() or 0 tx in block";
      throw BtcUtils::BlockDeserializingException();
   }

   //compute the merkle root and compare to the header's
   BinaryData computedMerkleRoot = BtcUtils::calculateMerkleRoot(allTxHashes);
   if (computedMerkleRoot != bh.getMerkleRoot()) {
      LOGERR << "Merkle root mismatch! Raw block data is corrupt!";
      throw BtcUtils::BlockDeserializingException();
   }
}

////////
void StoredHeader::pprintFullBlock(uint32_t indent) const
{
   pprintOneLine(indent);
   if (numTx > 10000) {
      std::cout << "      <No tx to print>" << std::endl;
      return;
   }

   for (const auto& stxPair : stxMap) {
      stxPair.second.pprintFullTx(indent+3);
   }
}

/////////////////////////////////////////////////////////////////////////////
// DBBlock
DBBlock::~DBBlock()
{}

bool DBBlock::isInitialized() const
{
   return !dataCopy.empty();
}

uint32_t DBBlock::getNumTx()
{
   return isInitialized() ? numTx : 0;
}

void DBBlock::setHeightAndDup(uint32_t hgt, uint8_t dupID)
{
   blockHeight = hgt;
   duplicateID = dupID;
}

void DBBlock::setHeightAndDup(const BinaryData& hgtx)
{
   blockHeight = DBUtils::hgtxToHeight(hgtx);
   duplicateID = DBUtils::hgtxToDupID(hgtx);
}

BinaryData DBBlock::getDBKey(bool withPrefix) const
{
   if (blockHeight == UINT32_MAX || duplicateID == UINT8_MAX) {
      throw std::range_error("Requesting DB key for incomplete SBH");
   }

   if (withPrefix) {
      return DBUtils::getBlkDataKey(blockHeight, duplicateID);
   } else {
      return DBUtils::getBlkDataKeyNoPrefix(blockHeight, duplicateID);
   }
}

void DBBlock::createFromBlockHeader(const BlockHeader& bh)
{
   const auto& rawHeader = bh.getRawData();
   if (rawHeader.empty()) {
      throw std::runtime_error("header does not carry data");
   }
   setHeaderData(bh.getRawData());
   numTx = bh.getNumTx();
   numBytes = bh.getBlockSize();
   blockHeight = bh.getBlockHeight();
   duplicateID = UINT8_MAX;
   isMainBranch = bh.isMainBranch();
   hasBlockHeader = true;

   fileID = bh.getBlockFileId();
   offset = bh.getOffset();
   uniqueID = bh.getUniqueID();
   merkleValid = bh.isMerkleValid();
}

////////
void DBBlock::setHeaderData(const BinaryData& header80B)
{
   if (header80B.getSize() != HEADER_SIZE) {
      LOGERR << "Asked to unserialize a non-80-byte header";
      return;
   }
   dataCopy.copyFrom(header80B);
   BtcUtils::getHash256(header80B, thisHash);
}

BlockHeader DBBlock::getBlockHeaderCopy() const
{
   if (!isInitialized()) {
      throw std::runtime_error("SBH is no initialized");
   }

   BlockHeader bh(dataCopy);
   bh.setNumTx(numTx);
   bh.setBlockSize(numBytes);
   bh.setBlockFileId(fileID);
   bh.setBlockFileOffset(offset);
   return bh;
}

BinaryData DBBlock::getSerializedBlockHeader() const
{
   if (!isInitialized()) {
      return {};
   }
   return dataCopy;
}

void DBBlock::unserializeDBValue(const BinaryData& bd)
{
   BinaryRefReader brr(bd);
   unserializeDBValue(brr);
}

void DBBlock::unserializeDBValue(BinaryDataRef bdr)
{
   BinaryRefReader brr(bdr);
   unserializeDBValue(brr);
}

////////
void DBBlock::unserializeDBValue(BinaryRefReader& brr)
{
   if (brr.getSize() < HEADER_SIZE + 26) {
      std::stringstream err;
      err << "buffer is too small: " << dataCopy.getSize();
      err << " bytes. expected: " << HEADER_SIZE + 26;

      LOGERR << err.str();
      throw BtcUtils::BlockDeserializingException(err.str());
   }

   brr.get_BinaryData(dataCopy, HEADER_SIZE);
   BinaryData hgtx = brr.get_BinaryData(4);
   blockHeight = DBUtils::hgtxToHeight(hgtx);
   duplicateID = DBUtils::hgtxToDupID(hgtx);
   BtcUtils::getHash256(dataCopy, thisHash);
   numBytes = brr.get_uint32_t();
   numTx = brr.get_uint32_t();
   fileID = brr.get_uint16_t();
   offset = brr.get_uint64_t();
   uniqueID = brr.get_uint32_t();
}

void DBBlock::serializeDBValue(BinaryWriter& bw) const
{
   if (!isInitialized()) {
      LOGERR << "Attempted to serialize uninitialized block header";
      return;
   }

   bw.put_BinaryData(dataCopy);
   bw.put_uint32_t(numBytes);
   bw.put_uint32_t(numTx);
   bw.put_uint64_t(offset);
   bw.put_uint16_t(fileID);
   //set valid merkle by default, update it on failed check
   bw.put_uint8_t(merkleValid);
}

////
void DBBlock::pprintOneLine(uint32_t indent) const
{
   for (uint32_t i = 0; i < indent; i++) {
      std::cout << " ";
   }
   std::cout << "HEADER: " << thisHash.getSliceCopy(0, 4).toHexStr()
      << " (" << blockHeight << "," << (uint32_t)duplicateID << ")"
      << "     #Tx: " << numTx
      << " Applied: " << (blockAppliedToDB ? "T" : "F")
      << std::endl;
}

/////////////////////////////////////////////////////////////////////////////
// StoredTx
void StoredTx::addTxOutToMap(uint16_t idx, const TxOut& txout)
{
   if (idx >= numTxOut) {
      LOGERR << "TxOutIdx is greater than numTxOut of stored tx";
      return;
   }
   StoredTxOut stxo;
   stxo.unserialize(txout.serialize());
   stxoMap[idx] = stxo;
}

void StoredTx::addStoredTxOutToMap(uint16_t idx, const StoredTxOut& stxo)
{
   if (idx >= numTxOut) {
      LOGERR << "TxOutIdx is greater than numTxOut of stored tx";
      return;
   }
   stxoMap[idx] = stxo;
}

StoredTxOut& StoredTx::initAndGetStxoByIndex(uint16_t index)
{
   auto& stxo = stxoMap[index];
   stxo.parentHash = thisHash;
   stxo.txVersion = version;
   return stxo;
}

bool StoredTx::isRBF() const
{
   return rbfFlag;
}

////////
void StoredTx::serializeDBValue(BinaryWriter& bw, ARMORY_DB_TYPE dbType) const
{
   TX_SERIALIZE_TYPE serType;
   switch (dbType)
   {
      // In most cases, if storing separate TxOuts, fragged Tx is fine
      // UPDATE:  I'm not sure there's a good reason to NOT frag ever
      case ARMORY_DB_TYPE::Bare:    serType = TX_SERIALIZE_TYPE::FRAGGED; break;
      case ARMORY_DB_TYPE::Full:    serType = TX_SERIALIZE_TYPE::FRAGGED; break;
      case ARMORY_DB_TYPE::Super:   serType = TX_SERIALIZE_TYPE::FRAGGED; break;
      default:
         LOGERR << "Invalid DB mode in serializeStoredTxValue";
   }

   if (serType == TX_SERIALIZE_TYPE::FULL && !haveAllTxOut()) {
      LOGERR << "Supposed to write out full Tx, but don't have it";
      return;
   }

   if (thisHash.empty()) {
      LOGERR << "Do not know tx hash to be able to DB-serialize StoredTx";
      return;
   }

   uint16_t version = (uint16_t)READ_UINT32_LE(dataCopy.getPtr());
   BitPacker<uint32_t> bitpack;
   bitpack.putBits((uint32_t)ARMORY_DB_VERSION, 16);
   bitpack.putBits((uint32_t)version, 2);
   bitpack.putBits((uint32_t)serType, 4);

   bw.put_BitPacker(bitpack);
   bw.put_BinaryData(thisHash);

   if (serType == TX_SERIALIZE_TYPE::FULL) {
      bw.put_BinaryData(getSerializedTx());
   } else if(serType == TX_SERIALIZE_TYPE::FRAGGED) {
      bw.put_BinaryData(getSerializedTxFragged());
   } else {
      bw.put_var_int(numTxOut);
   }
}

////////
bool StoredTx::haveAllTxOut() const
{
   if (!isInitialized()) {
      return false;
   }

   if (!isFragged) {
      return true;
   }
   return stxoMap.size() == numTxOut;
}

////////
BinaryData StoredTx::getSerializedTx() const
{
   if (!isInitialized()) {
      return {};
   }

   if (!isFragged) {
      return dataCopy;
   } else if (!haveAllTxOut()) {
      return {};
   }

   BinaryWriter bw;
   if (numBytes != UINT32_MAX) {
      bw.reserve(numBytes);
   }
   if (txInCutOff == SIZE_MAX) {
      return {};
   }

   bw.put_BinaryData(dataCopy.getPtr(), txInCutOff);
   uint16_t i=0;
   for (const auto& stxoPair : stxoMap) {
      if (stxoPair.first != i++) {
         LOGERR << "Indices out of order accessing stxoMap...?!";
         return {};
      }
      bw.put_BinaryData(stxoPair.second.getSerializedTxOut());
   }

   bw.put_BinaryData(
      dataCopy.getPtr() + txInCutOff,
      dataCopy.getSize() - txInCutOff);
   return bw.getData();
}

void StoredTx::pprintFullTx(uint32_t indent) const
{
   pprintOneLine(indent);
   if (numTxOut > 10000) {
      std::cout << "         <No txout to print>" << std::endl;
      return;
   }

   for (const auto& stxoPair : stxoMap) {
      stxoPair.second.pprintOneLine(indent+3);
   }
}

Tx StoredTx::getTxCopy() const
{
   throw std::runtime_error("[StoredTx::getTxCopy] deprecated");
   #if 0
   if (!haveAllTxOut()) {
      throw std::runtime_error(
         "Cannot get tx copy, because don't have full StoredTx!");
   }

   Tx returnTx{getSerializedTx()};
   returnTx.setRBF(rbfFlag);
   returnTx.setBlockId(blockId);
   returnTx.setTxIndex(txIndex);
   return returnTx;
   #endif
}

void StoredTx::setKeyData(uint32_t height, uint8_t dup, uint16_t txIdx)
{
   blockHeight = height;
   duplicateID = dup;
   txIndex     = txIdx;

   for (auto& stxoPair : stxoMap) {
      stxoPair.second.blockHeight = height;
      stxoPair.second.txIndex     = txIdx;
      stxoPair.second.txOutIndex  = stxoPair.first;
   }
}

StoredTx& StoredTx::createFromTx(BinaryDataRef rawTx, bool doFrag, bool withTxOuts)
{
   Tx tx(rawTx);
   return createFromTx(tx, doFrag, withTxOuts);
}

StoredTx& StoredTx::createFromTx(const Tx& tx, bool doFrag, bool withTxOuts)
{
   thisHash  = tx.getThisHash();
   numTxOut  = tx.getNumTxOut();
   version   = tx.getVersion();
   lockTime  = tx.getLockTime();
   numBytes  = tx.getSize();
   isFragged = doFrag;

   uint32_t span = tx.getTxOutOffset(numTxOut) - tx.getTxOutOffset(0);
   fragBytes = numBytes - span;
   txInCutOff = tx.getTxOutOffset(0);

   if (!doFrag) {
      dataCopy = tx.serialize();
   } else {
      BinaryRefReader brr(tx.getPtr(), tx.getSize());
      uint32_t firstOut  = tx.getTxOutOffset(0);
      uint32_t afterLast = tx.getTxOutOffset(numTxOut);
      uint32_t _span = afterLast - firstOut;
      dataCopy.resize(tx.getSize() - _span);
      brr.get_BinaryData(dataCopy.getPtr(), firstOut);
      brr.advance(_span);
      brr.get_BinaryData(
         dataCopy.getPtr() + firstOut,
         brr.getSizeRemaining()
      );
   }

   bool isCoinbase = tx.getTxInCopy(0).isCoinbase();
   if (withTxOuts) {
      for(uint32_t txo = 0; txo < tx.getNumTxOut(); txo++) {
         StoredTxOut stxo;
         stxo.unserialize(tx.getTxOutCopy(txo).serialize());
         stxo.txVersion  = tx.getVersion();
         stxo.txOutIndex = txo;
         stxo.isCoinbase = isCoinbase;
         stxo.parentHash = thisHash;
         stxoMap.emplace(txo, std::move(stxo));
      }
   }

   //only significant for ZC
   unixTime = tx.getTxTime();
   return *this;
}

////////////////////////////////////////////////////////////////////////////////
// DBTx
DBTx::~DBTx()
{}

bool DBTx::isInitialized() const
{
   return !dataCopy.empty();
}

BinaryData DBTx::getDBKey(bool withPrefix) const
{
   if (blockHeight == UINT32_MAX ||
      duplicateID == UINT8_MAX  ||
      txIndex == UINT16_MAX) {
      LOGERR << "Requesting DB key for incomplete STX";
      return {};
   }

   if (withPrefix) {
      return DBUtils::getBlkDataKey(
         blockHeight, duplicateID, txIndex);
   } else {
      return DBUtils::getBlkDataKeyNoPrefix(
         blockHeight, duplicateID, txIndex);
   }
}

BinaryData DBTx::getDBKeyOfChild(uint16_t i, bool withPrefix) const
{
   return (getDBKey(withPrefix) + WRITE_UINT16_BE(i));
}

////////
void DBTx::unserialize(const BinaryData& data, bool fragged)
{
   BinaryRefReader brr(data);
   unserialize(brr, fragged);
}

void DBTx::unserialize(BinaryDataRef data, bool fragged)
{
   BinaryRefReader brr(data);
   unserialize(brr, fragged);
}

void DBTx::unserialize(BinaryRefReader& brr, bool fragged)
{
   std::vector<size_t> offsetsIn, offsetsOut;
   uint32_t nbytes = BtcUtils::StoredTxCalcLength(brr.getCurrPtr(),
      brr.getSize(), fragged, &offsetsIn, &offsetsOut, nullptr);

   if (offsetsOut.size() < 1) {
      LOGERR << "Couldn't deserialize db value";
      return;
   }

   if (brr.getSizeRemaining() < nbytes) {
      LOGERR << "Not enough bytes in BRR to unserialize StoredTx";
      return;
   }

   brr.get_BinaryData(dataCopy, nbytes);
   isFragged  = fragged;
   numTxOut   = offsetsOut.size()-1;
   txInCutOff = offsetsOut[0];
   version    = READ_UINT32_LE(dataCopy.getPtr());
   lockTime   = READ_UINT32_LE(dataCopy.getPtr() + nbytes - 4);

   if (isFragged) {
      fragBytes = nbytes;
      numBytes = UINT32_MAX;
   } else {
      numBytes = nbytes;
      uint32_t span = offsetsOut[numTxOut] - offsetsOut[0];
      fragBytes = numBytes - span;
      BtcUtils::getHash256(dataCopy, thisHash);
   }
}

void DBTx::unserializeDBValue(const BinaryData& bd)
{
   BinaryRefReader brr(bd);
   unserializeDBValue(brr);
}

void DBTx::unserializeDBValue(BinaryDataRef bdr)
{
   BinaryRefReader brr(bdr);
   unserializeDBValue(brr);
}

void DBTx::unserializeDBValue(BinaryRefReader& brr)
{
   // flags
   //    DBVersion      4 bits
   //    TxVersion      2 bits
   //    HowTxSer       4 bits   (FullTxOut, TxNoTxOuts, numTxOutOnly)
   BitUnpacker<uint32_t> bitunpack(brr); // flags
   unserArmVer =                    bitunpack.getBits(16);
   unserTxVer  =                    bitunpack.getBits(2);
   unserTxType = (TX_SERIALIZE_TYPE)bitunpack.getBits(4);

   if (unserArmVer != ARMORY_DB_VERSION) {
      LOGWARN << "Version mismatch in unserialize DB tx";
   }
   brr.get_BinaryData(thisHash, 32);

   if (unserTxType == TX_SERIALIZE_TYPE::FULL ||
      unserTxType == TX_SERIALIZE_TYPE::FRAGGED) {
      unserialize(brr, unserTxType == TX_SERIALIZE_TYPE::FRAGGED);
   } else {
      numTxOut = (uint32_t)brr.get_var_int();
   }

   if (brr.getSizeRemaining() == 4) {
      //this is for ZC tx, as regular Tx don't have custom time stamps
      unixTime = brr.get_uint32_t();
   }
}

////////
BinaryData DBTx::getSerializedTxFragged() const
{
   if (!isInitialized()) {
      return {};
   }

   if (isFragged) {
      return dataCopy;
   }

   if (numBytes == UINT32_MAX) {
      LOGERR << "Do not know size of tx in order to serialize it";
      return {};
   }

   BinaryWriter bw;
   std::vector<size_t> outOffsets;
   BtcUtils::StoredTxCalcLength(dataCopy.getPtr(), dataCopy.getSize(),
      false, nullptr, &outOffsets, nullptr);
   uint32_t firstOut  = outOffsets[0];
   uint32_t afterLast = outOffsets[outOffsets.size()-1];
   uint32_t span = afterLast - firstOut;

   BinaryData output;
   output.resize(dataCopy.getSize() - span);
   dataCopy.getSliceRef(0,  firstOut).copyTo(output.getPtr());
   dataCopy.getSliceRef(afterLast, 4).copyTo(output.getPtr()+firstOut);
   return output;
}

void DBTx::unserializeDBKey(BinaryDataRef key)
{
   BinaryRefReader brr(key);
   if (key.getSize() == 6) {
      DBUtils::readBlkDataKeyNoPrefix(
         brr, blockHeight, duplicateID, txIndex);
   } else if (key.getSize() == 7) {
      DBUtils::readBlkDataKey(
         brr, blockHeight, duplicateID, txIndex);
   } else {
      LOGERR << "Invalid key for StoredTx";
   }
}

BinaryData DBTx::getHgtX() const
{
   return getDBKey(false).getSliceCopy(0, 4);
}

const BinaryData& DBTx::getThisHash() const
{
   return thisHash;
}

////////
void DBTx::pprintOneLine(uint32_t indent) const
{
   for (uint32_t i = 0; i < indent; i++) {
      std::cout << " ";
   }
   std::cout << "TX:  " << thisHash.getSliceCopy(0, 4).toHexStr()
      << " (" << blockHeight
      << "," << (uint32_t)duplicateID
      << "," << txIndex << ")"
      << "   #TXO: " << numTxOut
      << std::endl;
}

////////////////////////////////////////////////////////////////////////////////
// StoredTxOut
StoredTxOut::StoredTxOut()
   : txVersion(UINT32_MAX), blockHeight(UINT32_MAX),
   txIndex(UINT16_MAX), txOutIndex(UINT16_MAX),
   spentness(SPENTNESS::SPENTUNK), isCoinbase(false)
{}

bool StoredTxOut::isInitialized() const
{
   return !dataCopy.empty();
}

////////
void StoredTxOut::unserialize(const BinaryData& data)
{
   BinaryRefReader brr(data);
   unserialize(brr);
}

void StoredTxOut::unserialize(BinaryDataRef data)
{
   BinaryRefReader brr(data);
   unserialize(brr);
}

void StoredTxOut::unserialize(BinaryRefReader& brr)
{
   if (brr.getSizeRemaining() < 8) {
      LOGERR << "Not enough bytes in BRR to unserialize StoredTxOut";
      return;
   }

   uint32_t numBytes = BtcUtils::TxOutCalcLength(
      brr.getCurrPtr(), brr.getSizeRemaining());

   if (brr.getSizeRemaining() < numBytes) {
      LOGERR << "Not enough bytes in BRR to unserialize StoredTxOut";
      return;
   }
   brr.get_BinaryData(dataCopy, numBytes);
}

void StoredTxOut::unserializeDBValue(const BinaryData& bd)
{
   BinaryRefReader brr(bd);
   unserializeDBValue(brr);
}

void StoredTxOut::unserializeDBValue(BinaryDataRef bdr)
{
   BinaryRefReader brr(bdr);
   unserializeDBValue(brr);
}

void StoredTxOut::unserializeDBValue(BinaryRefReader& brr)
{
   // Similar to TxValue flags
   //    DBVersion   4 bits
   //    TxVersion   2 bits
   //    Spentness   2 bits
   BitUnpacker<uint16_t> bitunpack(brr);
   unserArmVer = bitunpack.getBits(4);
   txVersion   = bitunpack.getBits(2);
   spentness   = (SPENTNESS)bitunpack.getBits(2);
   isCoinbase  = bitunpack.getBit();

   unserialize(brr);
   if (spentness == SPENTNESS::SPENT && brr.getSizeRemaining() >= 8) {
      spentByTxInKey = brr.get_BinaryData(8);
   }
}

////////
void StoredTxOut::serializeDBValue(BinaryWriter& bw) const
{
   serializeDBValue(bw, txVersion, isCoinbase, dataCopy.getRef(),
      spentness, spentByTxInKey.getRef());
}

void StoredTxOut::serializeDBValue(
   BinaryWriter& bw,
   uint16_t txVersion, bool isCoinbase,
   const BinaryDataRef dataRef,
   SPENTNESS spentness, BinaryDataRef spentByTxin)
{
   size_t len = 2 + dataRef.getSize();
   bw.reserve(len);

   BitPacker<uint16_t> bitpack;
   bitpack.putBits((uint16_t)ARMORY_DB_VERSION, 4);
   bitpack.putBits((uint16_t)txVersion, 2);
   bitpack.putBits((uint16_t)spentness, 2);
   bitpack.putBit(isCoinbase);
   bitpack.putBits(0, 2);
   bw.put_BitPacker(bitpack);
   bw.put_BinaryData(dataRef);  // 8-byte value, var_int sz, pkscript

   if (spentness == SPENTNESS::SPENT) {
      if (spentByTxin.empty()) {
         LOGERR << "Need to write out spentByTxIn but no spentness data";
      }
      bw.put_BinaryDataRef(spentByTxin);
   }
}

////////
BinaryData StoredTxOut::getDBKey(bool withPrefix) const
{
   if (blockHeight == UINT32_MAX ||
      //duplicateID == UINT8_MAX  ||
      txIndex     == UINT16_MAX ||
      txOutIndex  == UINT16_MAX) {
      return {};
   }

   if (withPrefix) {
      return DBUtils::getBlkDataKey(
         blockHeight, 0, txIndex, txOutIndex);
   } else {
      return DBUtils::getBlkDataKeyNoPrefix(
         blockHeight, 0, txIndex, txOutIndex);
   }
}

////////
BinaryData StoredTxOut::getSpentnessKey() const
{
   if (Armory::Config::DBSettings::getDbType() != ARMORY_DB_TYPE::Super) {
      return getDBKey(false);
   }

   if (blockHeight == UINT32_MAX ||
      txIndex == UINT16_MAX ||
      txOutIndex == UINT16_MAX) {
      return {};
   }

   return DBUtils::getDBSuperSpentnessKey(
      blockHeight, 0, txIndex, txOutIndex);
}

BinaryData StoredTxOut::getDBKeyOfParentTx(bool withPrefix) const
{
   BinaryData stxoKey = getDBKey(withPrefix);
   if (withPrefix) {
      return stxoKey.getSliceCopy(0, 7);
   } else {
      return stxoKey.getSliceCopy(0, 6);
   }
}

////////
const BinaryData& StoredTxOut::getHgtX() const
{ 
   if (!hgtX.empty()) {
      return hgtX;
   }
   hgtX = getDBKey(false).getSliceCopy(0, 4);
   return hgtX;
}

unsigned StoredTxOut::getHeight() const
{
   auto& hgtx = getHgtX();
   return DBUtils::hgtxToHeight(hgtx);
}

////////
bool StoredTxOut::matchesDBKey(BinaryDataRef dbkey) const
{
   if (dbkey.getSize() == 8) {
      return (getDBKey(false) == dbkey);
   } else if(dbkey.getSize() == 9) {
      return (getDBKey(true) == dbkey);
   } else {
      LOGERR << "Non STXO-DBKey passed in to check match against STXO";
      return false;
   }
}

StoredTxOut& StoredTxOut::createFromTxOut(const TxOut& txout)
{
   unserialize(txout.serialize());
   return *this;
}

const BinaryData& StoredTxOut::getSerializedTxOut() const
{
   if (!isInitialized()) {
      throw std::runtime_error(
         "Attempted to get serialized TxOut, but not initialized");
   }
   return dataCopy;
}

////////
TxOut StoredTxOut::getTxOutCopy() const
{
   if (!isInitialized()) {
      throw std::runtime_error("Attempted to get TxOut copy but not initialized");
   }
   return {dataCopy};
}

const BinaryData& StoredTxOut::getScrAddress() const
{
   if (!scrAddr.empty()) {
      return scrAddr;
   }

   BinaryRefReader brr(dataCopy);
   brr.advance(8);
   uint32_t scrsz = (uint32_t)brr.get_var_int();
   scrAddr = BtcUtils::getTxOutScrAddr(brr.get_BinaryDataRef(scrsz));
   return scrAddr;
}

BinaryDataRef StoredTxOut::getScriptRef() const
{
   BinaryRefReader brr(dataCopy);
   brr.advance(8);
   uint32_t scrsz = (uint32_t)brr.get_var_int();
   return brr.get_BinaryDataRef(scrsz);
}

uint64_t StoredTxOut::getValue() const
{
   if( !isInitialized()) {
      return UINT64_MAX;
   }
   return *(uint64_t*)dataCopy.getPtr();
}

void StoredTxOut::unserializeDBKey(BinaryDataRef key)
{
   BinaryRefReader brr(key);
   uint8_t dump;
   if (key.getSize() == 8) {
      DBUtils::readBlkDataKeyNoPrefix(brr,
         blockHeight, dump, txIndex, txOutIndex);
   } else if (key.getSize() == 9) {
      DBUtils::readBlkDataKey(brr, blockHeight, dump, txIndex, txOutIndex);
   } else {
      LOGERR << "Invalid key for StoredTxOut";
   }
}

bool StoredTxOut::isSpent() const
{
   return spentness == SPENTNESS::SPENT;
}

////////
void StoredTxOut::pprintOneLine(uint32_t indent) const
{
   for (uint32_t i = 0; i < indent; i++) {
      std::cout << " ";
   }

   std::string pprintHash("");
   if (!parentHash.empty()) {
      pprintHash = parentHash.getSliceCopy(0,4).toHexStr();
   }
   std::cout << "TXOUT:   "
      << "  (" << blockHeight
      << "," << txIndex
      << "," << txOutIndex << ")"
      << " Value=" << (double)(getValue())/(100000000.0)
      << " isCB: " << (isCoinbase ? "(X)" : "   ");

   if (spentness == SPENTNESS::SPENTUNK) {
      std::cout << " Spnt: " << "<-----UNKNOWN---->" << std::endl;
   } else if (spentness == SPENTNESS::UNSPENT) {
      std::cout << " Spnt: " << "<                >" << std::endl;
   } else {
      std::cout << " Spnt: " << "<" <<
         spentByTxInKey.toHexStr() << ">" << std::endl;
   }
}
