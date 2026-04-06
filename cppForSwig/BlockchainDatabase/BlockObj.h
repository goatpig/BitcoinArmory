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

#include <iostream>
#include <vector>

#include <Utils/BinaryData.h>
#include "TxClasses.h"

namespace Armory
{
   class Blockchain;
}

class LMDBBlockDatabase;

////////
class BlockHeader
{
   friend class Armory::Blockchain;
   friend class testBlockHeader;
   friend class BlockData;

public:
   BlockHeader(void);
   explicit BlockHeader(const uint8_t*, uint32_t);
   explicit BlockHeader(BinaryRefReader&);
   explicit BlockHeader(BinaryDataRef);

   void clearDataCopy(void);
   uint32_t getVersion(void) const;
   const BinaryData& getThisHash(void) const;
   BinaryData getPrevHash(void) const;
   const BinaryData& getNextHash(void) const;
   BinaryData getMerkleRoot(void) const;
   BinaryData getDiffBits(void) const;
   uint32_t getTimestamp(void) const;
   uint32_t getNonce(void) const;
   uint32_t getBlockHeight(void) const;
   void setBlockHeight(unsigned);
   bool isMainBranch(void) const;
   bool isOrphan(void) const;
   double getDifficulty(void) const;
   double getDifficultySum(void) const;

   BinaryDataRef getThisHashRef(void) const;
   BinaryDataRef getPrevHashRef(void) const;
   BinaryDataRef getNextHashRef(void) const;
   BinaryDataRef getMerkleRootRef(void) const;
   BinaryDataRef getDiffBitsRef(void) const;

   uint32_t getNumTx(void) const;
   uint64_t getOffset(void) const;
   uint32_t getBlockFileNum(void) const;

   const uint8_t* getPtr(void) const;
   size_t getSize(void) const;
   bool isInitialized(void) const;
   uint32_t getBlockSize(void) const;
   void setBlockSize(uint32_t);
   void setNumTx(uint32_t);

   void setBlockFile(std::string);
   void setBlockFileNum(uint32_t);
   void setBlockFileOffset(uint64_t);

   const BinaryData& serialize(void) const;
   bool hasFilePos(void) const;

   void unserialize(const uint8_t*, uint32_t);
   void unserialize(const BinaryDataRef&);
   void unserialize(BinaryRefReader&);

   uint8_t getDuplicateID(void) const;
   void setDuplicateID(uint8_t);
   BinaryData getBlockDataKey(void) const;
   unsigned int getThisID(void) const;
   void setUniqueID(unsigned int);

   void pprint(std::ostream& = std::cout, int=0, bool=true) const;
   void pprintAlot(std::ostream& = std::cout);

private:
   BinaryData     dataCopy_;
   bool           isInitialized_ = false;
   bool           isMainBranch_ = false;
   bool           isOrphan_ = true;
   bool           isFinishedCalc_ = false;
   // Specific to the DB storage
   uint8_t        duplicateID_ = 0xFF; // ID of this blk rel to others at same height
   uint32_t       blockHeight_ = UINT32_MAX;

   uint32_t       numTx_ = UINT32_MAX;
   uint32_t       numBlockBytes_; // includes header + nTx + sum(Tx)

   // Derived properties - we expect these to be set after construct/copy
   BinaryData     thisHash_;
   double         difficultyDbl_ = 0.0;

   // Need to compute these later
   BinaryData     nextHash_;
   double         difficultySum_ = 0.0;

   uint32_t       blkFileNum_ = UINT32_MAX;
   uint64_t       blkFileOffset_ = SIZE_MAX;
   unsigned int   uniqueID_ = UINT32_MAX;
};

////////////////////////////////////////////////////////////////////////////////
class DBOutPoint : public Outpoint
{
private:
   LMDBBlockDatabase* db_;

public:
   DBOutPoint(Outpoint, LMDBBlockDatabase*);
   BinaryDataRef getDBkey(void) const;

};

////////////////////////////////////////////////////////////////////////////////
// This class is mainly for sorting by priority
class UnspentTxOut
{
public:
   UnspentTxOut(void);
   UnspentTxOut(const BinaryData&, uint32_t, uint32_t,
      uint64_t, const BinaryData&);

   BinaryData getTxHash(void) const;
   uint32_t getTxtIndex(void) const;
   uint32_t getTxOutIndex(void) const;
   uint64_t getValue(void) const;
   uint64_t getTxHeight(void) const;
   uint32_t isMultisigRef(void) const;

   Outpoint getOutPoint(void) const;
   const BinaryData& getScript(void) const;
   BinaryData getRecipientScrAddr(void) const;

   uint32_t getNumConfirm(uint32_t) const;
   void pprintOneLine(uint32_t=UINT32_MAX);

   // These four methods are listed from steepest-to-shallowest in terms of
   // how much they favor large inputs over small inputs.
   // NOTE:  This isn't useful at all anymore:  it was hardly useful even before
   //        I had UTXO sorting in python.  This was really more experimental
   //        than anything, so I wouldn't bother doing anything with it unless
   //        you want to use it as a template for custom sorting in C++
   static bool CompareNaive(const UnspentTxOut&, const UnspentTxOut&);
   static bool CompareTech1(const UnspentTxOut&, const UnspentTxOut&);
   static bool CompareTech2(const UnspentTxOut&, const UnspentTxOut&);
   static bool CompareTech3(const UnspentTxOut&, const UnspentTxOut&);
   static void sortTxOutVect(std::vector<UnspentTxOut>&, int=1);

public:
   BinaryData txHash_;
   uint32_t   txOutIndex_;
   uint32_t   txHeight_;
   uint32_t   txIndex_;
   uint64_t   value_;
   BinaryData script_;
   bool       isMultisigRef_;

   // This can be set and used as part of a compare function:  if you want
   // each TxOut prioritization to be dependent on the target Tx amount.
   uint64_t   targetTxAmount_;
};
