////////////////////////////////////////////////////////////////////////////////
//                                                                            //
//  Copyright (C) 2017, goatpig                                               //
//  Distributed under the MIT license                                         //
//  See LICENSE-MIT or https://opensource.org/licenses/MIT                    //
//                                                                            //
////////////////////////////////////////////////////////////////////////////////

#ifndef _H_COINSELECTION
#define _H_COINSELECTION

#include <stdint.h>
#include <memory>
#include <functional>
#include <vector>
#include <string>
#include <set>

#include "TxClasses.h"
#include "ScriptRecipient.h"

#define DUST 10000
#define RANDOM_ITER_COUNT 10
#define ONE_BTC 100000000.0f

#define WEIGHT_NOZC     1000000.0f
#define WEIGHT_PRIORITY 50.0f
#define WEIGHT_NUMADDR  100000.0f
#define WEIGHT_TXSIZE   100.0f
#define WEIGHT_OUTANON  30.0f

namespace Armory
{
   namespace Wallets
   {
      class AssetWallet;
   };

   namespace CoinSelection
   {
      class CoinSelectionException : public std::runtime_error
      {
      public:
         CoinSelectionException(const std::string& err) : std::runtime_error(err)
         {}
      };

      //////////////////////////////////////////////////////////////////////////
      struct RestrictedUtxoSet
      {
         std::vector<UTXO> allUtxos_;
         bool haveAll_ = false;
         std::set<UTXO> selection_;
         std::function<std::vector<UTXO>(uint64_t val)> getUtxoLbd_;

         RestrictedUtxoSet(std::function<std::vector<UTXO>(uint64_t val)> lbd) :
            getUtxoLbd_(lbd)
         {}

         const std::vector<UTXO>& getAllUtxos(void)
         {
            if (!haveAll_)
            {
               allUtxos_ = getUtxoLbd_(UINT64_MAX);
               haveAll_ = true;
            }

            return allUtxos_;
         }

         void filterUtxos(const BinaryData& txhash)
         {
            getAllUtxos();

            for (auto& utxo : allUtxos_)
            {
               if (utxo.getTxHash() == txhash)
                  selection_.insert(utxo);
            }
         }

         uint64_t getBalance(void) const
         {
            uint64_t bal = 0;
            for (auto& utxo : selection_)
               bal += utxo.getValue();

            return bal;
         }

         uint64_t getFeeSum(float fee_byte)
         {
            uint64_t fee = 0;
            for (auto& utxo : selection_)
            {
               fee += uint64_t(utxo.getInputRedeemSize() * fee_byte);

               try
               {
                  fee += uint64_t(utxo.getWitnessDataSize() * fee_byte);
               }
               catch (std::exception&)
               {
               }
            }

            return fee;
         }

         std::vector<UTXO> getUtxoSelection(void) const
         {
            std::vector<UTXO> utxoVec;

            for (auto& utxo : selection_)
               utxoVec.push_back(utxo);

            return utxoVec;
         }
      };

      //////////////////////////////////////////////////////////////////////////
      class PaymentStruct
      {
         using RecipientMap = const std::map<unsigned,
            std::vector<std::shared_ptr<Signing::ScriptRecipient>>>;

      private:
         const RecipientMap& recipients_;

         const uint64_t fee_;
         const float fee_byte_;

         uint64_t spendVal_;
         size_t size_;

         const uint32_t flags_ = 0;

      public:
         PaymentStruct(const RecipientMap& recipients,
            uint64_t fee, float fee_byte, uint32_t flags) :
            recipients_(recipients), fee_(fee), fee_byte_(fee_byte),
            flags_(flags)
         {
            init();
         }

         void init(void);
         size_t getRecipientCount(void) const;
         const RecipientMap& getRecipientMap(void) const
         { return recipients_; }

         uint64_t fee(void) const { return fee_; }
         float fee_byte(void) const { return fee_byte_; }

         uint64_t spendVal(void) const { return spendVal_; }
         size_t size(void) const { return size_; }
         void addToSize(size_t val) { size_ += val; }

         uint32_t flags(void) const { return flags_; }
      };

      //////////////////////////////////////////////////////////////////////////
      struct UtxoSelection
      {
         std::vector<UTXO> utxoVec_;

         uint64_t value_ = 0;
         uint64_t fee_ = 0;
         float fee_byte_ = 0.0f;

         size_t size_ = 0;
         size_t witnessSize_ = 0;
         float bumpPct_ = 0.0f;

         bool hasChange_ = false;

         UtxoSelection(void)
         {}

