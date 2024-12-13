////////////////////////////////////////////////////////////////////////////////
//                                                                            //
//  Copyright (C) 2011-2015, Armory Technologies, Inc.                        //
//  Distributed under the GNU Affero General Public License (AGPL v3)         //
//  See LICENSE-ATI or http://www.gnu.org/licenses/agpl.html                  //
//                                                                            //
//                                                                            //
//  Copyright (C) 2016-2021, goatpig                                          //
//  Distributed under the MIT license                                         //
//  See LICENSE-MIT or https://opensource.org/licenses/MIT                    //
//                                                                            //
////////////////////////////////////////////////////////////////////////////////


#ifndef _BLOCKOBJ_H_
#define _BLOCKOBJ_H_


#include <iostream>
#include <vector>
#include <map>
#include <set>
#include <cassert>
#include <functional>

#include "BinaryData.h"
#include "BtcUtils.h"
#include "TxClasses.h"

////////////////////////////////////////////////////////////////////////////////
class LMDBBlockDatabase; 
class Tx;
class TxIn;
class TxOut;

class BlockHeader
{
   friend class Blockchain;
   friend class testBlockHeader;
   friend class BlockData;

public:

   /////////////////////////////////////////////////////////////////////////////
   BlockHeader(void) :
      isInitialized_(false),
      isMainBranch_(false),
      isOrphan_(false),
      isFinishedCalc_(false),
      duplicateID_(UINT8_MAX),
      numTx_(UINT32_MAX),
      numBlockBytes_(UINT32_MAX)
   { }

   explicit BlockHeader(uint8_t const * ptr, uint32_t size) { unserialize(ptr, size); }
   explicit BlockHeader(BinaryRefReader & brr)    { unserialize(brr); }
   explicit BlockHeader(BinaryDataRef str)        { unserialize(str); }
   explicit BlockHeader(BinaryData const & str)   { unserialize(str); }

   // SWIG needs a non-overloaded method
   BlockHeader & unserialize_1_(BinaryData const & str) { unserialize(str); return *this; }

   uint32_t           getVersion(void) const      { return READ_UINT32_LE(getPtr() );   }
   BinaryData const & getThisHash(void) const     { return thisHash_;                   }
   BinaryData         getPrevHash(void) const     { return BinaryData(getPtr()+4 ,32);  }
   BinaryData const & getNextHash(void) const     { return nextHash_;                   }
   BinaryData         getMerkleRoot(void) const   { return BinaryData(getPtr()+36,32);  }
   BinaryData         getDiffBits(void) const     { return BinaryData(getPtr()+72,4 );  }
   uint32_t           getTimestamp(void) const    { return READ_UINT32_LE(getPtr()+68); }
   uint32_t           getNonce(void) const        { return READ_UINT32_LE(getPtr()+76); }
   uint32_t           getBlockHeight(void) const  { return blockHeight_;                }
   void               setBlockHeight(unsigned hgt){ blockHeight_ = hgt;                 }
   bool               isMainBranch(void) const    { return isMainBranch_;               }
   bool               isOrphan(void) const        { return isOrphan_;                   }
   double             getDifficulty(void) const   { return difficultyDbl_;              }
   double             getDifficultySum(void) const{ return difficultySum_;              }

   /////////////////////////////////////////////////////////////////////////////
   BinaryDataRef  getThisHashRef(void) const   { return thisHash_.getRef();            }
   BinaryDataRef  getPrevHashRef(void) const   { return BinaryDataRef(getPtr()+4, 32); }
   BinaryDataRef  getNextHashRef(void) const   { return nextHash_.getRef();            }
   BinaryDataRef  getMerkleRootRef(void) const { return BinaryDataRef(getPtr()+36,32); }
   BinaryDataRef  getDiffBitsRef(void) const   { return BinaryDataRef(getPtr()+72,4 ); }
   uint32_t       getNumTx(void) const         { return numTx_; }

