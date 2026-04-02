
////////////////////////////////////////////////////////////////////////////////
//                                                                            //
//  Copyright (C) 2016-2025, goatpig                                          //
//  Distributed under the MIT license                                         //
//  See LICENSE-MIT or https://opensource.org/licenses/MIT                    //
//                                                                            //
////////////////////////////////////////////////////////////////////////////////

#include "Loader.h"
#include <Utils/ArmoryConfig.h>
#include <Utils/BtcUtils.h>
#include <Utils/DBUtils.h>
#include <Utils/Cryptography.h>

#include <Wallets/Wallets.h>
#include <Wallets/IOHeader.h>
#include <Wallets/KDF.h>
#include <Wallets/Accounts/AddressAccounts.h>
#include <Wallets/Seeds/Seeds.h>

using namespace Armory;
using namespace Armory::Bridge;

using namespace std::chrono_literals;
using namespace std::string_view_literals;

#define WALLET_135_HEADER "\xbaWALLET\x00"sv
#define PYBTC_ADDRESS_SIZE 237

////////////////////////////////////////////////////////////////////////////////
//
//// WalletFileInfo
//
////////////////////////////////////////////////////////////////////////////////
WalletFileInfo::WalletFileInfo(const std::filesystem::path& path,
   WalletLoadState state) :
   path_(path), loadState_(state),
   staged_(state == WalletLoadState::Ready ? true : false)
{}

////////
const std::filesystem::path& WalletFileInfo::path() const
{
   return path_;
}

bool WalletFileInfo::staged() const
{
   return staged_;
}

WalletLoadState WalletFileInfo::state() const
{
   return loadState_;
}

void WalletFileInfo::setState(WalletLoadState state)
{
   loadState_ = state;
   if (state == WalletLoadState::Ready) {
      staged_ = true;
   } else {
      staged_ = false;
   }
}

void WalletFileInfo::setStaged(bool staged)
{
   if (staged && loadState_ != WalletLoadState::Ready) {
      std::runtime_error("state and stage mismatch");
   }
   staged_ = staged;
}

////////////////////////////////////////////////////////////////////////////////
//
//// LMDBWalletInfo
//
////////////////////////////////////////////////////////////////////////////////
LMDBWalletInfo::LMDBWalletInfo(const std::filesystem::path& path,
   std::shared_ptr<Wallets::AssetWallet> wltPtr, bool loaded) :
   WalletFileInfo(path, wltPtr ? WalletLoadState::Ready : WalletLoadState::Encrypted),
   wltPtr_(wltPtr)
{
   if (loaded) {
      setState(WalletLoadState::Loaded);
   }
}

////////
const Wallets::WalletId& LMDBWalletInfo::walletId() const
{
   if (walletId_.empty()) {
      if (wltPtr_ == nullptr) {
         throw std::runtime_error("missing wlt ptr");
      }
      walletId_ = wltPtr_->getID();
   }
   return walletId_;
}

bool LMDBWalletInfo::hasAccountIds() const
{
   if (wltPtr_ == nullptr) {
      return false;
   }
   try {
      wltPtr_->getMainAccountID();
      return true;
   } catch (const Wallets::WalletException&) {
      return false;
   }
}

std::set<Wallets::AddressAccountId> LMDBWalletInfo::getAccountIds() const
{
   if (wltPtr_ == nullptr) {
      return {};
   }
   return wltPtr_->getAccountIDs();
}

const std::string& LMDBWalletInfo::name() const
{
   if (name_.empty()) {
      if (wltPtr_ == nullptr) {
         throw std::runtime_error("no wlt ptr");
      }
      name_ = wltPtr_->getLabel();
   }
   return name_;
}

////////
void LMDBWalletInfo::unlockControlHeader(const Passphrase::UnlockFunc& lbd)
{
   auto wltPtr = Wallets::AssetWallet::loadMainWalletFromFile(
      Wallets::IO::ReadOnlyFileParams{path(), lbd});
   wltPtr_ = wltPtr;
   setState(WalletLoadState::Ready);
}

