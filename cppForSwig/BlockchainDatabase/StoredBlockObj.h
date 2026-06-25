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

#pragma once

#include <vector>
#include <map>
#include <functional>

#include <Utils/BinaryData.h>
#include "BlockObj.h"
#include "bdmenums.h"

#define ARMORY_DB_VERSION   0x9702
#define ARMORY_DB_DEFAULT   ARMORY_DB_FULL
#define UTXO_STORAGE        SCRIPT_UTXO_VECTOR

enum class DB_SELECT : int
{
   HEADERS,
   SCRADDR,
   TXOUTS,
   TXINS,
   TXHINTS,
   TXFILTERS,
   KNOWNHASHES,
   ZERO_CONF,
};

enum class TX_SERIALIZE_TYPE : int
{
   FULL = 1,
   FRAGGED,
   COUNTOUT
};

enum class SPENTNESS : int
{
   UNSPENT = 1,
   SPENT,
   SPENTUNK,
};

class Tx;
class TxIn;
class TxOut;
class TxIOPair;

namespace Armory
{
   enum class ScriptPrefix : uint8_t;
   class BlockHeader;
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

public:
   Armory::Hash32 metaHash; //32 bytes
   Armory::Hash32 topScannedBlkHash; //32 bytes
   uint64_t metaInt = UINT64_MAX;
   uint32_t armoryVer = ARMORY_DB_VERSION;
   BinaryData magicBytes;
   ARMORY_DB_TYPE armoryType = ARMORY_DB_TYPE::Bare; //default db mode
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
      SPENTNESS, BinaryDataRef);

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
   uint16_t          txIndex;
   uint16_t          txOutIndex;
   BinaryData        parentHash;
   SPENTNESS         spentness;
   bool              isCoinbase;
   BinaryData        spentByTxInKey;

   mutable BinaryData hgtX;
   mutable BinaryData scrAddr;

   uint32_t          unserArmVer;
   uint32_t          unserDbType;
   unsigned          parentTxOutCount = 0;
   BinaryData        spenderHash;
};

struct TxOutData
{
   const Armory::Types::Amount amount;
   const Armory::Types::BlockId blockID;
   const Armory::Types::TxId txId;
   const Armory::Types::TxIOId txOutId;
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
   Armory::BlockHeader getBlockHeaderCopy(void) const;
   BinaryData getSerializedBlockHeader(void) const;
   void createFromBlockHeader(const Armory::BlockHeader&);
   uint32_t getNumTx(void);

   void setHeightAndDup(uint32_t, uint8_t);
   void setHeightAndDup(const BinaryData&);
   void setHeaderData(const BinaryData&);

   void unserializeDBValue(BinaryRefReader&);
   void serializeDBValue(BinaryWriter&) const;

   void unserializeDBValue(const BinaryData&);
   void unserializeDBValue(BinaryDataRef);
   BinaryData getDBKey(bool = true) const;
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
   bool           isMainBranch = false;
   bool           blockAppliedToDB = false;
   bool           isPartial = false;
   bool           hasBlockHeader = false;
   bool           merkleValid = true;

   // We don't actually enforce these members.  They're solely for recording
   // the values that were unserialized with everything else, so that we can
   // later check that DB data matches what we were expecting
   uint32_t        unserArmVer;
   uint32_t        unserBlkVer;
   ARMORY_DB_TYPE  unserDbType;

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
