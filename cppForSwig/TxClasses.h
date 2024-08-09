////////////////////////////////////////////////////////////////////////////////
//                                                                            //
//  Copyright (C) 2011-2015, Armory Technologies, Inc.                        //
//  Distributed under the GNU Affero General Public License (AGPL v3)         //
//  See LICENSE-ATI or http://www.gnu.org/licenses/agpl.html                  //
//                                                                            //
//                                                                            //
//  Copyright (C) 2016, goatpig                                               //            
//  Distributed under the MIT license                                         //
//  See LICENSE-MIT or https://opensource.org/licenses/MIT                    //                                   
//                                                                            //
////////////////////////////////////////////////////////////////////////////////
#ifndef _H_TX_CLASSES
#define _H_TX_CLASSES

#include "BinaryData.h"
#include "BtcUtils.h"
#include "DBUtils.h"

//PayStruct flags
#define USE_FULL_CUSTOM_LIST  1
#define ADJUST_FEE            2
#define SHUFFLE_ENTRIES       4

////
class RecipientReuseException
{
private:
   std::vector<std::string> addrVec_;
   uint64_t total_;
   uint64_t value_;

public:
   RecipientReuseException(
      const std::vector<BinaryData>& scrAddrVec, uint64_t total, uint64_t val) :
      total_(total), value_(val)
   {
      for (auto& scrAddr : scrAddrVec)
      {
         auto&& addr58 = BtcUtils::scrAddrToBase58(scrAddr);
         addrVec_.push_back(addr58);
      }
   }

   const std::vector<std::string>& getAddresses(void) const { return addrVec_; }
   uint64_t total(void) const { return total_; }
   uint64_t value(void) const { return value_; }
};

////////////////////////////////////////////////////////////////////////////////
// OutPoint is just a reference to a TxOut
class OutPoint
{
   friend class BlockDataManager;

public:
   OutPoint(void) : txHash_(32), txOutIndex_(UINT32_MAX) { }

   explicit OutPoint(uint8_t const * ptr, uint32_t remaining) { unserialize(ptr, remaining); }
   explicit OutPoint(BinaryData const & txHash, uint32_t txOutIndex) :
      txHash_(txHash), txOutIndex_(txOutIndex) { }

   BinaryData const &   getTxHash(void)     const { return txHash_; }
   BinaryDataRef        getTxHashRef(void)  const { return BinaryDataRef(txHash_); }
   uint32_t             getTxOutIndex(void) const { return txOutIndex_; }
   bool                 isCoinbase(void) const { return txHash_ == BtcUtils::EmptyHash_; }

   void setTxHash(BinaryData const & hash) { txHash_.copyFrom(hash); }
   void setTxOutIndex(uint32_t idx) { txOutIndex_ = idx; }

   // Define these operators so that we can use OutPoint as a map<> key
   bool operator<(OutPoint const & op2) const;
   bool operator==(OutPoint const & op2) const;

   void        serialize(BinaryWriter & bw) const;
   BinaryData  serialize(void) const;
   void        unserialize(uint8_t const * ptr, uint32_t remaining);
   void        unserialize(BinaryReader & br);
   void        unserialize(BinaryRefReader & brr);
   void        unserialize(BinaryData const & bd);
   void        unserialize(BinaryDataRef const & bdRef);

   void unserialize_swigsafe_(BinaryData const & rawOP) { unserialize(rawOP); }

protected:
   BinaryData txHash_;
   uint32_t   txOutIndex_;

   //this member isn't set by ctor, but processed after the first call to
   //get DBKey
   mutable BinaryData DBkey_;
};

////////////////////////////////////////////////////////////////////////////////
class TxIn
{
   friend class BlockDataManager;

public:
   TxIn(void) : 
      dataCopy_(0), scriptType_(TXIN_SCRIPT_NONSTANDARD), scriptOffset_(0) 
   {}

   uint8_t const *  getPtr(void) const { assert(isInitialized()); return dataCopy_.getPtr(); }
   size_t           getSize(void) const { assert(isInitialized()); return dataCopy_.getSize(); }
   bool             isStandard(void) const { return scriptType_ != TXIN_SCRIPT_NONSTANDARD; }
   bool             isCoinbase(void) const { return (scriptType_ == TXIN_SCRIPT_COINBASE); }
   bool             isInitialized(void) const { return dataCopy_.getSize() > 0; }
   OutPoint         getOutPoint(void) const;