std::shared_ptr<Wallets::AssetWallet>&& LMDBWalletInfo::moveWltPtr()
{
   if (state() != WalletLoadState::Ready || !staged()) {
      throw std::runtime_error("wallet isnt ready");
   }
   setState(WalletLoadState::Loaded);
   return std::move(wltPtr_);
}

bool LMDBWalletInfo::isWatchingOnly() const
{
   auto wltSingle = std::dynamic_pointer_cast<Wallets::AssetWallet_Single>(wltPtr_);
   if (wltSingle == nullptr) {
      return false;
   }
   return wltSingle->isWatchingOnly();
}

////////////////////////////////////////////////////////////////////////////////
//
//// A135FileInfo
//
////////////////////////////////////////////////////////////////////////////////
A135FileInfo::A135FileInfo(std::shared_ptr<Armory135Header> headerPtr) :
   WalletFileInfo(headerPtr->path(), WalletLoadState::Legacy),
   a135Ptr_(headerPtr)
{}

const Wallets::WalletId& A135FileInfo::walletId() const
{
   return a135Ptr_->getID();
}

bool A135FileInfo::hasAccountIds() const
{
   return false;
}

std::set<Wallets::AddressAccountId> A135FileInfo::getAccountIds() const
{
   throw std::runtime_error("no accountId for legacy wallets");
}

const std::string& A135FileInfo::name() const
{
   return a135Ptr_->getLabel();
}

std::shared_ptr<Wallets::AssetWallet_Single> A135FileInfo::migrate(
   const Passphrase::UnlockFunc& passLbd,
   const Wallets::IO::CreateWalletParams& params) const
{
   return a135Ptr_->migrate(passLbd, params);
}

////////
bool A135FileInfo::isEncrypted() const
{
   return a135Ptr_->isEncrypted_;
}

bool A135FileInfo::isWatchingOnly() const
{
   return a135Ptr_->watchingOnly_;
}

uint32_t A135FileInfo::kdfMem() const
{
   return a135Ptr_->kdfMem_;
}

int64_t A135FileInfo::highestUsedIndex() const
{
   return a135Ptr_->highestUsedIndex_;
}

size_t A135FileInfo::addressCount() const
{
   return a135Ptr_->addrMap_.size();
}

const std::string& A135FileInfo::description() const
{
   return a135Ptr_->labelDescription_;
}

uint64_t A135FileInfo::timestamp() const
{
   return a135Ptr_->timestamp_;
}

std::string A135FileInfo::version() const
{
   switch (a135Ptr_->version_)
   {
      case 13500000:
         return {"1.35"};

      default:
         return {"N/A"};
   }
}

////////////////////////////////////////////////////////////////////////////////
//
//// Armory135Header
//
////////////////////////////////////////////////////////////////////////////////
Armory135Header::Armory135Header(const std::filesystem::path& path) :
   path_(path)
{
   parseFile();
}

////////
const std::filesystem::path& Armory135Header::path() const
{
   return path_;
}

bool Armory135Header::isInitialized() const
{
   return version_ != UINT32_MAX;
}

int32_t Armory135Header::errorCode() const
{
   return errorCode_;
}

const Wallets::WalletId& Armory135Header::getID() const
{
   return walletID_;
}

const std::string& Armory135Header::getLabel() const
{
   return labelName_;
}

////////////////////////////////////////////////////////////////////////////////
void Armory135Header::verifyChecksum(
   const BinaryDataRef& val, const BinaryDataRef& chkSum)
{
   if (val.isZero() && chkSum.isZero()) {
      return;
   }

   auto computedChkSum = BtcUtils::getHash256(val);
   if (computedChkSum.getSliceRef(0, 4) != chkSum) {
      throw std::runtime_error("failed checksum");
   }
}

