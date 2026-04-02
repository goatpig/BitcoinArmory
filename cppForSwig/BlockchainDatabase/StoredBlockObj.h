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
#include <map>
#include <functional>

#include <Utils/BinaryData.h>
#include "BlockObj.h"
#include "bdmenums.h"

#define ARMORY_DB_VERSION   0x9701
#define ARMORY_DB_DEFAULT   ARMORY_DB_FULL
#define UTXO_STORAGE        SCRIPT_UTXO_VECTOR

enum DB_TX_AVAIL
{
   DB_TX_EXISTS,
   DB_TX_GETBLOCK,
   DB_TX_UNKNOWN
};

enum class DB_SELECT : int
{
   HEADERS,
   BLKDATA,
   SSH,
   SUBSSH,
   SUBSSH_META,
   HISTORY,
   STXO,
   TXHINTS,
   ZERO_CONF,
   TXFILTERS,
   SPENTNESS,
   COUNT
};

enum TX_SERIALIZE_TYPE
{
   TX_SER_FULL,
   TX_SER_FRAGGED,
   TX_SER_COUNTOUT
};

enum TXOUT_SPENTNESS
{
   TXOUT_UNSPENT,
   TXOUT_SPENT,
   TXOUT_SPENTUNK,
};

enum MERKLE_SER_TYPE
{
   MERKLE_SER_NONE,
   MERKLE_SER_PARTIAL,
   MERKLE_SER_FULL
};

enum SCRIPT_UTXO_TYPE
{
   SCRIPT_UTXO_VECTOR,
   SCRIPT_UTXO_TREE
};

class BlockHeader;
class Tx;
class TxIn;
class TxOut;
class TxIOPair;

namespace Armory
{
   enum class ScriptPrefix : uint8_t;
}

////////////////////////////////////////////////////////////////////////////////
struct StoredDBInfo
{
   StoredDBInfo(void);

   bool isInitialized(void) const;
   static BinaryData getDBKey(uint16_t = 0);

   void unserializeDBValue(BinaryRefReader&);
   void serializeDBValue(BinaryWriter&) const;
   void unserializeDBValue(const BinaryData&);
   void unserializeDBValue(BinaryDataRef);

   void pprintOneLine(uint32_t = 3) const;

public:
   BinaryData magic;
   uint32_t topBlkHgt = 0;
   BinaryData metaHash; //32 bytes
   BinaryData topScannedBlkHash; //32 bytes
   uint32_t appliedToHgt = 0;
   uint32_t armoryVer = ARMORY_DB_VERSION;
   ARMORY_DB_TYPE armoryType = ARMORY_DB_TYPE::Full; //default db mode
   uint64_t metaInt = UINT64_MAX;
};

////////////////////////////////////////////////////////////////////////////////
struct StoredTxOut
{
   StoredTxOut(void);

   bool isInitialized(void) const;
   bool isSpent(void) const;
   void unserialize(const BinaryData&);
   void unserialize(BinaryDataRef);
   void unserialize(BinaryRefReader&);

   void unserializeDBValue(BinaryRefReader&);
   void serializeDBValue(BinaryWriter&) const;
   void unserializeDBValue(const BinaryData&);
   void unserializeDBValue(BinaryDataRef);
   void unserializeDBKey(BinaryDataRef);

   static void serializeDBValue(
      BinaryWriter&, uint16_t, bool,
      const BinaryDataRef,
      TXOUT_SPENTNESS, BinaryDataRef);

   BinaryData getDBKey(bool = true) const;
   BinaryData getSpentnessKey(void) const;
   BinaryData getDBKeyOfParentTx(bool = true) const;
   const BinaryData& getHgtX(void) const;
   unsigned getHeight(void) const;

   StoredTxOut& createFromTxOut(const TxOut&);
   const BinaryData& getSerializedTxOut(void) const;
   TxOut getTxOutCopy(void) const;

   const BinaryData& getScrAddress(void) const;
   BinaryDataRef     getScriptRef(void) const;
   uint64_t          getValue(void) const;