   // Script ops
   BinaryData       getScript(void) const;
   BinaryDataRef    getScriptRef(void) const;
   size_t           getScriptSize(void) { return getSize() - (scriptOffset_ + 4); }
   TXIN_SCRIPT_TYPE getScriptType(void) const { return scriptType_; }
   uint32_t         getScriptOffset(void) const { return scriptOffset_; }

   // SWIG doesn't handle these enums well, so we will provide some direct bools
   bool             isScriptStandard() const   { return scriptType_ != TXIN_SCRIPT_NONSTANDARD; }
   bool             isScriptStdUncompr() const { return scriptType_ == TXIN_SCRIPT_STDUNCOMPR; }
   bool             isScriptStdCompr() const  { return scriptType_ == TXIN_SCRIPT_STDCOMPR; }
   bool             isScriptCoinbase() const   { return scriptType_ == TXIN_SCRIPT_COINBASE; }
   bool             isScriptSpendMulti() const { return scriptType_ == TXIN_SCRIPT_SPENDMULTI; }
   bool             isScriptSpendPubKey() const { return scriptType_ == TXIN_SCRIPT_SPENDPUBKEY; }
   bool             isScriptSpendP2SH() const  { return scriptType_ == TXIN_SCRIPT_SPENDP2SH; }
   bool             isScriptNonStd() const    { return scriptType_ == TXIN_SCRIPT_NONSTANDARD; }

   uint32_t         getIndex(void) const { return index_; }
   uint32_t         getSequence() const  { return READ_UINT32_LE(getPtr() + getSize() - 4); }

   /////////////////////////////////////////////////////////////////////////////
   const BinaryData&  serialize(void) const { return dataCopy_; }

   /////////////////////////////////////////////////////////////////////////////
   void unserialize_checked(uint8_t const * ptr,
      uint32_t        size,
      uint32_t        nbytes = 0,
      uint32_t        idx = UINT32_MAX);

   void unserialize(BinaryData const & str,
      uint32_t       nbytes = 0,
      uint32_t        idx = UINT32_MAX);

   void unserialize(BinaryDataRef  str,
      uint32_t       nbytes = 0,
      uint32_t        idx = UINT32_MAX);

   void unserialize(BinaryRefReader & brr,
      uint32_t       nbytes = 0,
      uint32_t        idx = UINT32_MAX);

   void unserialize_swigsafe_(BinaryData const & rawIn) { unserialize(rawIn); }

   /////////////////////////////////////////////////////////////////////////////
   // Not all TxIns have sendor info.  Might have to go to the Outpoint and get
   // the corresponding TxOut to find the sender.  In the case the sender is
   // not available, return false and don't write the output
   bool       getSenderScrAddrIfAvail(BinaryData & addrTarget) const;
   BinaryData getSenderScrAddrIfAvail(void) const;

   void pprint(std::ostream & os = std::cout, int nIndent = 0, bool pBigendian = true) const;


private:
   BinaryData       dataCopy_;

   // Derived properties - we expect these to be set after construct/copy
   uint32_t         index_;
   TXIN_SCRIPT_TYPE scriptType_;
   uint32_t         scriptOffset_;
};


////////////////////////////////////////////////////////////////////////////////
class TxOut
{
   friend class BlockDataManager;
   friend class ZeroConfContainer;

public:

   /////////////////////////////////////////////////////////////////////////////
   TxOut(void) : dataCopy_(0)
   {}

   uint8_t const * getPtr(void) const { return dataCopy_.getPtr(); }
   uint32_t        getSize(void) const { return (uint32_t)dataCopy_.getSize(); }
   uint64_t        getValue(void) const { return READ_UINT64_LE(dataCopy_.getPtr()); }
   bool            isStandard(void) const { return scriptType_ != TXOUT_SCRIPT_NONSTANDARD; }
   bool            isInitialized(void) const { return dataCopy_.getSize() > 0; }
   uint32_t        getIndex(void) { return index_; }

