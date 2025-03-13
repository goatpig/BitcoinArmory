////////////////////////////////////////////////////////////////////////////////
//                                                                            //
//  Copyright (C) 2017, goatpig                                               //
//  Distributed under the MIT license                                         //
//  See LICENSE-MIT or https://opensource.org/licenses/MIT                    //
//                                                                            //
////////////////////////////////////////////////////////////////////////////////

#include "CoinSelection.h"
#include <random>
#include "Wallets.h"

using namespace std;
using namespace Armory::Signing;
using namespace Armory::Wallets;
using namespace Armory::CoinSelection;

////////////////////////////////////////////////////////////////////////////////
//                                                                            //
// CoinSelection                                                              //
//                                                                            //
////////////////////////////////////////////////////////////////////////////////
vector<UTXO> CoinSelection::checkForRecipientReuse(
   PaymentStruct& payStruct, const vector<UTXO>& utxoVec)
{
   //look for recipient reuse
   auto getUtxoLambda = getUTXOsForVal_;
   if (utxoVec.size() > 0)
   {
      auto getUtxoLBD = [&utxoVec](uint64_t)->vector<UTXO>
      {
         return utxoVec;
      };

      getUtxoLambda = getUtxoLBD;
   }

   RestrictedUtxoSet r_utxos(getUtxoLambda);
   set<BinaryData> addrSet;
   uint64_t spendSum = 0;

   const auto& recMap = payStruct.getRecipientMap();
   for (const auto& group : recMap)
   {
      for (const auto& recipient : group.second)
      {
         auto& output = recipient->getSerializedScript();
         if (output.getSize() < 9)
            continue;

         BinaryRefReader brr(output.getRef());
         brr.advance(8);
         auto scriptLen = brr.get_var_int();
         auto script = brr.get_BinaryDataRef(scriptLen);

         auto&& scrAddr = BtcUtils::getScrAddrForScript(script);

         auto addrBookIter = addrBook_.find(scrAddr);
         if (addrBookIter == addrBook_.end())
            continue;

         //log recipient
         addrSet.insert(scrAddr);
         spendSum += recipient->getValue();

         //round up utxos
         auto&& txHashVec = addrBookIter->getTxHashList();
         for (auto& txhash : txHashVec)
            r_utxos.filterUtxos(txhash);
      }
   }

   auto available_balance = r_utxos.getBalance();
   auto balance_and_fee = available_balance;
   if (payStruct.fee() > 0)
   {
      balance_and_fee += payStruct.fee();
   }
   else
   {
      balance_and_fee += r_utxos.getFeeSum(payStruct.fee_byte());
      balance_and_fee += uint64_t(payStruct.fee_byte() * payStruct.size());
   }

   if (spendSum > 0 && balance_and_fee < spendSum)
   {
      vector<BinaryData> addrVec;
      for (auto& addr : addrSet)
         addrVec.push_back(addr);
      throw RecipientReuseException(addrVec, spendSum, available_balance);
   }

   return r_utxos.getUtxoSelection();
}

////////////////////////////////////////////////////////////////////////////////
UtxoSelection CoinSelection::getUtxoSelectionForRecipients(
   PaymentStruct& payStruct, const vector<UTXO>& utxoVec)
{
   try
   {
      auto&& utxoSelection = checkForRecipientReuse(payStruct, utxoVec);
      except_ptr_ = nullptr;

      if (utxoSelection.size() > 0)
         return getUtxoSelection(payStruct, utxoSelection);
   }
   catch (...)
   {
      except_ptr_ = current_exception();
   }

   if (utxoVec.size() == 0)
   {
      updateUtxoVector(payStruct.spendVal());
      return getUtxoSelection(payStruct, utxoVec_);
   }
   else
   {
      return getUtxoSelection(payStruct, utxoVec);
   }
}

