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
#include "LedgerEntry.h"

using namespace std;

////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////
//
// LedgerEntry
//
////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////

LedgerEntry LedgerEntry::EmptyLedger_;
map<BinaryData, LedgerEntry> LedgerEntry::EmptyLedgerMap_;
BinaryData LedgerEntry::EmptyID_ = BinaryData(0);

////////////////////////////////////////////////////////////////////////////////
string LedgerEntry::getWalletID(void) const
{
   return ID_;
}

////////////////////////////////////////////////////////////////////////////////
void LedgerEntry::setWalletID(const string& str)
{
   ID_ = str;
}

////////////////////////////////////////////////////////////////////////////////
bool LedgerEntry::operator<(LedgerEntry const & le2) const
{  
   if( blockNum_ != le2.blockNum_)
      return blockNum_ < le2.blockNum_;
   else if( index_ != le2.index_)
      return index_ < le2.index_;
   else
      return false;
}

//////////////////////////////////////////////////////////////////////////////
bool LedgerEntry::operator==(LedgerEntry const & le2) const
{
   //TODO
   //return (blockNum_ == le2.blockNum_ && 
           //index_    == le2.index_ && 
           //txTime_   == le2.txTime_);
   return (blockNum_ == le2.blockNum_ && index_ == le2.index_);
}

//////////////////////////////////////////////////////////////////////////////
void LedgerEntry::pprint(void)
{
   cout << "LedgerEntry: " << endl;
   cout << "   ID      : " << getWalletID() << endl;
   cout << "   Value   : " << getValue()/1e8 << endl;
   cout << "   BlkNum  : " << getBlockNum() << endl;
   cout << "   TxHash  : " << getTxHash().copySwapEndian().toHexStr() << endl;
   cout << "   TxIndex : " << getIndex() << endl;
   cout << "   Coinbase: " << (isCoinbase() ? 1 : 0) << endl;
   cout << "   sentSelf: " << (isSentToSelf() ? 1 : 0) << endl;
   cout << "   isChange: " << (isChangeBack() ? 1 : 0) << endl;
   cout << "   isOptInRBF: " << (isOptInRBF() ? 1 : 0) << endl;
   for (auto& addr : scrAddrSet_)
      cout << "   scrAddr: " << addr.toHexStr() << endl;
   cout << endl;
}

//////////////////////////////////////////////////////////////////////////////
void LedgerEntry::pprintOneLine(void) const
{
   printf("   Addr:%s Tx:%s:%02d   BTC:%0.3f   Blk:%06d\n", 
                           "   ",
                           getTxHash().getSliceCopy(0,8).toHexStr().c_str(),
                           getIndex(),
                           getValue()/1e8,
                           getBlockNum());
}

//////////////////////////////////////////////////////////////////////////////
bool LedgerEntry::operator>(LedgerEntry const & le2) const
{
   if (blockNum_ != le2.blockNum_)
      return blockNum_ > le2.blockNum_;
   else if (index_ != le2.index_)
      return index_ > le2.index_;
   else
      return false;

}

//////////////////////////////////////////////////////////////////////////////
void LedgerEntry::purgeLedgerMapFromHeight(
   map<BinaryData, LedgerEntry>& leMap, 
   uint32_t purgeFrom)
{
   //Remove all entries starting this height, included.
   

   BinaryData cutOffHeight(6);
   auto heightPtr = cutOffHeight.getPtr();

   uint8_t* purgeFromPtr = reinterpret_cast<uint8_t*>(&purgeFrom);
   memset(heightPtr, 0, 6);
   heightPtr[0] = purgeFromPtr[2];
   heightPtr[1] = purgeFromPtr[1];
   heightPtr[2] = purgeFromPtr[0];

   auto cutOffIterPair = leMap.equal_range(cutOffHeight);
   leMap.erase(cutOffIterPair.first, leMap.end());
}

//////////////////////////////////////////////////////////////////////////////
void LedgerEntry::purgeLedgerVectorFromHeight(
  vector<LedgerEntry>& leVec,
  uint32_t purgeFrom)
{
   //Remove all entries starting this height, included.
   uint32_t i = 0;

   sort(leVec.begin(), leVec.end());

   for (const auto& le : leVec)
   {
      if (le.getBlockNum() >= purgeFrom)
         break;

      i++;
   }
   
   leVec.erase(leVec.begin() +i, leVec.end());

}

