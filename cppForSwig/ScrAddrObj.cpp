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

#include "ScrAddrObj.h"
#include <BlockchainDatabase/lmdb_wrapper.h>
#include <BlockchainDatabase/txio.h>
#include <BlockchainDatabase/StoredBlockObj.h>

using namespace Armory;

////////////////////////////////////////////////////////////////////////////////
// ScrAddrObj Methods
ScrAddrObj::ScrAddrObj(const Types::ScrAddr& addr, Types::ScrAddrId id) :
   scrAddr_(addr), id_(id)
{}

////////
const Types::ScrAddr& ScrAddrObj::getScrAddr() const
{
   return scrAddr_;
}

////////
void ScrAddrObj::updateTxIOCache(
   LMDBBlockDatabase* db, const std::set<Types::BlockId>& invalids,
   Types::BlockId start, Types::BlockId end)
{
   /* NOTE:
      We can't simply clamp TxOut history search to requested range and
      call it a day.
      This is because TxIns are keyed by TxOuts, not by ScrAddr. We cannot
      know which spender to lookup without knowing the TxOuts, yet a TxIn
      can spend from any TxOut, regardless of age. Therefor, filtering TxOuts
      by request range is not enough, we have to check all unspent TxOuts
      every pass.
   */

   //grab txio range
   auto txOutData = db->getTxOutHistoryForScrAddrKey(id_, start, end);

   //gather set of txio keys to check for spenders
   std::vector<Types::TxIOKey> txOutKeys;
   txOutKeys.reserve(txOutData.size() + txioCache_.size());
   for (const auto& txout : txOutData) {
      txOutKeys.emplace_back(txout.first);
   }

   //add in cache entries
   for (auto& txioPair : txioCache_) {
      auto& txio = txioPair.second;
      if (txio.hasTxIn()) {
         //txio has spender
         auto blockId = Types::getBlockIDFromTxKey(txio.getTxIOKeyOfInput());
         if (invalids.find(blockId) != invalids.end()) {
            //spender is invalid, clear it and proceed
            txio.setTxIn(Types::INVALID_TXIO_KEY);
         } else {
            //spender is valid, skip
            continue;
         }
      }

      auto outBlockId = Types::getBlockIDFromTxKey(txio.getTxIOKeyOfOutput());
      if (outBlockId > end) {
         //txout goes over end range, ignore
         continue;
      }

      auto iter = txOutData.find(txioPair.first);
      if (iter != txOutData.end()) {
         //already aware of this key, skip
         continue;
      }

      //add output key to spender lookup
      txOutKeys.emplace_back(txioPair.first);
   }

   //add new TxOuts to cache right away
   for (const auto& txopair : txOutData) {
      //emplace blindly, TxOut keys are unique
      txioCache_.emplace(txopair.first, TxIOPair{
         txopair.first, txopair.second.amount, scrAddr_});
   }

   //lookup spenders
   auto txInKeys = db->getTxInHistoryForTxOutHistory(txOutKeys);

   //update spenders in cache
   for (const auto& keyPair : txInKeys) {
      auto& txio = txioCache_.at(keyPair.first);
      txio.setTxIn(keyPair.second);
   }
}

std::map<Types::TxIOKey, TxIOPair> ScrAddrObj::getTxios(
   LMDBBlockDatabase* db, const std::set<Types::BlockId>& invalids,
   Types::BlockId start, Types::BlockId end)
{
   //we build the reply from the txio cache, so we update that first
   updateTxIOCache(db, invalids, start, end);

   std::map<Types::TxIOKey, TxIOPair> result;
   for (const auto& txioPair : txioCache_) {
      const auto& txio = txioPair.second;
      auto outBlockId = Types::getBlockIDFromTxKey(txio.getTxIOKeyOfOutput());
      if (outBlockId > end) {
         //txout goes over end range, skip
         continue;
      }

      //if we have a spender, check it vs start and end range
      if (txio.hasTxIn()) {
         auto inBlockId = Types::getBlockIDFromTxKey(txio.getTxIOKeyOfInput());
         if (inBlockId >= start && inBlockId <= end) {
            //spender is within range, take txio as is
            result.emplace(txioPair);
            continue;
         }
      }

      //if we got this far, either the txio is unspent or
      //the spender is out of range, check output vs start range
      if (outBlockId < start) {
         continue;
      }

      //only the txout for this txio is eligible
      result.emplace(txioPair.first, TxIOPair{
         txioPair.first, txio.getAmount(), scrAddr_
      });
   }
   return result;
}