////////////////////////////////////////////////////////////////////////////////
UtxoSelection CoinSelection::getUtxoSelection(
   PaymentStruct& payStruct, const std::vector<UTXO>& utxoVec)
{
   if (utxoVec.empty()) {
      throw CoinSelectionException("cannot select from empty utxos");
   }

   //sanity check
   auto utxoVecVal = tallyValue(utxoVec);
   if (utxoVecVal < payStruct.spendVal()) {
      throw CoinSelectionException("spend value > usable balance");
   }

   if (topHeight_ == UINT32_MAX) {
      throw CoinSelectionException("uninitialized top height");
   }

   std::vector<UtxoSelection> selections;
   bool useExhaustiveList = payStruct.flags() & USE_FULL_CUSTOM_LIST;
   if (!useExhaustiveList) {
      uint64_t compiledFee_oneOutput = payStruct.fee();
      uint64_t compiledFee_manyOutputs = payStruct.fee();
      if (payStruct.fee() == 0 && payStruct.fee_byte() > 0.0f) {
         //no flat fee but a fee_byte is available

         //1 uncompressed p2pkh input + txoutSizeByte + 1 change output
         compiledFee_oneOutput = float(215 + payStruct.size()) * payStruct.fee_byte();

         //figure out average txin count
         float valPct = float(payStruct.spendVal()) / float(utxoVecVal);
         if (valPct > 1.0f) {
            valPct = 1.0f;
         }
         auto averageTxInCount = unsigned(valPct * float(utxoVec.size()));

         //medianTxInCount p2pkh inputs + txoutSizeByte + 1 change output
         compiledFee_manyOutputs = 10 +
            float(averageTxInCount * 180 + 35 + payStruct.size()) * payStruct.fee_byte();
      }

      //create deterministic selections
      for (unsigned i = 0; i < 8; i++) {
         auto sortedVec = CoinSorting::sortCoins(utxoVec, topHeight_, i);

         //one utxo, single val
         auto utxos1 = CoinSubSelection::selectOneUtxo_SingleSpendVal(
            sortedVec, payStruct.spendVal(), compiledFee_oneOutput);
         if (!utxos1.empty()) {
            selections.emplace_back(UtxoSelection{utxos1});
         }

         //one utxo, double val
         auto utxos2 = CoinSubSelection::selectOneUtxo_DoubleSpendVal(
            sortedVec, payStruct.spendVal(), compiledFee_oneOutput);
         if (!utxos2.empty()) {
            selections.emplace_back(UtxoSelection{utxos2});
         }

         //many utxos, single val
         auto utxos3 = CoinSubSelection::selectManyUtxo_SingleSpendVal(
            sortedVec, payStruct.spendVal(), compiledFee_manyOutputs);
         if (!utxos3.empty()) {
            selections.emplace_back(UtxoSelection{utxos3});
         }

         //many utxos, double val
         auto utxos4 = CoinSubSelection::selectManyUtxo_DoubleSpendVal(
            sortedVec, payStruct.spendVal(), compiledFee_manyOutputs);
         if (!utxos4.empty()) {
            selections.emplace_back(UtxoSelection{utxos4});
         }
      }

      //create random selections
      for (unsigned i = 8; i < 10; i++) {
         for (unsigned y = 0; y < RANDOM_ITER_COUNT; y++) {
            auto sortedVec = CoinSorting::sortCoins(utxoVec, topHeight_, i);

            //many utxo, single val
            auto utxos5 = CoinSubSelection::selectManyUtxo_SingleSpendVal(
               sortedVec, payStruct.spendVal(), compiledFee_manyOutputs);
            if (!utxos5.empty()) {
               selections.emplace_back(UtxoSelection{utxos5});
            }

            //many utxos, double val
            auto utxos6 = CoinSubSelection::selectManyUtxo_DoubleSpendVal(
               sortedVec, payStruct.spendVal(), compiledFee_manyOutputs);
            if (utxos6.empty()) {
               selections.emplace_back(UtxoSelection{utxos6});
            }
         }
      }
   } else {
      auto copyUtxoVec = utxoVec;
      selections.emplace_back(UtxoSelection{copyUtxoVec});
   }

   //score them, pick top one
   float topScore = 0.0f;
   UtxoSelection* selectPtr = nullptr;
   for (auto& selection : selections) {
      try {
         auto score = SelectionScoring::computeScore(
            selection, payStruct, topHeight_);
         if (score > topScore || selectPtr == nullptr) {
            topScore = score;
            selectPtr = &selection;
         }
      } catch (const std::exception&) {
         continue;
      }
   }

   //sanity check
   if (selectPtr == nullptr) {
      throw CoinSelectionException("failed to select utxos");
   }

   //consolidate in case our selection hits addresses with several utxos
   fleshOutSelection(utxoVec, *selectPtr, payStruct);

   //one last shuffle for the good measure
   bool shuffle = payStruct.flags() & SHUFFLE_ENTRIES;
   if (shuffle) {
      selectPtr->shuffle();
   }
   return *selectPtr;
}

////////////////////////////////////////////////////////////////////////////////
void CoinSelection::updateUtxoVector(uint64_t value)
{
   if (utxoVecValue_ >= value)
      return;

   utxoVec_ = move(getUTXOsForVal_(value));
   utxoVecValue_ = tallyValue(utxoVec_);

   if (utxoVecValue_ < value)
      throw CoinSelectionException("could not fetch enough utxos");
}

////////////////////////////////////////////////////////////////////////////////
uint64_t CoinSelection::tallyValue(const vector<UTXO>& utxoVec)
{
   uint64_t val = 0;
   for (auto& utxo : utxoVec)
      val += utxo.getValue();

   return val;
}

////////////////////////////////////////////////////////////////////////////////
uint64_t CoinSelection::getFeeForMaxVal(
   size_t txOutSize, float fee_byte,
   const vector<UTXO>& coinControlVec)
{

   //version, locktime, txin & txout count + outputs size
   size_t txSize = 10 + txOutSize;
   size_t witnessSize = 0;

   const vector<UTXO>* utxoVecPtr = &coinControlVec;

   if (coinControlVec.size() == 0)
   {
      updateUtxoVector(spendableValue_);
      utxoVecPtr = &utxoVec_;
   }

   for (auto& utxo : *utxoVecPtr)
   {
      txSize += utxo.getInputRedeemSize();
      if (utxo.isSegWit())
         witnessSize += utxo.getWitnessDataSize();
   }

   if (witnessSize != 0)
   {
      txSize += 2;
      if (coinControlVec.size() == 0)
         txSize += utxoVec_.size();
      else
         txSize += coinControlVec.size();
   }

   uint64_t fee = uint64_t(fee_byte * float(txSize));
   fee += uint64_t(float(witnessSize) * 0.25f * fee_byte);
   return fee;
}

