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

#include <Utils/BinaryData.h>

//PayStruct flags
#define USE_FULL_CUSTOM_LIST  1
#define ADJUST_FEE            2
#define SHUFFLE_ENTRIES       4

enum class TxOutScriptType : int;
enum class TxInScriptType : int;

////
class RecipientReuseException
{
private:
   std::vector<std::string> addrVec_;
   uint64_t total_;
   uint64_t value_;

public:
   RecipientReuseException(const std::vector<BinaryData>&, uint64_t, uint64_t);
   const std::vector<std::string>& getAddresses(void) const;
   uint64_t total(void) const;
   uint64_t value(void) const;
};

////////////////////////////////////////////////////////////////////////////////
// Outpoint is just a reference to a TxOut
class Outpoint
{
   friend class BlockDataManager;

public:
   explicit Outpoint(const uint8_t*, size_t);
   explicit Outpoint(const BinaryData&, uint32_t);

   const BinaryData& getTxHash(void) const;
   BinaryDataRef getTxHashRef(void) const;
   uint32_t getTxOutIndex(void) const;
   bool isCoinbase(void) const;

   void setTxHash(const BinaryData&);
   void setTxOutIndex(uint32_t);

   // Define these operators so that we can use Outpoint as a map<> key
   bool operator<(const Outpoint&) const;
   bool operator==(const Outpoint&) const;

   void        serialize(BinaryWriter&) const;
   BinaryData  serialize(void) const;
   void        unserialize(const uint8_t*, size_t);
   void        unserialize(BinaryReader&);
   void        unserialize(BinaryRefReader&);
   void        unserialize(const BinaryData&);
   void        unserialize(const BinaryDataRef&);

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

private:
   void unserialize_checked(const uint8_t*, size_t, size_t=0,
      uint32_t=UINT32_MAX);

public:
   TxIn(const uint8_t*, size_t, size_t, uint32_t=UINT32_MAX);
   TxIn(BinaryDataRef, size_t, uint32_t=UINT32_MAX);
   TxIn(BinaryRefReader, size_t, uint32_t=UINT32_MAX);

   const uint8_t* getPtr(void) const;
   size_t getSize(void) const;
   bool isStandard(void) const;
   bool isCoinbase(void) const;
   bool isInitialized(void) const;
   Outpoint getOutPoint(void) const;

   // Script ops
   BinaryData        getScript(void) const;
   BinaryDataRef     getScriptRef(void) const;
   size_t            getScriptSize(void)const;
   TxInScriptType    getScriptType(void) const;
   uint32_t          getScriptOffset(void) const;
   uint32_t          getIndex(void) const;
   uint32_t          getSequence(void) const;
   const BinaryData& serialize(void) const;

   /////////////////////////////////////////////////////////////////////////////
   // Not all TxIns have sender info. Might have to go to the Outpoint and get
   // the corresponding TxOut to find the sender. In the case the sender is
   // not available, return false and don't write the output
   bool       getSenderScrAddrIfAvail(BinaryData&) const;
   BinaryData getSenderScrAddrIfAvail(void) const;

   void pprint(std::ostream& = std::cout, int=0, bool=true) const;

private:
   BinaryData       dataCopy_;

   // Derived properties - we expect these to be set after construct/copy
   uint32_t         index_;
   TxInScriptType   scriptType_;
   uint32_t         scriptOffset_;
};

////////////////////////////////////////////////////////////////////////////////
class TxOut
{
   friend class BlockDataManager;

private:
   void unserialize(const uint8_t*, size_t, size_t, uint32_t);

public:
   TxOut(BinaryDataRef, size_t=0, uint32_t=UINT32_MAX);
   TxOut(BinaryRefReader&, size_t=0, uint32_t=UINT32_MAX);
   TxOut(const uint8_t*, size_t, uint32_t=UINT32_MAX);

   const uint8_t*    getPtr(void) const;
   uint32_t          getSize(void) const;
   uint64_t          getValue(void) const;
   bool              isStandard(void) const;
   bool              isInitialized(void) const;
   uint32_t          getIndex(void);

   ////////
   const BinaryData& getScrAddressStr(void) const;
   BinaryDataRef     getScrAddressRef(void) const;

   ////////
   BinaryData        getScript(void) const;
   BinaryDataRef     getScriptRef(void) const;
   TxOutScriptType   getScriptType(void) const;
   uint32_t          getScriptSize(void) const;
   size_t            getScriptOffset(void) const;

   ////////
   BinaryData        serialize(void) const;
   BinaryDataRef     serializeRef(void) const;
   void              pprint(std::ostream& = std::cout, int=0, bool=true);

private:
   BinaryData        dataCopy_;

   // Derived properties - we expect these to be set after construct/copy
   BinaryData        uniqueScrAddr_;
   TxOutScriptType   scriptType_;
   uint32_t          scriptOffset_;
   uint32_t          index_;
};

////////////////////////////////////////////////////////////////////////////////
class Tx
{
private:
   void unserialize(const uint8_t*, size_t);

public:
   explicit Tx(const uint8_t*, size_t);
   explicit Tx(BinaryRefReader&);
   explicit Tx(BinaryDataRef);

   bool operator==(const Tx&) const;

