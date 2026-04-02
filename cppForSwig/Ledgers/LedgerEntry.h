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

#include <set>
#include <map>
#include <vector>
#include <functional>
#include <Utils/BinaryData.h>

class TxIOPair;

////////////////////////////////////////////////////////////////////////////////
//
// LedgerEntry
//
// LedgerEntry class is used for bother ScrAddresses and BtcWallets.  Members
// have slightly different meanings (or irrelevant) depending which one it's
// used with.
//
//  Address -- Each entry corresponds to ONE TxIn OR ONE TxOut
//
//    scrAddr_    -  useless - just repeating this address
//    value_     -  net debit/credit on addr balance, in Satoshis (1e-8 BTC)
//    blockNum_  -  block height of the tx in which this txin/out was included
//    txHash_    -  hash of the tx in which this txin/txout was included
//    index_     -  index of the txin/txout in this tx
//    isValid_   -  default to true -- invalidated due to reorg/double-spend
//    isCoinbase -  is the input side a coinbase/generation input
//    isSentToSelf_ - if this is a txOut, did it come from ourself?
//    isChangeBack_ - meaningless:  can't quite figure out how to determine
//                    this unless I do a prescan to determine if all txOuts
//                    are ours, or just some of them
//    isOptInRBF_ - is the sequence number opting into RBF
//    usesWitness - does the input or output use a witness format
//
//  BtcWallet -- Each entry corresponds to ONE WHOLE TRANSACTION
//
//    scrAddr_    -  useless - originally had a purpose, but lost it
//    value_     -  total debit/credit on WALLET balance, in Satoshis (1e-8 BTC)
//    blockNum_  -  block height of the block in which this tx was included
//    txHash_    -  hash of this tx 
//    index_     -  index of the tx in the block
//    isValid_   -  default to true -- invalidated due to reorg/double-spend
//    isCoinbase -  is the input side a coinbase/generation input
//    isSentToSelf_ - if we supplied inputs and rx ALL outputs
//    isChangeBack_ - if we supplied inputs and rx ANY outputs
//    isOptInRBF_ -  is there an input that opts into RBF
//    usesWitness - are the marker and flag for segwit set
//
////////////////////////////////////////////////////////////////////////////////

namespace Armory
{
   enum class ScriptPrefix : uint8_t;

   ////
   namespace Ledgers
   {
      class Context;

      class Entry
      {
      public:
         Entry(const std::string&, int64_t, uint32_t,
            const BinaryData&, uint32_t, uint32_t,
            std::set<BinaryData>&,
            bool, bool, bool,
            bool, bool, bool);

         std::string       getWalletID(void) const;
         int64_t           getValue(void) const;
         uint32_t          getBlockNum(void) const;
         const BinaryData& getTxHash(void) const;
         uint32_t          getIndex(void) const;
         uint32_t          getTxTime(void) const;
         bool              isCoinbase(void) const;
         bool              isSentToSelf(void) const;
         bool              isChangeBack(void) const;
         bool              isOptInRBF(void) const;
         bool              usesWitness(void) const;
         bool              isChainedZC(void) const;

         ScriptPrefix getScriptType(void) const;
         const std::set<BinaryData>& getScrAddrList(void) const;

         bool operator<(const Entry&) const;
         bool operator>(const Entry&) const;
         bool operator==(const Entry&) const;

         void pprint(void);
         void pprintOneLine(void) const;

         static void purgeLedgerMapFromHeight(
            std::map<BinaryData, Entry>&,
            uint32_t);
         static void purgeLedgerVectorFromHeight(
            std::vector<Entry>&,
            uint32_t);

      private:
         std::string ID_; //holds either a scrAddr or a walletId
         int64_t     value_;
         uint32_t    blockNum_;
         BinaryData  txHash_;
         uint32_t    index_; // either a tx index, txout index or txin index
         uint32_t    txTime_ = 0;
         bool        isCoinbase_ = false;
         bool        isSentToSelf_ = false;
         bool        isChangeBack_ = false;
         bool        isOptInRBF_ = false;
         bool        usesWitness_ = false;
         bool        isChainedZC_ = false;

         //for matching scrAddr comments to LedgerEntries on the Python side
         std::set<BinaryData> scrAddrSet_;
      };

      struct DescendingOrder
      {
         bool operator()(const Entry&, const Entry&) const;
      };

      class Delegate
      {
      private:
         const std::function<std::vector<Entry>(uint32_t)> getHistoryPage_;
         const std::function<uint32_t(uint32_t)> getBlockInVicinity_;
         const std::function<uint32_t(uint32_t)> getPageIdForBlockHeight_;
         const std::function<uint32_t(void)> getPageCount_;

      public:
         Delegate(
            std::function<std::vector<Entry>(uint32_t)>,
            std::function<uint32_t(uint32_t)>,
            std::function<uint32_t(uint32_t)>,
            std::function<uint32_t(void)>);

         std::vector<Entry> getHistoryPage(uint32_t) const;
         uint32_t getBlockInVicinity(uint32_t) const;
         uint32_t getPageIdForBlockHeight(uint32_t) const;
         uint32_t getPageCount(void) const;
      };

      std::map<BinaryData, Entry> computeLedgerMap(
         const std::map<BinaryData, TxIOPair>&,
         uint32_t, uint32_t, const std::string&,
         const Context&);
   } //namespace Ledgers
} //namespace Armory