////////////////////////////////////////////////////////////////////////////////
void CoinSelection::fleshOutSelection(const vector<UTXO>& utxoVec,
   UtxoSelection& utxoSelect, PaymentStruct& payStruct)
{
   //TODO: this is specialized for fee_byte, add a flat fee spec as well

   auto newOutputCount = payStruct.getRecipientCount();
   if (utxoSelect.hasChange_)
      ++newOutputCount;

   if (newOutputCount <= utxoSelect.utxoVec_.size())
      return;

   //we are creating more outputs than inputs, let's try to even things out
   set<const UTXO*> utxoSet;

   //look for outputs with the same script as those we are already consuming
   for (auto& utxo : utxoVec)
   {
      //no zc
      if (utxo.getNumConfirm(topHeight_) == 0)
         continue;

      for (auto& selected : utxoSelect.utxoVec_)
      {
         if (utxo == selected)
            continue;

         if (utxo.getScript() != selected.getScript())
            continue;

         utxoSet.insert(&utxo);
      }
   }

   if (utxoSet.size() == 0)
      return;

   //sort by fee * value, ascending
   struct FeeValScore
   {
      const UTXO* utxo_;
      const uint64_t fee_;
      const uint64_t score_;
      const unsigned order_;
      size_t size_;

      FeeValScore(const UTXO* utxo, float fee_byte, unsigned i) :
         utxo_(utxo), fee_(getFee(fee_byte)), score_(utxo->value_*fee_), order_(i)
      {}

      bool operator<(const FeeValScore& rhs) const
      {
         if (score_ != rhs.score_)
            return score_ < rhs.score_;

         return order_ < rhs.order_;
      }

      uint64_t getFee(float fee_byte)
      {
         if (utxo_ == nullptr)
            throw CoinSelectionException("null utxo ptr");

         size_ = utxo_->getInputRedeemSize() + 1;
         if (utxo_->isSegWit())
            size_ += utxo_->getWitnessDataSize() + 1;

         auto fee = uint64_t(float(utxo_->getInputRedeemSize()) * fee_byte);
         if (utxo_->isSegWit())
            fee += uint64_t(float(utxo_->getWitnessDataSize()) * 0.25f * fee_byte);

         return fee;
      }
   };

   set<FeeValScore> feeValSet;

   auto setIter = utxoSet.begin();
   for (unsigned i = 0; i < utxoSet.size(); i++)
   {
      feeValSet.insert(FeeValScore(*setIter, utxoSelect.fee_byte_, i));
      ++setIter;
   }

   //do not let fee climb by more than 20%, but with at least 1 added input
   uint64_t extra_fee = 0;
   for (auto& feeValScore : feeValSet)
   {
      auto diffPct = float(extra_fee) / float(utxoSelect.fee_);
      if (diffPct >= 0.20f)
         break;

      utxoSelect.utxoVec_.push_back(*feeValScore.utxo_);
      extra_fee += utxoSelect.fee_;
   }

   utxoSelect.computeSizeAndFee(payStruct);
}

////////////////////////////////////////////////////////////////////////////////
//                                                                            //
// CoinSorting                                                                //
//                                                                            //
////////////////////////////////////////////////////////////////////////////////
set<CoinSorting::ScoredUtxo_Float> CoinSorting::ruleset_1(
   const vector<UTXO>& utxoVec, unsigned topHeight)
{
   float one_third = 1.0f / 3.0f;

   set<ScoredUtxo_Float> sufSet;

   unsigned i = 0;
   for (auto& utxo : utxoVec)
   {
      auto nConf = utxo.getNumConfirm(topHeight);
      float priority = float(nConf * utxo.getValue());
      float finalVal = pow(priority, one_third);

      ScoredUtxo_Float suf(&utxo, finalVal, i++);
      sufSet.insert(move(suf));
   }

   return sufSet;
}

