////////////////////////////////////////////////////////////////////////////////
//                                                                            //
//  Copyright (C) 2020-2026, goatpig                                          //
//  Distributed under the MIT license                                         //
//  See LICENSE-MIT or https://opensource.org/licenses/MIT                    //
//                                                                            //
////////////////////////////////////////////////////////////////////////////////

#pragma once

#include <map>
#include <set>
#include <functional>

#include <Utils/Types.h>
#include <Utils/BinaryData.h>

class LMDBBlockDatabase;
class Tx;
class TxOut;
class TxIOPair;

namespace Armory
{
   class Blockchain;
   class BlockchainData;

   namespace ZeroConf
   {
      class ZeroConfCallbacks;
      using TxIOKeys = std::set<Types::TxIOKey>;

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
         std::set<Types::ScrAddr> scrAddrs;
         std::map<Types::TxKey, Types::TxHash> invalidatedKeys;

         void mergeTxios(const ParsedZCData&);
      };

      ////
      class OutPointRef
      {
      private:
         Types::TxHash txHash_;
         Types::TxIOId txOutIndex_ = UINT16_MAX;
         Types::TxIOKey dbKey_ = Types::INVALID_TXIO_KEY;
         uint32_t time_ = UINT32_MAX;

      public:
         void unserialize(const uint8_t*, uint32_t);
         void unserialize(BinaryDataRef);

         void resolveDbKey(LMDBBlockDatabase*);
         bool isResolved(void) const;
         bool isInitialized(void) const;

         const Types::TxHash& getTxHash(void) const;
         Types::TxIOId getIndex(void) const;

         const Types::TxIOKey& getDbKey(void) const;
         void setDbKey(const Types::TxKey&);

         void reset(InputResolution);
         bool isZc(void) const;

         void setTime(uint32_t);
         uint32_t getTime(void) const;
      };

      ////
      struct ParsedTxIn
      {
         OutPointRef opRef;
         Types::ScrAddr scrAddr;
         uint64_t value = UINT64_MAX;

         bool isResolved(void) const;
      };

      struct ParsedTxOut
      {
         Types::ScrAddr scrAddr;
         uint64_t value = UINT64_MAX;
         size_t offset;
         size_t len;

         bool isInitialized(void) const;
      };

      ////
      class ParsedTx
      {
      private:
         const Types::TxKey zcKey_;

         mutable Types::TxHash txHash_;
         std::shared_ptr<Tx> tx_;

      public:
         ParsedTxStatus state{ParsedTxStatus::Uninitialized};
         std::vector<ParsedTxIn> inputs;
         std::vector<ParsedTxOut> outputs;
         bool isRBF = false;
         bool isChainedZc = false;

      public:
         ParsedTx(const Types::TxKey&);

         void setTx(BinaryDataRef, uint32_t);
         void setTxHash(const Types::TxHash&);
         void resetInputResolution(InputResolution);

         bool isResolved(void) const;
         const Types::TxHash& getTxHash(void) const;
         const Types::TxKey& getKey(void) const;
         const Tx& getTxObj(void) const;
      };

      ////
      struct FilteredZeroConfData
      {
         std::map<Types::ScrAddr, std::map<Types::TxIOKey, std::shared_ptr<TxIOPair>>> scrAddrTxioMap;
         std::map<Types::TxHash, std::map<unsigned, Types::TxKey>> outPointsSpentByKey;
         TxIOKeys txOutsSpentByZC;
         std::map<Types::TxKey, std::shared_ptr<std::set<Types::ScrAddr>>> keyToSpentScrAddr;
         std::map<Types::TxKey, std::set<Types::ScrAddr>> keyToFundedScrAddr;

         std::map<Types::BdvId, ParsedZCData> flaggedBDVs;
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
         std::map<Types::TxHash, Types::TxKey> txHashToDBKey_; //<txHash, zcKey>
         std::map<Types::TxKey, std::shared_ptr<ParsedTx>> txMap_; //<zcKey, zcTx>