   /////////////////////////////////////////////////////////////////////////////
   BinaryData const & getScrAddressStr(void) const { return uniqueScrAddr_; }
   BinaryDataRef      getScrAddressRef(void) const { return uniqueScrAddr_.getRef(); }

   /////////////////////////////////////////////////////////////////////////////
   BinaryData         getScript(void);
   BinaryDataRef      getScriptRef(void);
   TXOUT_SCRIPT_TYPE  getScriptType(void) const { return scriptType_; }
   uint32_t           getScriptSize(void) const { return getSize() - scriptOffset_; }
   size_t             getScriptOffset(void) const { return scriptOffset_; }

   // SWIG doesn't handle these enums well, so we will provide some direct bools
   bool isScriptStandard(void)    { return scriptType_ != TXOUT_SCRIPT_NONSTANDARD; }
   bool isScriptStdHash160(void)  { return scriptType_ == TXOUT_SCRIPT_STDHASH160; }
   bool isScriptStdPubKey65(void) { return scriptType_ == TXOUT_SCRIPT_STDPUBKEY65; }
   bool isScriptStdPubKey33(void) { return scriptType_ == TXOUT_SCRIPT_STDPUBKEY33; }
   bool isScriptP2SH(void)        { return scriptType_ == TXOUT_SCRIPT_P2SH; }
   bool isScriptNonStd(void)      { return scriptType_ == TXOUT_SCRIPT_NONSTANDARD; }


   /////////////////////////////////////////////////////////////////////////////
   BinaryData serialize(void) const { return BinaryData(dataCopy_); }
   BinaryDataRef serializeRef(void) const { return dataCopy_.getRef(); }

   /////////////////////////////////////////////////////////////////////////////
   void unserialize_checked(uint8_t const *   ptr,
      uint32_t          size,
      uint32_t          nbytes = 0,
      uint32_t          idx = UINT32_MAX);

   void unserialize(BinaryData const & str,
      uint32_t           nbytes = 0,
      uint32_t        idx = UINT32_MAX);

   void unserialize(BinaryDataRef const & str,
      uint32_t          nbytes = 0,
      uint32_t        idx = UINT32_MAX);
   void unserialize(BinaryRefReader & brr,
      uint32_t          nbytes = 0,
      uint32_t        idx = UINT32_MAX);

   void unserialize_swigsafe_(BinaryData const & rawOut) { unserialize(rawOut); }

   void  pprint(std::ostream & os = std::cout, int nIndent = 0, bool pBigendian = true);

private:
   BinaryData        dataCopy_;

   // Derived properties - we expect these to be set after construct/copy
   BinaryData        uniqueScrAddr_;
   TXOUT_SCRIPT_TYPE scriptType_;
   uint32_t          scriptOffset_;
   uint32_t          index_;
};

////////////////////////////////////////////////////////////////////////////////
class Tx
{
   friend class BlockDataManager;
   friend class TransactionVerifier;

public:
   Tx(void) : isInitialized_(false), offsetsTxIn_(0), offsetsTxOut_(0) {}
   explicit Tx(uint8_t const * ptr, uint32_t size) { unserialize(ptr, size); }
   explicit Tx(BinaryRefReader & brr)     { unserialize(brr); }
   explicit Tx(BinaryData const & str)    { unserialize(str); }
   explicit Tx(BinaryDataRef const & str) { unserialize(str); }

   uint8_t const *    getPtr(void)  const { return dataCopy_.getPtr(); }
   size_t             getSize(void) const { return dataCopy_.getSize(); }

   /////////////////////////////////////////////////////////////////////////////
   uint32_t           getVersion(void)   const { return READ_UINT32_LE(dataCopy_.getPtr()); }
   size_t             getNumTxIn(void)   const { return offsetsTxIn_.size() - 1; }
   size_t             getNumTxOut(void)  const { return offsetsTxOut_.size() - 1; }
   const BinaryData&  getThisHash(void)  const;
   bool               isInitialized(void) const { return isInitialized_; }
   bool               isCoinbase(void) const;

   /////////////////////////////////////////////////////////////////////////////
   size_t             getTxInOffset(uint32_t i) const  { return offsetsTxIn_[i]; }
   size_t             getTxOutOffset(uint32_t i) const { return offsetsTxOut_[i]; }
   size_t             getWitnessOffset(uint32_t i) const { return  offsetsWitness_[i]; }

