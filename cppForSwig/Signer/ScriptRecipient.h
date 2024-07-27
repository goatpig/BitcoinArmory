////////////////////////////////////////////////////////////////////////////////
//                                                                            //
//  Copyright (C) 2016-2021, goatpig                                          //
//  Distributed under the MIT license                                         //
//  See LICENSE-MIT or https://opensource.org/licenses/MIT                    //
//                                                                            //
////////////////////////////////////////////////////////////////////////////////

#ifndef _H_SCRIPT_RECIPIENT
#define _H_SCRIPT_RECIPIENT

#include <stdint.h>
#include "BinaryData.h"
#include "BtcUtils.h"
#include "ResolverFeed.h"

class TxOut;

namespace Armory
{
   namespace Signing
   {
      struct Serializer;

      ////
      enum SpendScriptType
      {
         SST_P2PKH,
         SST_P2PK,
         SST_P2SH,
         SST_P2WPKH,
         SST_P2WSH,
         SST_OPRETURN,
         SST_UNIVERSAL
      };

      ////
      class ScriptRecipientException : public std::runtime_error
      {
      public:
         ScriptRecipientException(const std::string& err) :
            std::runtime_error(err)
         {}
      };

      //////////////////////////////////////////////////////////////////////////
      class ScriptRecipient
      {
         friend struct Serializer;

      protected:
         const SpendScriptType type_;
         uint64_t value_ = UINT64_MAX;

         mutable BinaryData script_;
         std::map<BinaryData, BIP32_AssetPath> bip32Paths_;
         std::map<BinaryData, BinaryData> prioprietaryPSBTData_;

      public:
         //tors
         ScriptRecipient(SpendScriptType sst, uint64_t value) :
            type_(sst), value_(value)
         {}

         //virtuals
         virtual const BinaryData& getSerializedScript(void) const
         {
            if (script_.empty())
               serialize();

            return script_;
         }

         virtual ~ScriptRecipient(void) = 0;
         virtual void serialize(void) const = 0;
         virtual size_t getSize(void) const = 0;

         //locals
         virtual uint64_t getValue(void) const 
         {
            if (value_ == 0)
               throw ScriptRecipientException("invalid recipient value");
            return value_;
         }

         void addBip32Path(const BIP32_AssetPath&);
         const std::map<BinaryData, BIP32_AssetPath>& getBip32Paths(void) const;

         void toPSBT(BinaryWriter&) const;
         void merge(std::shared_ptr<ScriptRecipient>);

         bool isSame(const ScriptRecipient& rhs)
         {
            if (type_ != rhs.type_)
               return false;

            if (value_ != rhs.value_)
               return false;

            return (getSerializedScript() == rhs.getSerializedScript());
         }

         //static
         static std::shared_ptr<ScriptRecipient> fromScript(BinaryDataRef);
         static std::shared_ptr<ScriptRecipient> fromPSBT(
            BinaryRefReader& brr, const TxOut&);
      };

      //////////////////////////////////////////////////////////////////////////
      class Recipient_P2PKH : public ScriptRecipient
      {
      private:
         const BinaryData h160_;

      public:
         Recipient_P2PKH(const BinaryData& h160, uint64_t val) :
            ScriptRecipient(SST_P2PKH, val), h160_(h160)
         {
            if (h160_.getSize() != 20)
               throw ScriptRecipientException("a160 is not 20 bytes long!");
         }

         void serialize(void) const override;

         //return size is harcoded
         size_t getSize(void) const override;
      };

      //////////////////////////////////////////////////////////////////////////
      class Recipient_P2PK : public ScriptRecipient
      {
      private:
         const BinaryData pubkey_;

      public:
         Recipient_P2PK(const BinaryData& pubkey, uint64_t val) :
            ScriptRecipient(SST_P2PK, val), pubkey_(pubkey)
         {
            if (pubkey.getSize() != 33 && pubkey.getSize() != 65)
               throw ScriptRecipientException("a160 is not 20 bytes long!");
         }

         void serialize(void) const override;

         //return size is hardcoded
         size_t getSize(void) const override;
      };

      //////////////////////////////////////////////////////////////////////////
      class Recipient_P2WPKH : public ScriptRecipient
      {
      private:
         const BinaryData h160_;

      public:
         Recipient_P2WPKH(const BinaryData& h160, uint64_t val) :
            ScriptRecipient(SST_P2WPKH, val), h160_(h160)
         {
            if (h160_.getSize() != 20)
               throw ScriptRecipientException("a160 is not 20 bytes long!");
         }

         void serialize(void) const override;
         size_t getSize(void) const override;
      };

      //////////////////////////////////////////////////////////////////////////
      class Recipient_P2SH : public ScriptRecipient
      {
      private:
         const BinaryData h160_;

      public:
         Recipient_P2SH(const BinaryData& h160, uint64_t val) :
            ScriptRecipient(SST_P2SH, val), h160_(h160)
         {
            if (h160_.getSize() != 20)
               throw ScriptRecipientException("a160 is not 20 bytes long!");
         }

         void serialize(void) const override;
         size_t getSize(void) const override;
      };

      //////////////////////////////////////////////////////////////////////////
      class Recipient_P2WSH : public ScriptRecipient
      {
      private:
         const BinaryData h256_;

      public:
         Recipient_P2WSH(const BinaryData& h256, uint64_t val) :
            ScriptRecipient(SST_P2WSH, val), h256_(h256)
         {
            if (h256_.getSize() != 32)
               throw ScriptRecipientException("a256 is not 32 bytes long!");
         }

         void serialize(void) const override;
         size_t getSize(void) const override;
      };

      //////////////////////////////////////////////////////////////////////////
      class Recipient_OPRETURN : public ScriptRecipient
      {
      private:
         const BinaryData message_;

      public:
         Recipient_OPRETURN(const BinaryData& message) :
            ScriptRecipient(SST_OPRETURN, 0), message_(message)
         {
            if (message_.getSize() > 80)
               throw ScriptRecipientException(
                  "OP_RETURN message cannot exceed 80 bytes");
         }

         void serialize(void) const override;
         size_t getSize(void) const override;

         //override get value to avoid the throw since it has 0 for value
         uint64_t getValue(void) const override { return 0; }
      };

      //////////////////////////////////////////////////////////////////////////
      class Recipient_Universal : public ScriptRecipient
      {
      private:
         const BinaryData binScript_;

      public:
         Recipient_Universal(const BinaryData& script, uint64_t val) :
            ScriptRecipient(SST_UNIVERSAL, val), binScript_(script)
         {}

         void serialize(void) const override;
         size_t getSize(void) const override;
      };
   }; //namespace Signer
}; //namespace Armory
#endif