   /////////////////////////////////////////////////////////////////////////////
   const uint8_t*    getPtr(void)  const;
   size_t            getSize(void) const;
   uint32_t          getVersion(void) const;
   size_t            getNumTxIn(void) const;
   size_t            getNumTxOut(void) const;
   const BinaryData& getThisHash(void) const;
   bool              isInitialized(void) const;
   bool              isCoinbase(void) const;
   uint32_t          getLockTime(void) const;
   uint64_t          getSumOfOutputs(void) const;
   bool              isRBF(void) const;
   bool              isChained(void) const;
   bool              isSegWit(void) const;
   uint32_t          getTxTime(void) const;
   unsigned          getZcIndex(void) const;
   virtual uint32_t  getTxHeight(void) const;
   uint8_t           getDupId(void) const;
   uint32_t          getTxIndex(void) const;
   BinaryData        getDBKey(void) const;

   /////////////////////////////////////////////////////////////////////////////
   size_t            getTxInOffset(uint32_t) const;
   size_t            getTxOutOffset(uint32_t) const;
   size_t            getWitnessOffset(uint32_t) const;

   /////////////////////////////////////////////////////////////////////////////
   static Tx         createFromStr(const BinaryData&);
   BinaryData        serialize(void) const;
   BinaryData        serializeNoWitness(void) const;

   /////////////////////////////////////////////////////////////////////////////
   BinaryData        getScrAddrForTxOut(uint32_t) const;
   TxIn              getTxInCopy(uint32_t) const;
   TxOut             getTxOutCopy(uint32_t) const;

   /////////////////////////////////////////////////////////////////////////////
   // returns tx weight
   size_t getWeight(void) const;

   //returns v-size in bytes (getTxWeight name is left for compatibility)
   size_t getTxWeight(void) const;

   /////////////////////////////////////////////////////////////////////////////
   void setRBF(bool);
   void setChainedZC(bool);
   void setTxHeight(uint32_t) const;
   void setDupId(uint8_t) const;
   void setTxIndex(uint32_t) const;
   void setTxTime(uint32_t);
   void pushBackOpId(uint32_t) const;

private:
   // Full copy of the serialized tx
   BinaryData dataCopy_;
   bool isInitialized_{false};
   bool usesWitness_{false};

   uint32_t version_;
   uint32_t lockTime_;
   uint32_t txTime_{0};

   // Derived properties - we expect these to be set after construct/copy
   mutable BinaryData thisHash_;

   // Will always create TxIns and TxOuts on-the-fly; only store the offsets
   std::vector<size_t> offsetsTxIn_;
   std::vector<size_t> offsetsTxOut_;
   std::vector<size_t> offsetsWitness_;

   bool isRBF_ = false;
   bool isChainedZc_ = false;
   mutable uint32_t txHeight_ = UINT32_MAX;
   mutable uint8_t dupId_ = UINT8_MAX;
   mutable uint32_t txIndex_ = UINT32_MAX;
};

////////////////////////////////////////////////////////////////////////////////
struct UTXO
{
public:
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

public:
   UTXO(uint64_t, uint32_t, uint32_t,
      uint32_t, BinaryData, BinaryData);
   UTXO(void);

   bool operator==(const UTXO&) const;
   bool operator!=(const UTXO&) const;
   bool operator<(const UTXO&) const;

   bool isInitialized(void) const;
   uint64_t getValue(void) const;
   const BinaryData& getTxHash(void) const;
   std::string getTxHashStr(void) const;
   const BinaryData& getScript(void) const;
   uint32_t getTxIndex(void) const;
   uint32_t getTxOutIndex(void) const;
   uint32_t getNumConfirm(uint32_t) const;
   unsigned getPreferredSequence(void) const;
   uint32_t getHeight(void) const;
   BinaryData getRecipientScrAddr(void) const;

   //coin seletion methods
   bool isSegWit(void) const;
   unsigned getInputRedeemSize(void) const;
   unsigned getWitnessDataSize(void) const;

   BinaryData serialize(void) const;
   void unserialize(const BinaryData&);
   void unserializeRaw(const BinaryData&);
   BinaryData serializeTxOut(void) const;
};

////////////////////////////////////////////////////////////////////////////////
//this is a bit scuffed, shouldnt have a UTXO struct in the first place, but
// the change is out of the scope of this refactor
struct Output : public UTXO
{
public:
   BinaryData spenderHash;

public:
   Output(uint64_t, uint32_t, uint32_t,
      uint32_t, BinaryData, BinaryData,
      BinaryData);

   bool isSpent(void) const;
};

////////////////////////////////////////////////////////////////////////////////
class AddressBookEntry
{
private:
   BinaryData scrAddr_;
   std::vector<BinaryData> txHashList_;

public:
   struct Comparator
   {
      using is_transparent = void;
      bool operator()(const AddressBookEntry&, const AddressBookEntry&) const;
      bool operator()(const AddressBookEntry&, const BinaryData&) const;
      bool operator()(const BinaryData&, const AddressBookEntry&) const;
   };

public:
   AddressBookEntry(void);
   AddressBookEntry(BinaryDataRef);

   bool operator<(const AddressBookEntry&) const;
   const BinaryData& getScrAddr(void) const;
   const std::vector<BinaryData>& getTxHashList(void) const;
   void addTxHash(const BinaryData&);

   BinaryData serialize(void) const;
   void unserialize(const BinaryData&);
};

////////////////////////////////////////////////////////////////////////////////
enum OutputSpentnessState
{
   Unspent = 1,
   Spent = 2,
   Invalid = 3
};

struct SpentnessResult
{
   BinaryData spender_;
   unsigned height_ = UINT32_MAX;
   OutputSpentnessState state_ = OutputSpentnessState::Invalid;
};
