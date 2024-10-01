////////////////////////////////////////////////////////////////////////////////
//                                                                            //
//  Copyright (C) 2016-2021, goatpig                                          //
//  Distributed under the MIT license                                         //
//  See LICENSE-MIT or https://opensource.org/licenses/MIT                    //
//                                                                            //
////////////////////////////////////////////////////////////////////////////////

#ifndef _H_TRANSACTIONS_
#define _H_TRANSACTIONS_

#include <map>
#include <vector>

#include "BinaryData.h"
#include "EncryptionUtils.h"
#include "BtcUtils.h"
#include "BlockchainDatabase/BlockDataMap.h"
#include "Script.h"
#include "SigHashEnum.h"

namespace Armory
{
   namespace Signing
   {
      class UnsupportedSigHashTypeException : public std::runtime_error
      {
      public:
         UnsupportedSigHashTypeException(const std::string& what) :
            std::runtime_error(what)
         {}
      };

      //////////////////////////////////////////////////////////////////////////
      struct TxInData
      {
         BinaryDataRef outputHash_;
         uint32_t outputIndex_;
         uint32_t sequence_;
      };
      
      //////////////////////////////////////////////////////////////////////////
      class TransactionStub
      {
      protected:
         unsigned flags_ = 0;
         mutable std::shared_ptr<SigHashDataSegWit> sigHashDataObject_ = nullptr;

      public:
         mutable std::map<unsigned, size_t> lastCodeSeparatorMap_;

      public:
         TransactionStub(void)
         {}

         TransactionStub(unsigned flags) :
            flags_(flags)
         {}

         virtual ~TransactionStub(void) = 0;

         virtual BinaryDataRef getSerializedOutputScripts(void) const = 0;
         virtual std::vector<TxInData> getTxInsData(void) const = 0;
         virtual BinaryData getSubScript(unsigned index) const = 0;
         virtual BinaryDataRef getWitnessData(unsigned inputId) const = 0;

         virtual uint32_t getVersion(void) const = 0;
         virtual uint32_t getTxOutCount(void) const = 0;
         virtual uint32_t getLockTime(void) const = 0;

         //sw methods
         virtual BinaryData serializeAllOutpoints(void) const = 0;
         virtual BinaryData serializeAllSequences(void) const = 0;
         virtual BinaryDataRef getOutpoint(unsigned) const = 0;
         virtual uint64_t getOutpointValue(unsigned) const = 0;
         virtual unsigned getTxInSequence(unsigned) const = 0;

         //flags
         unsigned getFlags(void) const { return flags_; }
         void setFlags(unsigned flags) { flags_ = flags; }
         void resetFlags (void) { flags_ = 0; }

         //op_cs
         void setLastOpCodeSeparator(unsigned index, size_t offset) const
         {
            lastCodeSeparatorMap_[index] = offset;
         }

         unsigned getLastCodeSeparatorOffset(unsigned index) const
         {
            auto csIter = lastCodeSeparatorMap_.find(index);
            if (csIter == lastCodeSeparatorMap_.end())
               return 0;

            return csIter->second;
         }
      };

      //////////////////////////////////////////////////////////////////////////
      class SigHashData
      {
         //this class and its children do not return the sighash,
         //rather the data that will yield the hash
      private:
         virtual BinaryData getDataForSigHashAll(const TransactionStub&,
            BinaryDataRef, unsigned) = 0;

      public:
         BinaryData getDataForSigHash(SIGHASH_TYPE, const TransactionStub&,
            BinaryDataRef outputScript, unsigned inputIndex);
         
         std::vector<BinaryDataRef> tokenize(const BinaryData&, uint8_t);
      };

      //////////////////////////////////////////////////////////////////////////
      class SigHashDataLegacy : public SigHashData
      {
      private:
         BinaryData getDataForSigHashAll(const TransactionStub&,
            BinaryDataRef, unsigned);
      };

      //////////////////////////////////////////////////////////////////////////
      class SigHashDataSegWit : public SigHashData
      {
      private:
         bool initialized_ = false;
         BinaryData hashPrevouts_;
         BinaryData hashSequence_;
         BinaryData hashOutputs_;

      private:
         virtual uint32_t getSigHashAll_4Bytes(void) const
         {
            return 1;
         }

      private:
         BinaryData getDataForSigHashAll(const TransactionStub&,
            BinaryDataRef, unsigned);

         void computePreState(const TransactionStub&);
      };

      //////////////////////////////////////////////////////////////////////////
      class TransactionVerifier : public TransactionStub
      {
      public:
         typedef std::map<BinaryData, std::map<unsigned, UTXO>> utxoMap;

      private:
         utxoMap utxos_;
         const BCTX theTx_;

         uint64_t checkOutputs(void) const;
         void checkSigs(void) const;
         void checkSigs_NoCatch(void) const;
         TxInEvalState checkSig(unsigned, StackInterpreter* ptr=nullptr) const;

         mutable TxEvalState txEvalState_;

      protected:
         virtual std::unique_ptr<StackInterpreter>
            getStackInterpreter(unsigned) const;

      public:
         TransactionVerifier(const BCTX& theTx, const utxoMap& utxos) :
            utxos_(utxos), theTx_(theTx)
         {
            if (theTx.usesWitness_)
               setFlags(SCRIPT_VERIFY_SEGWIT);
         }

         TransactionVerifier(
            const BCTX& theTx, const std::vector<UnspentTxOut>& utxoVec) :
            theTx_(theTx)
         {
            for (auto& utxo : utxoVec)
            {
               UTXO new_obj(utxo.getValue(),
                  utxo.getTxHeight(), utxo.getTxtIndex(), utxo.getTxOutIndex(),
                  utxo.getTxHash(), utxo.getScript());
               auto& inner_map = utxos_[utxo.getTxHash()];
               inner_map.insert(
                  std::make_pair(utxo.getTxOutIndex(), std::move(new_obj)));
            }

            if (theTx.usesWitness_)
               setFlags(SCRIPT_VERIFY_SEGWIT);
         }

         TransactionVerifier(
            const BCTX& theTx, const std::vector<UTXO>& utxoVec) :
            theTx_(theTx)
         {
            for (auto& utxo : utxoVec)
            {
               auto& inner_map = utxos_[utxo.getTxHash()];
               inner_map.insert(std::make_pair(utxo.getTxOutIndex(), utxo));
            }

            if (theTx.usesWitness_)
               setFlags(SCRIPT_VERIFY_SEGWIT);
         }

         bool verify(bool noCatch = true, bool strict = true) const;
         TxEvalState evaluateState(bool strict = true) const;

         BinaryDataRef getSerializedOutputScripts(void) const;
         std::vector<TxInData> getTxInsData(void) const;
         BinaryData getSubScript(unsigned index) const;
         BinaryDataRef getWitnessData(unsigned inputId) const;

         uint32_t getVersion(void) const { return theTx_.version_; }
         uint32_t getTxOutCount(void) const { return theTx_.txouts_.size(); }
         uint32_t getLockTime(void) const { return theTx_.lockTime_; }

         //sw
         BinaryData serializeAllOutpoints(void) const;
         BinaryData serializeAllSequences(void) const;
         BinaryDataRef getOutpoint(unsigned) const;
         uint64_t getOutpointValue(unsigned) const;
         unsigned getTxInSequence(unsigned) const;
      };
   }; //namespace Signer
}; //namespace Armory

#endif
