////////////////////////////////////////////////////////////////////////////////
//                                                                            //
//  Copyright (C) 2017-2026, goatpig                                          //
//  Distributed under the MIT license                                         //
//  See LICENSE-MIT or https://opensource.org/licenses/MIT                    //
//                                                                            //
////////////////////////////////////////////////////////////////////////////////

#pragma once

#include <memory>
#include <Utils/BinaryData.h>
#include "AddressEntryType.h"

class AddressException : public std::runtime_error
{
public:
   AddressException(const std::string&);
};

namespace Armory
{
   std::string getNameForAddrType(int);

   namespace Wallets
   {
      class AssetId;
   }

   namespace Assets
   {
      class AssetEntry;
   }

   namespace Signing
   {
      class ScriptRecipient;
   }
}

////////////////////////////////////////////////////////////////////////////////
class AddressEntry
{
protected:
   const AddressEntryType type_;

   mutable std::string address_;
   mutable BinaryData hash_;
   mutable BinaryData prefixedHash_;
   mutable BinaryData script_;

public:
   //tors
   AddressEntry(AddressEntryType);
   virtual ~AddressEntry(void) = 0;

   //local
   virtual AddressEntryType getType(void) const;

   //virtual
   virtual const Armory::Wallets::AssetId& getID(void) const = 0;

   virtual const std::string& getAddress(void) const = 0;
   virtual std::shared_ptr<Armory::Signing::ScriptRecipient> getRecipient(
      uint64_t) const = 0;

   virtual const BinaryData& getHash(void) const = 0;
   virtual const BinaryData& getPrefixedHash(void) const = 0;
   virtual const BinaryData& getPreimage(void) const = 0;

   virtual const BinaryData& getScript(void) const = 0;

   //accounts for txhash + id as well as input script size
   virtual size_t getInputSize(void) const = 0;

   //throw by default, SW types will overload
   virtual size_t getWitnessDataSize(void) const;

   //static
   static std::shared_ptr<AddressEntry> instantiate(
      std::shared_ptr<Armory::Assets::AssetEntry>, AddressEntryType);
   static uint8_t getPrefixByte(AddressEntryType);
};

////////////////////////////////////////////////////////////////////////////////
class AddressEntry_WithAsset
{
private:
   const std::shared_ptr<Armory::Assets::AssetEntry> asset_;
   const bool isCompressed_;

public:
   AddressEntry_WithAsset(std::shared_ptr<Armory::Assets::AssetEntry>, bool);
   virtual ~AddressEntry_WithAsset(void) = 0;

   const std::shared_ptr<Armory::Assets::AssetEntry> getAsset(void) const;
   bool isCompressed(void) const;
};

////////////////////////////////////////////////////////////////////////////////
class AddressEntry_Nested
{
private:
   std::shared_ptr<AddressEntry> addrPtr_;

public:
   AddressEntry_Nested(std::shared_ptr<AddressEntry>);
   virtual ~AddressEntry_Nested(void) = 0;

   std::shared_ptr<AddressEntry> getPredecessor(void) const;
};

////////////////////////////////////////////////////////////////////////////////
class AddressEntry_P2PKH : public AddressEntry, public AddressEntry_WithAsset
{
public:
   AddressEntry_P2PKH(std::shared_ptr<Armory::Assets::AssetEntry>, bool);

   //virtual
   const Armory::Wallets::AssetId& getID(void) const override;
   const std::string& getAddress(void) const override;

   const BinaryData& getPrefixedHash(void) const override;
   const BinaryData& getHash(void) const override;
   const BinaryData& getPreimage(void) const override;

   std::shared_ptr<Armory::Signing::ScriptRecipient> getRecipient(
      uint64_t) const override;
   const BinaryData& getScript(void) const override;

   //size (accounts for outpoint and sequence)
   size_t getInputSize(void) const override;
};

////////////////////////////////////////////////////////////////////////////////
class AddressEntry_P2PK : public AddressEntry, public AddressEntry_WithAsset
{
public:
   AddressEntry_P2PK(std::shared_ptr<Armory::Assets::AssetEntry>, bool);
   //virtual
   const Armory::Wallets::AssetId& getID(void) const override;
   const std::string& getAddress(void) const override;

   const BinaryData& getHash(void) const override;
   const BinaryData& getPrefixedHash(void) const override;
   const BinaryData& getPreimage(void) const override;

   std::shared_ptr<Armory::Signing::ScriptRecipient> getRecipient(
      uint64_t) const override;
   const BinaryData& getScript(void) const override;

   //size (accounts for outpoint and sequence)
   size_t getInputSize(void) const override;
};

