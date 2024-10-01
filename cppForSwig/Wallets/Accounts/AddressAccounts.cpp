////////////////////////////////////////////////////////////////////////////////
//                                                                            //
//  Copyright (C) 2017-2021, goatpig                                          //
//  Distributed under the MIT license                                         //
//  See LICENSE-MIT or https://opensource.org/licenses/MIT                    //
//                                                                            //
////////////////////////////////////////////////////////////////////////////////

#include "AccountTypes.h"
#include "Assets.h"
#include "AddressAccounts.h"
#include "../EncryptedDB.h"
#include "../WalletFileInterface.h"
#include "../DecryptedDataContainer.h"

using namespace std;
using namespace Armory::Assets;
using namespace Armory::Accounts;
using namespace Armory::Wallets;

std::string legacyChangeComment("[[ Change received ]]");

////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////
//// AddressAccount
////////////////////////////////////////////////////////////////////////////////
AddressAccount::AddressAccount(const std::string& dbName,
   const AddressAccountId& id) :
   dbName_(dbName), ID_(id)
{}

////////////////////////////////////////////////////////////////////////////////
unique_ptr<AddressAccount> AddressAccount::make_new(
   const string& dbName, shared_ptr<AccountType> accType,
   shared_ptr<Encryption::DecryptedDataContainer> decrData,
   unique_ptr<Encryption::Cipher> cipher,
   const std::function<std::shared_ptr<AssetEntry>(void)>& getRootLbd)
{
   if (accType == nullptr)
      throw AccountException("[make_new] null accType");

   unique_ptr<AddressAccount> addressAccountPtr;
   const auto& addressAccountId = accType->getAccountID();
   addressAccountPtr.reset(new AddressAccount(dbName, addressAccountId));

   //create root asset
   auto createRootAsset =
      [&decrData, &getRootLbd, &addressAccountId](
         shared_ptr<AccountType_BIP32> accBip32,
         const NodeRoot& nodeRoot,
         unique_ptr<Encryption::Cipher> cipher_copy)->
      shared_ptr<AssetEntry_BIP32Root>
   {
      //get last node
      const auto& derPath = DerivationTree::toPath32(nodeRoot.path);
      uint32_t node_id = 0;
      auto nodeIt = prev(derPath.end());
      if (nodeIt != derPath.end())
         node_id = *nodeIt;

      //create ids
      AssetAccountId aaid(addressAccountId, node_id);
      shared_ptr<AssetEntry_BIP32Root> rootAsset;

      //setup bip32 root object from base58 string
      BIP32_Node node;
      node.initFromBase58(nodeRoot.b58Root);

      auto chaincode = node.moveChaincode();
      auto pubkey = node.movePublicKey();
      AssetId assetId(aaid, AssetId::getRootKey());

      if (node.isPublic())
      {
         //WO wallet
         rootAsset = make_shared<AssetEntry_BIP32Root>(
            assetId,
            pubkey, nullptr,
            chaincode,
            node.getDepth(), node.getLeafID(), 
            node.getParentFingerprint(), accBip32->getSeedFingerprint(),
            derPath);
      }
      else
      {
         //full wallet
         ReentrantLock lock(decrData.get());

         //encrypt private root
         auto&& encrypted_root =
            decrData->encryptData(cipher_copy.get(), node.getPrivateKey());

         //create assets
         auto cipherData =
            make_unique<Encryption::CipherData>(encrypted_root, move(cipher_copy));
         auto priv_asset =
            make_shared<Asset_PrivateKey>(assetId ,move(cipherData));

         rootAsset = make_shared<AssetEntry_BIP32Root>(
            assetId,
            pubkey, priv_asset,
            chaincode,
            node.getDepth(), node.getLeafID(), 
            node.getParentFingerprint(), accBip32->getSeedFingerprint(),
            derPath);
      }

      return rootAsset;
   };

   //create account
   auto createNewAccount = [&dbName](
      shared_ptr<AssetEntry_BIP32Root> rootAsset,
      shared_ptr<DerivationScheme_BIP32> derScheme)->
      shared_ptr<AssetAccountData>
   {
      if(rootAsset == nullptr)
         throw AccountException("null root asset");

      //der scheme
      if(derScheme == nullptr)
      {
         auto chaincode = rootAsset->getChaincode();
         if (chaincode.getSize() == 0)
            throw AccountException("invalid chaincode");

         derScheme = make_shared<DerivationScheme_BIP32>(
            chaincode, rootAsset->getDepth(), rootAsset->getLeafID());
      }

      //instantiate account
      return make_shared<AssetAccountData>(
         AssetAccountTypeEnum_Plain,
         rootAsset->getAccountID(),
         rootAsset, derScheme, dbName);
   };

   //body
   switch (accType->type())
   {
   case AccountTypeEnum_ArmoryLegacy:
   {
      auto accPtr = dynamic_pointer_cast<AccountType_ArmoryLegacy>(accType);
      auto aaid = accPtr->getOuterAccountID();

      //first derived asset
      shared_ptr<AssetEntry_Single> firstAsset;

      if (!getRootLbd)
         throw AccountException("[make_new] undefined root lbd");
      auto rootPtr = getRootLbd();
      auto root135 = dynamic_pointer_cast<AssetEntry_ArmoryLegacyRoot>(rootPtr);
      if (root135 == nullptr)
         throw AccountException("[make_new] expected legacy root");

      //chaincode has to be a copy cause the derscheme ctor moves it in
      SecureBinaryData chaincode = root135->getChaincode();
      auto derScheme = make_shared<DerivationScheme_ArmoryLegacy>(
         chaincode);

      AssetKeyType firstAssetKey = 0;
      AssetId assetId(aaid, firstAssetKey);
      if (!root135->hasPrivateKey())
      {
         //WO
         firstAsset = derScheme->computeNextPublicEntry(
            root135->getPubKey()->getUncompressedKey(),
            assetId);
      }
      else
      {
         //full wallet
         ReentrantLock lock(decrData.get());
         const auto& privRootRef = decrData->getClearTextAssetData(
            root135->getPrivKey());

         firstAsset = derScheme->computeNextPrivateEntry(
            decrData,
            privRootRef, move(cipher),
            assetId);
      }

      //instantiate account and set first entry
      auto asset_account = make_shared<AssetAccountData>(
         AssetAccountTypeEnum_Plain, aaid,
         //no root asset for legacy derivation scheme, using first entry instead
         nullptr, derScheme, dbName);
      asset_account->assets_.insert(make_pair(firstAssetKey, firstAsset));

      //add the asset account
      addressAccountPtr->addAccount(asset_account);
      break;
   }

   case AccountTypeEnum_BIP32:
   case AccountTypeEnum_BIP32_Salted:
   {
      auto accBip32 = dynamic_pointer_cast<AccountType_BIP32>(accType);
      if (accBip32 == nullptr)
         throw AccountException("unexpected account type");

      //grab derivation tree, generate node roots
      const auto& derTree = accBip32->getDerivationTree();
      auto walletRootBip32 =
         dynamic_pointer_cast<AssetEntry_BIP32Root>(getRootLbd());
      
      ReentrantLock lock(decrData.get());
      auto nodeRoots = derTree.resolveNodeRoots(decrData, walletRootBip32);

      for (const auto& nodeRoot : nodeRoots)
      {
         if (nodeRoot.b58Root.empty())
            throw AccountException("[make_new] skipped path");

         unique_ptr<Encryption::Cipher> cipher_copy;
         if (cipher != nullptr)
            cipher_copy = cipher->getCopy();

         auto root_obj = createRootAsset(accBip32,
            nodeRoot, move(cipher_copy));

         //derivation scheme object
         shared_ptr<DerivationScheme_BIP32> derScheme = nullptr;
         if (accType->type() == AccountTypeEnum_BIP32_Salted)
         {
            auto accSalted = 
               dynamic_pointer_cast<AccountType_BIP32_Salted>(accType);
            if (accSalted == nullptr)
               throw AccountException("unexpected account type");

            if (accSalted->getSalt().getSize() != 32)
               throw AccountException("invalid salt len");

            auto chaincode = root_obj->getChaincode();
            auto salt = accSalted->getSalt();
            derScheme = 
               make_shared<DerivationScheme_BIP32_Salted>(
                  salt, chaincode, 
                  root_obj->getDepth(), root_obj->getLeafID());
         }

         //create and add the asset account
         auto account_obj = createNewAccount(root_obj, derScheme);
         addressAccountPtr->addAccount(account_obj);
      }

      break;
   }

   case AccountTypeEnum_ECDH:
   {
      auto accEcdh = dynamic_pointer_cast<AccountType_ECDH>(accType);
      if (accEcdh == nullptr)
         throw AccountException("unexpected account type");
      const auto& aaID = accEcdh->getOuterAccountID();
      AssetId assetId(aaID, AssetId::getRootKey());

      //root asset
      shared_ptr<AssetEntry_Single> rootAsset;
      if (accEcdh->isWatchingOnly())
      {
         //WO
         auto pubkeyCopy = accEcdh->getPubKey();
         rootAsset = make_shared<AssetEntry_Single>(
            assetId,
            pubkeyCopy, nullptr);
      }
      else
      {
         //full wallet
         auto pubkey = accEcdh->getPubKey();
         if (pubkey.getSize() == 0)
         {
            auto&& pubkey_unc =
               CryptoECDSA().ComputePublicKey(accEcdh->getPrivKey());
            pubkey = move(CryptoECDSA().CompressPoint(pubkey_unc));
         }

         ReentrantLock lock(decrData.get());

         //encrypt private root
         auto&& cipher_copy = cipher->getCopy();
         auto&& encrypted_root =
            decrData->encryptData(cipher_copy.get(), accEcdh->getPrivKey());

         //create assets
         auto cipherData = make_unique<Encryption::CipherData>(
            encrypted_root, move(cipher_copy));
         auto priv_asset = make_shared<Asset_PrivateKey>(
            assetId, move(cipherData));
         rootAsset = make_shared<AssetEntry_Single>(
            assetId, pubkey, priv_asset);
      }

      //derivation scheme
      auto derScheme = make_shared<DerivationScheme_ECDH>();

      //account
      auto assetAccount = make_shared<AssetAccountData>(
         AssetAccountTypeEnum_ECDH, aaID,
         rootAsset, derScheme, dbName);

      addressAccountPtr->addAccount(assetAccount);
      break;
   }

   default:
      throw AccountException("unknown account type");
   }

   //set the address types
   addressAccountPtr->addressTypes_ = accType->getAddressTypes();

   //set default address type
   addressAccountPtr->defaultAddressEntryType_ =
      accType->getDefaultAddressEntryType();

   //set inner and outer accounts
   try 
   {
      addressAccountPtr->outerAccountId_ = accType->getOuterAccountID();
      addressAccountPtr->innerAccountId_ = accType->getInnerAccountID();
   }
   catch (const IdException&)
   {}

   //sanity checks
   if (addressAccountPtr->accountDataMap_.empty())
   {
      throw AccountException("[make_new] address account has no"
         " asset account!");
   }

   //check outer account, set default if empty
   if (!addressAccountPtr->outerAccountId_.isValid())
   {
      addressAccountPtr->outerAccountId_ =
         addressAccountPtr->accountDataMap_.begin()->first;

      LOGWARN << "empty outer account id, defaulting to " <<
         addressAccountPtr->outerAccountId_.toHexStr();
   }

   if (!addressAccountPtr->innerAccountId_.isValid())
   {
      addressAccountPtr->innerAccountId_ =
         addressAccountPtr->outerAccountId_;

      LOGWARN << "empty inner account id, defaulting to outer account id";
   }

   return addressAccountPtr;
}