////////////////////////////////////////////////////////////////////////////////
vector<UTXO> CoinSorting::sortCoins(
   const vector<UTXO>& utxoVec, unsigned topHeight, unsigned ruleset)
{
   vector<UTXO> finalVec;
   
   if (utxoVec.size() == 0)
      return finalVec;

   switch (ruleset)
   {
   case 0:
   {
      set<ScoredUtxo_Unsigned> suuSet;

      unsigned i = 0;
      for (auto& utxo : utxoVec)
      {
         auto nConf = utxo.getNumConfirm(topHeight);
         ScoredUtxo_Unsigned suu(&utxo, nConf, i++);
         
         suuSet.insert(move(suu));
      }

      for (auto& suu : suuSet)
         finalVec.push_back(*suu.utxo_);

      break;
   }

   case 1:
   {
      auto&& sufSet = ruleset_1(utxoVec, topHeight);
      for (auto& suf : sufSet)
         finalVec.push_back(*suf.utxo_);

      break;
   }

   case 2:
   {
      set<ScoredUtxo_Float> sufSet;

      unsigned i = 0;
      for (auto& utxo : utxoVec)
      {
         auto nConf = utxo.getNumConfirm(topHeight);
         float priority = float(nConf * utxo.getValue() + 1);
         float logVal = log(priority) + 4;
         float finalVal = pow(logVal, 4);

         ScoredUtxo_Float suf(&utxo, finalVal, i++);
         sufSet.insert(move(suf));
      }

      for (auto& suf : sufSet)
         finalVec.push_back(*suf.utxo_);

      break;
   }

   case 3:
   {
      set<ScoredUtxo_Unsigned> suuSet;

      unsigned i = 0;
      for (auto& utxo : utxoVec)
      {
         auto nConf = utxo.getNumConfirm(topHeight);
         if (nConf == 0)
            continue;

         ScoredUtxo_Unsigned suu(&utxo, nConf, i++);

         suuSet.insert(move(suu));
      }

      for (auto& suu : suuSet)
         finalVec.push_back(*suu.utxo_);

      break;
   }

   case 4:
   {
      map<BinaryData, vector<UTXO>> addrUtxoMap;
      vector<const UTXO*> zcVec;

      //sort utxos by address, ignore ZC
      for (auto& utxo : utxoVec)
      {
         auto nConf = utxo.getNumConfirm(topHeight);
         if (nConf == 0)
         {
            zcVec.push_back(&utxo);
            continue;
         }

         auto&& addr = utxo.getRecipientScrAddr();

         auto vecIter = addrUtxoMap.find(addr);
         if (vecIter == addrUtxoMap.end())
            vecIter = addrUtxoMap.insert(
                        make_pair(move(addr), vector<UTXO>())).first;
         
         auto& utxoVec = vecIter->second;
         utxoVec.push_back(utxo);
      }

      //compute rule 1 score for each address vector, then sort by single highest utxo score
      set<ScoredUtxoVector_Float> suvfSet;

      unsigned i = 0;
      for (auto& utxoV : addrUtxoMap)
      {
         auto&& sufSet = ruleset_1(utxoV.second, topHeight);

         vector<UTXO> scoredUtxoVector;
         for (auto& suf : sufSet)
            scoredUtxoVector.push_back(*suf.utxo_);

         auto score = sufSet.begin()->score_;

         ScoredUtxoVector_Float suvf(move(scoredUtxoVector), score, i++);
         suvfSet.insert(move(suvf));
      }

      //expand result in vector
      for (auto& suvf : suvfSet)
      {
         finalVec.insert(finalVec.end(),
            suvf.utxoVec_.begin(), suvf.utxoVec_.end());
      }

      //append ZC
      for (auto utxoPtr : zcVec)
         finalVec.push_back(*utxoPtr);

      break;
   }

   case 5:
   case 6:
   case 7:
   {
      if (utxoVec.size() == 1)
      {
         finalVec = utxoVec;
         break;
      }

      //apply ruleset_1
      auto&& sufSet = ruleset_1(utxoVec, topHeight);
      
      //left rotate * (ruleset - 4)
      auto count = ruleset - 4;
      if (count > sufSet.size())
         count = count % sufSet.size();

      auto iter = sufSet.begin();
      for (unsigned i = 0; i < count; i++)
         iter++;

      while (finalVec.size() < sufSet.size())
      {
         if (iter == sufSet.end())
            iter = sufSet.begin();

         finalVec.push_back(*iter->utxo_);
         ++iter;
      }

      break;
   }

   case 8:
   {
      vector<const UTXO*> utxos;
      vector<const UTXO*> zcVec;

      for (auto& utxo : utxoVec)
      {
         auto nConf = utxo.getNumConfirm(topHeight);
         if (nConf == 0)
         {
            zcVec.push_back(&utxo);
            continue;
         }

         utxos.push_back(&utxo);
      }

      std::random_device rd;
      std::mt19937 g(rd());
      shuffle(utxos.begin(), utxos.end(), g);

      for (auto utxoPtr : utxos)
         finalVec.push_back(*utxoPtr);

      for (auto utxoPtr : zcVec)
         finalVec.push_back(*utxoPtr);

      break;
   }

   case 9:
   {
      //apply ruleset_1
      auto&& sufSet = ruleset_1(utxoVec, topHeight);
      for (auto& suf : sufSet)
         finalVec.push_back(*suf.utxo_);

      //count utxos - zc
      unsigned count = 0;
      for (auto& utxo : utxoVec)
      {
         if (utxo.getNumConfirm(topHeight) == 0)
            continue;

         ++count;
      }

      unsigned top1 = max(count / 3, unsigned(5));
      unsigned topsz = min(top1, count);

      //random swap 2 entries topsz times
      unsigned bracket = max(count - topsz, unsigned(1));
      for (unsigned i = 0; i < topsz; i++)
      {
         auto v1 = rand() % topsz;
         auto v2 = rand() % bracket;

         if (v1 == v2)
            continue;

         iter_swap(finalVec.begin() + v1, finalVec.begin() + v2);
      }

      break;
   }


   default:
      throw CoinSelectionException("invalid coin sorting ruleset");
   }

   return finalVec;
}

////////////////////////////////////////////////////////////////////////////////
//                                                                            //
// CoinSubSelection                                                           //
//                                                                            //
////////////////////////////////////////////////////////////////////////////////
vector<UTXO> CoinSubSelection::selectOneUtxo_SingleSpendVal(
   const vector<UTXO>& utxoVec, uint64_t spendVal, uint64_t fee)
{
   vector<UTXO> retVec;

   auto target = spendVal + fee;
   uint64_t bestMatch = UINT64_MAX;
   unsigned bestID = 0;

   for (unsigned i = 0; i < utxoVec.size(); i++)
   {
      auto& utxo = utxoVec[i];

      if (utxo.getValue() < target)
         continue;

      auto diff = utxo.getValue() - target;
      if (diff == 0)
      {
         retVec.push_back(utxo);
         return retVec;
      }
      
      if (bestMatch != UINT64_MAX)
      {
         if (bestMatch > DUST && diff > bestMatch)
            continue;
         else if (bestMatch < DUST && diff < bestMatch)
            continue;
      }

      bestMatch = diff;
      bestID = i;
   }

   if (bestMatch != UINT64_MAX)
      retVec.push_back(utxoVec[bestID]);

   return retVec;
}

////////////////////////////////////////////////////////////////////////////////
vector<UTXO> CoinSubSelection::selectManyUtxo_SingleSpendVal(
   const vector<UTXO>& utxoVec, uint64_t spendVal, uint64_t fee)
{
   vector<UTXO> retVec;

   auto target = spendVal + fee;
   unsigned count = 0;
   uint64_t tally = 0;

   for (auto& utxo : utxoVec)
   {
      ++count;
      tally += utxo.getValue();

      if (tally >= target)
         break;
   }

   retVec.insert(retVec.end(), utxoVec.begin(), utxoVec.begin() + count);
   return retVec;
}

