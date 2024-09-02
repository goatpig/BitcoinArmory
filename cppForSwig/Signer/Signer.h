////////////////////////////////////////////////////////////////////////////////
//                                                                            //
//  Copyright (C) 2016-2022, goatpig                                          //
//  Distributed under the MIT license                                         //
//  See LICENSE-MIT or https://opensource.org/licenses/MIT                    //
//                                                                            //
////////////////////////////////////////////////////////////////////////////////

#ifndef _H_SIGNER
#define _H_SIGNER

#include <set>

#include "TxEvalState.h"
#include "TxClasses.h"
#include "EncryptionUtils.h"
#include "Transactions.h"
#include "ScriptRecipient.h"
#include "ResolverFeed.h"

#define SCRIPT_SPENDER_VERSION_MAX 2
#define SCRIPT_SPENDER_VERSION_MIN 0
#define DEFAULT_RECIPIENT_GROUP 0xFFFFFFFF

namespace Armory
{
   namespace Signing
   {
      struct Serializer;
      struct Deserializer;

      //////////////////////////////////////////////////////////////////////////
      class SignerDeserializationError : public std::runtime_error
      {
      public:
         SignerDeserializationError(const std::string& e) :
            std::runtime_error(e)
         {}
      };

      ////
      class SpenderException : public std::runtime_error
      {
      public:
         SpenderException(const std::string& e) :
            std::runtime_error(e)
         {}
      };

      ////
      class PSBTDeserializationError : public std::runtime_error
      {
      public:
         PSBTDeserializationError (const std::string& e) :
            std::runtime_error(e)
         {}
      };

      //////////////////////////////////////////////////////////////////////////
      enum class SpenderStatus
      {
         //Not parsed yet/failed to parse entirely. This is
         //an invalid state
         Unknown = 0,

         //As the name suggests. This is a valid state
         Empty,

         //All public data has been resolved. This is a valid state
         Resolved,

         //Resolved & partially signed (only applies to multisig scripts)
         //This is an invalid state
         PartiallySigned,

         //Resolved & signed. This is a valid state
         Signed
      };

      enum class SignerStringFormat
      {
         Unknown = 0,
         TxSigCollect_Modern,
         TxSigCollect_Legacy,
         PSBT
      };

      //////////////////////////////////////////////////////////////////////////
      class ScriptSpender
      {
         friend class Signer;
         friend struct Serializer;
         friend struct Deserializer;

      private:
         SpenderStatus segwitStatus_ = SpenderStatus::Unknown;
         BinaryData finalWitnessData_;
         BinaryData finalInputScript_;

         mutable BinaryData serializedInput_;

         SpenderStatus legacyStatus_ = SpenderStatus::Unknown;
         bool isP2SH_ = false;
         bool isCSV_ = false;
         bool isCLTV_ = false;

         unsigned sequence_ = UINT32_MAX;
         mutable BinaryData outpoint_;

         //
         std::map<unsigned, std::shared_ptr<StackItem>> legacyStack_;
         std::map<unsigned, std::shared_ptr<StackItem>> witnessStack_;

         SIGHASH_TYPE sigHashType_ = SIGHASH_ALL;
         BinaryData getSerializedOutpoint(void) const;

         std::shared_ptr<std::map<BinaryData, Tx>> txMap_;
         std::map<BinaryData, BIP32_AssetPath> bip32Paths_;

         std::map<BinaryData, BinaryData> prioprietaryPSBTData_;

      protected:
         mutable UTXO utxo_;

      private:
         static BinaryData serializeScript(
            const std::vector<std::shared_ptr<StackItem>>& stack, bool no_throw=false);
         static BinaryData serializeWitnessData(
            const std::vector<std::shared_ptr<StackItem>>& stack,
            unsigned& itemCount, bool no_throw=false);

         bool compareEvalState(const ScriptSpender&) const;
         BinaryData getAvailableInputScript(void) const;

         void parseScripts(StackResolver&);
         void processStacks();

         void updateStack(std::map<unsigned, std::shared_ptr<StackItem>>&,
            const std::vector<std::shared_ptr<StackItem>>&);
         void updateLegacyStack(const std::vector<std::shared_ptr<StackItem>>&);
         void updateWitnessStack(const std::vector<std::shared_ptr<StackItem>>&);

         BinaryDataRef getRedeemScriptFromStack(
            const std::map<unsigned, std::shared_ptr<StackItem>>*) const;
         std::map<BinaryData, BinaryData> getPartialSigs(void) const;