////////////////////////////////////////////////////////////////////////////////
void AddressAccount::commit(shared_ptr<IO::WalletDBInterface> iface)
{
   if (iface == nullptr)
      throw AccountException("commit: null iface");

   //id as key
   auto idKey = ID_.getSerializedKey(ADDRESS_ACCOUNT_PREFIX);

   //data
   BinaryWriter bwData;

   //outer and inner account
   outerAccountId_.serializeValue(bwData);
   innerAccountId_.serializeValue(bwData);

   //address type set
   bwData.put_var_int(addressTypes_.size());

   for (auto& addrType : addressTypes_)
      bwData.put_uint32_t(addrType);

   //default address type
   bwData.put_uint32_t(defaultAddressEntryType_);

   //asset accounts count
   bwData.put_var_int(accountDataMap_.size());

   auto uniqueTx = iface->beginWriteTransaction(dbName_);
   shared_ptr<IO::DBIfaceTransaction> sharedTx(move(uniqueTx));

   //asset accounts
   for (auto& accDataPair : accountDataMap_)
   {
      shared_ptr<AssetAccount> aaPtr;
      switch (accDataPair.second->type_)
      {
      case AssetAccountTypeEnum_Plain:
      {
         aaPtr = make_shared<AssetAccount>(
            accDataPair.second);
         break;
      }

      case AssetAccountTypeEnum_ECDH:
      {
         aaPtr = make_shared<AssetAccount_ECDH>(
            accDataPair.second);
         break;
      }

      default:
         throw AccountException("invalid asset account type");
      }

      //append asset account id to serialized address account data
      auto assetAccountID = aaPtr->getID();
      assetAccountID.serializeValue(bwData);

      aaPtr->commit(iface);
   }

   //put address account data
   sharedTx->insert(idKey, bwData.getData());

   //put instantiated address types
   for (auto& addrPair : instantiatedAddressTypes_)
      writeAddressType(sharedTx, addrPair.first, addrPair.second);
}