   const std::string&  getFileName(void) const { return blkFile_; }
   uint64_t       getOffset(void) const { return blkFileOffset_; }
   uint32_t       getBlockFileNum(void) const { return blkFileNum_; }
   /////////////////////////////////////////////////////////////////////////////
   uint8_t const * getPtr(void) const  {
      assert(isInitialized_);
      return dataCopy_.getPtr();
   }
   size_t        getSize(void) const {
      assert(isInitialized_);
      return dataCopy_.getSize();
   }
   bool            isInitialized(void) const { return isInitialized_; }
   uint32_t        getBlockSize(void) const { return numBlockBytes_; }
   void            setBlockSize(uint32_t sz) { numBlockBytes_ = sz; }
   void            setNumTx(uint32_t ntx) { numTx_ = ntx; }

   /////////////////////////////////////////////////////////////////////////////
   void           setBlockFile(std::string filename)     {blkFile_       = filename;}
   void           setBlockFileNum(uint32_t fnum)    {blkFileNum_    = fnum;}
   void           setBlockFileOffset(uint64_t offs) {blkFileOffset_ = offs;}

   /////////////////////////////////////////////////////////////////////////////
   void          pprint(std::ostream & os= std::cout, int nIndent=0, bool pBigendian=true) const;
   void          pprintAlot(std::ostream & os= std::cout);

   /////////////////////////////////////////////////////////////////////////////
   const BinaryData& serialize(void) const   { return dataCopy_; }

   bool hasFilePos(void) const { return blkFileNum_ != UINT32_MAX; }

   /////////////////////////////////////////////////////////////////////////////
   void unserialize(uint8_t const * ptr, uint32_t size);
   void unserialize(BinaryData const & str) { unserialize(str.getRef()); }
   void unserialize(BinaryDataRef const & str);
   void unserialize(BinaryRefReader & brr);

   void unserialize_swigsafe_(BinaryData const & rawHead) { unserialize(rawHead); }

   uint8_t getDuplicateID(void) const { return duplicateID_; }
   void    setDuplicateID(uint8_t d)  { duplicateID_ = d; }

   void clearDataCopy() {dataCopy_.resize(0);}

   BinaryData getBlockDataKey(void) const
   {
      return DBUtils::getBlkDataKeyNoPrefix(blockHeight_, duplicateID_);
   }

   unsigned int getThisID(void) const { return uniqueID_; }
   void setUniqueID(unsigned int& ID) { uniqueID_ = ID; }

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

   std::string         blkFile_;
   uint32_t       blkFileNum_ = UINT32_MAX;
   uint64_t       blkFileOffset_ = SIZE_MAX;

   unsigned int uniqueID_ = UINT32_MAX;
};

////////////////////////////////////////////////////////////////////////////////
class TxRef
{
   friend class BlockDataManager;
   friend class Tx;

public:
   /////////////////////////////////////////////////////////////////////////////
   TxRef() { }
   TxRef(BinaryDataRef bdr) { setRef(bdr); }

   /////////////////////////////////////////////////////////////////////////////
   void setRef(BinaryDataRef bdr);

   /////////////////////////////////////////////////////////////////////////////
   bool           isInitialized(void)  const { return dbKey6B_.getSize() > 0; }
   bool           isNull(void) const { return !isInitialized(); }

   /////////////////////////////////////////////////////////////////////////////
   const BinaryData& getDBKey(void) const { return dbKey6B_; }
   BinaryDataRef  getDBKeyRef(void) const { return dbKey6B_.getRef(); }
   void           setDBKey(BinaryData    const & bd) { dbKey6B_.copyFrom(bd); }
   void           setDBKey(BinaryDataRef const & bd) { dbKey6B_.copyFrom(bd); }


   /////////////////////////////////////////////////////////////////////////////
   BinaryData     getDBKeyOfChild(uint16_t i) const
   {
      return dbKey6B_ + WRITE_UINT16_BE(i);
   }

   uint16_t           getBlockTxIndex(void) const;
   uint32_t           getBlockHeight(void) const;
   uint8_t            getDuplicateID(void) const;

   /////////////////////////////////////////////////////////////////////////////
   void               pprint(std::ostream & os = std::cout, int nIndent = 0) const;