         UtxoSelection(std::vector<UTXO>& utxovec) :
            utxoVec_(move(utxovec))
         {}

         void computeSizeAndFee(const PaymentStruct&);
         void shuffle(void);
      };

      //////////////////////////////////////////////////////////////////////////
      class CoinSelection
      {
      private:
         std::vector<UTXO> utxoVec_;
         uint64_t utxoVecValue_ = 0;
         std::function<std::vector<UTXO>(uint64_t val)> getUTXOsForVal_;
         const uint64_t spendableValue_;
         unsigned topHeight_ = UINT32_MAX;

         std::set<AddressBookEntry> addrBook_;

         std::exception_ptr except_ptr_ = nullptr;

      protected:
         UtxoSelection getUtxoSelection(
            PaymentStruct&, const std::vector<UTXO>&);

         void fleshOutSelection(const std::vector<UTXO>&,
            UtxoSelection& utxoSelect,
            PaymentStruct& payStruct);

         void updateUtxoVector(uint64_t value);
         static uint64_t tallyValue(const std::vector<UTXO>&);

         std::vector<UTXO> checkForRecipientReuse(
            PaymentStruct&, const std::vector<UTXO>&);

      public:
         CoinSelection(std::function<std::vector<UTXO>(uint64_t val)> func,
            const std::vector<AddressBookEntry>& addrBook,
            uint64_t spendableValue, uint32_t topHeight) :
            getUTXOsForVal_(func),
            spendableValue_(spendableValue),
            topHeight_(topHeight)
         {
            //for random shuffling
            srand(time(0));
            for (auto& entry : addrBook)
               addrBook_.insert(entry);
         }

         UtxoSelection getUtxoSelectionForRecipients(
            PaymentStruct& payStruct, const std::vector<UTXO>&);

         uint64_t getFeeForMaxVal(
            size_t txOutSize, float fee_byte, const std::vector<UTXO>&);

         void rethrow(void)
         {
            if (except_ptr_ != nullptr)
               std::rethrow_exception(except_ptr_);
         }
      };

      //////////////////////////////////////////////////////////////////////////
      struct CoinSorting
      {
      private:

         ///////////////////////////////////////////////////////////////////////
         struct ScoredUtxo_Unsigned
         {
            const UTXO* utxo_;
            const unsigned score_;
            const unsigned order_;

            ScoredUtxo_Unsigned(const UTXO* utxo, unsigned score,
               unsigned order) :
               utxo_(utxo), score_(score), order_(order)
            {}

            bool operator< (const ScoredUtxo_Unsigned& rhs) const
            {
               if (score_ != rhs.score_)
                  return score_ > rhs.score_; //descending order
               else
                  return order_ < rhs.order_; //respect order of appearnce
            }
         };

         ///////////////////////////////////////////////////////////////////////
         struct ScoredUtxo_Float
         {
            const UTXO* utxo_;
            const float score_;
            const unsigned order_;

            ScoredUtxo_Float(const UTXO* utxo, float score,
               unsigned order) :
               utxo_(utxo), score_(score), order_(order)
            {}

            bool operator< (const ScoredUtxo_Float& rhs) const
            {
               if (score_ != rhs.score_)
                  return score_ > rhs.score_; //descending order
               else
                  return order_ < rhs.order_; //respect order of appearnce
            }
         };

         ///////////////////////////////////////////////////////////////////////
         struct ScoredUtxoVector_Float
         {
            const std::vector<UTXO> utxoVec_;
            const float score_;
            const unsigned order_;

            ScoredUtxoVector_Float(std::vector<UTXO> utxoVec, float score,
               unsigned order) :
               utxoVec_(std::move(utxoVec)), score_(score), order_(order)
            {}

            bool operator< (const ScoredUtxoVector_Float& rhs) const
            {
               if (score_ != rhs.score_)
                  return score_ > rhs.score_; //descending order
               else
                  return order_ < rhs.order_; //respect order of appearnce
            }
         };

      private:

         ///////////////////////////////////////////////////////////////////////
         static std::set<ScoredUtxo_Float> ruleset_1(
            const std::vector<UTXO>&, unsigned);

      public:
         static std::vector<UTXO> sortCoins(
            const std::vector<UTXO>& utxoVec,
            unsigned topHeight, unsigned ruleset);
      };

      //////////////////////////////////////////////////////////////////////////
      struct CoinSubSelection
      {
         //single spendval
         static std::vector<UTXO> selectOneUtxo_SingleSpendVal(
            const std::vector<UTXO>&, uint64_t spendVal, uint64_t fee);