////////////////////////////////////////////////////////////////////////////////
void AddressAccount::addAccount(shared_ptr<AssetAccount> account)
{
   addAccount(account->data_);
}

////////////////////////////////////////////////////////////////////////////////
void AddressAccount::addAccount(std::shared_ptr<AssetAccountData> accPtr)
{
   auto& accID = accPtr->id_;
   if (!accID.isValid())
      throw AccountException("invalid account id length");

   auto insertPair = accountDataMap_.emplace(accID, accPtr);
   if (!insertPair.second)
      throw AccountException("already have this asset account");
}

////////////////////////////////////////////////////////////////////////////////
unique_ptr<AddressAccount> AddressAccount::readFromDisk(
   shared_ptr<IO::WalletIfaceTransaction> tx, const AddressAccountId& ID)
{
   const auto& dbName = tx->getDbName();

   //get data from disk
   auto key = ID.getSerializedKey(ADDRESS_ACCOUNT_PREFIX);
   auto&& diskDataRef = tx->getDataRef(key);
   BinaryRefReader brr(diskDataRef);

   //instantiate ptr
   unique_ptr<AddressAccount> accPtr;
   accPtr.reset(new AddressAccount(dbName, ID));

   //outer and inner account ids
   AssetAccountId outId, innId;
   try
   {
      outId = AssetAccountId::deserializeValue(brr);
   }
   catch (const IdException&)
   {
      //possibly an old id, let's try that
      outId = AssetAccountId::deserializeValueOld(ID, brr);
   }

   try
   {
      innId = AssetAccountId::deserializeValue(brr);
   }
   catch (const IdException&)
   {
      //possibly an old id, let's try that
      innId = AssetAccountId::deserializeValueOld(ID, brr);
   }

   accPtr->outerAccountId_ = outId;
   accPtr->innerAccountId_ = innId;

   //sanity checks on ids
   if (!accPtr->outerAccountId_.isValid() ||
      !accPtr->innerAccountId_.isValid())
   {
      throw AccountException("[readFromDisk] invalid asset account ids");
   }

   if (accPtr->outerAccountId_.getAddressAccountId() != ID ||
      accPtr->innerAccountId_.getAddressAccountId() != ID)
   {
      throw AccountException("[readFromDisk] account ids mismatch");
   }

   //address type set
   auto count = brr.get_var_int();
   for (unsigned i = 0; i < count; i++)
      accPtr->addressTypes_.insert(AddressEntryType(brr.get_uint32_t()));

   //default address type
   accPtr->defaultAddressEntryType_ = AddressEntryType(brr.get_uint32_t());

   //asset accounts
   count = brr.get_var_int();
   for (unsigned i = 0; i < count; i++)
   {
      auto len = brr.get_var_int();
      BinaryWriter bw_asset_key(1 + len);
      bw_asset_key.put_uint8_t(ASSET_ACCOUNT_PREFIX);
      bw_asset_key.put_BinaryData(brr.get_BinaryData(len));

      auto accData = AssetAccount::loadFromDisk(
         bw_asset_key.getData(), tx);
      accPtr->accountDataMap_.emplace(accData->id_, move(accData));
   }

   //instantiated address types
   auto idKey = accPtr->getID().getSerializedKey(ADDRESS_TYPE_PREFIX);
   auto dbIter = tx->getIterator();
   dbIter->seek(idKey);
   while (dbIter->isValid())
   {
      auto&& key = dbIter->key();
      if (!key.startsWith(idKey.getRef()))
         break;

      auto&& data = dbIter->value();
      if (data.getSize() != 4)
      {
         LOGWARN << "unexpected address entry type val size!";
         dbIter->advance();
         continue;
      }

      auto aeType = AddressEntryType(*(uint32_t*)data.getPtr());
      auto assetID = AssetId::deserializeKey(key, ADDRESS_TYPE_PREFIX);
      accPtr->instantiatedAddressTypes_.insert(make_pair(assetID, aeType));

      dbIter->advance();
   }

   return accPtr;
}

