////////////////////////////////////////////////////////////////////////////////
//                                                                            //
//  Copyright (C) 2020-2025, goatpig                                          //
//  Distributed under the MIT license                                         //
//  See LICENSE-MIT or https://opensource.org/licenses/MIT                    //
//                                                                            //
////////////////////////////////////////////////////////////////////////////////

#pragma once

#include <map>
#include <set>
#include <functional>

#include <Utils/BinaryData.h>

class LMDBBlockDatabase;
class Tx;
class TxOut;
class TxIOPair;

namespace Armory
{
   class Blockchain;

   namespace ZeroConf
   {
      class ZeroConfCallbacks;

      enum class ParsedTxStatus : int
      {
         Uninitialized,
         Resolved,
         ResolveAgain,
         Unresolved,
         Mined,
         Invalid,
         Skip
      };

      enum class InputResolution : int
      {
         Both,
         Unconfirmed,
         Mined
      };

      ////
      struct ParsedZCData
      {
         std::set<BinaryData> scrAddrs;
         std::map<BinaryData, BinaryData> invalidatedKeys;

         void mergeTxios(const ParsedZCData&);
      };

      ////
      class OutPointRef
      {
      private:
         BinaryData txHash_;
         unsigned txOutIndex_ = UINT16_MAX;
         BinaryData dbKey_;
         uint64_t time_ = UINT64_MAX;

      public:
         void unserialize(const uint8_t*, uint32_t);
         void unserialize(BinaryDataRef);

         void resolveDbKey(LMDBBlockDatabase*);
         void setDbKey(const BinaryData&);

         bool isResolved(void) const;
         bool isInitialized(void) const;

         BinaryDataRef getTxHashRef(void) const;
         unsigned getIndex(void) const;

         const BinaryData& getDbKey(void) const;
         BinaryDataRef getDbTxKeyRef(void) const;

         void reset(InputResolution);
         bool isZc(void) const;

         void setTime(uint64_t);
         uint64_t getTime(void) const;
      };

      ////
      struct ParsedTxIn
      {
         OutPointRef opRef;
         BinaryData scrAddr;
         uint64_t value = UINT64_MAX;

         bool isResolved(void) const;
      };

      struct ParsedTxOut
      {
         BinaryData scrAddr;
         uint64_t value = UINT64_MAX;
         size_t offset;
         size_t len;

         bool isInitialized(void) const;
      };

      ////
      class ParsedTx
      {
      private:
         const BinaryData zcKey_;
         uint32_t txIndex_;

         mutable BinaryData txHash_;
         std::shared_ptr<Tx> tx_;

      public:
         ParsedTxStatus state{ParsedTxStatus::Uninitialized};
         std::vector<ParsedTxIn> inputs;
         std::vector<ParsedTxOut> outputs;
         bool isRBF = false;
         bool isChainedZc = false;

      public:
         ParsedTx(BinaryData&);

         void setTx(BinaryDataRef, uint32_t);
         void setTxHash(const BinaryData&);
         void resetInputResolution(InputResolution);

         bool isResolved(void) const;
         const BinaryData& getTxHash(void) const;
         BinaryDataRef getKeyRef(void) const;
         const BinaryData& getKey(void) const;
         const Tx& getTxObj(void) const;
      };

      ////
      struct FilteredZeroConfData
      {
         std::map<BinaryData, std::map<BinaryData, std::shared_ptr<TxIOPair>>> scrAddrTxioMap;
         std::map<BinaryDataRef, std::map<unsigned, BinaryDataRef>> outPointsSpentByKey;
         std::set<BinaryData> txOutsSpentByZC;
         std::map<BinaryDataRef, std::shared_ptr<std::set<BinaryDataRef>>> keyToSpentScrAddr;
         std::map<BinaryDataRef, std::set<BinaryDataRef>> keyToFundedScrAddr;

         std::map<uint64_t, ParsedZCData> flaggedBDVs;
         std::shared_ptr<ParsedTx> txPtr;

         bool isEmpty(void) const;
         bool isValid(void) const;
      };

      ////
      class MempoolSnapshot;
      class MempoolData
      {
         /*
         blockHeight:
            uint32_t
         dupId:
            uint8_t

         txId:
            uint16_t
         outputId:
            uint16_t

         zcId:
            uint32_t

         zcTag:
            0xFFFF

         ------

         blockKey:
            [blockHeight (BE) << 8 | dupId] (4 bytes)

         txKey:
            [blockKey | txId (BE)] (6 bytes)

         zcKey:
            [zcTag | zcId (BE)] (6 bytes)

         txOutKey:
            [zcKey/txKey | outputId (BE)] (8 bytes)
         */