//////////////////////////////////////////////////////////////////////////////
map<BinaryData, LedgerEntry> LedgerEntry::computeLedgerMap(
   const map<BinaryData, TxIOPair>& txioMap,
   uint32_t startBlock, uint32_t endBlock, const string& ID,
   const LMDBBlockDatabase* db, const Blockchain* bc, 
   const ZeroConfContainer* zc)
{
   map<BinaryData, LedgerEntry> leMap;

   //arrange txios by transaction
   map<BinaryData, deque<const TxIOPair*>> TxnTxIOMap;

   for (const auto& txio : txioMap)
   {
      auto&& txOutDBKey = txio.second.getDBKeyOfOutput().getSliceCopy(0, 6);

      auto& txioVec = TxnTxIOMap[txOutDBKey];
      txioVec.push_back(&txio.second);

      if (txio.second.hasTxIn())
      {
         auto txInDBKey = txio.second.getDBKeyOfInput().getSliceCopy(0, 6);

         auto& _txioVec = TxnTxIOMap[txInDBKey];
         _txioVec.push_back(&txio.second);
      }
   }

   //convert TxIO to ledgers
   for (const auto& txioVec : TxnTxIOMap)
   {
      //reset ledger variables
      BinaryData txHash;

      uint32_t blockNum;
      uint32_t txTime;
      uint16_t txIndex;

      set<BinaryData> scrAddrSet;

      bool isRBF = false;
      bool usesWitness = false;
      bool isChained = false;
      
      //grab iterator
      auto txioIter = txioVec.second.cbegin();

      //get txhash, block, txIndex and txtime
      if (!txioVec.first.startsWith(DBUtils::ZeroConfHeader_))
      {
         blockNum = DBUtils::hgtxToHeight(txioVec.first.getSliceRef(0, 4));
         txIndex = READ_UINT16_BE(txioVec.first.getSliceRef(4, 2));
         txTime = bc->getHeaderByHeight(blockNum, 0xFF)->getTimestamp();

         txHash = db->getTxHashForLdbKey(txioVec.first);
      }
      else
      {
         blockNum = UINT32_MAX;
         txIndex = READ_UINT32_BE(txioVec.first.getSliceRef(2, 4));
         txTime = (*txioIter)->getTxTime();

         auto ss = zc->getSnapshot();
         txHash = ss->getHashForKey(txioVec.first);
      }

      if (blockNum < startBlock || blockNum > endBlock)
         continue;

      bool isCoinbase=false;
      int64_t value=0;
      int64_t valIn=0, valOut=0;
      uint32_t nTxInAreOurs = 0, nTxOutAreOurs = 0;
     
      while (txioIter != txioVec.second.cend())
      {
         const auto& txio = *(*txioIter);
         if (blockNum == UINT32_MAX)
         {
            if (txio.isRBF())
               isRBF = true;
            
            if (txio.getTxTime() > txTime)
               txTime = txio.getTxTime();
         }

         if (txio.getDBKeyOfOutput().startsWith(txioVec.first))
         {
            isCoinbase |= txio.isFromCoinbase();
            valIn += txio.getValue();
            value += txio.getValue();

            nTxOutAreOurs++;
         }

         if (txio.hasTxIn() &&
            txio.getDBKeyOfInput().startsWith(txioVec.first))
         {
            valOut -= txio.getValue();
            value -= txio.getValue();

            nTxInAreOurs++;

            if (txio.isChainedZC())
               isChained = true;
         }

         scrAddrSet.insert(txio.getScrAddr());
         ++txioIter;
      }

      bool isSentToSelf = false;
      bool isChangeBack = false;
      
      if (nTxInAreOurs * nTxOutAreOurs > 0)
      {
         //if some of the txins AND some of the txouts are ours, this could be an STS
         //pull the txn and compare the txin and txout counts

         uint32_t nTxOutInTx = UINT32_MAX;
         if (!txioVec.first.startsWith(DBUtils::ZeroConfHeader_))
         {
            nTxOutInTx = db->getStxoCountForTx(txioVec.first.getSliceRef(0, 6));
         }
         else
         {
            auto ss = zc->getSnapshot();
            auto ptx = ss->getTxByKey(txioVec.first);
            if(ptx != nullptr)
               nTxOutInTx = ptx->outputs_.size();
         }

         if (nTxOutInTx == nTxOutAreOurs)
         {
            value = valIn;
            isSentToSelf = true;
         }
      }
      else if (nTxInAreOurs != 0 && (valIn + valOut) < 0)
         isChangeBack = true;

      LedgerEntry le(ID,
         value,
         blockNum,
         txHash,
         txIndex,
         txTime,
         isCoinbase,
         isSentToSelf,
         isChangeBack,
         isRBF,
         usesWitness,
         isChained);

      /*
      When signing a tx online, the wallet knows the txhash, therefor it can register all
      comments on outgoing addresses under the txhash.

      When the tx is signed offline, there is no guarantee that the txhash will be known
      when the offline tx is crafted. Therefor the comments for each outgoing address are
      registered under that address only. 

      In order for the GUI to be able to resolve outgoing address comments, the ledger entry
      needs to carry those for pay out transactions
      */

      if (value < 0)
      {
         try
         {
            //grab tx by hash
            auto&& payout_tx = db->getFullTxCopy(txioVec.first);
         
            //get scrAddr for each txout
            for (unsigned i=0; i < payout_tx.getNumTxOut(); i++)
            {
               auto&& txout = payout_tx.getTxOutCopy(i);
               scrAddrSet.insert(txout.getScrAddressStr());
            }
         }
         catch (exception&)
         {
            auto ss = zc->getSnapshot();
            auto ptx = ss->getTxByKey(txioVec.first);
            if (ptx == nullptr)
            {
               LOGWARN << "failed to get tx for ledger parsing";
            }
            else
            {
               for (auto& txout : ptx->outputs_)
                  scrAddrSet.insert(txout.scrAddr_);
            }
         }
      }

      le.scrAddrSet_ = move(scrAddrSet);
      leMap[txioVec.first] = le;
   }

   return leMap;
}