      private:
         ScriptSpender(void)
         {}

         void setUtxo(const UTXO& utxo) { utxo_ = utxo; }
         void merge(const ScriptSpender&);

      public:
         ScriptSpender(const BinaryDataRef txHash, unsigned index)
         {
            BinaryWriter bw;
            bw.put_BinaryDataRef(txHash);
            bw.put_uint32_t(index);

            outpoint_ = bw.getData();
         }

         ScriptSpender(const UTXO& utxo) :
            utxo_(utxo)
         {}

         ScriptSpender(const ScriptSpender& ss)
         {
            outpoint_ = ss.getOutpoint();
            sequence_ = ss.sequence_;
            merge(ss);
         }

         virtual ~ScriptSpender() = default;

         bool isP2SH(void) const { return isP2SH_; }
         bool isSegWit(void) const;

         //set
         void setSigHashType(SIGHASH_TYPE sht) { sigHashType_ = sht; }
         void setSequence(unsigned s) { sequence_ = s; }
         void setWitnessData(const std::vector<std::shared_ptr<StackItem>>&);
         void flagP2SH(bool flag) { isP2SH_ = flag; }

         //get
         SIGHASH_TYPE getSigHashType(void) const { return sigHashType_; }
         unsigned getSequence(void) const { return sequence_; }
         BinaryDataRef getOutputScript(void) const;
         BinaryDataRef getOutputHash(void) const;
         unsigned getOutputIndex(void) const;
         BinaryData getSerializedInput(bool, bool) const;
         BinaryData getEmptySerializedInput(void) const;
         BinaryDataRef getFinalizedWitnessData(void) const;
         BinaryData serializeAvailableWitnessData(void) const;
         BinaryDataRef getOutpoint(void) const;
         uint64_t getValue(void) const;

         const UTXO& getUtxo(void) const;

         unsigned getFlags(void) const
         {
            unsigned flags = SCRIPT_VERIFY_SEGWIT;
            if (isP2SH_)
               flags |= SCRIPT_VERIFY_P2SH | SCRIPT_VERIFY_P2SH_SHA256;

            if (isCSV_)
               flags |= SCRIPT_VERIFY_CHECKSEQUENCEVERIFY;

            if (isCLTV_)
               flags |= SCRIPT_VERIFY_CHECKLOCKTIMEVERIFY;

            return flags;
         }

         virtual uint8_t getSigHashByte(void) const
         {
            uint8_t hashbyte;
            switch (sigHashType_)
            {
            case SIGHASH_ALL:
               hashbyte = 1;
               break;

            default:
               throw ScriptException("unsupported sighash type");
            }

            return hashbyte;
         }

         bool isResolved(void) const;
         bool isSigned(void) const;
         bool isInitialized(void) const;

         bool canBeResolved(void) const;

         bool operator==(const ScriptSpender& rhs)
         {
            try
            {
               return this->getOutpoint() == rhs.getOutpoint();
            }
            catch (const std::exception&)
            {
               return false;
            }
         }

         bool verifyEvalState(unsigned);
         void injectSignature(SecureBinaryData&, unsigned sigId = UINT32_MAX);
         void seedResolver(std::shared_ptr<ResolverFeed>, bool) const;
         void sign(std::shared_ptr<SignerProxy>);

         void toPSBT(BinaryWriter& bw) const;
         static std::shared_ptr<ScriptSpender> fromPSBT(
            BinaryRefReader&, const TxIn&,
            std::shared_ptr<std::map<BinaryData, Tx>>);

         void setTxMap(std::shared_ptr<std::map<BinaryData, Tx>>);
         bool setSupportingTx(BinaryDataRef rawTx);
         bool setSupportingTx(Tx);

         const Tx& getSupportingTx(void) const;
         bool haveSupportingTx(void) const;
         std::map<unsigned, BinaryData> getRelevantPubkeys() const;

         std::map<BinaryData, BIP32_AssetPath>& getBip32Paths(void)
         { return bip32Paths_; }

         //debug
         void prettyPrint(std::ostream&) const;
      };

      //////////////////////////////////////////////////////////////////////////
      class Signer : public TransactionStub
      {
         friend class SignerProxyFromSigner;
         friend struct Deserializer;
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

         std::map<unsigned, std::shared_ptr<BIP32_PublicDerivedRoot>> bip32PublicRoots_;
         std::map<BinaryData, BinaryData> prioprietaryPSBTData_;