   bool matchesDBKey(BinaryDataRef) const;
   void pprintOneLine(uint32_t = 3) const;

public:
   uint32_t          txVersion;
   BinaryData        dataCopy;
   uint32_t          blockHeight;
   uint8_t           duplicateID;
   uint16_t          txIndex;
   uint16_t          txOutIndex;
   BinaryData        parentHash;
   TXOUT_SPENTNESS   spentness;
   bool              isCoinbase;
   BinaryData        spentByTxInKey;

   mutable BinaryData hgtX;
   mutable BinaryData scrAddr;

   uint32_t          unserArmVer;
   uint32_t          unserDbType;
   unsigned          parentTxOutCount = 0;
   BinaryData        spenderHash;
};

////////////////////////////////////////////////////////////////////////////////
struct DBTx
{
   virtual ~DBTx(void) = 0;

   bool isInitialized(void) const;

   BinaryData getSerializedTxFragged(void) const;
   void unserialize(const BinaryData&, bool = false);
   void unserialize(BinaryDataRef, bool = false);
   virtual void unserialize(BinaryRefReader&, bool = false);

   void unserializeDBValue(BinaryRefReader&);
   void unserializeDBValue(const BinaryData&);
   void unserializeDBValue(BinaryDataRef);
   void unserializeDBKey(BinaryDataRef);

   BinaryData getDBKey(bool = true) const;
   BinaryData getDBKeyOfChild(uint16_t, bool = true) const;
   BinaryData getHgtX(void) const;
   const BinaryData& getThisHash(void) const;

   void pprintOneLine(uint32_t = 3) const;

   virtual StoredTxOut& initAndGetStxoByIndex(uint16_t) = 0;
   virtual bool haveAllTxOut(void) const = 0;

public:
   BinaryData        thisHash;
   uint32_t          lockTime = 0;
   uint32_t          unixTime = 0;

   BinaryData        dataCopy;
   bool              isFragged = false;
   uint32_t          version = 0;
   uint32_t          blockHeight = UINT32_MAX;
   uint8_t           duplicateID = UINT8_MAX;
   uint16_t          txIndex = UINT16_MAX;
   uint16_t          numTxOut = UINT16_MAX;
   uint32_t          numBytes = UINT32_MAX;
   uint32_t          fragBytes = UINT32_MAX;
   size_t            txInCutOff = SIZE_MAX;

   // We don't actually enforce these members.  They're solely for recording
   // the values that were unserialized with everything else, so that we can
   // leter check that it
   uint32_t          unserArmVer;
   uint32_t          unserTxVer;
   TX_SERIALIZE_TYPE unserTxType;
};

////////////////////////////////////////////////////////////////////////////////
struct StoredTx : public DBTx
{
   StoredTx& createFromTx(const Tx&, bool = true, bool = true);
   StoredTx& createFromTx(BinaryDataRef, bool = true, bool = true);
   void serializeDBValue(BinaryWriter&, ARMORY_DB_TYPE) const;

   BinaryData getSerializedTx(void) const;
   Tx getTxCopy(void) const;
   void setKeyData(uint32_t, uint8_t, uint16_t);

   void addTxOutToMap(uint16_t, const TxOut&);
   void addStoredTxOutToMap(uint16_t, const StoredTxOut&);
   void pprintFullTx(uint32_t = 3) const;
   virtual StoredTxOut& initAndGetStxoByIndex(uint16_t);

   virtual bool haveAllTxOut(void) const;
   bool isRBF(void) const;

public:
   std::map<uint16_t, StoredTxOut> stxoMap;
   bool rbfFlag = false;
};

////////////////////////////////////////////////////////////////////////////////
struct DBBlock
{
   virtual ~DBBlock(void);

   bool isInitialized(void) const;
   BlockHeader getBlockHeaderCopy(void) const;
   BinaryData getSerializedBlockHeader(void) const;
   void createFromBlockHeader(const BlockHeader&);
   uint32_t getNumTx(void);