////////////////////////////////////////////////////////////////////////////////
void AddressAccount::extendPublicChain(
   std::shared_ptr<IO::WalletDBInterface> iface, unsigned count,
   const function<void(int)>& progressCallback)
{
   for (auto& accDataPair : accountDataMap_)
   {
      auto accountPtr = getAccountForID(accDataPair.first);
      accountPtr->extendPublicChain(iface, count, progressCallback);
   }
}

////////////////////////////////////////////////////////////////////////////////
void AddressAccount::extendPublicChain(
   std::shared_ptr<IO::WalletDBInterface> iface,
   const AssetAccountId& id, unsigned count,
   const function<void(int)>& progressCallback)
{
   auto accountPtr = getAccountForID(id);
   accountPtr->extendPublicChain(iface, count, progressCallback);
}

////////////////////////////////////////////////////////////////////////////////
void AddressAccount::extendPublicChainToIndex(
   std::shared_ptr<IO::WalletDBInterface> iface,
   const AssetAccountId& accountID, unsigned index,
   const std::function<void(int)>& progressCallback)
{
   auto accountPtr = getAccountForID(accountID);
   accountPtr->extendPublicChainToIndex(iface, index, progressCallback);
}

////////////////////////////////////////////////////////////////////////////////
void AddressAccount::extendPrivateChain(
   std::shared_ptr<IO::WalletDBInterface> iface,
   shared_ptr<Encryption::DecryptedDataContainer> ddc,
   unsigned count)
{
   for (auto& accDataPair : accountDataMap_)
   {
      auto accountPtr = getAccountForID(accDataPair.first);
      accountPtr->extendPrivateChain(iface, ddc, count);
   }
}

////////////////////////////////////////////////////////////////////////////////
void AddressAccount::extendPrivateChainToIndex(
   std::shared_ptr<IO::WalletDBInterface> iface,
   shared_ptr<Encryption::DecryptedDataContainer> ddc,
   const AssetAccountId& accountID, unsigned count)
{
   auto account = getAccountForID(accountID);
   account->extendPrivateChainToIndex(iface, ddc, count);
}

////////////////////////////////////////////////////////////////////////////////
shared_ptr<AddressEntry> AddressAccount::getNewAddress(
   std::shared_ptr<IO::WalletDBInterface> iface, AddressEntryType aeType)
{
   if (!outerAccountId_.isValid())
      throw AccountException("no currently active asset account");

   return getNewAddress(iface, outerAccountId_, aeType);
}