////////////////////////////////////////////////////////////////////////////////
vector<UTXO> CoinSubSelection::selectOneUtxo_DoubleSpendVal(
   const vector<UTXO>& utxoVec, uint64_t spendVal, uint64_t fee)
{
   vector<UTXO> retVec;

   int64_t idealTarget = spendVal * 2 + fee;
   uint64_t minTarget = max(uint64_t(0.75f * float(idealTarget)), spendVal + fee);
   uint64_t maxTarget = uint64_t(1.25f * float(idealTarget));

   int64_t bestMatch = INT64_MAX;
   unsigned bestId = 0;

   for (unsigned i = 0; i < utxoVec.size(); i++)
   {
      auto& utxo = utxoVec[i];
      auto value = utxo.getValue();

      if (value >= minTarget && value <= maxTarget)
      {
         auto match = abs(int64_t(value) - idealTarget);
         if (match < bestMatch)
         {
            bestMatch = match;
            bestId = i;
         }
      }
   }

   if (bestMatch != INT64_MAX)
      retVec.push_back(utxoVec[bestId]);
   return retVec;
}

////////////////////////////////////////////////////////////////////////////////
vector<UTXO> CoinSubSelection::selectManyUtxo_DoubleSpendVal(
   const vector<UTXO>& utxoVec, uint64_t spendVal, uint64_t fee)
{
   vector<UTXO> retVec;

   int64_t idealTarget = spendVal * 2;
   uint64_t minTarget = max(uint64_t(0.8f * float(idealTarget)), spendVal + fee);

   int64_t tally = 0;
   unsigned count = 0;

   for (auto& utxo : utxoVec)
   {
      ++count;
      int64_t newtally = tally + utxo.getValue();

      if (newtally < (int64_t)minTarget)
      {
         tally = newtally;
         continue;
      }

      auto currdiff = abs(idealTarget - tally);
      auto newdiff = abs(idealTarget - newtally);

      if (currdiff < newdiff)
         break;

      tally = newtally;
   }

   if (tally > (int64_t)minTarget)
      retVec.insert(retVec.end(), utxoVec.begin(), utxoVec.begin() + count);
   return retVec;
}

////////////////////////////////////////////////////////////////////////////////
//                                                                            //
// SelectionScoring                                                           //
//                                                                            //
////////////////////////////////////////////////////////////////////////////////
float SelectionScoring::computeScore(
   UtxoSelection& utxoSelect, const PaymentStruct& payStruct,
   unsigned topHeight)
{
   if (utxoSelect.utxoVec_.empty()) {
      throw CoinSelectionException("empty utxovec");
   }
   Scores score;
   static float priorityThreshold = ONE_BTC * 144.0f / 250.0f;

   //tally some values
   set<BinaryData> addrSet;
   uint64_t valConf = 0;

   for (auto& utxo : utxoSelect.utxoVec_) {
      auto val = utxo.getValue();
      auto nConf = utxo.getNumConfirm(topHeight);
      valConf += val*nConf;

      if (nConf == 0) {
         score.hasZC_ = 1.0f;
      }
      addrSet.emplace(utxo.getRecipientScrAddr());
   }

   //get tx size
   utxoSelect.computeSizeAndFee(payStruct);

   //compute address score
   score.numAddrFactor_ = 4.0f / pow(float(addrSet.size() + 1), 2);

   //get trailing 0 count for change and spendval
   auto targetVal = payStruct.spendVal() + utxoSelect.fee_;
   auto changeVal = utxoSelect.value_ - targetVal;
   auto changeVal_zeroCount = (int)getTrailingZeroCount(changeVal);
   auto spendVal_zeroCount = (int)getTrailingZeroCount(payStruct.spendVal());

   //compute outAnonFactor
   if (changeVal == 0) {
      score.outAnonFactor_ = 1.0f;
   } else {
      int zeroDiff = spendVal_zeroCount - changeVal_zeroCount;

      if (zeroDiff == 2) {
         score.outAnonFactor_ = 0.2f;
      } else if (zeroDiff == 1) {
         score.outAnonFactor_ = 0.7f;
      } else if (zeroDiff < 1) {
         score.outAnonFactor_ = float(abs(zeroDiff) + 1);
      }
   }

   if (score.outAnonFactor_ > 0 && changeVal != 0) {
      auto outValDiff = abs(int64_t(changeVal - targetVal));
      float diffPct = float(outValDiff) / max(changeVal, targetVal);
      if (diffPct < 0.2f) {
         score.outAnonFactor_ *= 1.0f;
      } else if (diffPct < 0.5f) {
         score.outAnonFactor_ *= 0.7f;
      } else if (diffPct < 1.0f) {
         score.outAnonFactor_ *= 0.3f;
      } else {
         score.outAnonFactor_ = 0;
      }
   }

   //compute input priority
   if (score.hasZC_ != 0.0f) {
      float fPriority = float(valConf)/float(utxoSelect.size_);

      if (fPriority < priorityThreshold) {
         score.priorityFactor_ = 0.0f;
      } else if (fPriority < 10.0f*priorityThreshold) {
         score.priorityFactor_ = 0.7f;
      } else if (fPriority < 100.0f*priorityThreshold) {
         score.priorityFactor_ = 0.9f;
      } else {
         score.priorityFactor_ = 1.0f;
      }
   }

   //compute tx size factor
   unsigned numKb = utxoSelect.size_ / 1024;
   if (numKb < 1) {
      score.txSizeFactor_ = 1.0f;
   } else if (numKb < 2) {
      score.txSizeFactor_ = 0.2f;
   } else if (numKb < 3) {
      score.txSizeFactor_ = 0.1f;
   } else {
      score.txSizeFactor_ = -1.0f;
   }

   return score.compileValue();
}

////////////////////////////////////////////////////////////////////////////////
unsigned SelectionScoring::getTrailingZeroCount(uint64_t val)
{
   if (val == 0) {
      return 0;
   }
   unsigned i = 10;
   unsigned count = 0;
   while (true) {
      if (val % i != 0) {
         break;
      }
      i *= 10;
      count++;
   }
   return count;
};