////////////////////////////////////////////////////////////////////////////////
void Armory135Header::parseFile()
{
   /*
   Simply return on any failure, the version_ field will not be initialized
   until the whole header is parsed and checksums pass
   */

   uint32_t version = UINT32_MAX;
   try {
      //grab root key & address chain length from python wallet
      auto fileMap = FileUtils::FileMap(path_, false);
      BinaryRefReader brr(fileMap.ptr(), fileMap.size());

      //file type
      auto fileTypeStr = brr.get_BinaryData(8);
      if (fileTypeStr != BinaryData::fromString(WALLET_135_HEADER, 8)) {
         errorCode_ = A135_ERROR_NOTAWALLET;
         return;
      }

      //version
      version = brr.get_uint32_t();

      //magic bytes
      auto magicBytes = brr.get_BinaryData(4);
      if (magicBytes != Config::BitcoinSettings::getMagicBytes()) {
         errorCode_ = A135_ERROR_MAGICBYTE;
         return;
      }

      //flags
      auto flags = brr.get_uint64_t();
      isEncrypted_  = flags & 0x0000000000000001;
      watchingOnly_ = flags & 0x0000000000000002;

      //wallet ID
      auto walletIDbin = brr.get_BinaryData(6);
      auto idStr = BtcUtils::base58_encode(walletIDbin);
      walletID_ = Wallets::WalletId{std::string_view{idStr}};

      //creation timestamp
      timestamp_ = brr.get_uint64_t();

      //label name & description
      auto labelNameBd = brr.get_BinaryData(32);
      auto labelDescBd = brr.get_BinaryData(256);

      auto labelNameLen = strnlen(labelNameBd.getCharPtr(), 32);
      labelName_ = std::string{labelNameBd.getCharPtr(), labelNameLen};

      auto labelDescriptionLen = strnlen(labelDescBd.getCharPtr(), 256);
      labelDescription_ = std::string{
         labelDescBd.getCharPtr(), labelDescriptionLen};

      //highest used chain index
      highestUsedIndex_ = brr.get_int64_t();

      {
         /* kdf params */
         auto kdfPayload      = brr.get_BinaryDataRef(256);
         BinaryRefReader brrPayload(kdfPayload);
         auto allKdfData      = brrPayload.get_BinaryDataRef(44);
         auto allKdfChecksum  = brrPayload.get_BinaryDataRef(4);

         //skip check if wallet is unencrypted
         if (isEncrypted_) {
            verifyChecksum(allKdfData, allKdfChecksum);

            BinaryRefReader brrKdf(allKdfData);
            kdfMem_    = brrKdf.get_uint64_t();
            kdfIter_  = brrKdf.get_uint32_t();
            kdfSalt_   = brrKdf.get_BinaryDataRef(32);
         }
      }

      //256 bytes skip
      brr.advance(256);

      /* root address */
      auto rootAddrRef = brr.get_BinaryDataRef(PYBTC_ADDRESS_SIZE);
      Armory135Address rootAddrObj;
      rootAddrObj.parseFromRef(rootAddrRef);
      addrMap_.emplace(BinaryData::fromString("ROOT"sv), rootAddrObj);

      //1024 bytes skip
      brr.advance(1024);

      {
         /* wallet entries */
         while (brr.getSizeRemaining() > 0) {
            auto entryType = brr.get_uint8_t();
            switch (entryType)
            {
               case WLT_DATATYPE_KEYDATA:
               {
                  auto key = brr.get_BinaryDataRef(20);
                  auto val = brr.get_BinaryDataRef(PYBTC_ADDRESS_SIZE);

                  Armory135Address addrObj;
                  addrObj.parseFromRef(val);
                  addrMap_.emplace(key, addrObj);
                  break;
               }

               case WLT_DATATYPE_ADDRCOMMENT:
               {
                  auto key = brr.get_BinaryDataRef(20);
                  auto len = brr.get_uint16_t();
                  auto val = brr.get_String(len);

                  commentMap_.insert(make_pair(key, val));
                  break;
               }

               case WLT_DATATYPE_TXCOMMENT:
               {
                  auto key = brr.get_BinaryDataRef(32);
                  auto len = brr.get_uint16_t();
                  auto val = brr.get_String(len);

                  commentMap_.insert(make_pair(key, val));
                  break;
               }

               case WLT_DATATYPE_OPEVAL:
                  throw std::runtime_error("not supported");

               case WLT_DATATYPE_DELETED:
               {
                  auto len = brr.get_uint16_t();
                  brr.advance(len);
                  break;
               }

               default:
                  throw std::runtime_error("invalid wallet entry");
            }
         }
      }
   } catch (const std::exception& e) {
      LOGWARN << "failed to load legacy wallet at " << path_.string() << " with error: ";
      LOGWARN << "   " << e.what();
      errorCode_ = A135_ERROR_CUSTOM;
      return;
   }

   version_ = version;
}

