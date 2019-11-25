////////////////////////////////////////////////////////////////////////////////
//                                                                            //
//  Copyright (C) 2017, goatpig                                               //
//  Distributed under the MIT license                                         //
//  See LICENSE-MIT or https://opensource.org/licenses/MIT                    //
//                                                                            //
////////////////////////////////////////////////////////////////////////////////

#ifndef _H_ADDRESSES
#define _H_ADDRESSES

#include <memory>

#include "BinaryData.h"
#include "ScriptRecipient.h"
#include "Assets.h"
#include "BlockDataManagerConfig.h"

class AddressException : public std::runtime_error
{
public:
   AddressException(const std::string& err) : std::runtime_error(err)
   {}
};

#define ADDRESS_TYPE_PREFIX   0xD8

////
enum AddressEntryType
{
   AddressEntryType_Default = 0,
   AddressEntryType_P2PKH = 1,
   AddressEntryType_P2PK = 2,
   AddressEntryType_P2WPKH = 3,
   AddressEntryType_Multisig = 4,
   AddressEntryType_Compressed = 0x10000000,
   AddressEntryType_P2SH = 0x40000000,
   AddressEntryType_P2WSH = 0x80000000
};

#define ADDRESS_NESTED_MASK      0xC0000000
#define ADDRESS_COMPRESSED_MASK  0x10000000
#define ADDRESS_TYPE_MASK        0x0FFFFFFF

////////////////////////////////////////////////////////////////////////////////
class AddressEntry
{
protected:
   const AddressEntryType type_;

   mutable BinaryData address_;
   mutable BinaryData hash_;
   mutable BinaryData prefixedHash_;
   mutable BinaryData script_;

public:
   //tors
   AddressEntry(AddressEntryType aetype) :
      type_(aetype)
   {}

   virtual ~AddressEntry(void) = 0;

   //local
   virtual AddressEntryType getType(void) const { return type_; }

   //virtual
   virtual const BinaryData& getID(void) const = 0;

   virtual const BinaryData& getAddress() const = 0;
   virtual std::shared_ptr<ScriptRecipient> getRecipient(uint64_t) const = 0;
   
   virtual const BinaryData& getHash(void) const = 0;
   virtual const BinaryData& getPrefixedHash(void) const = 0;
   virtual const BinaryData& getPreimage(void) const = 0;

   virtual const BinaryData& getScript(void) const = 0;

   //accounts for txhash + id as well as input script size
   virtual size_t getInputSize(void) const = 0;

   //throw by default, SW types will overload
   virtual size_t getWitnessDataSize(void) const
   {
      throw std::runtime_error("no witness data");
   }

   //static
   static std::shared_ptr<AddressEntry> instantiate(
      std::shared_ptr<AssetEntry>, AddressEntryType);
   static uint8_t getPrefixByte(AddressEntryType);
};

////////////////////////////////////////////////////////////////////////////////
class AddressEntry_WithAsset
{
private:
   const std::shared_ptr<AssetEntry> asset_;
   const bool isCompressed_;

public:
   AddressEntry_WithAsset(std::shared_ptr<AssetEntry> asset, bool isCompressed) :
      asset_(asset), isCompressed_(isCompressed)
   {}

   virtual ~AddressEntry_WithAsset(void) = 0;

   const std::shared_ptr<AssetEntry> getAsset(void) const { return asset_; }
   bool isCompressed(void) const { return isCompressed_; }
};

////////////////////////////////////////////////////////////////////////////////
class AddressEntry_P2PKH : public AddressEntry, public AddressEntry_WithAsset
{
public:
   //tors
   AddressEntry_P2PKH(std::shared_ptr<AssetEntry> asset, bool isCompressed) :
      AddressEntry(AddressEntryType_P2PKH), 
      AddressEntry_WithAsset(asset, isCompressed)
   {
      auto asset_single = std::dynamic_pointer_cast<AssetEntry_Single>(asset);
      if (asset_single == nullptr)
         throw AddressException("need single asset for single signer address type");
   }

   //virtual
   const BinaryData& getID(void) const;
   const BinaryData& getAddress(void) const;

   const BinaryData& getPrefixedHash(void) const;
   const BinaryData& getHash(void) const;
   const BinaryData& getPreimage(void) const;

   std::shared_ptr<ScriptRecipient> getRecipient(uint64_t) const;
   const BinaryData& getScript(void) const;

   //size (accounts for outpoint and sequence)
   size_t getInputSize(void) const;
};

////////////////////////////////////////////////////////////////////////////////
class AddressEntry_P2PK : public AddressEntry, public AddressEntry_WithAsset
{
public:
   //tors
   AddressEntry_P2PK(std::shared_ptr<AssetEntry> asset, bool isCompressed) :
      AddressEntry(AddressEntryType_P2PK), 
      AddressEntry_WithAsset(asset, isCompressed)
   {
      auto asset_single = std::dynamic_pointer_cast<AssetEntry_Single>(asset);
      if (asset_single == nullptr)
         throw AddressException("need single asset for single signer address type");
   }

   //virtual
   const BinaryData& getID(void) const;
   const BinaryData& getAddress(void) const;

   const BinaryData& getHash(void) const;
   const BinaryData& getPrefixedHash(void) const;
   const BinaryData& getPreimage(void) const;