////////////////////////////////////////////////////////////////////////////////
//                                                                            //
// UtxoSelection                                                              //
//                                                                            //
////////////////////////////////////////////////////////////////////////////////
void UtxoSelection::computeSizeAndFee(
   const PaymentStruct& payStruct)
{
   //txin and witness sizes
   value_ = 0;
   witnessSize_ = 0;
   size_t txInSize = 0;
   bool sw = false;

   for (const auto& utxo : utxoVec_) {
      value_ += utxo.getValue();
      txInSize += utxo.getInputRedeemSize();

      if (!utxo.isSegWit()) {
         continue;
      }
      witnessSize_ += utxo.getWitnessDataSize();
      sw = true;
   }

   auto txOutSize = payStruct.size();

   //version + locktime + txin count + txout count + txinSize + txoutSize
   unsigned txSize = 10 + txInSize + txOutSize;
   if (sw) {
      //witness data size + 1 varint per utxo + flag & marker
      txSize += witnessSize_ + utxoVec_.size() + 2;
   }

   bool forcedFee = false;
   uint64_t compiled_fee = payStruct.fee();
   if (compiled_fee != 0) {
      fee_byte_ = float(compiled_fee) / float(txSize - witnessSize_ * 0.75f);
      forcedFee = true;
   } else if (payStruct.fee_byte() > 0.0f) {
      compiled_fee = uint64_t(float(txSize - witnessSize_) * payStruct.fee_byte());
      compiled_fee += uint64_t(float(witnessSize_) * payStruct.fee_byte() * 0.25f);
      fee_byte_ = payStruct.fee_byte();
   }
   fee_ = compiled_fee;
   if (fee_ > value_ + payStruct.spendVal()) {
      throw CoinSelectionException("fee > value");
   }

   //figure out change + sanity check
   uint64_t targetVal = payStruct.spendVal() + fee_;

   uint64_t changeVal = value_ - targetVal;
   if (changeVal < fee_ && !forcedFee) {
      //figure out the fee cost of spending this tiny changeVal
      auto spendChangeValTxFee = uint64_t(fee_byte_ * 225.0f);
      if (changeVal < spendChangeValTxFee * 2) {
         compiled_fee += changeVal;
         changeVal = 0;

         fee_byte_ = float(compiled_fee) / float(txSize - witnessSize_ * 0.75f);
         fee_ = compiled_fee;

         targetVal = payStruct.spendVal() + compiled_fee;
      }
   }

   if (changeVal != 0) {
      //size between p2pkh and p2sh doesn't vary enough to matter
      txOutSize += 35;
      if (!forcedFee) {
         compiled_fee += uint64_t(35 * fee_byte_);
         fee_ = compiled_fee;
      }
      hasChange_ = true;
   }

   if (targetVal > value_) {
      throw CoinSelectionException("targetVal > value");
   }

   size_ = 10 + txOutSize + txInSize;
   if (sw) {
      size_ += 2 + witnessSize_ + utxoVec_.size();
   }
   targetVal = payStruct.spendVal() + fee_;
   changeVal = value_ - targetVal;

   bool adjustFee = payStruct.flags() & ADJUST_FEE;
   if (adjustFee && !forcedFee && changeVal > 0) {
      auto spendVal_ZeroCount =
         (int)SelectionScoring::getTrailingZeroCount(payStruct.spendVal());

      auto change_ZeroCount =
         (int)SelectionScoring::getTrailingZeroCount(changeVal);

      while (true) {
         if (change_ZeroCount >= spendVal_ZeroCount) {
            return;
         }
         unsigned factor = unsigned(pow(10, spendVal_ZeroCount--));
         unsigned value_off = changeVal / factor;
         value_off *= factor;

         unsigned stripped_val = changeVal - value_off;
         auto bumpPct = float(stripped_val) / float(compiled_fee);
         if (bumpPct > 0.10f) {
            continue;
         }
         bumpPct_ = bumpPct;
         fee_ += stripped_val;
         return;
      }
   }
}

////////////////////////////////////////////////////////////////////////////////
void UtxoSelection::shuffle()
{
   if (utxoVec_.size() < 2) {
      return;
   }
   std::random_device rd;
   std::mt19937 g(rd());
   std::shuffle(utxoVec_.begin(), utxoVec_.end(), g);
}

////////////////////////////////////////////////////////////////////////////////
//                                                                            //
// PayementStruct                                                             //
//                                                                            //
////////////////////////////////////////////////////////////////////////////////
void PaymentStruct::init()
{
   if (getRecipientCount() == 0) {
      throw CoinSelectionException("empty recipients map");
   }
   spendVal_ = 0;
   size_ = 0;

   for (const auto& group : recipients_) {
      for (auto& recipient : group.second) {
         auto rcVal = recipient->getValue();
         if (rcVal == 0) {
            auto rc_opreturn =
               std::dynamic_pointer_cast<Recipient_OPRETURN>(recipient);

            if (rc_opreturn == nullptr) {
               throw CoinSelectionException("recipient has null value");
            }
         }

         spendVal_ += rcVal;
         size_ += recipient->getSize();
      }
   }
}

////////////////////////////////////////////////////////////////////////////////
size_t PaymentStruct::getRecipientCount() const
{
   size_t count = 0;
   for (const auto& group : recipients_) {
      count += group.second.size();
   }
   return count;
}

////////////////////////////////////////////////////////////////////////////////
//                                                                            //
// CoinSelectionInstance                                                      //
//                                                                            //
////////////////////////////////////////////////////////////////////////////////
CoinSelectionInstance::CoinSelectionInstance(
   std::shared_ptr<AssetWallet> const walletPtr,
   std::function<std::vector<UTXO>(uint64_t)> getUtxoLbd,
   const vector<AddressBookEntry>& addrBook, 
   uint64_t spendableBalance, unsigned topHeight) :
   cs_(getFetchLambdaFromWallet(walletPtr, getUtxoLbd), addrBook, spendableBalance, topHeight),
   walletPtr_(walletPtr), spendableBalance_(spendableBalance)
{}