   /////////////////////////////////////////////////////////////////////////////
   static Tx          createFromStr(BinaryData const & bd) { return Tx(bd); }

   /////////////////////////////////////////////////////////////////////////////
   BinaryData         serialize(void) const    { return dataCopy_; }
   BinaryData         serializeNoWitness(void) const;

   /////////////////////////////////////////////////////////////////////////////
   void unserialize(uint8_t const * ptr, size_t size);
   void unserialize(BinaryData const & str) { unserialize(str.getPtr(), str.getSize()); }
   void unserialize(BinaryDataRef const & str) { unserialize(str.getPtr(), str.getSize()); }
   void unserialize(BinaryRefReader & brr);
   void unserialize_swigsafe_(BinaryData const & rawTx) { unserialize(rawTx); }

   /////////////////////////////////////////////////////////////////////////////
   uint32_t    getLockTime(void) const { return lockTime_; }
   uint64_t    getSumOfOutputs(void) const;


   BinaryData getScrAddrForTxOut(uint32_t txOutIndex) const;

   /////////////////////////////////////////////////////////////////////////////
   // These are not pointers to persistent object, these methods actually 
   // CREATES the TxIn/TxOut.  But the construction is fast, so it's
   // okay to do it on the fly
   //TODO: convert to pointer/reference instaed of copy
   TxIn     getTxInCopy(int i) const;
   TxOut    getTxOutCopy(int i) const;

   bool isRBF(void) const;
   void setRBF(bool isTrue)
   {
      isRBF_ = isTrue;
   }

   bool isChained(void) const { return isChainedZc_; }
   void setChainedZC(bool isTrue)
   {
      isChainedZc_ = isTrue;
   }

   virtual uint32_t getTxHeight(void) const { return txHeight_; }
   void setTxHeight(uint32_t height) const { txHeight_ = height; }

   uint32_t getTxIndex(void) const { return txIndex_; }
   void setTxIndex(uint32_t index) const { txIndex_ = index; }

   /////////////////////////////////////////////////////////////////////////////
   void pprint(std::ostream & os = std::cout, int nIndent = 0, bool pBigendian = true) const;
   void pprintAlot(std::ostream & os = std::cout) const;

   bool operator==(const Tx& rhs) const
   {
      if (this->isInitialized() && rhs.isInitialized())
         return this->thisHash_ == rhs.thisHash_;
      return false;
   }

   void setTxTime(uint32_t txtime) { txTime_ = txtime; }
   uint32_t getTxTime(void) const { return txTime_; }

   // returns tx weight
   size_t getWeight(void) const;

   //returns v-size in bytes (getTxWeight name is left for compatibility)
   size_t getTxWeight(void) const;

   const std::vector<uint32_t> getOpIdVec(void) const
   { return outpointIdVec_; }

   void pushBackOpId(uint32_t id) const
   { outpointIdVec_.push_back(id); }

   unsigned getZcIndex(void) const;
   bool isSegWit(void) const;

private:
   // Full copy of the serialized tx
   BinaryData    dataCopy_;
   bool          isInitialized_;
   bool          usesWitness_ = false;

   uint32_t      version_;
   uint32_t      lockTime_;

   // Derived properties - we expect these to be set after construct/copy
   mutable BinaryData    thisHash_;

   // Will always create TxIns and TxOuts on-the-fly; only store the offsets
   std::vector<size_t> offsetsTxIn_;
   std::vector<size_t> offsetsTxOut_;
   std::vector<size_t> offsetsWitness_;
   mutable std::vector<uint32_t> outpointIdVec_;

   uint32_t      txTime_ = 0;

   bool isRBF_ = false;
   bool isChainedZc_ = false;
   mutable uint32_t txHeight_ = UINT32_MAX;
   mutable uint32_t txIndex_ = UINT32_MAX;
};

///////////////////////////////////////////////////////////////////////////////
struct TxComparator
{
   bool operator() (const Tx& lhs, const Tx& rhs) const
   {
      if (lhs.getTxHeight() == rhs.getTxHeight())
         return lhs.getTxIndex() < rhs.getTxIndex();

      return lhs.getTxHeight() < rhs.getTxHeight();
   }
};