////////////////////////////////////////////////////////////////////////////////
shared_ptr<AddressEntry> AddressAccount::getNewAddress(
   std::shared_ptr<IO::WalletDBInterface> iface,
   const AssetAccountId& accountId, AddressEntryType aeType)
{
   if (aeType == AddressEntryType_Default)
      aeType = defaultAddressEntryType_;

   auto aeIter = addressTypes_.find(aeType);
   if (aeIter == addressTypes_.end())
   {
      throw AccountException(
         "[getNewAddress] invalid address type for this account");
   }

   auto accountPtr = getAccountForID(accountId);
   auto assetPtr = accountPtr->getNewAsset(iface);
   auto addrPtr = AddressEntry::instantiate(assetPtr, aeType);

   //keep track of the address type for this asset if it doesnt use the 
   //account default
   if (aeType != defaultAddressEntryType_)
   {
      //update on disk
      updateInstantiatedAddressType(iface, addrPtr);
   }

   return addrPtr;
}

////////////////////////////////////////////////////////////////////////////////
shared_ptr<AddressEntry> AddressAccount::getNewChangeAddress(
   std::shared_ptr<IO::WalletDBInterface> iface, AddressEntryType aeType)
{
   if (!innerAccountId_.isValid())
   {
      throw AccountException(
         "[getNewChangeAddress] no currently active asset account");
   }

   return getNewAddress(iface, innerAccountId_, aeType);
}

////////////////////////////////////////////////////////////////////////////////
shared_ptr<AddressEntry> AddressAccount::peekNextChangeAddress(
   std::shared_ptr<IO::WalletDBInterface> iface, AddressEntryType aeType)
{
   if (aeType == AddressEntryType_Default)
      aeType = defaultAddressEntryType_;

   auto aeIter = addressTypes_.find(aeType);
   if (aeIter == addressTypes_.end())
   {
      throw AccountException(
         "[peekNextChangeAddress] invalid address type for this account");
   }

   auto accountPtr = getAccountForID(innerAccountId_);
   auto assetPtr = accountPtr->getNewAsset(iface);
   auto addrPtr = AddressEntry::instantiate(assetPtr, aeType);

   return addrPtr;
}

////////////////////////////////////////////////////////////////////////////////
bool AddressAccount::isAssetChange(const AssetId& id) const
{
   if (innerAccountId_ != outerAccountId_)
      return id.belongsTo(innerAccountId_);

   if (!isLegacy())
      return false;

   if (!getComment_)
      return false;

   //TODO: get addr for asset
   BinaryData standin_replace_later;

   const auto& comment = getComment_(standin_replace_later);
   return (comment == legacyChangeComment);
}

////////////////////////////////////////////////////////////////////////////////
bool AddressAccount::isAssetInUse(const AssetId& id) const
{
   auto accPtr = getAccountForID(id);
   return accPtr->isAssetInUse(id);
}

////////////////////////////////////////////////////////////////////////////////
bool AddressAccount::hasAddressType(AddressEntryType aeType)
{
   if (aeType == AddressEntryType_Default)
      return true;

   auto iter = addressTypes_.find(aeType);
   return iter != addressTypes_.end();
}

////////////////////////////////////////////////////////////////////////////////
shared_ptr<AssetEntry> AddressAccount::getAssetForID(const AssetId& id) const
{
   if (!id.isValid())
      throw AccountException("invalid asset ID");

   auto accountPtr = getAccountForID(id);
   return accountPtr->getAssetForID(id);
}

////////////////////////////////////////////////////////////////////////////////
const pair<AssetId, AddressEntryType>&
   AddressAccount::getAssetIDPairForAddr(const BinaryData& scrAddr)
{
   updateAddressHashMap();

   auto iter = addressHashes_.find(scrAddr);
   if (iter == addressHashes_.end())
      throw AccountException("unknown scrAddr");

   return iter->second;
}

////////////////////////////////////////////////////////////////////////////////
const pair<AssetId, AddressEntryType>&
   AddressAccount::getAssetIDPairForAddrUnprefixed(const BinaryData& scrAddr)
{
   updateAddressHashMap();

   const auto& addressTypeSet = getAddressTypeSet();
   set<uint8_t> usedPrefixes;
   for (auto& addrType : addressTypeSet)
   {
      BinaryWriter bw;
      auto prefixByte = AddressEntry::getPrefixByte(addrType);
      auto insertIter = usedPrefixes.insert(prefixByte);
      if (!insertIter.second)
         continue;

      bw.put_uint8_t(prefixByte);
      bw.put_BinaryData(scrAddr);

      auto& addrData = bw.getData();
      auto iter = addressHashes_.find(addrData);
      if (iter == addressHashes_.end())
         continue;

      return iter->second;
   }

   throw AccountException("unknown scrAddr");
}