////////////////////////////////////////////////////////////////////////////////
function<vector<UTXO>(uint64_t)> CoinSelectionInstance::
getFetchLambdaFromWallet(shared_ptr<AssetWallet> const walletPtr,
   function<vector<UTXO>(uint64_t)> lbd)
{
   if (walletPtr == nullptr)
      throw runtime_error("null wallet ptr");

   auto fetchLbd = [walletPtr, lbd](uint64_t val)->vector<UTXO>
   {
      auto&& vecUtxo = lbd(val);
      decorateUTXOs(walletPtr, vecUtxo);

      return vecUtxo;
   };

   return fetchLbd;
}

////////////////////////////////////////////////////////////////////////////////
void CoinSelectionInstance::decorateUTXOs(
   shared_ptr<AssetWallet> const walletPtr, vector<UTXO>& vecUtxo)
{
   if (walletPtr == nullptr)
      throw runtime_error("nullptr wallet");

   for (auto& utxo : vecUtxo)
   {
      auto&& scrAddr = utxo.getRecipientScrAddr();
      auto& ID = walletPtr->getAssetIDForScrAddr(scrAddr);
      auto addrPtr = walletPtr->getAddressEntryForID(ID.first);

      utxo.txinRedeemSizeBytes_ = 0;
      utxo.witnessDataSizeBytes_ = 0;
      utxo.isInputSW_ = false;

      while (true)
      {
         utxo.txinRedeemSizeBytes_ += addrPtr->getInputSize();

         try
         {
            utxo.witnessDataSizeBytes_ += addrPtr->getWitnessDataSize();
            utxo.isInputSW_ = true;
         }
         catch (runtime_error&)
         {}

         auto addrNested = dynamic_pointer_cast<AddressEntry_Nested>(addrPtr);
         if (addrNested == nullptr)
            break;

         addrPtr = addrNested->getPredecessor();
      }
   }
}

////////////////////////////////////////////////////////////////////////////////
void CoinSelectionInstance::selectUTXOs(vector<UTXO>& vecUtxo,
   uint64_t fee, float fee_byte, unsigned flags)
{
   uint64_t spendableVal = 0;
   for (auto& utxo : vecUtxo)
      spendableVal += utxo.getValue();

   //sanity check
   checkSpendVal(spendableVal);

   //decorate coin control selection
   decorateUTXOs(walletPtr_, vecUtxo);

   state_utxoVec_ = vecUtxo;

   PaymentStruct payStruct(recipients_, fee, fee_byte, flags);
   selection_ = move(
      cs_.getUtxoSelectionForRecipients(payStruct, vecUtxo));
}

////////////////////////////////////////////////////////////////////////////////
bool CoinSelectionInstance::selectUTXOs(uint64_t fee, float fee_byte,
   unsigned flags)
{
   try {
      //sanity check
      checkSpendVal(spendableBalance_);
      state_utxoVec_.clear();

      PaymentStruct payStruct(recipients_, fee, fee_byte, flags);
      selection_ = std::move(
         cs_.getUtxoSelectionForRecipients(payStruct, vector<UTXO>()));
      return true;
   } catch (const CoinSelectionException&) {}
   return false;
}

////////////////////////////////////////////////////////////////////////////////
void CoinSelectionInstance::updateState(
   uint64_t fee, float fee_byte, unsigned flags)
{
   PaymentStruct payStruct(recipients_, fee, fee_byte, flags);
   selection_ = move(
      cs_.getUtxoSelectionForRecipients(payStruct, state_utxoVec_));
}

////////////////////////////////////////////////////////////////////////////////
unsigned CoinSelectionInstance::addRecipient(
   const BinaryData& hash, uint64_t value)
{
   unsigned id = 0;
   if (recipients_.size() != 0)
   {
      auto iter = recipients_.rbegin();
      id = iter->first + 1;
   }

   addRecipient(id, hash, value);
   return id;
}

////////////////////////////////////////////////////////////////////////////////
void CoinSelectionInstance::addRecipient(
   unsigned id, const BinaryData& hash, uint64_t value)
{
   if (hash.empty())
      throw CoinSelectionException("[addRecipient] empty script hash");
   
   vector<shared_ptr<ScriptRecipient>> recVec;
   recVec.emplace_back(createRecipient(hash, value));
   recipients_.emplace(id, move(recVec));
}

////////////////////////////////////////////////////////////////////////////////
void CoinSelectionInstance::addRecipient(
   unsigned id, const string& addrStr, uint64_t value)
{
   vector<shared_ptr<ScriptRecipient>> recVec;
   recVec.emplace_back(createRecipient(addrStr, value));
   recipients_.emplace(id, move(recVec));
}

////////////////////////////////////////////////////////////////////////////////
shared_ptr<ScriptRecipient> CoinSelectionInstance::createRecipient(
   const BinaryData& prefixedHash, uint64_t value)
{
   if (prefixedHash.empty())
      throw ScriptRecipientException("[createRecipient] empty hash");

   shared_ptr<ScriptRecipient> rec;
   uint8_t scrType = *prefixedHash.getPtr();

   const auto p2pkh_byte = Armory::Config::BitcoinSettings::getPubkeyHashPrefix();
   const auto p2sh_byte = Armory::Config::BitcoinSettings::getScriptHashPrefix();

   if (scrType == p2pkh_byte)
   {
      rec = make_shared<Recipient_P2PKH>(
         prefixedHash.getSliceRef(1, prefixedHash.getSize() - 1), value);
   }
   else if (scrType == p2sh_byte)
   {
      rec = make_shared<Recipient_P2SH>(
         prefixedHash.getSliceRef(1, prefixedHash.getSize() - 1), value);
   }
   else if(scrType == SCRIPT_PREFIX_P2WPKH)
   {
      auto&& hashVal = prefixedHash.getSliceCopy(1, prefixedHash.getSize() - 1);
      rec = make_shared<Recipient_P2WPKH>(
         hashVal, value);
   }
   else if (scrType == SCRIPT_PREFIX_P2WSH)
   {
      auto&& hashVal = prefixedHash.getSliceCopy(1, prefixedHash.getSize() - 1);
      rec = make_shared<Recipient_P2WSH>(
         hashVal, value);
   }
   else
   {
      throw ScriptRecipientException("unexpected script type");
   }

   return rec;
}

