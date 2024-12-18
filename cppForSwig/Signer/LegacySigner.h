////////////////////////////////////////////////////////////////////////////////
//                                                                            //
//  Copyright (C) 2016-2022, goatpig                                          //
//  Distributed under the MIT license                                         //
//  See LICENSE-MIT or https://opensource.org/licenses/MIT                    //
//                                                                            //
////////////////////////////////////////////////////////////////////////////////

#ifndef _H_LEGACY_SIGNER
#define _H_LEGACY_SIGNER

#include "Script.h"

namespace Armory
{
   namespace LegacySigner
   {
      //////////////////////////////////////////////////////////////////////////
      class LegacyScriptException : public std::runtime_error
      {
      public:
         LegacyScriptException(const std::string& what) : std::runtime_error(what)
         {}
      };

      //////////////////////////////////////////////////////////////////////////
      struct StackItem
      {
      public:
         const Signing::StackItemType type_;

      protected:
         const unsigned id_;

      public:
         StackItem(Signing::StackItemType type, unsigned id) :
            type_(type), id_(id)
         {}

         virtual ~StackItem(void) = 0;
         virtual bool isSame(const StackItem* obj) const = 0;
         unsigned getId(void) const { return id_; }

         virtual bool isValid(void) const { return true; }
         static std::shared_ptr<StackItem> deserialize(const BinaryDataRef&);
      };

      ////
      struct StackItem_PushData : public StackItem
      {
         const BinaryData data_;

         StackItem_PushData(unsigned id, BinaryData&& data) :
            StackItem(Signing::StackItemType::PushData, id),
            data_(std::move(data))
         {}

         bool isSame(const StackItem* obj) const;
      };

      ////
      struct StackItem_Sig : public StackItem
      {
         const SecureBinaryData data_;

         StackItem_Sig(unsigned id, SecureBinaryData&& data) :
            StackItem(Signing::StackItemType::Sig, id),
            data_(std::move(data))
         {}

         bool isSame(const StackItem* obj) const;
      };

      ////
      struct StackItem_MultiSig : public StackItem
      {
         std::map<unsigned, SecureBinaryData> sigs_;
         const unsigned m_;

         StackItem_MultiSig(unsigned id, unsigned m) :
            StackItem(Signing::StackItemType::MultiSig, id), m_(m)
         {}

         void setSig(unsigned id, SecureBinaryData& sig)
         {
            auto sigpair = std::make_pair(id, std::move(sig));
            sigs_.insert(move(sigpair));
         }

         bool isSame(const StackItem* obj) const;
         bool isValid(void) const { return sigs_.size() == m_; }
      };

      ////
      struct StackItem_OpCode : public StackItem
      {
         const uint8_t opcode_;

         StackItem_OpCode(unsigned id, uint8_t opcode) :
            StackItem(Signing::StackItemType::OpCode, id),
            opcode_(opcode)
         {}

         bool isSame(const StackItem* obj) const;
      };

      ////
      struct StackItem_SerializedScript : public StackItem
      {
         const BinaryData data_;

         StackItem_SerializedScript(unsigned id, BinaryData&& data) :
            StackItem(Signing::StackItemType::SerializedScript, id),
            data_(std::move(data))
         {}

         bool isSame(const StackItem* obj) const;
         BinaryData serialize(void) const;
      };

      //////////////////////////////////////////////////////////////////////////
      enum class SpenderStatus
      {
         SpenderStatus_Unkonwn,
         SpenderStatus_Partial,
         SpenderStatus_Resolved
      };

      //////////////////////////////////////////////////////////////////////////
      class ScriptSpender
      {
      private:
         SpenderStatus legacyStatus_ = SpenderStatus::SpenderStatus_Unkonwn;
         SpenderStatus segwitStatus_ = SpenderStatus::SpenderStatus_Unkonwn;

         BinaryData serializedScript_;
         BinaryData witnessData_;

         std::map<unsigned, std::shared_ptr<StackItem>> partialStack_;
         std::map<unsigned, std::shared_ptr<StackItem>> partialWitnessStack_;

      private:
         ScriptSpender(void) {}

      public:
         static std::shared_ptr<ScriptSpender> deserExtState(BinaryDataRef);
         SecureBinaryData getSig(void) const;
      };

      //////////////////////////////////////////////////////////////////////////
      class Signer
      {
      private:
         bool isSegWit_ = false;
         std::vector<std::shared_ptr<ScriptSpender>> spenders_;

      private:
         Signer(void) {}
         void deser(BinaryDataRef);

      public:
         static Signer deserExtState(BinaryDataRef);
         std::map<unsigned, SecureBinaryData> getSigs(void) const;
      };
   }; //namespace LegacySigner
}; //namespace 

#endif //_H_LEGACY_SIGNER