////////////////////////////////////////////////////////////////////////////////
void AddressAccount::updateAddressHashMap()
{
   ReentrantLock lock(this);

   for (auto accountDataPair : accountDataMap_)
   {
      auto accountPtr = getAccountForID(accountDataPair.first);
      auto& hashMap = accountPtr->getAddressHashMap(addressTypes_);
      if (hashMap.size() == 0)
         continue;

      AssetAccountData::AddrHashMapType::const_iterator hashMapIter;

      auto idIter = topHashedAssetId_.find(accountDataPair.first);
      if (idIter == topHashedAssetId_.end())
      {
         hashMapIter = hashMap.begin();
      }
      else
      {
         hashMapIter = hashMap.find(idIter->second);
         ++hashMapIter;

         if (hashMapIter == hashMap.end())
            continue;
      }

      while (hashMapIter != hashMap.end())
      {
         for (auto& hash : hashMapIter->second)
         {
            auto&& inner_pair = make_pair(hashMapIter->first, hash.first);
            auto&& outer_pair = make_pair(hash.second, move(inner_pair));
            addressHashes_.emplace(outer_pair);
         }

         ++hashMapIter;
      }

      const auto& assetID = hashMap.rbegin()->first;
      auto insertIter = topHashedAssetId_.emplace(
         accountDataPair.first, assetID);
      if (!insertIter.second)
         insertIter.first->second = assetID;
   }
}

////////////////////////////////////////////////////////////////////////////////
const map<BinaryData, pair<AssetId, AddressEntryType>>&
   AddressAccount::getAddressHashMap()
{
   updateAddressHashMap();
   return addressHashes_;
}

////////////////////////////////////////////////////////////////////////////////
const shared_ptr<AssetAccountData>& AddressAccount::getAccountDataForID(
   const AssetAccountId& id) const
{
   auto iter = accountDataMap_.find(id);
   if (iter == accountDataMap_.end())
      throw AccountException("[getAccountDataForID] invalid account ID");

   return iter->second;
}

////////////////////////////////////////////////////////////////////////////////
size_t AddressAccount::getNumAssetAccounts() const
{
   return accountDataMap_.size();
}

////////////////////////////////////////////////////////////////////////////////
set<AssetAccountId> AddressAccount::getAccountIdSet(void) const
{
   set<AssetAccountId> result;
   for (const auto& accDataPair : accountDataMap_)
      result.emplace(accDataPair.first);

   return result;
}

////////////////////////////////////////////////////////////////////////////////
unique_ptr<AssetAccount> AddressAccount::getAccountForID(
   const AssetId& id) const
{
   return getAccountForID(id.getAssetAccountId());
}

////////////////////////////////////////////////////////////////////////////////
unique_ptr<AssetAccount> AddressAccount::getAccountForID(
   const AssetAccountId& id) const
{
   auto accData = getAccountDataForID(id);
   switch (accData->type_)
   {
      case AssetAccountTypeEnum_Plain:
         return make_unique<AssetAccount>(accData);

      case AssetAccountTypeEnum_ECDH:
         return make_unique<AssetAccount_ECDH>(accData);

      default:
         throw AccountException("[getAccountForID] unknown asset account type");
   }
}

////////////////////////////////////////////////////////////////////////////////
unique_ptr<AssetAccount> AddressAccount::getOuterAccount() const
{
   return getAccountForID(getOuterAccountID());
}

////////////////////////////////////////////////////////////////////////////////
shared_ptr<AssetEntry> AddressAccount::getOuterAssetRoot() const
{
   auto account = getOuterAccount();
   return account->getRoot();
}

////////////////////////////////////////////////////////////////////////////////
AddressAccountPublicData AddressAccount::exportPublicData() const
{
   AddressAccountPublicData aapd(ID_, outerAccountId_, innerAccountId_);

   //address
   aapd.defaultAddressEntryType_ = defaultAddressEntryType_;
   aapd.addressTypes_ = addressTypes_;
   aapd.instantiatedAddressTypes_ = instantiatedAddressTypes_;

   //asset accounts
   for (auto& assetAccPair : accountDataMap_)
   {
      auto accPtr = getAccountForID(assetAccPair.first);
      const auto& assetData = assetAccPair.second;
      if (assetData == nullptr)
         continue;

      /*
      Only check account root type if it has a root to begin with. Some
      accounts do not carry roots (e.g. Armory135 wallets)
      */
      shared_ptr<AssetEntry_Single> woRoot = nullptr;
      {
         auto rootSingle = dynamic_pointer_cast<AssetEntry_Single>(
            assetData->root_);

         if (rootSingle != nullptr)
            woRoot = rootSingle->getPublicCopy();
      }

      SecureBinaryData rootData;
      if (woRoot != nullptr)
         rootData = woRoot->serialize();

      SecureBinaryData derData;
      std::shared_ptr<AssetAccountExtendedData> extended = nullptr;
      if (assetData->derScheme_ != nullptr)
      {
         derData = assetData->derScheme_->serialize();

         //check for salts
         auto derEcdh = dynamic_pointer_cast<DerivationScheme_ECDH>(
            assetData->derScheme_);
         if (derEcdh != nullptr)
         {
            auto saltMap = derEcdh->getSaltMap();
            auto salts = std::make_shared<AssetAccountSaltMap>();

            for (const auto& saltPair : saltMap)
               salts->salts_.emplace(saltPair.second, saltPair.first);
            extended = salts;
         }
      }

      AssetAccountPublicData assaPD {
         assetData->id_,
         rootData, derData,
         accPtr->getHighestUsedIndex(), accPtr->getLastComputedIndex() };
      assaPD.extendedData = extended;

      aapd.accountDataMap_.emplace(assetData->id_, move(assaPD));
   }

   return aapd;
}