         friend class MempoolSnapshot;

      public:
         //TODO: shouldn't use references for txHashes anymore
         std::map<BinaryDataRef, BinaryDataRef> txHashToDBKey_; //<txHash, zcKey>
         std::map<BinaryData, std::shared_ptr<ParsedTx>> txMap_; //<zcKey, zcTx>

         //<txOutKey, bool> (true for valid, false for dropped)
         std::map<BinaryData, bool> txOutsSpentByZC_;

         //<scrAddr, <txOutKey>>
         std::map<BinaryData, std::set<BinaryData>> scrAddrMap_;

         //<zcKey/txKey, txio>>
         std::map<BinaryData, std::shared_ptr<TxIOPair>> txioMap_;

         std::shared_ptr<MempoolData> parent_;

      private:
         std::set<BinaryData>* getTxioKeysFromParent(BinaryDataRef) const;
         const std::set<BinaryData>& getTxioKeysForScrAddr(BinaryDataRef) const;
         std::set<BinaryData>& getTxioKeysForScrAddr_NoThrow(BinaryDataRef);

      public:
         unsigned getParentCount(void) const;
         void copyFrom(const MempoolData&);

         std::shared_ptr<ParsedTx> getTx(BinaryDataRef) const;
         std::shared_ptr<const TxIOPair> getTxio(BinaryDataRef) const;
         BinaryDataRef getKeyForHash(BinaryDataRef) const;
         bool isTxOutSpentByZC(BinaryDataRef) const;

         ////
         void dropFromSpentTxOuts(BinaryDataRef);
         void dropFromScrAddrMap(BinaryDataRef, BinaryDataRef);
         void dropTxHashToDBKey(BinaryDataRef);

         void dropTxiosForZC(BinaryDataRef);
         void dropTxioInputs(BinaryDataRef, const std::set<BinaryData>&);
         void dropTx(BinaryDataRef);

         ////
         static std::shared_ptr<MempoolData> mergeWithParent(
            std::shared_ptr<MempoolData>);
      };

      ////
      class MempoolSnapshot
      {
      private:
         unsigned depth_;
         unsigned threshold_;
         std::shared_ptr<MempoolData> data_;
         unsigned topID_ = 0;

         //for unit tests
         unsigned mergeCount_ = 0;

      private:
         std::shared_ptr<ParsedTx> getTxByKey_NoConst(BinaryDataRef) const;
         std::set<BinaryData> findChildren(BinaryDataRef);

      public:
         MempoolSnapshot(unsigned, unsigned);
         static std::shared_ptr<MempoolSnapshot> copy(
            std::shared_ptr<MempoolSnapshot>,
            unsigned, unsigned);

         const std::set<BinaryData>& getTxioKeysForScrAddr(BinaryDataRef) const;
         std::map<BinaryDataRef, std::shared_ptr<const TxIOPair>>
            getTxioMapForScrAddr(BinaryDataRef) const;
         std::shared_ptr<const TxIOPair> getTxioByKey(BinaryDataRef) const;

         std::shared_ptr<const ParsedTx> getTxByKey(BinaryDataRef) const;
         std::shared_ptr<const ParsedTx> getTxByHash(BinaryDataRef) const;
         TxOut getTxOutCopy(BinaryDataRef, uint16_t) const;

         BinaryDataRef getKeyForHash(BinaryDataRef) const;
         BinaryDataRef getHashForKey(BinaryDataRef) const;
         bool hasHash(BinaryDataRef) const;

         uint32_t getTopZcID(void) const;
         bool isTxOutSpentByZC(BinaryDataRef) const;

         void preprocessZcMap(LMDBBlockDatabase*, Blockchain*);
         std::map<BinaryData, std::shared_ptr<ParsedTx>> dropZc(BinaryDataRef);

         void stageNewZC(std::shared_ptr<ParsedTx>, const FilteredZeroConfData&);
         void commitNewZCs(void);
         unsigned getMergeCount(void) const { return mergeCount_; }
      };

      void preprocessTx(ParsedTx&, LMDBBlockDatabase*, Blockchain*);
      void preprocessZcMap(
         const std::map<BinaryData, std::shared_ptr<ParsedTx>>&,
         LMDBBlockDatabase*, Blockchain*);
      void finalizeParsedTxResolution(
         std::shared_ptr<ParsedTx>,
         LMDBBlockDatabase*, const std::set<BinaryData>&,
         std::shared_ptr<MempoolSnapshot>);

      FilteredZeroConfData filterParsedTx(
         std::shared_ptr<ParsedTx>,
         const std::function<bool(const BinaryData&)>&,
         ZeroConfCallbacks*);
   } //namespace ZeroConf
} //namespace Armory
