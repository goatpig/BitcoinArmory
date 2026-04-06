////////////////////////////////////////////////////////////////////////////////
//                                                                            //
//  Copyright (C) 2016-2025, goatpig                                          //
//  Distributed under the MIT license                                         //
//  See LICENSE-MIT or https://opensource.org/licenses/MIT                    //
//                                                                            //
////////////////////////////////////////////////////////////////////////////////

#pragma once

#include <vector>
#include <string>
#include <map>
#include <memory>
#include <stdexcept>

#include <Utils/SecureBinaryData.h>
#include <TxClasses.h>
#include "SigHashEnum.h"
#include "ResolverFeed.h"

#define SCRIPT_SPENDER_VERSION_MAX 2
#define SCRIPT_SPENDER_VERSION_MIN 0
#define DEFAULT_RECIPIENT_GROUP 0xFFFFFFFF

namespace Armory
{
   namespace Signing
   {
      class StackResolver;
      class StackItem;
      using SignerFunc = std::function<SecureBinaryData(
         BinaryDataRef, const BinaryData&, bool)>;

      //////////////////////////////////////////////////////////////////////////
      class SpenderException : public std::runtime_error
      {
      public:
         SpenderException(const std::string&);
      };

      //////////////////////////////////////////////////////////////////////////
      enum class SpenderStatus : int
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

      ////////
      struct ResolverFeed_SpenderResolutionChecks : public ResolverFeed
      {
         std::map<BinaryData, BinaryData> hashMap;

         BinaryData getByVal(const BinaryData&) override;
         const SecureBinaryData& getPrivKeyForPubkey(const BinaryData&) override;
         BIP32_AssetPath resolveBip32PathForPubkey(const BinaryData&) override;
         void setBip32PathForPubkey(
            const BinaryData&, const BIP32_AssetPath&) override;
      };

      //////////////////////////////////////////////////////////////////////////
      struct KeyAndSig
      {
         BinaryData        pubkey;
         SecureBinaryData  sig;
      };

      ////////
      class ScriptSpender
      {
         using StackItemMap = std::map<unsigned, std::shared_ptr<StackItem>>;
         using StackItemVec = std::vector<std::shared_ptr<StackItem>>;

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
         StackItemMap legacyStack_;
         StackItemMap witnessStack_;
         SIGHASH_TYPE sigHashType_ = SIGHASH_ALL;

         std::shared_ptr<std::map<BinaryData, Tx>> txMap_;
         std::map<BinaryData, BIP32_AssetPath> bip32Paths_;
         std::map<BinaryData, BinaryData> prioprietaryPSBTData_;

      protected:
         mutable UTXO utxo_;

      private:
         ScriptSpender(void);

         static BinaryData serializeScript(const StackItemVec&, bool=false);
         static BinaryData serializeWitnessData(const StackItemVec&,
            unsigned&, bool=false);

         bool compareEvalState(const ScriptSpender&) const;
         BinaryData getAvailableInputScript(void) const;
         BinaryData getSerializedOutpoint(void) const;

         void processStacks();

         void updateStack(StackItemMap&, const StackItemVec&);
         void updateLegacyStack(const StackItemVec&);
         void updateWitnessStack(const StackItemVec&);
         std::map<BinaryData, BinaryData> getPartialSigs(void) const;

      public:
         ScriptSpender(const BinaryDataRef, unsigned);
         ScriptSpender(const UTXO&);
         ScriptSpender(const ScriptSpender&);
         ~ScriptSpender(void) = default;

         bool operator==(const ScriptSpender&) const;

         //set
         void setStates(SpenderStatus, SpenderStatus, bool, bool, bool);
         void setSigHashType(SIGHASH_TYPE);
         void setSequence(unsigned);
         void setFinalScript(BinaryDataRef);
         void setWitnessScript(BinaryDataRef);
         void setLegacyData(const StackItemVec&);
         void setWitnessData(const StackItemVec&);
         void setBip32Paths(std::map<BinaryData, BIP32_AssetPath>&);
         void flagP2SH(bool);
         void setUtxo(const UTXO&);

         //get
         bool isP2SH(void) const;
         bool isSegWit(void) const;
         bool isCSV(void) const;
         bool isCLTV(void) const;

         SIGHASH_TYPE getSigHashType(void) const;
         unsigned getSequence(void) const;
         BinaryDataRef getOutputScript(void) const;
         BinaryDataRef getOutputHash(void) const;
         unsigned getOutputIndex(void) const;
         const BinaryData& getFinalInputScript(void) const;
         BinaryData getSerializedInput(bool, bool) const;
         BinaryData getEmptySerializedInput(void) const;
         const BinaryData& getFinalizedWitnessData(void) const;
         BinaryData serializeAvailableWitnessData(void) const;
         BinaryDataRef getOutpoint(void) const;
         uint64_t getValue(void) const;
         const UTXO& getUtxo(void) const;

         const StackItemMap& getLegacyStack(void) const;
         SpenderStatus getLegacyStatus(void) const;

         const StackItemMap& getWitnessStack(void) const;
         SpenderStatus getWitnessStatus(void) const;

         unsigned getFlags(void) const;
         virtual uint8_t getSigHashByte(void) const;

         bool isResolved(void) const;
         bool isSigned(void) const;
         bool isInitialized(void) const;
         bool canBeResolved(void) const;
         bool hasUtxo(void) const;

         //sig parsing & resolution
         void parseScripts(StackResolver&);
         void merge(const ScriptSpender&);
         BinaryDataRef getRedeemScriptFromStack(bool) const;

         //sig checking
         bool verifyEvalState(unsigned);
         void injectSignature(SecureBinaryData&, unsigned=UINT32_MAX);
         void seedResolver(std::shared_ptr<ResolverFeed>, bool) const;
         void sign(const SignerFunc&);

         void toPSBT(BinaryWriter&) const;
         static std::shared_ptr<ScriptSpender> fromPSBT(
            BinaryRefReader&, const TxIn&,
            std::shared_ptr<std::map<BinaryData, Tx>>);

         void setTxMap(std::shared_ptr<std::map<BinaryData, Tx>>);
         bool setSupportingTx(BinaryDataRef);
         bool setSupportingTx(Tx);

         const Tx& getSupportingTx(void) const;
         bool haveSupportingTx(void) const;
         std::map<unsigned, KeyAndSig> getRelevantPubkeys(void) const;
         std::map<BinaryData, BIP32_AssetPath>& getBip32Paths(void);

         //debug
         void prettyPrint(std::ostream&) const;
      };
   } //namespace Signing
} //namespace Armory