////////////////////////////////////////////////////////////////////////////////
class AddressEntry_P2WPKH : public AddressEntry, public AddressEntry_WithAsset
{
public:
   AddressEntry_P2WPKH(std::shared_ptr<Armory::Assets::AssetEntry>);

   //virtual
   const Armory::Wallets::AssetId& getID(void) const override;
   const std::string& getAddress(void) const override;

   const BinaryData& getHash(void) const override;
   const BinaryData& getPrefixedHash(void) const override;
   const BinaryData& getPreimage(void) const override;

   std::shared_ptr<Armory::Signing::ScriptRecipient> getRecipient(
      uint64_t) const override;
   const BinaryData& getScript(void) const override;

   //size (accounts for outpoint and sequence)
   size_t getInputSize(void) const override;
   size_t getWitnessDataSize(void) const override;
};

////////////////////////////////////////////////////////////////////////////////
class AddressEntry_Multisig : public AddressEntry, public AddressEntry_WithAsset
{
public:
   AddressEntry_Multisig(std::shared_ptr<Armory::Assets::AssetEntry>, bool);

   //virtual
   const Armory::Wallets::AssetId& getID(void) const override;
   const std::string& getAddress(void) const override;

   const BinaryData& getHash(void) const override;
   const BinaryData& getPrefixedHash(void) const override;
   const BinaryData& getPreimage(void) const override;

   std::shared_ptr<Armory::Signing::ScriptRecipient> getRecipient(
      uint64_t) const override;
   const BinaryData& getScript(void) const override;

   //size (accounts for outpoint and sequence)
   size_t getInputSize(void) const override;
};

////////////////////////////////////////////////////////////////////////////////
class AddressEntry_ScriptHash : public AddressEntry, public AddressEntry_WithAsset
{
public:
   AddressEntry_ScriptHash(std::shared_ptr<Armory::Assets::AssetEntry>);

   const Armory::Wallets::AssetId& getID(void) const override;

   const std::string& getAddress(void) const override;
   std::shared_ptr<Armory::Signing::ScriptRecipient> getRecipient(
      uint64_t) const override;

   const BinaryData& getHash(void) const override;
   const BinaryData& getPrefixedHash(void) const override;
   const BinaryData& getPreimage(void) const override;

   const BinaryData& getScript(void) const override;
   size_t getInputSize(void) const override;
};

////////////////////////////////////////////////////////////////////////////////
class AddressEntry_RawScript : public AddressEntry, public AddressEntry_WithAsset
{
public:
   AddressEntry_RawScript(std::shared_ptr<Armory::Assets::AssetEntry>);

   const Armory::Wallets::AssetId& getID(void) const override;

   const std::string& getAddress(void) const override;
   std::shared_ptr<Armory::Signing::ScriptRecipient> getRecipient(
      uint64_t) const override;

   const BinaryData& getHash(void) const override;
   const BinaryData& getPrefixedHash(void) const override;
   const BinaryData& getPreimage(void) const override;

   const BinaryData& getScript(void) const override;
   size_t getInputSize(void) const override;
};

////////////////////////////////////////////////////////////////////////////////
class AddressEntry_P2SH : public AddressEntry, public AddressEntry_Nested
{
public:
   AddressEntry_P2SH(std::shared_ptr<AddressEntry>);

   //virtual
   const Armory::Wallets::AssetId& getID(void) const override;
   const std::string& getAddress(void) const override;

   const BinaryData& getHash(void) const override;
   const BinaryData& getPrefixedHash(void) const override;
   const BinaryData& getPreimage(void) const override;
   
   const BinaryData& getScript(void) const;
   std::shared_ptr<Armory::Signing::ScriptRecipient> getRecipient(
      uint64_t) const override;

   AddressEntryType getType(void) const override;

   //size (accounts for outpoint and sequence)
   size_t getInputSize(void) const override;
};


////////////////////////////////////////////////////////////////////////////////
class AddressEntry_P2WSH : public AddressEntry, public AddressEntry_Nested
{
public:
   AddressEntry_P2WSH(std::shared_ptr<AddressEntry>);

   //virtual
   const Armory::Wallets::AssetId& getID(void) const override;
   const std::string& getAddress(void) const override;

   const BinaryData& getHash(void) const override;
   const BinaryData& getPrefixedHash(void) const override;
   const BinaryData& getPreimage(void) const override;

   const BinaryData& getScript(void) const override;
   std::shared_ptr<Armory::Signing::ScriptRecipient> getRecipient(
      uint64_t) const override;

   AddressEntryType getType(void) const override;

   //size (accounts for outpoint and sequence)
   size_t getInputSize(void) const override { return 41; }
   size_t getWitnessDataSize(void) const override;
};
