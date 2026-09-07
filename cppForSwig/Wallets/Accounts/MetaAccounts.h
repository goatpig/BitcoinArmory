////////////////////////////////////////////////////////////////////////////////
//                                                                            //
//  Copyright (C) 2017-2026, goatpig                                          //
//  Distributed under the MIT license                                         //
//  See LICENSE-MIT or https://opensource.org/licenses/MIT                    //
//                                                                            //
////////////////////////////////////////////////////////////////////////////////

#pragma once

#include <memory>
#include <map>

#include <Utils/ReentrantLock.h>
#include <Utils/SecureBinaryData.h>
#include "AccountTypes.h"

#define META_ACCOUNT_COMMENTS    0x000000C0
#define META_ACCOUNT_AUTHPEER    0x000000C1
#define META_ACCOUNT_PREFIX      0xF1

////
namespace Armory
{
   namespace Wallets
   {
      namespace IO
      {
         class DBIfaceTransaction;
         class WalletDBInterface;
      };
   };

   namespace Assets
   {
      class MetaData;
      class CommentData;
   }

   namespace Accounts
   {
      class MetaDataAccount : public Lockable
      {
      private:
         MetaAccountType type_ = MetaAccountType::Unset;
         BinaryData ID_;
         const std::string dbName_;

         uint32_t lastAssetId_ = UINT32_MAX;
         std::map<unsigned, std::shared_ptr<Assets::MetaData>> assets_;

      private:
         bool writeAssetToDisk(
            std::shared_ptr<Wallets::IO::DBIfaceTransaction>,
            std::shared_ptr<Assets::MetaData>) const;

      public:
         MetaDataAccount(const std::string& dbName) :
            dbName_(dbName)
         {}

         //Lockable virtuals
         void initAfterLock(void) {}
         void cleanUpBeforeUnlock(void) {}

         //storage methods
         void readFromDisk(std::shared_ptr<Wallets::IO::WalletDBInterface>,
            const BinaryData&);
         void commit(std::unique_ptr<Wallets::IO::DBIfaceTransaction>) const;
         void updateOnDisk(std::shared_ptr<Wallets::IO::DBIfaceTransaction>);
         std::shared_ptr<MetaDataAccount> copy(const std::string&) const;

         //setup methods
         void reset(void);
         void make_new(MetaAccountType);

         //
         std::shared_ptr<Assets::MetaData> getMetaDataByIndex(uint32_t) const;
         uint32_t getNextAssetId(void);
         void addAsset(std::shared_ptr<Assets::MetaData>);
         const std::map<uint32_t, std::shared_ptr<Assets::MetaData>>&
         getAssetMap(void) const;
         void eraseMetaDataByIndex(uint32_t);
         MetaAccountType getType(void) const;
         const BinaryData& getID(void) const;
      };

      struct PeerAssetMap
      {
         //<name, authorized pubkey>
         std::map<std::string,
            std::pair<const SecureBinaryData*, const std::string*>> nameKeyMapOneWay;
         std::map<std::string,
            std::pair<const SecureBinaryData*, const std::string*>> nameKeyMapTwoWay;

         //<pubkey, sig>
         std::pair<SecureBinaryData, SecureBinaryData> rootSignature;

         //<pubkey, <description, assetId>>
         std::map<SecureBinaryData,
            std::pair<std::string, uint32_t>> peerRootKeys;

         SecureBinaryData masterKey;
      };

      //////////////////////////////////////////////////////////////////////////
      struct PeerAccountHelper
      {
         static PeerAssetMap getAssetMap(
            const MetaDataAccount*);
         static std::map<SecureBinaryData, std::set<unsigned>> getKeyIndexMap(
            const MetaDataAccount*, bool);

         static int addAsset(MetaDataAccount*, const SecureBinaryData&,
            const std::vector<std::string>&, const std::string&, bool,
            std::shared_ptr<Wallets::IO::DBIfaceTransaction>);

         static void addRootSignature(MetaDataAccount*,
            const SecureBinaryData&, const SecureBinaryData&,
            std::shared_ptr<Wallets::IO::DBIfaceTransaction>);

         static unsigned addRootPeer(MetaDataAccount*,
            const SecureBinaryData&, const std::string&,
            std::shared_ptr<Wallets::IO::DBIfaceTransaction>);

         static void addMasterKey(MetaDataAccount*,
            const SecureBinaryData&,
            std::shared_ptr<Wallets::IO::DBIfaceTransaction>);
         static void clearMasterKeyAssets(MetaDataAccount*);
      };

      //////////////////////////////////////////////////////////////////////////
      struct CommentAccountHelper
      {
         static std::shared_ptr<Assets::CommentData> getByKey(MetaDataAccount*,
            const BinaryData&);

         static int setAsset(
            MetaDataAccount*, const BinaryData&,
            const std::string&,
            std::shared_ptr<Wallets::IO::DBIfaceTransaction>);

         static int deleteAsset(
            MetaDataAccount*, const BinaryData&,
            std::shared_ptr<Wallets::IO::DBIfaceTransaction>);

         static std::map<BinaryData, std::string> getCommentMap(MetaDataAccount*);
      };
   }; //namespace Accounts
}; //namespace Armory
