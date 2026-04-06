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

#include "StoredBlockObj.h"
#include <Utils/BtcUtils.h>
#include <Utils/varint.h>
#include <Utils/DBUtils.h>
#include <Utils/ArmoryErrors.h>
#include <Utils/ArmoryConfig.h>
#include "txio.h"

using namespace Armory;

/////////////////////////////////////////////////////////////////////////////
// SDBI
StoredDBInfo::StoredDBInfo() :
   metaHash{BtcUtils::EmptyHash}
{}

bool StoredDBInfo::isInitialized() const
{
   return !magic.empty();
}

BinaryData StoredDBInfo::getDBKey(uint16_t id)
{
   BinaryWriter bw(3);
   bw.put_uint8_t((uint8_t)DbPrefix::DBINFO);
   bw.put_uint16_t(id, BE);
   return bw.getData();
}

////////
void StoredDBInfo::unserializeDBValue(BinaryRefReader& brr)
{
   if (brr.getSizeRemaining() < 44) {
      magic.resize(0);
      topBlkHgt = UINT32_MAX;
      metaHash.resize(0);
      return;
   }
   brr.get_BinaryData(magic, 4);

   BitUnpacker<uint32_t> bitunpack(brr);
   armoryVer  = bitunpack.getBits(16);
   if (armoryVer != ARMORY_DB_VERSION) {
      std::stringstream ss;
      ss << "DB version mismatch. Use another dbdir or empty the current one!";
      LOGERR << ss.str();
      throw DbErrorMsg(ss.str());
   }

   armoryType = (ARMORY_DB_TYPE)bitunpack.getBits(4);
   topBlkHgt = brr.get_uint32_t();
   appliedToHgt = brr.get_uint32_t();
   brr.get_BinaryData(metaHash, 32);
   brr.get_BinaryData(topScannedBlkHash, 32);
   metaInt = brr.get_uint64_t();
}