////////////////////////////////////////////////////////////////////////////////
void AddressAccount::importPublicData(const AddressAccountPublicData& aapd)
{
   //sanity check
   if (aapd.ID_ != ID_)
      throw AccountException("[importPublicData] ID mismatch");

   //synchronize address chains
   for (auto& assapd : aapd.accountDataMap_)
   {
      auto accPtr = getAccountForID(assapd.first);
      if (accPtr == nullptr)
         throw AccountException("[importPublicData] missing asset account");

      switch (accPtr->type())
      {
         case AssetAccountTypeEnum::AssetAccountTypeEnum_ECDH:
         {
            //ecdh account, inject the existing salts
            auto accEcdh = dynamic_cast<AssetAccount_ECDH*>(accPtr.get());
            if (accEcdh == nullptr)
               throw AccountException("[importPublicData] account isnt ECDH");

            auto saltMap = dynamic_pointer_cast<AssetAccountSaltMap>(
               assapd.second.extendedData);
            if (saltMap == nullptr)
            {
               throw AccountException("[importPublicData]"
                  " imported data missing salt map");
            }

            for (const auto& saltPair : saltMap->salts_)
            {
               if (accEcdh->addSalt(nullptr, saltPair.second) != saltPair.first)
               {
                  throw AccountException("[importPublicData]"
                     " injected salt order mismtach");
               }
            }
         }

         default:
            break;
      }

      //do not allow rollbacks
      if (assapd.second.lastComputedIndex_ > accPtr->getLastComputedIndex())
      {
         accPtr->extendPublicChainToIndex(
            nullptr, assapd.second.lastComputedIndex_, nullptr);
      }

      if (assapd.second.lastUsedIndex_ > accPtr->getHighestUsedIndex())
         accPtr->data_->lastUsedIndex_ = assapd.second.lastUsedIndex_;
   }

   //sync address set
   instantiatedAddressTypes_ = aapd.instantiatedAddressTypes_;

   //TODO: check the assets for addresses do exist

}

////////////////////////////////////////////////////////////////////////////////
void AddressAccount::updateInstantiatedAddressType(
   shared_ptr<IO::WalletDBInterface> iface,
   shared_ptr<AddressEntry> addrPtr)
{
   /***
   AddressAccount keeps track instantiated address types with a simple
   key-val scheme:

   (ADDRESS_PREFIX|Asset's ID):(AddressEntry type)

   Addresses using the account's default type are not recorded. Their type is
   infered on load by AssetAccounts' highest used index and the lack of explicit
   type entry.
   ***/

   //sanity check
   if (addrPtr->getType() == AddressEntryType_Default)
      throw AccountException("invalid address entry type");

   updateInstantiatedAddressType(iface, addrPtr->getID(), addrPtr->getType());
}
  
////////////////////////////////////////////////////////////////////////////////
void AddressAccount::updateInstantiatedAddressType(
   shared_ptr<IO::WalletDBInterface> iface,
   const AssetId& id, AddressEntryType aeType)
{
   //sanity check
   if (aeType != AddressEntryType_Default)
   {
      auto typeIter = addressTypes_.find(aeType);
      if (typeIter == addressTypes_.end())
         throw AccountException("invalid address type");
   }

   auto iter = instantiatedAddressTypes_.find(id);
   if (iter != instantiatedAddressTypes_.end())
   {
      //skip if type entry already exist and new type matches old one
      if (iter->second == aeType)
         return;

      //delete entry if new type matches default account type
      if (aeType == defaultAddressEntryType_)
      {
         instantiatedAddressTypes_.erase(iter);
         eraseInstantiatedAddressType(iface, id);
         return;
      }
   }

   //otherwise write address type to disk
   instantiatedAddressTypes_[id] = aeType;
   writeAddressType(iface, id, aeType);
}

////////////////////////////////////////////////////////////////////////////////
void AddressAccount::writeAddressType(shared_ptr<IO::WalletDBInterface> iface,
   const AssetId& id, AddressEntryType aeType)
{
   auto uniqueTx = iface->beginWriteTransaction(dbName_);
   shared_ptr<IO::DBIfaceTransaction> sharedTx(move(uniqueTx));

   writeAddressType(sharedTx, id, aeType);
}

////////////////////////////////////////////////////////////////////////////////
void AddressAccount::writeAddressType(shared_ptr<IO::DBIfaceTransaction> tx,
   const AssetId& id, AddressEntryType aeType)
{
   ReentrantLock lock(this);

   auto idKey = id.getSerializedKey(ADDRESS_TYPE_PREFIX);

   BinaryWriter bwData;
   bwData.put_uint32_t(aeType);

   tx->insert(idKey, bwData.getData());
}

////////////////////////////////////////////////////////////////////////////////
void AddressAccount::eraseInstantiatedAddressType(
   std::shared_ptr<IO::WalletDBInterface> iface, const AssetId& id)
{
   if (iface == nullptr)
      throw AccountException("eraseInstantiatedAddressType: null iface");

   ReentrantLock lock(this);

   auto idKey = id.getSerializedKey(ADDRESS_TYPE_PREFIX);

   auto tx = iface->beginWriteTransaction(dbName_);
   tx->erase(idKey);
}