   void setHeightAndDup(uint32_t, uint8_t);
   void setHeightAndDup(const BinaryData&);
   void setHeaderData(const BinaryData&);

   void unserializeDBValue(DB_SELECT, BinaryRefReader&, bool = false);
   void serializeDBValue(BinaryWriter&, DB_SELECT, ARMORY_DB_TYPE) const;

   void unserializeDBValue(DB_SELECT, const BinaryData&, bool = false);
   void unserializeDBValue(DB_SELECT, BinaryDataRef, bool = false);
   void unserializeDBKey(DB_SELECT, BinaryDataRef);
   BinaryData getDBKey(bool = true) const;

   bool isMerkleCreated(void);
   void pprintOneLine(uint32_t = 3) const;

   virtual void unserializeFullBlock(BinaryRefReader,
      bool = true, bool = false) = 0;

public:
   BinaryData     dataCopy;
   BinaryData     thisHash;
   uint32_t       numTx = UINT32_MAX;
   size_t         numBytes = UINT32_MAX;
   uint32_t       blockHeight = UINT32_MAX;
   uint8_t        duplicateID = UINT8_MAX;
   BinaryData     merkle;
   bool           merkleIsPartial = false;
   bool           isMainBranch = false;
   bool           blockAppliedToDB = false;
   bool           isPartial = false;
   bool           hasBlockHeader = false;

   // We don't actually enforce these members.  They're solely for recording
   // the values that were unserialized with everything else, so that we can
   // later check that DB data matches what we were expecting
   uint32_t        unserArmVer;
   uint32_t        unserBlkVer;
   ARMORY_DB_TYPE  unserDbType;
   MERKLE_SER_TYPE unserMkType;
   
   size_t         offset;
   uint16_t       fileID;
   unsigned int   uniqueID = UINT32_MAX;
};

////////////////////////////////////////////////////////////////////////////////
struct StoredHeader : public DBBlock
{
   BinaryData getSerializedBlock(void) const;

   Tx getTxCopy(uint16_t);
   BinaryData getSerializedTx(uint16_t);
   bool haveFullBlock(void) const;

   void addTxToMap(uint16_t, const Tx&);
   void addStoredTxToMap(uint16_t, const StoredTx&);

   void unserializeFullBlock(BinaryDataRef, bool = true, bool = false);
   virtual void unserializeFullBlock(BinaryRefReader,
      bool = true, bool = false);

   void unserializeSimple(BinaryRefReader);
   bool serializeFullBlock(BinaryWriter&) const;
   void setKeyData(uint32_t, uint8_t = UINT8_MAX);
   void pprintFullBlock(uint32_t = 3) const;

public:
   std::map<uint16_t, StoredTx> stxMap;
};

////////////////////////////////////////////////////////////////////////////////
// We must break out script histories into isolated sub-histories, to
// accommodate thoroughly re-used addresses like 1VayNert* and 1dice*. If
// we didn't do it, those DB entries would be many megabytes, and those many
// MB would be updated multiple times per block.   So we break them into
// subhistories by block.  This is exceptionally well-suited for SatoshiDice
// addresses since transactions in one block tend to be related to
// transactions in the previous few blocks before it.
struct StoredSubHistory
{
   StoredSubHistory(void);
   StoredSubHistory(const StoredSubHistory&);

   bool isInitialized(void) const;
   StoredSubHistory& operator=(const StoredSubHistory&);

   void unserializeDBValue(BinaryRefReader&);
   void serializeDBValue(BinaryWriter&) const;
   void unserializeDBValue(BinaryDataRef);
   void unserializeDBKey(BinaryDataRef, bool = true);

   uint64_t getSubHistoryBalance(bool = false) const;
   uint64_t getSubHistoryReceived(bool = false) const;

   static void compressMany(
      const std::map<BinaryDataRef, StoredSubHistory*>&,
      unsigned, unsigned, BinaryWriter&);

public:
   //track all TxIOs for this ScrAddr at given block
   BinaryData hgtX;
   std::map<BinaryData, TxIOPair> txioMap;
   uint32_t height;
   uint8_t  dupID;
   uint32_t txioCount;
};