void StoredDBInfo::serializeDBValue(BinaryWriter& bw) const
{
   BitPacker<uint32_t> bitpack;
   bitpack.putBits((uint32_t)armoryVer, 16);
   bitpack.putBits((uint32_t)armoryType, 4);

   bw.put_BinaryData(magic);
   bw.put_BitPacker(bitpack);
   bw.put_uint32_t(topBlkHgt); // top blk height
   bw.put_uint32_t(appliedToHgt); // top blk height

   if (metaHash.empty()) {
      bw.put_BinaryData(BtcUtils::EmptyHash);
   } else {
      bw.put_BinaryData(metaHash);
   }

   BinaryDataRef hashRef(topScannedBlkHash);
   if (topScannedBlkHash.empty()) {
      hashRef.setRef(BtcUtils::EmptyHash);
   }
   bw.put_BinaryDataRef(hashRef);
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

////////
void StoredDBInfo::pprintOneLine(uint32_t indent) const
{
   for (uint32_t i=0; i < indent; i++) {
      std::cout << " ";
   }
   std::cout << "DBINFO: " <<
      " TopBlk: " << topBlkHgt <<
      " , " << metaHash.getSliceCopy(0,4).toHexStr().c_str() <<
      std::endl;
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
   BlockHeader bh(brr);
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
   BlockHeader bh(brr); 
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
         stxo.duplicateID  = UINT8_MAX;
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
   if (!bh.isInitialized()) {
      LOGERR << "trying to create from uninitialized block header";
      return;
   }

   setHeaderData(bh.serialize());
   numTx = bh.getNumTx();
   numBytes = bh.getBlockSize();
   blockHeight = bh.getBlockHeight();
   duplicateID = UINT8_MAX;
   isMainBranch = bh.isMainBranch();
   hasBlockHeader = true;

   fileID = bh.getBlockFileNum();
   offset = bh.getOffset();
   uniqueID = bh.getThisID();
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
      return {};
   }

   BlockHeader bh(dataCopy);
   bh.setNumTx(numTx);
   bh.setBlockSize(numBytes);
   bh.setDuplicateID(duplicateID);
   bh.setBlockFileNum(fileID);
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

void DBBlock::unserializeDBValue(DB_SELECT db, const BinaryData& bd,
   bool ignoreMerkle)
{
   BinaryRefReader brr(bd);
   unserializeDBValue(db, brr, ignoreMerkle);
}

void DBBlock::unserializeDBValue(DB_SELECT db, BinaryDataRef bdr,
   bool ignoreMerkle)
{
   BinaryRefReader brr(bdr);
   unserializeDBValue(db, brr, ignoreMerkle);
}

////////
void DBBlock::unserializeDBValue(DB_SELECT db, BinaryRefReader& brr,
   bool ignoreMerkle)
{
   if (db == DB_SELECT::HEADERS) {
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
   } else if(db == DB_SELECT::BLKDATA) {
      if (brr.getSize() < HEADER_SIZE + 12) {
         std::stringstream err;
         err << "buffer is too small: " << dataCopy.getSize();
         err << " bytes. expected: " << HEADER_SIZE + 12;

         LOGERR << err.str();
         throw BtcUtils::BlockDeserializingException(err.str());
      }

      // Read the flags byte
      BitUnpacker<uint32_t> bitunpack(brr);
      unserArmVer       =                  bitunpack.getBits(16);
      unserBlkVer       =                  bitunpack.getBits(4);
      unserDbType       = (ARMORY_DB_TYPE) bitunpack.getBits(4);
      unserMkType       = (MERKLE_SER_TYPE)bitunpack.getBits(2);
      blockAppliedToDB  =                  bitunpack.getBit();

      // Unserialize the raw header into the SBH object
      brr.get_BinaryData(dataCopy, HEADER_SIZE);
      BtcUtils::getHash256(dataCopy, thisHash);
      numTx    = brr.get_uint32_t();
      numBytes = brr.get_uint32_t();

      if (unserArmVer != ARMORY_DB_VERSION) {
         LOGWARN << "Version mismatch in unserialize DB header";
      }
      if (!ignoreMerkle ) {
         uint32_t currPos = brr.getPosition();
         uint32_t totalSz = brr.getSize();
         if (unserMkType == MERKLE_SER_NONE) {
            merkle.resize(0);
         } else {
            merkleIsPartial = unserMkType == MERKLE_SER_PARTIAL;
            brr.get_BinaryData(merkle, totalSz - currPos);
         }
      }
   }
}

void DBBlock::serializeDBValue(BinaryWriter& bw, DB_SELECT db,
   ARMORY_DB_TYPE dbType
) const
{
   if (!isInitialized()) {
      LOGERR << "Attempted to serialize uninitialized block header";
      return;
   }

   if (db == DB_SELECT::HEADERS) {
      BinaryData hgtx = DBUtils::heightAndDupToHgtx(blockHeight, duplicateID);
      bw.put_BinaryData(dataCopy);
      bw.put_BinaryData(hgtx);
      bw.put_uint32_t(numBytes);
      bw.put_uint32_t(numTx);
      bw.put_uint16_t(fileID);
      bw.put_uint64_t(offset);
      bw.put_uint32_t(uniqueID);
   } else if (db == DB_SELECT::BLKDATA) {
      uint32_t version = READ_UINT32_LE(dataCopy.getPtr());

      // TODO:  We define merkle serialization types here, but we're not actually
      //        enforcing it in this function.  Either merkle_ member contains 
      //        the correct form of the merkle data or it doesn't.  We should 
      //        figure out whether we need to make sure the correct data is 
      //        already here when this function starts, or guarantee the data
      //        is in the right form as part of this function.  For now I'm 
      //        assuming that it's already in the right form, and thus the
      //        determination of PARTIAL vs FULL is irrelevant
      MERKLE_SER_TYPE mtype;
      switch (dbType)
      {
         // If we store all the tx anyway, don't need any/partial merkle trees
         case ARMORY_DB_TYPE::Bare:    mtype = MERKLE_SER_NONE; break;
         case ARMORY_DB_TYPE::Full:    mtype = MERKLE_SER_NONE; break;
         case ARMORY_DB_TYPE::Super:   mtype = MERKLE_SER_NONE; break;
         default:
            LOGERR << "Invalid DB mode in serializeStoredHeaderValue";
      }

      // Override the above mtype if the merkle data is zero-length
      if (merkle.empty()) {
         mtype = MERKLE_SER_NONE;
      }

      // Create the flags byte
      BitPacker<uint32_t> bitpack;
      bitpack.putBits((uint32_t)ARMORY_DB_VERSION, 16);
      bitpack.putBits((uint32_t)version,           4);
      bitpack.putBits((uint32_t)dbType,            4);
      bitpack.putBits((uint32_t)mtype,             2);
      bitpack.putBit(blockAppliedToDB);

      bw.put_BitPacker(bitpack);
      bw.put_BinaryData(dataCopy);
      bw.put_uint32_t(numTx);
      bw.put_uint32_t(numBytes);

      if (mtype != MERKLE_SER_NONE ) {
         bw.put_BinaryData(merkle);
         if (merkle.empty()) {
            LOGERR << "Expected to serialize merkle tree, but empty string";
         }
      }
   }
}

void DBBlock::unserializeDBKey(DB_SELECT db, BinaryDataRef key)
{
   if (db == DB_SELECT::BLKDATA) {
      BinaryRefReader brr(key);
      if (key.getSize() == 4) {
         DBUtils::readBlkDataKeyNoPrefix(brr, blockHeight, duplicateID);
      } else if (key.getSize() == 5) {
         DBUtils::readBlkDataKey(brr, blockHeight, duplicateID);
      } else {
         LOGERR << "Invalid key for StoredHeader";
      }
   } else {
      LOGERR << "This method not intended for HEADERS DB";
   }
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

bool DBBlock::isMerkleCreated()
{
   return !merkle.empty();
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
      case ARMORY_DB_TYPE::Bare:    serType = TX_SER_FRAGGED; break;
      case ARMORY_DB_TYPE::Full:    serType = TX_SER_FRAGGED; break;
      case ARMORY_DB_TYPE::Super:   serType = TX_SER_FRAGGED; break;
      default:
         LOGERR << "Invalid DB mode in serializeStoredTxValue";
   }

   if (serType == TX_SER_FULL && !haveAllTxOut()) {
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

   if (serType == TX_SER_FULL) {
      bw.put_BinaryData(getSerializedTx());
   } else if(serType == TX_SER_FRAGGED) {
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
   if (!haveAllTxOut()) {
      throw std::runtime_error(
         "Cannot get tx copy, because don't have full StoredTx!");
   }

   Tx returnTx{getSerializedTx()};
   returnTx.setRBF(rbfFlag);
   returnTx.setTxHeight(blockHeight);
   returnTx.setTxIndex(txIndex);
   returnTx.setDupId(duplicateID);
   return returnTx;
}

void StoredTx::setKeyData(uint32_t height, uint8_t dup, uint16_t txIdx)
{
   blockHeight = height;
   duplicateID = dup;
   txIndex     = txIdx;

   for (auto& stxoPair : stxoMap) {
      stxoPair.second.blockHeight = height;
      stxoPair.second.duplicateID = dup;
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

   if (unserTxType == TX_SER_FULL || unserTxType == TX_SER_FRAGGED) {
      unserialize(brr, unserTxType == TX_SER_FRAGGED);
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

   BinaryData output(dataCopy.getSize() - span);
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
   : txVersion(UINT32_MAX), dataCopy(0), blockHeight(UINT32_MAX),
   duplicateID(UINT8_MAX), txIndex(UINT16_MAX), txOutIndex(UINT16_MAX),
   parentHash(0), spentness(TXOUT_SPENTUNK), isCoinbase(false),
   spentByTxInKey(0)
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
   unserArmVer =                  bitunpack.getBits(4);
   txVersion   =                  bitunpack.getBits(2);
   spentness   = (TXOUT_SPENTNESS)bitunpack.getBits(2);
   isCoinbase  =                  bitunpack.getBit();

   unserialize(brr);
   if (spentness == TXOUT_SPENT && brr.getSizeRemaining() >= 8) {
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
   TXOUT_SPENTNESS spentness, BinaryDataRef spentByTxin)
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

   if (spentness == TXOUT_SPENT) {
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
      duplicateID == UINT8_MAX  ||
      txIndex     == UINT16_MAX ||
      txOutIndex  == UINT16_MAX) {
      return {};
   }

   if (withPrefix) {
      return DBUtils::getBlkDataKey(
         blockHeight, duplicateID, txIndex, txOutIndex);
   } else {
      return DBUtils::getBlkDataKeyNoPrefix(
         blockHeight, duplicateID, txIndex, txOutIndex);
   }
}

////////
BinaryData StoredTxOut::getSpentnessKey() const
{
   if (Armory::Config::DBSettings::getDbType() != ARMORY_DB_TYPE::Super) {
      return getDBKey(false);
   }

   if (blockHeight == UINT32_MAX ||
      duplicateID == UINT8_MAX ||
      txIndex == UINT16_MAX ||
      txOutIndex == UINT16_MAX) {
      return {};
   }

   return DBUtils::getDBSuperSpentnessKey(
      blockHeight, duplicateID, txIndex, txOutIndex);
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
   if (key.getSize() == 8) {
      DBUtils::readBlkDataKeyNoPrefix(brr,
         blockHeight, duplicateID, txIndex, txOutIndex);
   } else if (key.getSize() == 9) {
      DBUtils::readBlkDataKey(brr, blockHeight, duplicateID, txIndex, txOutIndex);
   } else {
      LOGERR << "Invalid key for StoredTxOut";
   }
}

bool StoredTxOut::isSpent() const
{
   return spentness == TXOUT_SPENT;
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
      << "," << (uint32_t)duplicateID
      << "," << txIndex
      << "," << txOutIndex << ")"
      << " Value=" << (double)(getValue())/(100000000.0)
      << " isCB: " << (isCoinbase ? "(X)" : "   ");

   if (spentness == TXOUT_SPENTUNK) {
      std::cout << " Spnt: " << "<-----UNKNOWN---->" << std::endl;
   } else if (spentness == TXOUT_UNSPENT) {
      std::cout << " Spnt: " << "<                >" << std::endl;
   } else {
      std::cout << " Spnt: " << "<" <<
         spentByTxInKey.toHexStr() << ">" << std::endl;
   }
}

////////////////////////////////////////////////////////////////////////////////
// The list of spent/unspent txOuts is exactly what is needed to construct
// a full vector<TxIOPair> for each address.  Keep in mind that this list
// only contains TxOuts and spentness of those TxOuts that are:
//    (1) Already in the blockchain
//    (2) On the longest chain at the time is was written
// It contains no zero-confirmation tx data, and it may not be accurate
// if there was a reorg since it was written.  Part of the challenge of
// implementing this DB stuff correctly is making sure both conditions 
// above are adhered to, despite TxIOPair objects being used in RAM to store
// zero-confirmation data as well as in-blockchain data.
StoredScriptHistory::StoredScriptHistory() :
   uniqueKey(0),
   version(UINT32_MAX),
   totalTxioCount(0),
   totalUnspent(0)
{}

bool StoredScriptHistory::isInitialized() const
{
   return !uniqueKey.empty();
}

////////
void StoredScriptHistory::unserializeDBValue(BinaryRefReader& brr)
{
   // Now read the stored data fro this registered address
   BitUnpacker<uint16_t> bitunpack(brr);
   auto dbType = (ARMORY_DB_TYPE)bitunpack.getBits(4);
   if (dbType != ARMORY_DB_TYPE::Super) {
      scanHeight = brr.get_int32_t();
      tallyHeight = brr.get_int32_t();
   }

   totalTxioCount = brr.get_var_int();
   subHistMap.clear();
   subsshSummary.clear();

   // We shouldn't end up with empty ssh's, but should catch it just in case
   if (totalTxioCount == 0) {
      return;
   }

   try {
      totalUnspent = brr.get_uint64_t();
      auto sumSize = brr.get_uint32_t();
      for (unsigned i = 0; i < sumSize; i++) {
         unsigned height = brr.get_var_int();
         unsigned sum = brr.get_var_int();
         subsshSummary[height] = sum;
      }
   } catch (const std::runtime_error& e) {
      LOGERR << "StoredScriptHistory deser error";
      throw e;
   }
}

void StoredScriptHistory::serializeDBValue(BinaryWriter& bw,
   ARMORY_DB_TYPE dbType) const
{
   size_t len = 13 + BtcUtils::calcVarIntSize(totalTxioCount);
   if (dbType != ARMORY_DB_TYPE::Super) {
      len += 8;
   }
   for (const auto& sum : subsshSummary) {
      len += BtcUtils::calcVarIntSize(sum.first) +
         BtcUtils::calcVarIntSize(sum.second);
   }

   bw.reserve(len);

   // Write out all the flags
   BitPacker<uint16_t> bitpack;
   bitpack.putBits((uint16_t)dbType, 4);
   bitpack.putBits((uint16_t)SCRIPT_UTXO_VECTOR, 2);
   bw.put_BitPacker(bitpack);

   //
   if (dbType != ARMORY_DB_TYPE::Super) {
      bw.put_int32_t(scanHeight);
      bw.put_int32_t(tallyHeight);
   }

   bw.put_var_int(totalTxioCount);
   bw.put_uint64_t(totalUnspent);

   //
   bw.put_uint32_t(subsshSummary.size());
   for (const auto& sum : subsshSummary) {
      bw.put_var_int(sum.first);
      bw.put_var_int(sum.second);
   }
}

void StoredScriptHistory::unserializeDBValue(const BinaryData& bd)
{
   BinaryRefReader brr(bd);
   unserializeDBValue(brr);
}

void StoredScriptHistory::unserializeDBValue(BinaryDataRef bdr)
{
   BinaryRefReader brr(bdr);
   unserializeDBValue(brr);
}

////////
void StoredScriptHistory::decompressManySubssh(BinaryDataRef data,
   unsigned height_base, unsigned spent_offset,
   unsigned lower_bound, unsigned upper_bound,
   std::function<bool(unsigned, uint8_t)>& isDupIdValid)
{
   BinaryRefReader brr(data);

   //get subbssh count
   auto count = brr.get_var_int();
   for (unsigned i = 0; i < count; i++) {
      StoredSubHistory subssh;

      //get height offset
      auto height_offset = brr.get_var_int();
      auto this_height = height_offset + height_base;
      if (this_height > upper_bound) {
         return;
      }
      subssh.height = this_height;

      //dupid
      auto dupId = brr.get_uint8_t();

      //get txio count
      auto txio_count = brr.get_var_int();

      //grab all txios
      for (unsigned y = 0; y < txio_count; y++) {
         //value
         uint64_t value = brr.get_var_int();

         //spent flag
         auto flag = brr.get_uint8_t();

         switch (flag)
         {
            case 0:
            {
               //unspent
               auto txid = brr.get_var_int();
               auto outputid = brr.get_var_int();
               if (this_height < lower_bound) {
                  continue;
               }

               auto outputKey = DBUtils::getBlkDataKeyNoPrefix(
                  this_height, dupId, txid, outputid);
               auto emplaceIter = subssh.txioMap.emplace(
                  outputKey, TxIOPair{outputKey, value}).first;

               if (txid == 0) {
                  emplaceIter->second.setFromCoinbase(true);
               }
               break;
            }

            case 1:
            {
               //funded and spent in same block
               auto txid_output = brr.get_var_int();
               auto iid_output = brr.get_var_int();

               auto txid_input = brr.get_var_int();
               auto iid_input = brr.get_var_int();
               if (this_height < lower_bound) {
                  continue;
               }

               auto outputKey = DBUtils::getBlkDataKeyNoPrefix(
                  this_height, dupId, txid_output, iid_output);
               auto inputKey = DBUtils::getBlkDataKeyNoPrefix(
                  this_height, dupId, txid_input, iid_input);
               auto emplaceIter = subssh.txioMap.emplace(
                  outputKey, TxIOPair{outputKey, value}).first;

               emplaceIter->second.setTxIn(inputKey);
               break;
            }

            case 0xFF:
            {
               //spent

               //get output height offset and dupid
               auto output_height = brr.get_var_int() + spent_offset;
               auto output_dupid = brr.get_uint8_t();

               auto txid_output = brr.get_var_int();
               auto iid_output = brr.get_var_int();

               auto txid_input = brr.get_var_int();
               auto iid_input = brr.get_var_int();
               if (this_height < lower_bound) {
                  continue;
               }

               auto outputKey = DBUtils::getBlkDataKeyNoPrefix(
                  output_height, output_dupid, txid_output, iid_output);
               auto inputKey = DBUtils::getBlkDataKeyNoPrefix(
                  this_height, dupId, txid_input, iid_input);
               auto emplaceIter = subssh.txioMap.emplace(
                  outputKey, TxIOPair{outputKey, value}).first;

               emplaceIter->second.setTxIn(inputKey);
               break;
            }

            default:
               LOGERR << "unexpected spent flag in compressed subssh";
               throw std::runtime_error(
                  "unexpected spent flag in compressed subssh");
         }
      }

      if (!isDupIdValid(this_height, dupId)) {
         continue;
      }

      //add to subssh map
      subHistMap.emplace(
         DBUtils::getBlkDataKeyNoPrefix(this_height, dupId),
         std::move(subssh)
      );
   }
}

////////
BinaryData StoredScriptHistory::getDBKey(bool withPrefix) const
{
   BinaryWriter bw(1 + uniqueKey.getSize());
   if (withPrefix) {
      bw.put_uint8_t((uint8_t)DbPrefix::SCRIPT);
   }
   bw.put_BinaryData(uniqueKey);
   return bw.getData();
}

Armory::ScriptPrefix StoredScriptHistory::getScriptType() const
{
   if (uniqueKey.empty()) {
      return Armory::ScriptPrefix::NONSTD;
   } else {
      return (Armory::ScriptPrefix)uniqueKey[0];
   }
}

void StoredScriptHistory::unserializeDBKey(BinaryDataRef key, bool withPrefix)
{
   // Assume prefix
   if (withPrefix) {
      uniqueKey = key.getSliceCopy(1, key.getSize() - 1);
   } else {
      uniqueKey = key;
   }
}

bool StoredScriptHistory::haveFullHistoryLoaded() const
{
   //Shouldn't be using this call outside of C++ unit tests. It is supported to
   //accomodate for unit tests degree of data review, but it is painfully slow
   //and should be avoided in all speed critical operations. The method already
   //assumes we function in an environment with full history in ram, which
   //doesn't with the new backend anymore.

   if (!isInitialized()) {
      return false;
   }
   uint64_t numTxio = 0;
   for (const auto& stxoPair : subHistMap) {
      for (const auto& txioPair : stxoPair.second.txioMap) {
         if (txioPair.second.isUTXO()) {
            numTxio++;
         } else if (txioPair.second.hasTxIn()) {
            numTxio += 2;
         }
      }
   }

   if (numTxio > totalTxioCount) {
      LOGERR << "Somehow stored total is less than counted total...?";
   }
   return numTxio == totalTxioCount;
}

////////
uint64_t StoredScriptHistory::getScriptReceived(bool withMultisig) const
{
   if (!haveFullHistoryLoaded()) {
      return UINT64_MAX;
   }
   uint64_t bal = 0;
   for (const auto& sshPair : subHistMap) {
      bal += sshPair.second.getSubHistoryReceived(withMultisig);
   }
   return bal;
}

uint64_t StoredScriptHistory::getScriptBalance(bool withMultisig) const
{
   // If regular balance,
   if (!withMultisig) {
      return totalUnspent;
   }

   // If with multisig we have to load and count everything
   if (!haveFullHistoryLoaded()) {
      return UINT64_MAX;
   }

   uint64_t bal = 0;
   for (const auto& sshPair : subHistMap) {
      bal += sshPair.second.getSubHistoryBalance(withMultisig);
   }
   return bal;
}

////////
bool StoredScriptHistory::getFullTxioMap(
   std::map<BinaryData, TxIOPair>& mapToFill,
   bool withMultisig) const
{
   if (!haveFullHistoryLoaded()) {
      return false;
   }
   for (const auto sshPair : subHistMap) {
      const StoredSubHistory& subssh = sshPair.second;
      if (withMultisig) {
         // If with multisig, we can just copy everything
         mapToFill.insert(subssh.txioMap.begin(), subssh.txioMap.end());
      } else {
         // Otherwise, we have to filter out the multisig TxIOs
         for (const auto& txioPair : subssh.txioMap) {
            if (!txioPair.second.isMultisig()) {
               mapToFill.emplace(txioPair.first, txioPair.second);
            }
         }
      }
   }
   return true;
}

void StoredScriptHistory::mergeSubHistory(const StoredSubHistory& subssh)
{
   auto& subSshEntry = subHistMap[subssh.hgtX];
   if (!subSshEntry.isInitialized()) {
      subSshEntry = subssh;
      return;
   }
   for (const auto& txioPair : subssh.txioMap) {
      subSshEntry.txioMap.emplace(txioPair);
   }
}

////////
void StoredScriptHistory::insertTxio(const TxIOPair& txio)
{
   auto txioKey = txio.getDBKeyOfOutput();
   auto hgtX = txioKey.getSliceRef(0, 4);
   auto& subSshEntry = subHistMap[hgtX];
   if (!subSshEntry.isInitialized()) {
      subSshEntry.hgtX      = hgtX;
   }

   auto wasInserted = subSshEntry.txioMap.emplace(std::move(txioKey), txio);
   if (wasInserted.second == true) {
      if (!txio.hasTxIn() && !txio.isMultisig()) {
         totalUnspent += txio.getValue();
      }
      totalTxioCount++;
   }
}

void StoredScriptHistory::eraseTxio(const TxIOPair& txio)
{
   auto iter = subHistMap.find(txio.getDBKeyOfOutput().getSliceRef(0, 4));
   if (iter == subHistMap.end()) {
      return;
   }

   auto wasRemoved = iter->second.txioMap.erase(txio.getDBKeyOfOutput());
   if (wasRemoved == 1) {
      if (!txio.hasTxIn() && !txio.isMultisig()) {
         totalUnspent -= txio.getValue();
      }
      totalTxioCount--;
   }
}

void StoredScriptHistory::clear()
{
   uniqueKey.clear();
   version = UINT32_MAX;
   scanHeight = tallyHeight = -1;
   totalTxioCount = totalUnspent = 0;

   subsshSummary.clear();
   subHistMap.clear();
}

/////////
void StoredScriptHistory::addSummary(const StoredScriptHistory& ssh)
{
   totalTxioCount += ssh.totalTxioCount;
   totalUnspent += ssh.totalUnspent;
   subsshSummary.insert(
      ssh.subsshSummary.begin(),
      ssh.subsshSummary.end());
}

void StoredScriptHistory::substractSummary(const StoredScriptHistory& ssh)
{
   totalTxioCount -= ssh.totalTxioCount;
   totalUnspent -= ssh.totalUnspent;

   for (auto& summary_pair : ssh.subsshSummary) {
      auto summIter = subsshSummary.find(summary_pair.first);
      if (summIter == subsshSummary.end()) {
         LOGWARN << "missing entry in substractSummary";
         continue;
      }
      if (summary_pair.second >= summIter->second) {
         subsshSummary.erase(summary_pair.first);
         continue;
      }
      summIter->second -= summary_pair.second;
   }
}

////////////////////////////////////////////////////////////////////////////////
// SubSSH object code
//
// If the ssh has more than one TxIO, then we put them into SubSSH objects,
// which represent a list of TxIOs for the given block.  The complexity of
// doing it this way seems unnecessary, but it actually works quite efficiently
// for massively-reused addresses like SatoshiDice.
////////////////////////////////////////////////////////////////////////////////
StoredSubHistory::StoredSubHistory() :
   hgtX(0), height(0), dupID(0), txioCount(0)
{}

StoredSubHistory::StoredSubHistory(const StoredSubHistory& copy)
{
   *this = copy;
}

bool StoredSubHistory::isInitialized() const
{
   return !hgtX.empty();
}

StoredSubHistory& StoredSubHistory::operator=(const StoredSubHistory& copy)
{
   if (&copy == this) {
      return *this;
   }

   hgtX = copy.hgtX;
   txioMap = copy.txioMap;
   height = copy.height;
   dupID = copy.dupID;
   txioCount = copy.txioCount;
   return *this;
}

////////
void StoredSubHistory::unserializeDBValue(BinaryRefReader& brr)
{
   if (hgtX.getSize() != 4) {
      LOGERR << "Cannot unserialize DB value until key is set (hgt&dup)";
      hgtX.clear();
      return;
   }

   BinaryData fullTxKey(8);
   hgtX.copyTo(fullTxKey.getPtr());

   txioCount = (uint32_t)(brr.get_var_int());
   for (uint32_t i = 0; i < txioCount; i++) {
      BitUnpacker<uint8_t> bitunpack(brr);
      bool isFromSelf   = bitunpack.getBit();
      bool isCoinbase   = bitunpack.getBit();
      bool isSpent      = bitunpack.getBit();
      bool isMulti      = bitunpack.getBit();
      bool isUTXO       = bitunpack.getBit();

      // We always include the 8-byte value
      uint64_t txoValue = brr.get_uint64_t();
      std::map<BinaryData, TxIOPair>::iterator txioIter;

      if (!isSpent) {
         // First 4 bytes is same for all TxIOs, and was copied outside the loop.
         // So we grab the last four bytes and copy it to the end.
         brr.get_BinaryData(fullTxKey.getPtr() + 4, 4);
         txioIter = txioMap.emplace(
            fullTxKey, TxIOPair{fullTxKey, txoValue}).first;
      } else {
         //spent subssh, TxOut will always carry a full DBkey
         auto txOutKey = brr.get_BinaryData(8);
         txioIter = txioMap.emplace(
            txOutKey, TxIOPair{txOutKey, txoValue}).first;

         //4 bytes entry
         brr.get_BinaryData(fullTxKey.getPtr() + 4, 4);
         txioIter->second.setTxIn(fullTxKey);
      }

      txioIter->second.setUTXO(isUTXO);
      txioIter->second.setTxOutFromSelf(isFromSelf);
      txioIter->second.setFromCoinbase(isCoinbase);
      txioIter->second.setMultisig(isMulti);
   }
}

////////
void StoredSubHistory::serializeDBValue(BinaryWriter& bw) const
{
   size_t len = BtcUtils::calcVarIntSize(txioMap.size());
   for (const auto& txioPair : txioMap) {
      const auto& txio = txioPair.second;
      bool isSpent = txio.hasTxIn();
      len += 13; //bitpack + value + at least 4 bytes of txio key
      if (isSpent) {
         len += 8;
      }
   }

   bw.reserve(len);
   bw.put_var_int(txioMap.size());
   for (const auto& txioPair : txioMap) {
      TxIOPair const & txio = txioPair.second;
      bool isSpent = txio.hasTxIn();

      // If spent and only maintaining a pruned DB, skip it
      if (isSpent) {
         if (!txio.getTxRefOfInput().isInitialized()) {
            LOGERR << "TxIO is spent, but input is not initialized";
            continue;
         }
      }

      auto key8B = txio.getDBKeyOfOutput();
      BitPacker<uint8_t> bitpack;
      bitpack.putBit(txio.isTxOutFromSelf());
      bitpack.putBit(txio.isFromCoinbase());
      bitpack.putBit(txio.hasTxIn());
      bitpack.putBit(txio.isMultisig());
      bitpack.putBit(txio.isUTXO());
      bw.put_BitPacker(bitpack);

      if (!isSpent) {
         // Always write the value and last 4 bytes of dbkey (first 4 is in dbkey)
         bw.put_uint64_t(txio.getValue());
         bw.put_BinaryDataRef(key8B.getSliceRef(4, 4));
      } else {
         //spent subssh entry that marks the spent TxOut at the TxIn hgtX
         //write the full TxOut dbkey, since this is saved at TxIn hgtX
         bw.put_uint64_t(txio.getValue());
         bw.put_BinaryData(key8B);

         //Spent subssh are saved by TxIn hgtX, only write the last 4 bytes
         key8B = std::move(txio.getDBKeyOfInput());
         bw.put_BinaryDataRef(key8B.getSliceRef(4, 4));
      }
   }
}

void StoredSubHistory::unserializeDBValue(BinaryDataRef bdr)
{
   BinaryRefReader brr(bdr);
   unserializeDBValue(brr);
}

void StoredSubHistory::unserializeDBKey(BinaryDataRef key, bool withPrefix)
{
   uint32_t sz = key.getSize();
   BinaryRefReader brr(key);

   // Assume prefix
   if (withPrefix) {
      DBUtils::checkPrefixByte(brr, DbPrefix::SCRIPT);
      sz -= 1;
   }

   brr.advance(sz - 4);
   brr.get_BinaryData(hgtX, 4);

   uint8_t* hgtXptr = (uint8_t*)hgtX.getPtr();
   height = 0;
   uint8_t* hgt = (uint8_t*)&height;

   dupID = hgtXptr[3];
   hgt[0] = hgtXptr[2];
   hgt[1] = hgtXptr[1];
   hgt[2] = hgtXptr[0];
}

////////
uint64_t StoredSubHistory::getSubHistoryReceived(bool withMultisig) const
{
   uint64_t bal = 0;
   for (const auto& txioPair : txioMap) {
      if (txioPair.second.isUTXO() &&
         (!txioPair.second.isMultisig() || withMultisig)) {
         bal += txioPair.second.getValue();
      } else if (txioPair.second.hasTxIn()) {
         bal += txioPair.second.getValue();
      }
   }
   return bal;
}

uint64_t StoredSubHistory::getSubHistoryBalance(bool withMultisig) const
{
   uint64_t bal = 0;
   for (const auto& txioPair : txioMap) {
      if (!txioPair.second.hasTxIn()) {
         if (!txioPair.second.isMultisig() || withMultisig) {
            bal += txioPair.second.getValue();
         }
      }
   }
   return bal;
}

////////////////////////////////////////////////////////////////////////////////
void StoredSubHistory::compressMany(
   const std::map<BinaryDataRef, StoredSubHistory*>& ssh,
   unsigned start_offset, unsigned spent_offset,
   BinaryWriter& bw)
{
   //compute serialized size to prealloc bw
   size_t len = BtcUtils::calcVarIntSize(ssh.size());
   for (auto& subssh : ssh) {
      //height and dup
      len += BtcUtils::calcVarIntSize(
         subssh.second->height - start_offset) + 1;

      //txio count
      len += BtcUtils::calcVarIntSize(subssh.second->txioMap.size());
      for (const auto& txio_pair : subssh.second->txioMap) {
         const auto& txio = txio_pair.second;

         //value
         len += BtcUtils::calcVarIntSize(txio.getValue());
         if (!txio.hasTxIn()) {
            /* unspent */

            //flag
            ++len;

            //tx and output id
            len += BtcUtils::calcVarIntSize(
               txio.getTxRefOfOutput().getTxIndex());
            len += BtcUtils::calcVarIntSize(
               txio.getIndexOfOutput());
         } else {
            /* spent */

            //TxIOPair is slow, convert hgtx manually
            auto& outputRef = txio.getTxRefOfOutput();
            auto& txkeyRef = outputRef.getDBKey();

            auto keyptr = txkeyRef.getPtr();
            unsigned output_height = 0;
            auto heightPtr = (uint8_t*)&output_height;
            heightPtr[0] = keyptr[2];
            heightPtr[1] = keyptr[1];
            heightPtr[2] = keyptr[0];

            if (output_height != subssh.second->height) {
               //flag
               ++len;

               //output
               auto height = output_height - spent_offset;
               len += BtcUtils::calcVarIntSize(height) + 1;
               len += BtcUtils::calcVarIntSize(
                  txio.getTxRefOfOutput().getTxIndex());
               len += BtcUtils::calcVarIntSize(
                  txio.getIndexOfOutput());

               //input
               len += BtcUtils::calcVarIntSize(txio.getTxRefOfInput().getTxIndex());
               len += BtcUtils::calcVarIntSize(txio.getIndexOfInput());
            } else {
               /* fund and spend happen in same block, only record ids */

               //flag
               ++len;

               //output
               len += BtcUtils::calcVarIntSize(
                  txio.getTxRefOfOutput().getTxIndex());
               len += BtcUtils::calcVarIntSize(
                  txio.getIndexOfOutput());

               //input
               len += BtcUtils::calcVarIntSize(
                  txio.getTxRefOfInput().getTxIndex());
               len += BtcUtils::calcVarIntSize(
                  txio.getIndexOfInput());
            }
         }
      }
   }

   bw.reserve(len);

   //serialize
   bw.put_var_int(ssh.size());

   for (auto& subssh : ssh) {
      //put height offset
      bw.put_var_int(subssh.second->height - start_offset);

      //extract dupid from subssh and serialize its
      auto dupId = DBUtils::hgtxToDupID(subssh.first);
      bw.put_uint8_t(dupId);

      //put txio count
      bw.put_var_int(subssh.second->txioMap.size());

      //put txios
      for (const auto& txio_pair : subssh.second->txioMap) {
         const auto& txio = txio_pair.second;
         bw.put_var_int(txio.getValue());

         if (!txio.hasTxIn()) {
            //unspent

            //flag
            bw.put_uint8_t(0);

            //tx and output id
            bw.put_var_int(txio.getTxRefOfOutput().getTxIndex());
            bw.put_var_int(txio.getIndexOfOutput());
         } else {
            //spent

            //TxIOPair is slow, convert hgtx manually
            const auto& outputRef = txio.getTxRefOfOutput();
            const auto& txkeyRef = outputRef.getDBKey();

            auto keyptr = txkeyRef.getPtr();
            unsigned output_height = 0;
            auto heightPtr = (uint8_t*)&output_height;
            heightPtr[0] = keyptr[2];
            heightPtr[1] = keyptr[1];
            heightPtr[2] = keyptr[0];
            auto output_dupid = keyptr[3];

            if (output_height != subssh.second->height) {
               //flag
               bw.put_uint8_t(0xFF);

               //output
               auto height = output_height - spent_offset;
               bw.put_var_int(height);
               bw.put_uint8_t(output_dupid);
               bw.put_var_int(txio.getTxRefOfOutput().getTxIndex());
               bw.put_var_int(txio.getIndexOfOutput());

               //input
               bw.put_var_int(txio.getTxRefOfInput().getTxIndex());
               bw.put_var_int(txio.getIndexOfInput());
            } else {
               //fund and spend happen in same block, only record ids

               //flag
               bw.put_uint8_t(1);

               //output
               bw.put_var_int(txio.getTxRefOfOutput().getTxIndex());
               bw.put_var_int(txio.getIndexOfOutput());

               //input
               bw.put_var_int(txio.getTxRefOfInput().getTxIndex());
               bw.put_var_int(txio.getIndexOfInput());
            }
         }
      }
   }
}

////////////////////////////////////////////////////////////////////////////////
// StoredTxHints
StoredTxHints::StoredTxHints() :
   txHashPrefix(0), dbKeyList(0), preferredDBKey(0)
{}

bool StoredTxHints::isInitialized() const
{
   return !txHashPrefix.empty();
}

size_t StoredTxHints::getNumHints() const
{
   return dbKeyList.size();
}

BinaryDataRef StoredTxHints::getHint(uint32_t i) const
{
   return dbKeyList[i].getRef();
}

void StoredTxHints::unserializeDBValue(BinaryRefReader& brr)
{
   uint64_t numHints = (brr.getSizeRemaining()==0 ? 0 : brr.get_var_int());
   dbKeyList.resize((uint32_t)numHints);
   for (uint32_t i=0; i<numHints; i++) {
      brr.get_BinaryData(dbKeyList[i], 6);
   }
   // Preferred simply means it's supposed to be first in the list
   // This simply improves search time in the event there's multiple hints
   if (numHints > 0) {
      preferredDBKey = dbKeyList[0];
   }
}

void StoredTxHints::serializeDBValue(BinaryWriter& bw) const
{
   bw.put_var_int(dbKeyList.size());
   // Find and write the preferred key first, skip all unpreferred (the first
   // one in the list is the preferred key... that paradigm could be improved
   // for sure...)
   for (const auto& dbKey : dbKeyList) {
      if (dbKey != preferredDBKey) {
         continue;
      }
      bw.put_BinaryData(dbKey);
      break;
   }

   // Now write all the remaining keys in whatever order they are naturally
   // sorted (skip the preferred key since it was already written)
   for (const auto& dbKey : dbKeyList) {
      if (dbKey == preferredDBKey) {
         continue;
      }
      bw.put_BinaryData(dbKey);
   }
}

////////
void StoredTxHints::unserializeDBValue(const BinaryData& bd)
{
   BinaryRefReader brr(bd);
   unserializeDBValue(brr);
}

void StoredTxHints::unserializeDBValue(BinaryDataRef bdr)
{
   BinaryRefReader brr(bdr);
   unserializeDBValue(brr);
}

BinaryData StoredTxHints::serializeDBValue() const
{
   BinaryWriter bw;
   serializeDBValue(bw);
   return bw.getData();
}

////////
BinaryData StoredTxHints::getDBKey(bool withPrefix) const
{
   if (!withPrefix) {
      return txHashPrefix;
   } else {
      BinaryWriter bw(5);
      bw.put_uint8_t((uint8_t)DbPrefix::TXHINTS);
      bw.put_BinaryData(txHashPrefix);
      return bw.getData();
   }
}

void StoredTxHints::unserializeDBKey(BinaryDataRef key, bool withPrefix)
{
   if (withPrefix) {
      txHashPrefix = key.getSliceCopy(1, 4);
   } else {
      txHashPrefix = key;
   }
}

void StoredTxHints::setPreferredTx(
   uint32_t height, uint8_t dupID, uint16_t txIndex)
{
   preferredDBKey = DBUtils::getBlkDataKeyNoPrefix(height, dupID, txIndex);
}

void StoredTxHints::setPreferredTx(BinaryData dbKey6B)
{
   preferredDBKey = dbKey6B;
}

////////////////////////////////////////////////////////////////////////////////
// StoredHeadHgtList
StoredHeadHgtList::StoredHeadHgtList() :
   height(UINT32_MAX), preferredDup(UINT8_MAX)
{}

bool StoredHeadHgtList::isInitialized() const
{
   return height != UINT32_MAX;
}

void StoredHeadHgtList::setPreferredDupID(uint8_t newDup)
{
   preferredDup = newDup;
}

////////
void StoredHeadHgtList::unserializeDBValue(BinaryRefReader& brr)
{
   uint32_t numHeads = brr.get_uint8_t();
   dupAndHashList.resize(numHeads);
   preferredDup = UINT8_MAX;
   for (uint32_t i = 0; i < numHeads; i++) {
      uint8_t dup = brr.get_uint8_t();
      dupAndHashList[i].first = dup & 0x7f;
      brr.get_BinaryData(dupAndHashList[i].second, 32);
      if ((dup & 0x80) > 0) {
         preferredDup = dup & 0x7f;
      }
   }
}

void StoredHeadHgtList::serializeDBValue(BinaryWriter& bw) const
{
   bw.put_uint8_t(dupAndHashList.size());

   // Write the preferred/valid block header first
   for (const auto& dahPair : dupAndHashList) {
      if (dahPair.first != preferredDup) {
         continue;
      }
      bw.put_uint8_t(dahPair.first | 0x80);
      bw.put_BinaryData(dahPair.second);
      break;
   }

   // Now write everything else
   for (const auto& dahPair : dupAndHashList) {
      if (dahPair.first == preferredDup) {
         continue;
      }
      bw.put_uint8_t(dahPair.first & 0x7f);
      bw.put_BinaryData(dahPair.second);
   }
}

void StoredHeadHgtList::unserializeDBValue(const BinaryData& bd)
{
   BinaryRefReader brr(bd);
   unserializeDBValue(brr);
}

void StoredHeadHgtList::unserializeDBValue(BinaryDataRef bdr)
{
   BinaryRefReader brr(bdr);
   unserializeDBValue(brr);
}

BinaryData StoredHeadHgtList::serializeDBValue() const
{
   BinaryWriter bw;
   serializeDBValue(bw);
   return bw.getData();
}

////////
BinaryData StoredHeadHgtList::getDBKey(bool withPrefix) const
{
   BinaryWriter bw(5);
   if (withPrefix) {
      bw.put_uint8_t((uint8_t)DbPrefix::HEADHGT);
   }
   bw.put_uint32_t(height, BE);
   return bw.getData();

}

void StoredHeadHgtList::unserializeDBKey(BinaryDataRef key)
{
   BinaryRefReader brr(key);
   if (key.getSize() == 5) {
      uint8_t prefix = brr.get_uint8_t();
      if (prefix != (uint8_t)DbPrefix::HEADHGT) {
         LOGERR << "Unserialized HEADHGT key but wrong prefix";
         return;
      }
   }
   height = brr.get_uint32_t(BE);
}

////////
void StoredHeadHgtList::addDupAndHash(uint8_t dup, BinaryDataRef hash)
{
   for (auto& dah : dupAndHashList) {
      if (dah.first == dup) {
         if (dah.second != hash) {
            LOGERR << "Pushing different hash into existing HHL dupID"; 
         }
         dah = std::make_pair(dup, hash);
         return;
      }
   }
   dupAndHashList.emplace_back(std::make_pair(dup, hash));
}
