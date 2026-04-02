////////////////////////////////////////////////////////////////////////////////
//                                                                            //
//  Copyright (C) 2016-2025, goatpig                                          //
//  Distributed under the MIT license                                         //
//  See LICENSE-MIT or https://opensource.org/licenses/MIT                    //
//                                                                            //
////////////////////////////////////////////////////////////////////////////////

#pragma once

#include <set>
#include <functional>

#include "TxEvalState.h"
#include "TxClasses.h"
#include "Transactions.h"
#include "ScriptRecipient.h"

namespace Armory
{
   namespace Signing
   {
      class ScriptSpender;

      //////////////////////////////////////////////////////////////////////////
      class SignerDeserializationError : public std::runtime_error
      {
      public:
         SignerDeserializationError(const std::string&);
      };

      //////////////////////////////////////////////////////////////////////////
      enum class SignerStringFormat
      {
         Unknown = 0,
         TxSigCollect_Modern,
         TxSigCollect_Legacy,
         PSBT
      };

      //////////////////////////////////////////////////////////////////////////
      class Signer : public TransactionStub
      {
         using RecipientMap =
            std::map<unsigned, std::vector<std::shared_ptr<ScriptRecipient>>>;

      protected:
         unsigned version_ = 1;
         unsigned lockTime_ = 0;
         SignerStringFormat fromType_ = SignerStringFormat::Unknown;

         mutable BinaryData serializedSignedTx_;
         mutable BinaryData serializedUnsignedTx_;
         mutable BinaryData serializedOutputs_;

         std::vector<std::shared_ptr<ScriptSpender>> spenders_;
         RecipientMap recipients_;

         std::shared_ptr<ResolverFeed> resolverPtr_;
         std::shared_ptr<std::map<BinaryData, Tx>> supportingTxMap_;

         std::map<unsigned, std::shared_ptr<BIP32_PublicDerivedRoot>>
            bip32PublicRoots_;
         std::map<BinaryData, BinaryData> prioprietaryPSBTData_;

      protected:
         virtual std::shared_ptr<SigHashData> getSigHashDataForSpender(bool) const;
         SecureBinaryData signScript(
            BinaryDataRef,
            const SecureBinaryData&,
            std::shared_ptr<SigHashData>,
            unsigned);

         static std::unique_ptr<TransactionVerifier> getVerifier(
            std::shared_ptr<BCTX>,
            std::map<BinaryData, std::map<unsigned, UTXO>>&);

         BinaryData serializeAvailableResolvedData(void) const;

         static Signer createFromState(const std::string&);

      public:
         Signer(void);

         /*sigs*/

         //create sigs
         void sign(void);
         void injectSignature(unsigned, SecureBinaryData&, unsigned=UINT32_MAX);

         //sighash prestate methods
         BinaryData serializeAllOutpoints(void) const override;
         BinaryData serializeAllSequences(void) const override;
         BinaryDataRef getOutpoint(unsigned) const override;

         //checks sigs
         bool verify(void) const;
         bool verifyRawTx(const BinaryData&,
            const std::map<BinaryData, std::map<unsigned, BinaryData> >&);

         TxEvalState evaluateSignedState(void) const;
         static TxEvalState verify(
            const BinaryData&, //raw tx
            std::map<BinaryData, std::map<unsigned, UTXO>>&, //supporting outputs
            unsigned, //flags
            bool=true //strict verification (check balances)
         );

         /*script fetching*/

         BinaryDataRef getSerializedOutputScripts(void) const override;
         std::vector<TxInData> getTxInsData(void) const override;
         BinaryData getSubScript(unsigned) const override;
         BinaryDataRef getWitnessData(unsigned) const override;
         static std::map<unsigned, BinaryData> getPubkeysForScript(
            BinaryDataRef&, std::shared_ptr<ResolverFeed>);

         /*spender data getters*/
         std::shared_ptr<ScriptSpender> getSpender(unsigned) const;
         uint64_t getOutpointValue(unsigned) const override;
         unsigned getTxInSequence(unsigned) const override;

         /*recipient data getters*/
         std::shared_ptr<ScriptRecipient> getRecipient(unsigned) const;

         /*ser/deser operations*/

         //serialize tx
         BinaryDataRef serializeSignedTx(void) const;
         BinaryDataRef serializeUnsignedTx(bool=false);

         BinaryData getTxId(void);
         BinaryData getTxId_const(void) const;

         //state import/export
         void deserializeState(const BinaryDataRef&);
         void deserializeState_Legacy(const BinaryDataRef&);
         void merge(const Signer&);

         BinaryData serializeState(void) const;
         BinaryData serializeState_Legacy(void) const;
         std::string getSigCollectID(void) const;

         std::string toString(SignerStringFormat) const;
         static Signer fromString(const std::string&);
         std::string toTxSigCollect(bool) const;

         //PSBT
         BinaryData toPSBT(void) const;
         static Signer fromPSBT(BinaryDataRef);
         static Signer fromPSBT(const std::string&);

         /*signer state*/

         //state resolution
         std::set<unsigned> resolvePublicData(void);
         bool verifySpenderEvalState(void) const;

         //sig state
         bool isResolved(void) const;
         bool isSigned(void) const;

         //sw state
         bool isInputSW(unsigned) const;
         bool isSegWit(void) const;
         bool hasLegacyInputs (void) const;

         //string state
         SignerStringFormat deserializedFromType(void) const;
         bool canLegacySerialize(void) const;

         /*signer setup*/

         //tx setup
         uint32_t getLockTime(void) const override;
         void setLockTime(unsigned);
         uint32_t getVersion(void) const override;
         void setVersion(unsigned);

         //spender setup
         void populateUtxo(const UTXO&);
         void addSpender(std::shared_ptr<ScriptSpender>);
         virtual void addSpender_ByOutpoint(const BinaryData&,
            unsigned, unsigned);

         //recipients
         void addRecipient(std::shared_ptr<ScriptRecipient>);
         void addRecipient(std::shared_ptr<ScriptRecipient>, unsigned);
         std::vector<std::shared_ptr<ScriptRecipient>>
         getRecipientVector(void) const;
         const RecipientMap& getRecipientMap(void) const;

         //bip32 pathing
         void addBip32Root(std::shared_ptr<BIP32_PublicDerivedRoot>);
         void matchAssetPathsWithRoots(void);

         //counts
         uint32_t getTxInCount(void) const;
         uint32_t getTxOutCount(void) const override;

         //feeds setup
         void setFeed(std::shared_ptr<ResolverFeed>);
         void resetFeed(void);

         //supporting tx
         void addSupportingTx(BinaryDataRef);
         void addSupportingTx(Tx);
         const Tx& getSupportingTx(const BinaryData&) const;

         //values
         uint64_t getTotalInputsValue(void) const;
         uint64_t getTotalOutputsValue(void) const;

         //resets
         void clearSpenders(void);
         void clearRecipients(void);
         void clear(void);

         //debug
         void prettyPrint(void) const;
      };

      /*
      Message signing: get resolver for wallet holding the private key
      and lock it before calling signMessage. verifyMessageSignature
      can be called anytime.
      */
      BinaryData signMessage(
         const BinaryData&,
         const BinaryData&,
         std::shared_ptr<ResolverFeed>
      );

      bool verifyMessageSignature(
         const BinaryData&,
         const BinaryData&,
         const BinaryData&
      );
   } //namespace Signing
} //namespace Armory