////////////////////////////////////////////////////////////////////////////////
std::shared_ptr<Wallets::AssetWallet_Single> Armory135Header::migrate(
   const Passphrase::UnlockFunc& passLbd,
   const Wallets::IO::CreateWalletParams& params) const
{
   auto rootKey = BinaryData::fromString("ROOT"sv);
   auto rootAddrIter = addrMap_.find(rootKey);
   if (rootAddrIter == addrMap_.end()) {
      throw std::runtime_error("no root entry");
   }

   auto& rootAddrObj = rootAddrIter->second;
   auto chaincodeCopy = rootAddrObj.chaincode();

   //copy params over to set lookup
   Wallets::IO::CreateWalletParams newParams{
      params.folder,
      params.setPrivPassObj, params.setCtrlPassObj,
      params.progressFunc,
      addrMap_.size(),
      params.label, params.description
   };

   //try to decrypt the private root
   SecureBinaryData privKeyPass;
   SecureBinaryData decryptedRoot;
   {
      if (isEncrypted_ &&
         rootAddrObj.hasPrivKey() &&
         rootAddrObj.isEncrypted()) {
         //decrypt lbd
         auto decryptPrivKey = [this, &privKeyPass](
            const Passphrase::UnlockFunc& passLbd,
            const Armory135Address& rootAddrObj)->SecureBinaryData
         {
            while (true) {
               //prompt for passphrase
               auto result = passLbd({Wallets::EncryptionKeyId{}});
               if (!result.success) {
                  throw std::runtime_error("rejected migration");
               } else if (result.passphrase.empty()) {
                  //no passphrase, will proceed with WO migration
                  return {};
               }

               //kdf it
               Wallets::Encryption::KdfRomix myKdf{
                  kdfMem_, kdfIter_, kdfSalt_};
               auto derivedPass = myKdf.DeriveKey(result.passphrase);

               //decrypt the privkey
               auto decryptedKey = Cryptography::Encryption::AES::decryptCFB(
                  rootAddrObj.privKey(), derivedPass, rootAddrObj.iv());

               //generate pubkey
               auto computedPubKey = Cryptography::ECDSA::computePublicKey(
                  decryptedKey, false);

               if (rootAddrObj.pubKey() != computedPubKey) {
                  continue;
               }

               //compare pubkeys
               privKeyPass = std::move(result.passphrase);
               return decryptedKey;
            }
         };
         decryptedRoot = std::move(decryptPrivKey(passLbd, rootAddrObj));
      }
   }

   //create wallet
   std::unique_ptr<Seeds::ClearTextSeed> seed;
   if (decryptedRoot.empty()) {
      seed = std::make_unique<Seeds::ClearTextSeed_ArmoryPublic>(
         rootAddrObj.pubKey().getRef(), rootAddrObj.chaincode().getRef(),
         Seeds::LegacyType::Armory135
      );
   } else {
      /*
      Derive chaincode from the clear text root. If it matches the
      chaincode from file, this is a 1.35c root (deterministic chaincode)
      */
      auto derivedCc = BtcUtils::computeChainCode_ArmoryLegacy(decryptedRoot);
      if (derivedCc == chaincodeCopy) {
         //deterministic chaincode, clear it
         chaincodeCopy.clear();
      }
      seed = std::make_unique<Seeds::ClearTextSeed_Armory>(
         decryptedRoot, chaincodeCopy,
         Seeds::LegacyType::Armory135
      );
   }

   auto wallet = Wallets::AssetWallet_Single::createFromSeed(
      std::move(seed), newParams);

   //main account id, check it matches armory wallet id
   if (wallet->getID() != walletID_) {
      throw std::runtime_error("wallet id mismatch");
   }

   //run through addresses, figure out script types
   auto accID = wallet->getMainAccountID();
   auto mainAccPtr = wallet->getAccountForID(accID);

   //TODO: deal with imports

   std::map<Wallets::AssetId, AddressEntryType> typeMap;
   for (const auto& addrPair : addrMap_) {
      if (addrPair.second.chainIndex() < 0 ||
         addrPair.second.chainIndex() > highestUsedIndex_) {
         continue;
      }

      const auto& addrTypePair = mainAccPtr->getAssetIDPairForAddrUnprefixed(
         addrPair.second.scrAddr());

      if (addrTypePair.second != mainAccPtr->getDefaultAddressType()) {
         typeMap.emplace(addrTypePair);
      }
   }

   {
      //set script types
      auto dbtx = wallet->beginSubDBTransaction(walletID_, true);
      Wallets::AssetKeyType lastIndex = -1;
      for (const auto& typePair : typeMap) {
         //get int index for pair
         while (typePair.first.getAssetKey() != lastIndex) {
            wallet->getNewAddress();
            ++lastIndex;
         }
         wallet->getNewAddress(typePair.second);
         ++lastIndex;
      }

      while (lastIndex < highestUsedIndex_) {
         wallet->getNewAddress();
         ++lastIndex;
      }
   }

   //set name & desc
   if (!labelName_.empty()) {
      wallet->setLabel(labelName_);
   }

   if (!labelDescription_.empty()) {
      wallet->setDescription(labelDescription_);
   }

   {
      //add comments
      auto dbtx = wallet->beginSubDBTransaction(walletID_, true);
      for (const auto& commentPair : commentMap_) {
         wallet->setComment(commentPair.first, commentPair.second);
      }
   }
   return wallet;
}