////////////////////////////////////////////////////////////////////////////////
shared_ptr<ScriptRecipient> CoinSelectionInstance::createRecipient(
   const string& addrStr, uint64_t value)
{
   shared_ptr<ScriptRecipient> rec;

   try
   {
      auto scrAddr = move(BtcUtils::base58toScrAddr(addrStr));
      uint8_t scrType = *scrAddr.getPtr();

      if (scrType == Armory::Config::BitcoinSettings::getPubkeyHashPrefix())
      {
         rec = make_shared<Recipient_P2PKH>(
            scrAddr.getSliceRef(1, scrAddr.getSize() - 1), value);
      }
      else if (scrType == Armory::Config::BitcoinSettings::getScriptHashPrefix())
      {
         rec = make_shared<Recipient_P2SH>(
            scrAddr.getSliceRef(1, scrAddr.getSize() - 1), value);
      }
   }
   catch (const exception&)
   {
      auto scrAddrPair = move(BtcUtils::segWitAddressToScrAddr(addrStr));
      if (scrAddrPair.second != 0)
         throw runtime_error("[createRecipient] unsupported sw version");

      switch (scrAddrPair.first.getSize())
      {
      case 20:
         rec = make_shared<Recipient_P2WPKH>(scrAddrPair.first, value);
         break;

      case 32:
         rec = make_shared<Recipient_P2WSH>(scrAddrPair.first, value);
         break;

      default:
         break;
      }
   }

   if (rec == nullptr)
   {
      throw ScriptRecipientException(
         "[createRecipient] failed to create recipient");
   }

   return rec;
}

////////////////////////////////////////////////////////////////////////////////
void CoinSelectionInstance::updateRecipient(
   unsigned id, const BinaryData& hash, uint64_t value)
{
   recipients_.erase(id);
   addRecipient(id, hash, value);
}

////////////////////////////////////////////////////////////////////////////////
void CoinSelectionInstance::updateRecipient(
   unsigned id, const string& addrStr, uint64_t value)
{
   recipients_.erase(id);
   addRecipient(id, addrStr, value);
}

////////////////////////////////////////////////////////////////////////////////
void CoinSelectionInstance::updateOpReturnRecipient(
   unsigned id, const BinaryData& message)
{
   recipients_.erase(id);

   auto recipient = make_shared<Recipient_OPRETURN>(message);
   auto iter = recipients_.find(id);
   if (iter == recipients_.end())
   {
      LOGERR << "missing op return recipient";
      throw runtime_error("missing op return recipient");
   }
   iter->second.clear();
   iter->second.emplace_back(recipient);
}

////////////////////////////////////////////////////////////////////////////////
void CoinSelectionInstance::removeRecipient(unsigned id)
{
   recipients_.erase(id);
}

////////////////////////////////////////////////////////////////////////////////
void CoinSelectionInstance::resetRecipients()
{
   recipients_.clear();
}

////////////////////////////////////////////////////////////////////////////////
uint64_t CoinSelectionInstance::getSpendVal() const
{
   uint64_t total = 0;
   for (const auto& group : recipients_)
   {
      for (const auto& recipient : group.second)
         total += recipient->getValue();
   }

   return total;
}

////////////////////////////////////////////////////////////////////////////////
void CoinSelectionInstance::checkSpendVal(uint64_t spendableBalance) const
{
   try
   {
      auto total = getSpendVal();
      if (total == 0 || total > spendableBalance)
      {
         throw CoinSelectionException("Invalid spend value");
      }
   }
   catch (const ScriptRecipientException&)
   {
      throw CoinSelectionException("Invalid value in at least one recipient");
   }
}

////////////////////////////////////////////////////////////////////////////////
void CoinSelectionInstance::processCustomUtxoList(
   vector<UTXO>& utxos, uint64_t fee, float fee_byte, unsigned flags)
{
   if (utxos.empty())
      throw CoinSelectionException("empty custom utxo list!");
   
   selectUTXOs(utxos, fee, fee_byte, flags);
}

////////////////////////////////////////////////////////////////////////////////
uint64_t CoinSelectionInstance::getFeeForMaxValUtxoVector(
   const vector<BinaryData>& serializedUtxos, float fee_byte)
{
   auto txoutsize = 0;
   for (const auto& group : recipients_)
   {
      for (const auto& recipient : group.second)
         txoutsize += recipient->getSize();
   }

   vector<UTXO> utxoVec;
   if (serializedUtxos.size() > 0)
   {
      for (auto& rawUtxo : serializedUtxos)
      {
         UTXO utxo;
         utxo.unserialize(rawUtxo);
         utxoVec.push_back(move(utxo));
      }

      //decorate coin control selection
      decorateUTXOs(walletPtr_, utxoVec);
   }

   return cs_.getFeeForMaxVal(txoutsize, fee_byte, utxoVec);
}

////////////////////////////////////////////////////////////////////////////////
uint64_t CoinSelectionInstance::getFeeForMaxVal(float fee_byte)
{
   vector<BinaryData> utxos;
   return getFeeForMaxValUtxoVector(utxos, fee_byte);
}