         static std::vector<UTXO> selectManyUtxo_SingleSpendVal(
            const std::vector<UTXO>&, uint64_t spendVal, uint64_t fee);

         //double spendval
         static std::vector<UTXO> selectOneUtxo_DoubleSpendVal(
            const std::vector<UTXO>&, uint64_t spendVal, uint64_t fee);

         static std::vector<UTXO> selectManyUtxo_DoubleSpendVal(
            const std::vector<UTXO>&, uint64_t spendVal, uint64_t fee);
      };

      //////////////////////////////////////////////////////////////////////////
      struct SelectionScoring
      {
         struct Scores
         {
            float hasZC_ = 0.0f;
            float priorityFactor_ = 0.0f;
            float numAddrFactor_ = 0.0f;
            float txSizeFactor_ = 0.0f;
            float outAnonFactor_ = 0.0f;

            float compileValue(void) const
            {
               float finalVal = 0.0f;

               finalVal += hasZC_ * WEIGHT_NOZC;
               finalVal += priorityFactor_ * WEIGHT_PRIORITY;
               finalVal += numAddrFactor_ * WEIGHT_NUMADDR;
               finalVal += txSizeFactor_ * WEIGHT_TXSIZE;
               finalVal += outAnonFactor_ * WEIGHT_OUTANON;

               return finalVal;
            }
         };

         static float computeScore(UtxoSelection&, const PaymentStruct&,
            unsigned topHeight);

         static unsigned getTrailingZeroCount(uint64_t);
      };

      //////////////////////////////////////////////////////////////////////////
      class CoinSelectionInstance
      {
      private:
         CoinSelection cs_;

         using RecipientMap = std::map<unsigned,
            std::vector<std::shared_ptr<Signing::ScriptRecipient>>>;
         
         RecipientMap recipients_;
         UtxoSelection selection_;
         std::shared_ptr<Wallets::AssetWallet> const walletPtr_;

         std::vector<UTXO> state_utxoVec_;
         uint64_t spendableBalance_;

      private:
         static void decorateUTXOs(
            std::shared_ptr<Wallets::AssetWallet> const,
            std::vector<UTXO>&);
         static std::function<std::vector<UTXO>(uint64_t)>
         getFetchLambdaFromWallet(
            std::shared_ptr<Wallets::AssetWallet> const,
            std::function<std::vector<UTXO>(uint64_t)>);

         uint64_t getSpendVal(void) const;
         void checkSpendVal(uint64_t) const;
         void addRecipient(unsigned, const BinaryData&, uint64_t);
         void addRecipient(unsigned, const std::string&, uint64_t);
         void selectUTXOs(std::vector<UTXO>&, uint64_t fee,
            float fee_byte, unsigned flags);

      public:
         CoinSelectionInstance(std::shared_ptr<Wallets::AssetWallet>,
            std::function<std::vector<UTXO>(uint64_t)>,
            const std::vector<AddressBookEntry>& addrBook,
            uint64_t spendableBalance, unsigned topHeight);

         unsigned addRecipient(const BinaryData&, uint64_t);
         void updateRecipient(unsigned, const BinaryData&, uint64_t);
         void updateRecipient(unsigned, const std::string&, uint64_t);
         void updateOpReturnRecipient(unsigned, const BinaryData&);
         void removeRecipient(unsigned);
         void resetRecipients(void);
         const RecipientMap& getRecipients(void) const { return recipients_; }

         bool selectUTXOs(uint64_t fee, float fee_byte, unsigned flags);
         void processCustomUtxoList(
            std::vector<UTXO>& utxos,
            uint64_t fee, float fee_byte,
            unsigned flags);

         void updateState(uint64_t fee, float fee_byte, unsigned flags);

         uint64_t getFeeForMaxValUtxoVector(
            const std::vector<BinaryData>& serializedUtxos, float fee_byte);
         uint64_t getFeeForMaxVal(float fee_byte);

         size_t getSizeEstimate(void) const { return selection_.size_; }
         std::vector<UTXO> getUtxoSelection(void) const
         { return selection_.utxoVec_; }

         uint64_t getFlatFee(void) const { return selection_.fee_; }
         float getFeeByte(void) const { return selection_.fee_byte_; }

         bool isSW(void) const { return selection_.witnessSize_ != 0; }
         void rethrow(void) { cs_.rethrow(); }

         static std::shared_ptr<Signing::ScriptRecipient>
            createRecipient(const BinaryData&, uint64_t);
         static std::shared_ptr<Signing::ScriptRecipient>
            createRecipient(const std::string&, uint64_t);
      };
   }; //namespace CoinSelection
}; //namespace Armory

#endif