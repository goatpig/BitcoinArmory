////////////////////////////////////////////////////////////////////////////////
//                                                                            //
//  Copyright (C) 2016-2025, goatpig                                          //
//  Distributed under the MIT license                                         //
//  See LICENSE-MIT or https://opensource.org/licenses/MIT                    //
//                                                                            //
////////////////////////////////////////////////////////////////////////////////

#pragma once

#include <stdint.h>
#include <map>

#include <Utils/BinaryData.h>
#include "ResolverFeed.h"

class TxOut;

namespace Armory
{
   namespace Signing
   {
      enum class SpendScriptType : int
      {
         P2PKH,
         P2PK,
         P2SH,
         P2WPKH,
         P2WSH,
         OPRETURN,
         UNIVERSAL
      };

      ////
      class ScriptRecipientException : public std::runtime_error
      {
      public:
         ScriptRecipientException(const std::string&);
      };

      //////////////////////////////////////////////////////////////////////////
      class ScriptRecipient
      {
      protected:
         const SpendScriptType type_;
         uint64_t value_ = UINT64_MAX;

         mutable BinaryData script_;
         std::map<BinaryData, BIP32_AssetPath> bip32Paths_;
         std::map<BinaryData, BinaryData> prioprietaryPSBTData_;

         //PSBT redeem and witness scripts, by key, carried over as is
         std::map<BinaryData, BinaryData> scriptsPSBTData_;

      public:
         ScriptRecipient(SpendScriptType, uint64_t);
         virtual ~ScriptRecipient(void) = 0;

         //virtuals
         virtual const BinaryData& getSerializedScript(void) const;
         virtual void serialize(void) const = 0;
         virtual size_t getSize(void) const = 0;

         //locals
         virtual uint64_t getValue(void) const;
         void addBip32Path(const BIP32_AssetPath&);
         const std::map<BinaryData, BIP32_AssetPath>& getBip32Paths(void) const;

         void toPSBT(BinaryWriter&) const;
         void merge(std::shared_ptr<ScriptRecipient>);
         bool isSame(const ScriptRecipient&) const;

         //static
         static std::shared_ptr<ScriptRecipient> fromScript(BinaryDataRef);
         static std::shared_ptr<ScriptRecipient> fromPSBT(
            BinaryRefReader&, const TxOut&);
      };

      //////////////////////////////////////////////////////////////////////////
      class Recipient_P2PKH : public ScriptRecipient
      {
      private:
         const BinaryData h160_;

      public:
         Recipient_P2PKH(const BinaryData&, uint64_t);

         void serialize(void) const override;
         size_t getSize(void) const override;
      };

      //////////////////////////////////////////////////////////////////////////
      class Recipient_P2PK : public ScriptRecipient
      {
      private:
         const BinaryData pubkey_;

      public:
         Recipient_P2PK(const BinaryData&, uint64_t);

         void serialize(void) const override;
         size_t getSize(void) const override;
      };

      //////////////////////////////////////////////////////////////////////////
      class Recipient_P2WPKH : public ScriptRecipient
      {
      private:
         const BinaryData h160_;

      public:
         Recipient_P2WPKH(const BinaryData&, uint64_t);

         void serialize(void) const override;
         size_t getSize(void) const override;
      };

      //////////////////////////////////////////////////////////////////////////
      class Recipient_P2SH : public ScriptRecipient
      {
      private:
         const BinaryData h160_;

      public:
         Recipient_P2SH(const BinaryData&, uint64_t);

         void serialize(void) const override;
         size_t getSize(void) const override;
      };

      //////////////////////////////////////////////////////////////////////////
      class Recipient_P2WSH : public ScriptRecipient
      {
      private:
         const BinaryData h256_;

      public:
         Recipient_P2WSH(const BinaryData&, uint64_t);

         void serialize(void) const override;
         size_t getSize(void) const override;
      };

      //////////////////////////////////////////////////////////////////////////
      class Recipient_OPRETURN : public ScriptRecipient
      {
      private:
         const BinaryData message_;

      public:
         Recipient_OPRETURN(const BinaryData&);

         void serialize(void) const override;
         size_t getSize(void) const override;

         //override get value to avoid the throw since it has 0 for value
         uint64_t getValue(void) const override;
      };

      //////////////////////////////////////////////////////////////////////////
      class Recipient_Universal : public ScriptRecipient
      {
      private:
         const BinaryData binScript_;

      public:
         Recipient_Universal(const BinaryData&, uint64_t);

         void serialize(void) const override;
         size_t getSize(void) const override;
      };
   }; //namespace Signer
}; //namespace Armory