      protected:
         virtual std::shared_ptr<SigHashData> getSigHashDataForSpender(bool) const;
         SecureBinaryData signScript(
            BinaryDataRef script,
            const SecureBinaryData& privKey,
            std::shared_ptr<SigHashData>,
            unsigned index);

         static std::unique_ptr<TransactionVerifier> getVerifier(
            std::shared_ptr<BCTX>,
            std::map<BinaryData, std::map<unsigned, UTXO>>&);

         BinaryData serializeAvailableResolvedData(void) const;

         static Signer createFromState(const std::string&);
         void parseScripts(bool);
         void addBip32Root(std::shared_ptr<BIP32_PublicDerivedRoot>);
         void matchAssetPathsWithRoots(void);

      public:
         Signer(void) :
            TransactionStub(
               SCRIPT_VERIFY_P2SH |
               SCRIPT_VERIFY_SEGWIT |
               SCRIPT_VERIFY_P2SH_SHA256)
         {
            supportingTxMap_ = std::make_shared<std::map<BinaryData, Tx>>();
         }

         /*sigs*/

         //create sigs
         void sign(void);
         void injectSignature(
            unsigned, SecureBinaryData&, unsigned sigId = UINT32_MAX);

         //sighash prestate methods
         BinaryData serializeAllOutpoints(void) const override;
         BinaryData serializeAllSequences(void) const override;
         BinaryDataRef getOutpoint(unsigned) const override;

         //checks sigs
         bool verify(void) const;
         bool verifyRawTx(const BinaryData& rawTx,
            const std::map<BinaryData, std::map<unsigned, BinaryData> >& rawUTXOs);

         TxEvalState evaluateSignedState(void) const;
         static TxEvalState verify(
            const BinaryData&, //raw tx
            std::map<BinaryData, std::map<unsigned, UTXO>>&, //supporting outputs
            unsigned, //flags
            bool strict = true //strict verification (check balances)
            );

         /*script fetching*/

         BinaryDataRef getSerializedOutputScripts(void) const override;
         std::vector<TxInData> getTxInsData(void) const override;
         BinaryData getSubScript(unsigned index) const override;
         BinaryDataRef getWitnessData(unsigned inputId) const override;
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
         BinaryDataRef serializeUnsignedTx(bool loose = false);

         BinaryData getTxId(void);
         BinaryData getTxId_const(void) const;

         //state import/export
         void deserializeState(const BinaryDataRef&);
         void deserializeState_Legacy(const BinaryDataRef&);
         void merge(const Signer& rhs);

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
         bool isInputSW(unsigned inputId) const;
         bool isSegWit(void) const;
         bool hasLegacyInputs (void) const;

         //string state
         SignerStringFormat deserializedFromType(void) const;
         bool canLegacySerialize(void) const;

         /*signer setup*/

         //tx setup
         uint32_t getLockTime(void) const override { return lockTime_; }
         void setLockTime(unsigned locktime) { lockTime_ = locktime; }
         uint32_t getVersion(void) const override { return version_; }
         void setVersion(unsigned version) { version_ = version; }

         //spender setup
         void populateUtxo(const UTXO& utxo);
         void addSpender(std::shared_ptr<ScriptSpender>);
         virtual void addSpender_ByOutpoint(
            const BinaryData& hash, unsigned index, unsigned sequence);

         //recipients
         void addRecipient(std::shared_ptr<ScriptRecipient>);
         void addRecipient(std::shared_ptr<ScriptRecipient>, unsigned);
         std::vector<std::shared_ptr<ScriptRecipient>> getRecipientVector(void) const;
         const RecipientMap& getRecipientMap(void) const { return recipients_; }

         //counts
         uint32_t getTxInCount(void) const { return spenders_.size(); }
         uint32_t getTxOutCount(void) const override;

         //feeds setup
         void setFeed(std::shared_ptr<ResolverFeed> feedPtr) 
         { resolverPtr_ = feedPtr; }
         void resetFeed(void);

         //supporting tx
         void addSupportingTx(BinaryDataRef);
         void addSupportingTx(Tx);
         const Tx& getSupportingTx(const BinaryData&) const;

         //values
         uint64_t getTotalInputsValue(void) const;
         uint64_t getTotalOutputsValue(void) const;

         //resets
         void clearSpenders(void) { spenders_.clear(); }
         void clearRecipients(void) { recipients_.clear(); }
         void clear(void)
         {
            clearSpenders();
            clearRecipients();
            resetFeed();
         }

