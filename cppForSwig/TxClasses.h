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
#include <Utils/Types.h>

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
   static BinaryDataRef getRawData(const uint8_t*, size_t, size_t=0);
   void unserialize_checked(void);

public:
   TxIn(const uint8_t*, size_t, size_t, uint32_t=UINT32_MAX);
   TxIn(BinaryDataRef, size_t, uint32_t=UINT32_MAX);
   TxIn(BinaryRefReader, size_t, uint32_t=UINT32_MAX);

   const uint8_t* getPtr(void) const;
   size_t getSize(void) const;
   bool isStandard(void) const;
   bool isCoinbase(void) const;
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
   const BinaryData dataCopy_;

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
   static BinaryDataRef getRawData(const uint8_t*, size_t, size_t);
   void unserialize(void);

public:
   TxOut(BinaryDataRef, size_t=0, uint32_t=UINT32_MAX);
   TxOut(BinaryRefReader&, size_t=0, uint32_t=UINT32_MAX);
   TxOut(const uint8_t*, size_t, uint32_t=UINT32_MAX);

   const uint8_t*    getPtr(void) const;
   uint32_t          getSize(void) const;
   Armory::Types::Amount getAmount(void) const;
   bool              isStandard(void) const;
   Armory::Types::TxIOId getIndex(void);

   ////////
   const Armory::Types::ScrAddr& getScrAddress(void) const;

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
   const BinaryData  dataCopy_;

   // Derived properties - we expect these to be set after construct/copy
   Armory::Types::ScrAddr uniqueScrAddr_;
   TxOutScriptType   scriptType_;
   uint32_t          scriptOffset_;
   Armory::Types::TxIOId index_;
};

////////////////////////////////////////////////////////////////////////////////
class Tx
{
private:
   static Tx unserialize(const uint8_t*, size_t);
   Tx(BinaryDataRef,
      std::vector<size_t>&, std::vector<size_t>&, std::vector<size_t>&,
      uint32_t, uint32_t, bool);

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
   const Armory::Types::TxHash& getThisHash(void) const;
   bool              isCoinbase(void) const;
   uint32_t          getLockTime(void) const;
   uint64_t          getSumOfOutputs(void) const;
   bool              isRBF(void) const;
   bool              isChained(void) const;
   bool              isSegWit(void) const;
   uint32_t          getTxTime(void) const;
   Armory::Types::BlockId getBlockId(void) const;
   Armory::Types::TxId getTxIndex(void) const;
   Armory::Types::TxKey getDBKey(void) const;

   /////////////////////////////////////////////////////////////////////////////
   size_t            getTxInOffset(uint32_t) const;
   size_t            getTxOutOffset(uint32_t) const;
   size_t            getWitnessOffset(uint32_t) const;

   /////////////////////////////////////////////////////////////////////////////
   static Tx         createFromStr(const BinaryData&);
   BinaryData        serialize(void) const;
   BinaryData        serializeNoWitness(void) const;

   /////////////////////////////////////////////////////////////////////////////
   TxIn              getTxInCopy(Armory::Types::TxIOId) const;
   TxOut             getTxOutCopy(Armory::Types::TxIOId) const;

   /////////////////////////////////////////////////////////////////////////////
   // returns tx weight
   size_t getWeight(void) const;

   //returns v-size in bytes (getTxWeight name is left for compatibility)
   size_t getTxWeight(void) const;

   /////////////////////////////////////////////////////////////////////////////
   void setTxKey(Armory::Types::TxKey);
   void setRBF(bool);
   void setChainedZC(bool);
   void setTxTime(uint32_t);
   void pushBackOpId(uint32_t) const;

private:
   // Full copy of the serialized tx
   const BinaryData dataCopy_;

   // Will always create TxIns and TxOuts on-the-fly; only store the offsets
   const std::vector<size_t> offsetsTxIn_;
   const std::vector<size_t> offsetsTxOut_;
   const std::vector<size_t> offsetsWitness_;

   const uint32_t version_;
   const uint32_t lockTime_;
   const bool usesWitness_;

   uint32_t txTime_{UINT32_MAX};
   mutable Armory::Types::TxHash thisHash_;
   Armory::Types::TxKey txKey_;

   bool isRBF_ = false;
   bool isChainedZc_ = false;
};

////////////////////////////////////////////////////////////////////////////////
struct UTXO
{
public:
   Armory::Types::Amount amount = UINT64_MAX;
   uint32_t txHeight = UINT32_MAX;
   Armory::Types::TxId txIndex = UINT16_MAX;
   Armory::Types::TxIOId txOutIndex = UINT16_MAX;
   Armory::Types::TxHash txHash;
   BinaryData script;

   bool     isMultisigRef = false;
   unsigned preferredSequence = UINT32_MAX;

   //for coin selection
   bool     isInputSW = false;
   unsigned txinRedeemSizeBytes = UINT32_MAX;
   unsigned witnessDataSizeBytes = UINT32_MAX;

public:
   UTXO(void);
   UTXO(Armory::Types::Amount, uint32_t, Armory::Types::TxId,
      Armory::Types::TxIOId, Armory::Types::TxHash, BinaryData);

   bool operator==(const UTXO&) const;
   bool operator!=(const UTXO&) const;
   bool operator<(const UTXO&) const;

   bool isInitialized(void) const;
   Armory::Types::Amount getAmount(void) const;
   const Armory::Types::TxHash& getTxHash(void) const;
   std::string getTxHashStr(void) const;
   const BinaryData& getScript(void) const;
   Armory::Types::TxId getTxIndex(void) const;
   Armory::Types::TxIOId getTxOutIndex(void) const;
   uint32_t getNumConfirm(uint32_t) const;
   unsigned getPreferredSequence(void) const;
   uint32_t getHeight(void) const;
   Armory::Types::ScrAddr getRecipientScrAddr(void) const;

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