////////////////////////////////////////////////////////////////////////////////
//
//// Armory135Address
//
////////////////////////////////////////////////////////////////////////////////
void Armory135Address::parseFromRef(const BinaryDataRef& bdr)
{
   BinaryRefReader brrScrAddr(bdr);

   {
      //scrAddr, only to verify the checksum
      scrAddr_ = brrScrAddr.get_BinaryData(20);
      auto scrAddrChecksum = brrScrAddr.get_BinaryData(4);
      Armory135Header::verifyChecksum(scrAddr_, scrAddrChecksum);
   }

   //address version, unused
   brrScrAddr.get_uint32_t();

   //address flags
   auto addrFlags = brrScrAddr.get_uint64_t();
   hasPrivKey_    = addrFlags & 0x0000000000000001;
   hasPubKey_     = addrFlags & 0x0000000000000002;
   isEncrypted_   = addrFlags & 0x0000000000000004;

   //chaincode
   chaincode_ = SecureBinaryData{brrScrAddr.get_BinaryDataRef(32)};
   auto chaincodeChecksum = brrScrAddr.get_BinaryDataRef(4);
   Armory135Header::verifyChecksum(chaincode_, chaincodeChecksum);

   //chain index
   chainIndex_       = brrScrAddr.get_int64_t();
   depth_            = brrScrAddr.get_int64_t();

   //iv
   iv_               = SecureBinaryData{brrScrAddr.get_BinaryDataRef(16)};
   auto ivChecksum   = brrScrAddr.get_BinaryDataRef(4);
   if (isEncrypted_) {
      Armory135Header::verifyChecksum(iv_, ivChecksum);
   }

   //private key
   privKey_             = SecureBinaryData{brrScrAddr.get_BinaryDataRef(32)};
   auto privKeyChecksum = brrScrAddr.get_BinaryDataRef(4);
   if (hasPrivKey_) {
      Armory135Header::verifyChecksum(privKey_, privKeyChecksum);
   }

   //pub key
   pubKey_              = SecureBinaryData{brrScrAddr.get_BinaryDataRef(65)};
   auto pubKeyChecksum  = brrScrAddr.get_BinaryDataRef(4);
   Armory135Header::verifyChecksum(pubKey_, pubKeyChecksum);
}