////////////////////////////////////////////////////////////////////////////////
// TODO:  I just realized that this should probably hold a "first-born-block"
//        field for each address in the summary entry.  Though, maybe it's 
//        sufficient to just look at the first subSSH entry to get that info...
struct StoredScriptHistory
{
   StoredScriptHistory(void);

   bool isInitialized(void) const;
   void unserializeDBValue(BinaryRefReader&);
   void serializeDBValue(BinaryWriter&, ARMORY_DB_TYPE) const;
   void unserializeDBValue(const BinaryData&);
   void unserializeDBValue(BinaryDataRef);
   void unserializeDBKey(BinaryDataRef, bool = true);
   void decompressManySubssh(BinaryDataRef,
      unsigned, unsigned, unsigned, unsigned,
      std::function<bool(unsigned, uint8_t)>&);

   void addSummary(const StoredScriptHistory&);
   void substractSummary(const StoredScriptHistory&);

   BinaryData getDBKey(bool = true) const;
   Armory::ScriptPrefix getScriptType(void) const;

   uint64_t getScriptReceived(bool = false) const;
   uint64_t getScriptBalance(bool = false) const;

   bool haveFullHistoryLoaded(void) const;
   bool getFullTxioMap(std::map<BinaryData, TxIOPair>&,
      bool = false) const;

   void mergeSubHistory(const StoredSubHistory&);
   void insertTxio(const TxIOPair&);
   void eraseTxio(const TxIOPair&);
   void clear(void);

public:
   BinaryData uniqueKey;  // includes the prefix byte!
   uint32_t version;
   int32_t scanHeight = -1;
   int32_t tallyHeight = -1;
   uint64_t totalTxioCount;
   uint64_t totalUnspent;
   std::map<unsigned, unsigned> subsshSummary;

   // If this ssh has only one TxIO (most of them), then we don't bother
   // with supplemental entries just to hold that one TxIO in the DB.
   // We always stored them in RAM using the StoredSubHistory 
   // objects which will have the per-block lists of TxIOs.  But when
   // it gets serialized to disk, we will store single-Txio SSHs in
   // the base entry and forego extra DB entries.
   std::map<BinaryData, StoredSubHistory> subHistMap;
};

////////////////////////////////////////////////////////////////////////////////
struct StoredTxHints
{
   StoredTxHints(void);

   bool isInitialized(void) const;
   size_t getNumHints(void) const;
   BinaryDataRef getHint(uint32_t) const;

   void setPreferredTx(uint32_t, uint8_t, uint16_t);
   void setPreferredTx(BinaryData);

   void unserializeDBValue(BinaryRefReader&);
   void serializeDBValue(BinaryWriter&) const;
   void unserializeDBValue(const BinaryData&);
   void unserializeDBValue(BinaryDataRef);
   BinaryData serializeDBValue(void) const;
   void unserializeDBKey(BinaryDataRef, bool = true);
   BinaryData getDBKey(bool = true) const;

public:
   BinaryData txHashPrefix;
   std::vector<BinaryData> dbKeyList;
   BinaryData preferredDBKey;
};

////////////////////////////////////////////////////////////////////////////////
struct StoredHeadHgtList
{
   StoredHeadHgtList(void);

   bool isInitialized(void) const;
   void unserializeDBValue(BinaryRefReader&);
   void serializeDBValue(BinaryWriter&) const;
   void unserializeDBValue(const BinaryData&);
   void unserializeDBValue(BinaryDataRef);
   BinaryData serializeDBValue(void) const;
   void unserializeDBKey(BinaryDataRef);
   BinaryData getDBKey(bool = true) const;

   void addDupAndHash(uint8_t, BinaryDataRef);
   void setPreferredDupID(uint8_t);

public:
   uint32_t height;
   std::vector<std::pair<uint8_t, BinaryData>> dupAndHashList;
   uint8_t preferredDup;
};