////////////////////////////////////////////////////////////////////////////////
shared_ptr<AddressEntry> AddressAccount::getAddressEntryForID(
   const AssetId& ID) const
{
   //get the asset account
   auto account = getAccountForID(ID);

   //does this ID exist?
   if (!account->isAssetIDValid(ID))
      throw UnrequestedAddressException();

   //have we instantiated in address with this ID already?
   AddressEntryType aeType = defaultAddressEntryType_;
   auto addrIter = instantiatedAddressTypes_.find(ID);
   if (addrIter != instantiatedAddressTypes_.end())
      aeType = addrIter->second;

   auto assetPtr = account->getAssetForID(ID);
   auto addrPtr = AddressEntry::instantiate(assetPtr, aeType);
   return addrPtr;
}

////////////////////////////////////////////////////////////////////////////////
map<AssetId, shared_ptr<AddressEntry>> AddressAccount::getUsedAddressMap()
   const
{
   /***
   Expensive call, as addresses are built on the fly
   ***/

   map<AssetId, shared_ptr<AddressEntry>> result;

   for (auto& account : accountDataMap_)
   {
      const AssetAccount aa(account.second);

      auto usedIndex = aa.getHighestUsedIndex();
      if (usedIndex == -1)
         continue;

      for (AssetKeyType i = 0; i <= usedIndex; i++)
      {
         auto assetPtr = aa.getAssetForKey(i);
         auto& assetID = assetPtr->getID();

         shared_ptr<AddressEntry> addrPtr;
         auto iter = instantiatedAddressTypes_.find(assetID);
         if (iter == instantiatedAddressTypes_.end())
            addrPtr = AddressEntry::instantiate(assetPtr, defaultAddressEntryType_);
         else
            addrPtr = AddressEntry::instantiate(assetPtr, iter->second);

         result.insert(make_pair(assetID, addrPtr));
      }
   }

   return result;
}

////////////////////////////////////////////////////////////////////////////////
bool AddressAccount::isAssetUsed(const Wallets::AssetId& id) const
{
   auto acc = getAccountForID(id);
   if (acc == nullptr)
      return false;

   auto assetKey = id.getAssetKey();
   return assetKey > -1 && assetKey <= acc->getHighestUsedIndex();
}


////////////////////////////////////////////////////////////////////////////////
shared_ptr<Asset_PrivateKey> AddressAccount::fillPrivateKey(
   shared_ptr<IO::WalletDBInterface> iface,
   shared_ptr<Encryption::DecryptedDataContainer> ddc,
   const AssetId& id)
{
   if (!id.isValid())
      throw AccountException("invalid asset id");

   auto accountPtr = getAccountForID(id.getAssetAccountId());
   return accountPtr->fillPrivateKey(iface, ddc, id);
}

////////////////////////////////////////////////////////////////////////////////
shared_ptr<AssetEntry_BIP32Root> AddressAccount::getBip32RootForAssetId(
   const AssetId& assetId) const
{
   //sanity check
   if (!assetId.isValid())
      throw AccountException("invalid asset id");

   //get the asset account
   const auto& acc = getAccountDataForID(assetId.getAssetAccountId());

   //grab the account's root
   auto root = acc->root_;

   //is it bip32?
   auto rootBip32 = dynamic_pointer_cast<AssetEntry_BIP32Root>(root);
   if (rootBip32 == nullptr)
      throw AccountException("account isn't bip32");

   return rootBip32;
}

////////////////////////////////////////////////////////////////////////////////
bool AddressAccount::hasBip32Path(
   const Armory::Signing::BIP32_AssetPath& path) const
{
   //look for an account which root's path matches that of our desired path
   for (const auto& accountPair : accountDataMap_)
   {
      auto root = accountPair.second->root_;
      auto rootBip32 = dynamic_pointer_cast<AssetEntry_BIP32Root>(root);
      if (rootBip32 == nullptr)
         continue;

      auto rootPath = rootBip32->getDerivationPath();
      auto assetPath = path.getDerivationPathFromSeed();
      if (rootPath.empty() || 
         (rootPath.size() > assetPath.size()))
      {
         continue;
      }

      if (rootBip32->getSeedFingerprint(true) != path.getSeedFingerprint())
         return false;

      bool match = true;
      for (unsigned i=0; i<rootPath.size(); i++)
      {
         if (rootPath[i] != assetPath[i])
         {
            match = false;
            break;
         }
      }

      if (match)
         return true;
   }

   return false;
}

////////////////////////////////////////////////////////////////////////////////
bool AddressAccount::isLegacy() const
{
   return ID_ == AccountType_ArmoryLegacy::addrAccountId;
}

////////////////////////////////////////////////////////////////////////////////
AddressAccountPublicData::AddressAccountPublicData(
   const AddressAccountId& accId,
   const AssetAccountId& outId,
   const AssetAccountId& innId) :
   ID_(accId), outerAccountId_(outId), innerAccountId_(innId)
{}