         //<txOutKey, bool> (true for valid, false for dropped)
         std::map<Types::TxIOKey, bool> txOutsSpentByZC_;

         //<scrAddr, <txOutKey>>
         std::map<Types::ScrAddr, TxIOKeys> scrAddrMap_;

         //<zcKey/txKey, txio>>
         std::map<Types::TxIOKey, std::shared_ptr<TxIOPair>> txioMap_;

         std::shared_ptr<MempoolData> parent_;

      private:
         TxIOKeys* getTxioKeysFromParent(const Types::ScrAddr&) const;
         const TxIOKeys& getTxioKeysForScrAddr(const Types::ScrAddr&) const;
         TxIOKeys& getTxioKeysForScrAddr_NoThrow(const Types::ScrAddr&);

      public:
         unsigned getParentCount(void) const;
         void copyFrom(const MempoolData&);

         std::shared_ptr<ParsedTx> getTx(Types::TxKey) const;
         std::shared_ptr<const TxIOPair> getTxio(Types::TxIOKey) const;
         Types::TxKey getKeyForHash(const Types::TxHash&) const;
         bool isTxOutSpentByZC(Types::TxIOKey) const;

         ////
         void dropFromSpentTxOuts(Types::TxIOKey);
         void dropFromScrAddrMap(const Types::ScrAddr&, Types::TxKey);
         void dropTxHashToDBKey(const Types::TxHash&);

         void dropTxiosForZC(Types::TxKey);
         void dropTxioInputs(Types::TxKey, const TxIOKeys&);
         void dropTx(Types::TxKey);

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
         std::shared_ptr<ParsedTx> getTxByKey_NoConst(Types::TxKey) const;
         std::set<Types::TxKey> findChildren(Types::TxKey);

      public:
         MempoolSnapshot(unsigned, unsigned);
         static std::shared_ptr<MempoolSnapshot> copy(
            std::shared_ptr<MempoolSnapshot>,
            unsigned, unsigned);

         const TxIOKeys& getTxioKeysForScrAddr(const Types::ScrAddr&) const;
         std::map<Types::TxIOKey, std::shared_ptr<const TxIOPair>>
            getTxioMapForScrAddr(const Types::ScrAddr&) const;
         std::shared_ptr<const TxIOPair> getTxioByKey(Types::TxIOKey) const;

         std::shared_ptr<const ParsedTx> getTxByKey(Types::TxKey) const;
         std::shared_ptr<const ParsedTx> getTxByHash(const Types::TxHash&) const;
         TxOut getTxOutCopy(Types::TxKey, Types::TxIOId) const;

         Types::TxKey getKeyForHash(const Types::TxHash&) const;
         const Types::TxHash& getHashForKey(Types::TxKey) const;
         bool hasHash(const Types::TxHash&) const;

         Types::ZcId getTopZcID(void) const;
         bool isTxOutSpentByZC(Types::TxIOKey) const;

         void preprocessZcMap(LMDBBlockDatabase*, std::shared_ptr<BlockchainData>);
         std::map<Types::TxKey, std::shared_ptr<ParsedTx>> dropZc(Types::TxKey);

         void stageNewZC(std::shared_ptr<ParsedTx>, const FilteredZeroConfData&);
         void commitNewZCs(void);
         unsigned getMergeCount(void) const { return mergeCount_; }
      };

      void preprocessZcMap(
         const std::map<Types::TxKey, std::shared_ptr<ParsedTx>>&,
         LMDBBlockDatabase*, std::shared_ptr<BlockchainData>);
      void preprocessTx(ParsedTx&, LMDBBlockDatabase*,
         std::shared_ptr<BlockchainData>);
      void finalizeParsedTxResolution(
         std::shared_ptr<ParsedTx>,
         std::shared_ptr<Blockchain>, const std::set<BinaryData>&,
         std::shared_ptr<MempoolSnapshot>);
      FilteredZeroConfData filterParsedTx(
         std::shared_ptr<ParsedTx>,
         const std::function<bool(const BinaryData&)>&,
         ZeroConfCallbacks*);
   } //namespace ZeroConf
} //namespace Armory