////////////////////////////////////////////////////////////////////////////////
struct UTXO
{
   uint64_t   value_ = 0;
   uint32_t   txHeight_ = UINT32_MAX;
   uint32_t   txIndex_ = UINT32_MAX;
   uint32_t   txOutIndex_ = UINT32_MAX;
   BinaryData txHash_;
   BinaryData script_;

   bool       isMultisigRef_ = false;
   unsigned   preferredSequence_ = UINT32_MAX;

   //for coin selection
   bool isInputSW_ = false;
   unsigned txinRedeemSizeBytes_ = UINT32_MAX;
   unsigned witnessDataSizeBytes_ = UINT32_MAX;

   UTXO(uint64_t value, uint32_t txHeight, uint32_t txIndex,
      uint32_t txOutIndex, BinaryData txHash, BinaryData script) :
      txHash_(std::move(txHash)), txOutIndex_(txOutIndex), 
      txHeight_(txHeight), txIndex_(txIndex),
      value_(value), script_(std::move(script))
   {}

   UTXO(void) {}

   BinaryData getRecipientScrAddr(void) const
   {
      return BtcUtils::getTxOutScrAddr(script_);
   }

   uint64_t getValue(void) const { return value_; }
   const BinaryData& getTxHash(void) const { return txHash_; }
   std::string getTxHashStr(void) const { return txHash_.toHexStr(); }
   const BinaryData& getScript(void) const { return script_; }
   uint32_t getTxIndex(void) const { return txIndex_; }
   uint32_t getTxOutIndex(void) const { return txOutIndex_; }
   uint32_t getNumConfirm(uint32_t height) const
   {
      if (txHeight_ == UINT32_MAX)
         return 0;

      return height - txHeight_ + 1;
   }
   unsigned getPreferredSequence(void) const { return preferredSequence_; }

   uint32_t getHeight(void) const { return txHeight_; }

   BinaryData serialize(void) const;
   void unserialize(const BinaryData&);
   void unserializeRaw(const BinaryData&);
   BinaryData serializeTxOut(void) const;

   //coin seletion methods
   bool isSegWit(void) const { return isInputSW_; }
   unsigned getInputRedeemSize(void) const;
   unsigned getWitnessDataSize(void) const;

   bool operator==(const UTXO& rhs) const
   {
      if (rhs.getTxHash() != getTxHash())
         return false;

      return rhs.getTxOutIndex() == getTxOutIndex();
   }

   bool operator!=(const UTXO& rhs) const
   {
      return !(*this == rhs);
   }

   bool operator<(const UTXO& rhs) const
   {
      if (txHash_ != rhs.txHash_)
         return txHash_ < rhs.txHash_;

      if (txOutIndex_ != rhs.txOutIndex_)
         return txOutIndex_ < rhs.txOutIndex_;

      return false;
   }

   bool isInitialized(void) const { return !script_.empty(); }
};

////////////////////////////////////////////////////////////////////////////////
class AddressBookEntry
{
private:
   BinaryData scrAddr_;
   std::vector<BinaryData> txHashList_;

public:
   /////
   AddressBookEntry(void) : scrAddr_(BtcUtils::EmptyHash()) {}
   AddressBookEntry(BinaryData scraddr) : scrAddr_(scraddr) {}
   void addTxHash(const BinaryData& hash) { txHashList_.push_back(hash); }
   const BinaryData& getScrAddr() const { return scrAddr_; }

   /////
   const std::vector<BinaryData>& getTxHashList() const
   {
      return txHashList_;
   }

   /////
   bool operator<(AddressBookEntry const & abe2) const
   {
      return scrAddr_ < abe2.scrAddr_;
   }

   BinaryData serialize(void) const;
   void unserialize(const BinaryData& data);
};


////////////////////////////////////////////////////////////////////////////////
enum OutputSpentnessState
{
   Unspent = 1,
   Spent = 2,
   Invalid = 3
};

////
struct SpentnessResult
{
   BinaryData spender_;
   unsigned height_ = UINT32_MAX;
   OutputSpentnessState state_ = OutputSpentnessState::Invalid;
};

#endif