         //debug
         void prettyPrint(void) const;

         /*
         Message signing: get resolver for wallet holding the private key
         and lock it before calling signMessage. verifyMessageSignature
         can be called anytime.
         */
         static BinaryData signMessage(
            const BinaryData& message,
            const BinaryData& scrAddr,
            std::shared_ptr<ResolverFeed> walletResolver);

         static bool verifyMessageSignature(
            const BinaryData& message,
            const BinaryData& scraddr,
            const BinaryData& sig);
      };

      //////////////////////////////////////////////////////////////////////////
      class SignerProxy
      {
      protected:
         std::function<SecureBinaryData(
            BinaryDataRef, const BinaryData&, bool)> signerLambda_;

      public:
         virtual ~SignerProxy(void) = 0;

         SecureBinaryData sign(
            BinaryDataRef script,
            const BinaryData& pubkey,
            bool sw)
         {
            return std::move(signerLambda_(script, pubkey, sw));
         }
      };

      ////
      class SignerProxyFromSigner : public SignerProxy
      {
      private:
         void setLambda(Signer*,
            std::shared_ptr<ScriptSpender>, unsigned index,
            std::shared_ptr<ResolverFeed>);

      public:
         SignerProxyFromSigner(Signer* signer,
            unsigned index, std::shared_ptr<ResolverFeed> feedPtr)
         {
            auto spender = signer->getSpender(index);
            setLambda(signer, spender, index, feedPtr);
         }
      };

      //////////////////////////////////////////////////////////////////////////
      struct ResolverFeed_SpenderResolutionChecks : public ResolverFeed
      {
         std::map<BinaryData, BinaryData> hashMap;

         BinaryData getByVal(const BinaryData& val) override
         {
            auto iter = hashMap.find(val);
            if (iter == hashMap.end())
               throw std::runtime_error("invalid value");

            return iter->second;
         }

         const SecureBinaryData& getPrivKeyForPubkey(const BinaryData&) override
         {
            throw std::runtime_error("invalid value");
         }

         BIP32_AssetPath resolveBip32PathForPubkey(const BinaryData&) override;
         void setBip32PathForPubkey(
            const BinaryData&, const BIP32_AssetPath&) override;
      };


      //////////////////////////////////////////////////////////////////////////
      namespace PSBT
      {
         ///////////////////////////////////////////////////////////////////////
         enum ENUM_GLOBAL
         {
            PSBT_GLOBAL_UNSIGNED_TX = 0,
            PSBT_GLOBAL_XPUB        = 1,
            PSBT_GLOBAL_VERSION     = 0xfb,
            PSBT_GLOBAL_PROPRIETARY = 0xfc,
            PSBT_GLOBAL_SEPARATOR   = 0xff,
            PSBT_GLOBAL_MAGICWORD   = 0x70736274
         };

         ////
         enum ENUM_INPUT
         {
            PSBT_IN_NON_WITNESS_UTXO      = 0,
            PSBT_IN_WITNESS_UTXO          = 1,
            PSBT_IN_PARTIAL_SIG           = 2,
            PSBT_IN_SIGHASH_TYPE          = 3,
            PSBT_IN_REDEEM_SCRIPT         = 4,
            PSBT_IN_WITNESS_SCRIPT        = 5,
            PSBT_IN_BIP32_DERIVATION      = 6,
            PSBT_IN_FINAL_SCRIPTSIG       = 7,
            PSBT_IN_FINAL_SCRIPTWITNESS   = 8,
            PSBT_IN_POR_COMMITMENT        = 9,
            PSBT_IN_PROPRIETARY           = 0xfc
         };

         ////
         enum ENUM_OUTPUT
         {
            PSBT_OUT_REDEEM_SCRIPT     = 0,
            PSBT_OUT_WITNESS_SCRIPT    = 1,
            PSBT_OUT_BIP32_DERIVATION  = 2,
            PSBT_OUT_PROPRIETARY       = 0xfc
         };

         //exceptions
         class DeserError : std::runtime_error
         {
         public:
            DeserError(const std::string& err) :
               std::runtime_error(err)
            {}
         };

         //ser
         void init(BinaryWriter&);
         void setUnsignedTx(BinaryWriter&, const BinaryData&);
         void setSeparator(BinaryWriter&);
      }; //namespace PSBT
   }; //namespace Signer
}; //namespace Armory

#endif