   std::shared_ptr<ScriptRecipient> getRecipient(uint64_t) const;
   const BinaryData& getScript(void) const;

   //size (accounts for outpoint and sequence)
   size_t getInputSize(void) const;
};

////////////////////////////////////////////////////////////////////////////////
class AddressEntry_P2WPKH : public AddressEntry, public AddressEntry_WithAsset
{
public:
   //tors
   AddressEntry_P2WPKH(std::shared_ptr<AssetEntry> asset) :
      AddressEntry(AddressEntryType_P2WPKH), 
      AddressEntry_WithAsset(asset, true)
   {
      auto asset_single = std::dynamic_pointer_cast<AssetEntry_Single>(asset);
      if (asset_single == nullptr)
         throw AddressException("need single asset for single signer address type");
   }

   //virtual
   const BinaryData& getID(void) const;
   const BinaryData& getAddress(void) const;

   const BinaryData& getHash(void) const;
   const BinaryData& getPrefixedHash(void) const;
   const BinaryData& getPreimage(void) const;

   std::shared_ptr<ScriptRecipient> getRecipient(uint64_t) const;
   const BinaryData& getScript(void) const;

   //size (accounts for outpoint and sequence)
   size_t getInputSize(void) const { return 40; }
   size_t getWitnessDataSize(void) const { return 108; }
};

////////////////////////////////////////////////////////////////////////////////
class AddressEntry_Multisig : public AddressEntry, public AddressEntry_WithAsset
{
public:
   //tors
   AddressEntry_Multisig(std::shared_ptr<AssetEntry> asset, bool compressed) :
      AddressEntry(AddressEntryType_Multisig),
      AddressEntry_WithAsset(asset, compressed)
   {
      auto asset_ms = std::dynamic_pointer_cast<AssetEntry_Multisig>(asset);
      if (asset_ms == nullptr)
         throw AddressException("need multisig asset for multisig address type");
   }

   //virtual
   const BinaryData& getID(void) const;
   const BinaryData& getAddress(void) const;

   const BinaryData& getHash(void) const;
   const BinaryData& getPrefixedHash(void) const;
   const BinaryData& getPreimage(void) const;

   std::shared_ptr<ScriptRecipient> getRecipient(uint64_t) const;
   const BinaryData& getScript(void) const;

   //size (accounts for outpoint and sequence)
   size_t getInputSize(void) const;
};

////////////////////////////////////////////////////////////////////////////////
class AddressEntry_Nested
{
private:
   std::shared_ptr<AddressEntry> addrPtr_;

public:
   AddressEntry_Nested(std::shared_ptr<AddressEntry> addrPtr) :
      addrPtr_(addrPtr)
   {
      if (addrPtr_ == nullptr)
         throw AddressException("empty predecessor");
   }

   virtual ~AddressEntry_Nested(void) = 0;

   std::shared_ptr<AddressEntry> getPredecessor(void) const { return addrPtr_; }
};

////////////////////////////////////////////////////////////////////////////////
class AddressEntry_P2SH : public AddressEntry, public AddressEntry_Nested
{
public:
   //tors
   AddressEntry_P2SH(std::shared_ptr<AddressEntry> addrPtr) :
      AddressEntry(AddressEntryType_P2SH), 
      AddressEntry_Nested(addrPtr)
   {
      if (addrPtr->getType() & AddressEntryType_P2SH)
         throw AddressException("cannot nest P2SH in P2SH");
   }

   //virtual
   const BinaryData& getID(void) const;
   const BinaryData& getAddress(void) const;

   const BinaryData& getHash(void) const;
   const BinaryData& getPrefixedHash(void) const;
   const BinaryData& getPreimage(void) const;
   
   const BinaryData& getScript(void) const;
   std::shared_ptr<ScriptRecipient> getRecipient(uint64_t) const;

   AddressEntryType getType(void) const;

   //size (accounts for outpoint and sequence)
   size_t getInputSize(void) const;
};


////////////////////////////////////////////////////////////////////////////////
class AddressEntry_P2WSH : public AddressEntry, public AddressEntry_Nested
{
public:
   //tors
   AddressEntry_P2WSH(std::shared_ptr<AddressEntry> addrPtr) :
      AddressEntry(AddressEntryType_P2WSH), 
      AddressEntry_Nested(addrPtr)
   {
      if (addrPtr->getType() == AddressEntryType_P2WPKH)
         throw AddressException("cannot nest SW in P2WSH");

      if (addrPtr->getType() == AddressEntryType_P2WSH)
         throw AddressException("cannot nest P2WSH in P2WSH");
   }

   //virtual
   const BinaryData& getID(void) const;
   const BinaryData& getAddress(void) const;

   const BinaryData& getHash(void) const;
   const BinaryData& getPrefixedHash(void) const;
   const BinaryData& getPreimage(void) const;

   const BinaryData& getScript(void) const;
   std::shared_ptr<ScriptRecipient> getRecipient(uint64_t) const;

   AddressEntryType getType(void) const;

   //size (accounts for outpoint and sequence)
   size_t getInputSize(void) const { return 41; }
   size_t getWitnessDataSize(void) const;
};

#endif