   /////////////////////////////////////////////////////////////////////////////
   bool operator==(BinaryData const & dbkey) const { return dbKey6B_ == dbkey; }
   bool operator==(TxRef const & txr) const { return dbKey6B_ == txr.dbKey6B_; }

   bool operator>=(const BinaryData& dbkey) const { return dbKey6B_ >= dbkey; }

protected:
   BinaryData           dbKey6B_;
};

////////////////////////////////////////////////////////////////////////////////
class DBTxRef : public TxRef
{
public:
   DBTxRef()
   { }
   DBTxRef( const TxRef &txref, const LMDBBlockDatabase* db)
      : TxRef(txref), db_(db)
   { }
   
   BinaryData serialize() const; 
   
   BinaryData getBlockHash() const;
   uint32_t getBlockTimestamp() const;
   BinaryData getThisHash() const;
   Tx getTxCopy() const;
   bool isMainBranch()  const;
   
   /////////////////////////////////////////////////////////////////////////////
   // This as fast as you can get a single TxIn or TxOut from the DB.  But if 
   // need multiple of them from the same Tx, you should getTxCopy() and then
   // iterate over them in the Tx object
   TxIn  getTxInCopy(uint32_t i); 
   TxOut getTxOutCopy(uint32_t i);

private:
   const LMDBBlockDatabase*  db_;  
};

////////////////////////////////////////////////////////////////////////////////
class DBOutPoint : public OutPoint
{
private:
   LMDBBlockDatabase* db_;

public:
   DBOutPoint(OutPoint op, LMDBBlockDatabase* db) :
      OutPoint(op), db_(db)
   {}

   BinaryDataRef getDBkey(void) const;

};

////////////////////////////////////////////////////////////////////////////////
// This class is mainly for sorting by priority
class UnspentTxOut
{
public:
   UnspentTxOut(void);
   UnspentTxOut(BinaryData const & hash, uint32_t outIndex, uint32_t height, 
                uint64_t val, BinaryData const & script) :
      txHash_(hash), txOutIndex_(outIndex), txHeight_(height),
      value_(val), script_(script) 
   {}

   BinaryData   getTxHash(void) const      { return txHash_;     }
   uint32_t     getTxtIndex(void) const    { return txIndex_; }
   uint32_t     getTxOutIndex(void) const  { return txOutIndex_; }
   uint64_t     getValue(void)  const      { return value_;      }
   uint64_t     getTxHeight(void)  const   { return txHeight_;   }
   uint32_t     isMultisigRef(void) const  { return isMultisigRef_; }

   OutPoint getOutPoint(void) const { return OutPoint(txHash_, txOutIndex_); }

   BinaryData const & getScript(void) const      { return script_;     }
   BinaryData   getRecipientScrAddr(void) const;

   uint32_t   getNumConfirm(uint32_t currBlknum) const;
   void pprintOneLine(uint32_t currBlk=UINT32_MAX);


   //float getPriority(void);  
   //bool operator<(UnspentTxOut const & t2)
                     //{ return (getPriority() < t2.getPriority()); }
   //bool operator==(UnspentTxOut const & t2)
                     //{ return (getPriority() == t2.getPriority()); }

   // These four methods are listed from steepest-to-shallowest in terms of 
   // how much they favor large inputs over small inputs.  
   // NOTE:  This isn't useful at all anymore:  it was hardly useful even before
   //        I had UTXO sorting in python.  This was really more experimental 
   //        than anything, so I wouldn't bother doing anything with it unless
   //        you want to use it as a template for custom sorting in C++
   static bool CompareNaive(UnspentTxOut const & uto1, UnspentTxOut const & uto2);
   static bool CompareTech1(UnspentTxOut const & uto1, UnspentTxOut const & uto2);
   static bool CompareTech2(UnspentTxOut const & uto1, UnspentTxOut const & uto2);
   static bool CompareTech3(UnspentTxOut const & uto1, UnspentTxOut const & uto2);
   static void sortTxOutVect(std::vector<UnspentTxOut> & utovect, int sortType=1);


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

#endif

