////////////////////////////////////////////////////////////////////////////////
//                                                                            //
//  Copyright (C) 2025-2026, goatpig                                          //
//  Distributed under the MIT license                                         //
//  See LICENSE-MIT or https://opensource.org/licenses/MIT                    //
//                                                                            //
////////////////////////////////////////////////////////////////////////////////

#include <cstdlib>
#include <queue>
#include <algorithm>

#include "TestUtils.h"
#include <reorgTest/blkdata.h>
#include <Utils/ArmoryConfig.h>
#include <Utils/FileUtils.h>
#include <Ledgers/LedgerEntry.h>

#include <Wallets/Accounts/AddressAccounts.h>
#include <Wallets/Accounts/AccountTypes.h>
#include <Wallets/WalletFileInterface.h>
#include <Wallets/Seeds/Seeds.h>
#include <Wallets/AuthorizedPeers.h>

#include <BridgeAPI/CppBridge.h>
#include <BridgeAPI/BridgeSocket.h>
#include <BridgeAPI/ProtoCommandParser.h>
#include <BridgeAPI/Wallets/Manager.h>
#include <BridgeAPI/Wallets/Notifications.h>
#include <BridgeAPI/Wallets/TxIOCache.h>
#include <BridgeAPI/BlockchainDbClient.h>

#include "BDM_mainthread.h"
#include "Server.h"

#include <capnp/message.h>
#include <capnp/serialize.h>
#include "capnp/Bridge.capnp.h"
#include "capnp/Types.capnp.h"

using namespace Armory;

using namespace std::chrono_literals;
using namespace std::string_view_literals;

using MsgPtr = std::unique_ptr<Armory::Bridge::WritePayload_Bridge>;

////////////////////////////////////////////////////////////////////////////////
namespace {
   int CapnWalletState_Legacy = 1;
   int CapnWalletState_Encrypted = 2;
   int CapnWalletState_Ready = 3;
   std::string legacyAccId = Armory::Wallets::AddressAccountId(
      ARMORY_LEGACY_ACCOUNTID).toHexStr();

   /////////////////////////////////////////////////////////////////////////////
   // capnp stuff
   BinaryData serializeCapnp(capnp::MallocMessageBuilder& msg)
   {
      auto flat = capnp::messageToFlatArray(msg);
      auto bytes = flat.asBytes();
      return BinaryData(bytes.begin(), bytes.end());
   }

   struct WltListEntry
   {
      std::string walletId;
      std::vector<std::string> accountIds;
      int loadState;
      bool staged;
      bool isWO;
   };

   struct AddressData
   {
      struct Comparator
      {
         using is_transparent = void;
         bool operator()(const AddressData& lhs, const AddressData& rhs) const
         {
            return lhs.hash < rhs.hash;
         }
         bool operator()(const BinaryData& lhs, const AddressData rhs) const {
            return lhs < rhs.hash;
         }
         bool operator()(const AddressData& lhs, const BinaryData& rhs) const {
            return lhs.hash < rhs;
         }
      };

      const int32_t index;
      const BinaryData hash;
      const std::string addrStr;
      const bool isUsed;
      const uint32_t type;

      bool operator<(const AddressData& rhs) const
      {
         return hash < rhs.hash;
      }
   };

   struct WalletData
   {
      const std::string walletId;
      const std::string accountId;
      const std::string masterId;
      const std::string dbId;

      const std::string label;
      const std::string desc;

      const bool encrypted;
      const bool watchingOnly;
      const std::set<AddressData, AddressData::Comparator> addresses;
      const int64_t lookup;
      const int64_t useCount;

      const std::filesystem::path path;
      const uint32_t kdfMemReq;
   };

   std::filesystem::path fullBinPath;

   AddressData capnToAddressData(const Codec::Bridge::WalletData::AddressData::Reader& capnAddr)
   {
      auto capnHash = capnAddr.getPrefixedHash();
      BinaryData addrHash{capnHash.begin(), capnHash.end()};
      return AddressData{ capnAddr.getIndex(), std::move(addrHash),
         std::string(capnAddr.getAddressString()), capnAddr.getIsUsed(), capnAddr.getAddrType() };
   }

   WalletData capnToWalletData(const Codec::Bridge::WalletData::Reader& capnWlt)
   {
      auto capnAddrs = capnWlt.getAddressData();
      std::set<AddressData, AddressData::Comparator> addresses;
      for (const auto& capnAddr : capnAddrs) {
         addresses.emplace(capnToAddressData(capnAddr));
      }

      std::string walletId = capnWlt.getWalletId();
      return WalletData{
         walletId, capnWlt.getAccountId(), capnWlt.getMasterId(), capnWlt.getDbId(),
         capnWlt.getLabel(), capnWlt.getDesc(),
         capnWlt.getUsesEncryption(), capnWlt.getWatchingOnly(),
         std::move(addresses), capnWlt.getLookupCount(), capnWlt.getUseCount(),
         std::filesystem::path(std::string{capnWlt.getPath()}), capnWlt.getKdfMemReq()
      };
   }

   std::vector<Ledgers::Entry> capnToLedgers(
      const Codec::Types::TxLedger::Reader& capnLedgers)
   {
      std::vector<Ledgers::Entry> result;
      auto capnList = capnLedgers.getLedgers();
      result.reserve(capnList.size());

      for (auto capnLedger : capnList) {
         auto capnHash = capnLedger.getTxHash();
         BinaryData txHash{capnHash.begin(), capnHash.end()};

         auto capnAddrList = capnLedger.getScrAddrs();
         std::set<BinaryData> addrSet;
         for (auto capnAddr : capnAddrList) {
            addrSet.emplace(BinaryData{capnAddr.begin(), capnAddr.end()});
         }

         result.emplace_back(Ledgers::Entry{
            std::string{capnLedger.getWalletId()},
            capnLedger.getBalance(), capnLedger.getTxHeight(), txHash,
            capnLedger.getTxOutIndex(), capnLedger.getTxTime(),
            addrSet,
            capnLedger.getIsCoinbase(), capnLedger.getIsSTS(), capnLedger.getIsChangeBack(),
            capnLedger.getIsOptInRBF(), capnLedger.getIsChainedZC()
         });
      }
      return result;
   }

   std::vector<std::vector<Ledgers::Entry>> capnToLedgers(
      const capnp::List<Codec::Types::TxLedger, capnp::Kind::STRUCT>::Reader& capnLedgers)
   {
      std::vector<std::vector<Ledgers::Entry>> result;
      result.reserve(capnLedgers.size());

      for (auto txLedgers : capnLedgers) {
         result.emplace_back(capnToLedgers(txLedgers));
      }
      return result;
   }

   /////////////////////////////////////////////////////////////////////////////
   // bridge client side reply & notif handling
   struct NotifQueue
   {
      std::queue<BinaryData> queue;
      std::mutex mu;
      std::condition_variable cv;

      void push_back(BinaryData& data)
      {
         std::unique_lock<std::mutex> lock(mu);
         queue.emplace(std::move(data));
         cv.notify_all();
      }

      BinaryData pop()
      {
         std::unique_lock<std::mutex> lock(mu);
         while (queue.empty()) {
            cv.wait(lock);
         }

         auto data = std::move(queue.front());
         queue.pop();
         return data;
      }
   };

   std::mutex commsMutex;
   std::deque<MsgPtr> replyQueue;
   std::condition_variable commsCV;

   MsgPtr waitOnReply()
   {
      std::unique_lock<std::mutex> lock(commsMutex);
      while (replyQueue.empty()) {
         commsCV.wait(lock);
      }
      auto result = std::move(replyQueue.front());
      replyQueue.pop_front();
      return result;
   }

   void pushRequest(std::shared_ptr<Bridge::CppBridge> bridge,
      const BinaryData& rawRequest)
   {
      Bridge::ProtoCommandParser::processData(bridge, rawRequest);
   }

   /////////////////////////////////////////////////////////////////////////////
   // wallet creation from manager
   std::string createWOWallet(
      const std::filesystem::path& homedir,
      const std::vector<SecureBinaryData>& pubkeys,
      const std::vector<BinaryData>& scrAddrs = {})
   {
      Wallets::IO::CreateWalletParams params{
         homedir, {}, {},
         nullptr, 0
      };

      auto walletId = std::string{"walletWO_" +
         Cryptography::PRNG::fortuna.generateRandom(3).toHexStr()};

      //create empty WO wallet
      auto wltWO = Wallets::AssetWallet_Single::createBlank(
         {std::string_view{walletId}}, params);
      wltWO->setupImportAccount();

      for (auto pubkey : pubkeys) {
         wltWO->importPublicKey(pubkey, AddressEntryType(
            AddressEntryType::P2PKH | AddressEntryType::Uncompressed));
      }

      for (auto scrAddr : scrAddrs) {
         try {
            wltWO->importScrAddr(scrAddr);
         } catch (const AddressException&) {
            wltWO->importRawScript(scrAddr);
         }
      }
      return walletId;
   }

   std::string createWallet(const std::filesystem::path& homedir)
   {
      Wallets::IO::CreateWalletParams params{
         homedir,
         {1ms, 0, SecureBinaryData::fromString("privpass1")},
         {},
         nullptr, 4
      };

      std::unique_ptr<Seeds::ClearTextSeed> seed(
         new Seeds::ClearTextSeed_Armory());
      auto assetWlt = Wallets::AssetWallet_Single::createFromSeed(
         std::move(seed), params);
      return assetWlt->getID();
   }

   std::string createImportWallet(
      const std::filesystem::path& homedir,
      const std::vector<BinaryData>& privateKeys)
   {
      auto privPass = SecureBinaryData::fromString("privPass1");
      Wallets::IO::CreateWalletParams params{
         homedir,
         {1ms, 0, privPass},
         {},
         nullptr, 4
      };

      std::unique_ptr<Seeds::ClearTextSeed> seed(
         new Seeds::ClearTextSeed_Armory());
      auto assetWlt = Wallets::AssetWallet_Single::createFromSeed(
         std::move(seed), params);
      assetWlt->setupImportAccount();

      assetWlt->setPassphrasePromptLambda(
         [&privPass](const std::set<Wallets::EncryptionKeyId>&)->Passphrase::Result
         { return {privPass, true}; }
      );
      for (const auto& privKey : privateKeys) {
         SecureBinaryData privKeySBD{privKey};
         assetWlt->importPrivateKey(privKeySBD, AddressEntryType(
            AddressEntryType::P2PKH | AddressEntryType::Uncompressed
         ));
      }
      assetWlt->resetPassphrasePromptLambda();
      return assetWlt->getID();
   }

   /////////////////////////////////////////////////////////////////////////////
   // wallet creation from bridge
   WalletData progressWalletCreation(std::shared_ptr<Bridge::CppBridge> bridge,
      const std::string& callbackId, const std::string& passphrase,
      std::chrono::milliseconds targetMs, uint32_t targetMB, int lookup,
      bool WO=false, bool isBIP32=false, bool isRestore=false)
   {
      std::string masterId;
      std::filesystem::path path;
      int notifCount = 0;
      std::set<std::string> bip32Accs{ "BIP44", "BIP49", "BIP84" };

      bool run = true;
      while (run) {
         auto result = waitOnReply();
         kj::ArrayPtr<const capnp::word> words(
            reinterpret_cast<const capnp::word*>(result->data.getPtr()),
            result->data.getSize() / sizeof(capnp::word));
         capnp::FlatArrayMessageReader reader(words);
         auto fromBridge = reader.getRoot<Codec::Bridge::FromBridge>();
         if (fromBridge.which() != Codec::Bridge::FromBridge::NOTIFICATION) {
            throw std::runtime_error("invalid FromBridge which");
         }

         auto notif = fromBridge.getNotification();
         if (notif.getCallbackId() != callbackId) {
            throw std::runtime_error("invalid callbackId");
         }
         auto counter = notif.getCounter();

         switch (notif.which())
         {
            case Codec::Bridge::Notification::SET_PASSPHRASE:
            {
               auto wltNotif = notif.getSetPassphrase();
               switch (wltNotif.which())
               {
                  case Codec::Bridge::Notification::SetPassphraseRequest::CONTROL_PASS:
                  {
                     if (notifCount++ != 1) {
                        throw std::runtime_error("count != 1");
                     }
                     capnp::MallocMessageBuilder message;
                     auto toBridge = message.initRoot<Codec::Bridge::ToBridge>();
                     auto notifReply = toBridge.initNotification();
                     notifReply.setSuccess(false);
                     notifReply.setCounter(counter);
                     auto rawReq = serializeCapnp(message);
                     pushRequest(bridge, rawReq);
                     break;
                  }

                  case Codec::Bridge::Notification::SetPassphraseRequest::PRIVATE_PASS:
                  {
                     if (notifCount++ != 3) {
                        throw std::runtime_error("count != 3");
                     }
                     capnp::MallocMessageBuilder message;
                     auto toBridge = message.initRoot<Codec::Bridge::ToBridge>();
                     auto notifReply = toBridge.initNotification();
                     notifReply.setCounter(counter);

                     if (passphrase.empty()) {
                        notifReply.setSuccess(false);
                     } else {
                        notifReply.setSuccess(true);
                        auto capnSetPass = notifReply.initSetPassphrase();
                        capnSetPass.setPassphrase(passphrase);
                        if (targetMB == 0) {
                           capnSetPass.setKdfTargetMs(targetMs.count());
                        } else {
                           capnSetPass.setKdfTargetMB(targetMB);
                        }
                     }

                     auto rawReq = serializeCapnp(message);
                     pushRequest(bridge, rawReq);
                     break;
                  }

                  default:
                     throw std::runtime_error("unexpected wallet creation notif");
               }
               break;
            }

            case Codec::Bridge::Notification::WALLET_PROGRESS:
            {
               auto wltNotif = notif.getWalletProgress();
               switch (wltNotif.which())
               {
                  case Codec::Bridge::Notification::WalletProgress::CREATE_FILE:
                  {
                     if (notifCount++ != 0) {
                        throw std::runtime_error("count != 0");
                     }
                     path = std::filesystem::path(std::string{wltNotif.getCreateFile()});
                     break;
                  }

                  case Codec::Bridge::Notification::WalletProgress::INIT_FILE:
                  {
                     if (notifCount++ != 2) {
                        throw std::runtime_error("count != 2");
                     }
                     if (WO) {
                        ++notifCount;
                     }

                     auto fullPath = std::filesystem::path{"./fakehomedir"} / path;
                     if (!FileUtils::pathExists(fullPath, 0)) {
                        fullPath = std::filesystem::path{"./fakehomedir/temp"} / path;
                        if (!FileUtils::pathExists(fullPath, 0)) {
                           throw std::runtime_error("wallet path is invalid!");
                        }
                     }
                     masterId = wltNotif.getInitFile();
                     break;
                  }

                  case Codec::Bridge::Notification::WalletProgress::READ_FILE:
                  {
                     if (notifCount++ != 4) {
                        throw std::runtime_error("count != 4");
                     }
                     if (wltNotif.getReadFile() != masterId) {
                        throw std::runtime_error("masterId mismatch");
                     }
                     break;
                  }

                  case Codec::Bridge::Notification::WalletProgress::CREATE_ACCOUNT:
                  {
                     if (!isBIP32) {
                        if (notifCount++ != 5) {
                           throw std::runtime_error("count != 5");
                        }
                        EXPECT_EQ(wltNotif.getCreateAccount(), "Armory Legacy");
                     } else {
                        ++notifCount;
                        std::string accountName(wltNotif.getCreateAccount());
                        auto iter = bip32Accs.find(accountName);
                        if (iter == bip32Accs.end()) {
                           throw std::runtime_error("unexpected bip32 acc name");
                        }
                        bip32Accs.erase(iter);
                     }
                     break;
                  }

                  case Codec::Bridge::Notification::WalletProgress::EXTEND_CHAIN:
                  {
                     if (!isBIP32) {
                        if (notifCount++ != 6) {
                           throw std::runtime_error("count != 6");
                        }
                        auto extendNotif = wltNotif.getExtendChain();
                        EXPECT_EQ(extendNotif.getTotal(), lookup);
                        EXPECT_EQ(extendNotif.getCurrent(), 0);
                     } else {
                        auto count = notifCount++;
                        if (count < 6 || count > 10) {
                           throw std::runtime_error("invalid extend chain notif count");
                        }
                        auto extendNotif = wltNotif.getExtendChain();
                        EXPECT_EQ(extendNotif.getTotal(), lookup);
                        EXPECT_EQ(extendNotif.getCurrent(), 0);
                     }
                     break;
                  }

                  default:
                     throw std::runtime_error("unexpected wallet progress notif");
               }
               break;
            }

            case Codec::Bridge::Notification::CLEANUP:
            {
               run = false;
               break;
            }

            case Codec::Bridge::Notification::RESTORE:
            {
               throw std::runtime_error("got a restore notif in wallet creation progress!");
            }

            default:
               throw std::runtime_error(std::string{
                  "unexpected wallet notif: " + std::to_string(notif.which())});
         }
      }

      if (isBIP32) {
         EXPECT_TRUE(bip32Accs.empty());
      }
      auto totalCount = 7;
      if (isBIP32) {
         totalCount += isRestore ? 2 : 4;
      }
      if (!passphrase.empty() && notifCount != totalCount) {
         throw std::runtime_error("unexpected total notif count");
      }

      return WalletData{
         {}, {}, masterId, {},
         {}, {},
         true, false, {}, 0, 0,
         path, 0
      };
   }

   std::string createAWallet(std::shared_ptr<Bridge::CppBridge> bridge,
      std::chrono::milliseconds targetMs, uint32_t lookup, bool isBIP32)
   {
      uint64_t refId = rand();
      auto callbackId = Cryptography::PRNG::fortuna.generateRandom(10).toHexStr();

      capnp::MallocMessageBuilder message;
      auto toBridge = message.initRoot<Codec::Bridge::ToBridge>();
      toBridge.setReferenceId(refId);
      auto request = toBridge.initUtils();
      auto createWltReq = request.initCreateWallet();

      createWltReq.setCallbackId(callbackId);
      if (isBIP32) {
         createWltReq.setWalletType(
            Codec::Bridge::UtilsRequest::WalletType::STRUCTURED_BIP32);
      } else {
         createWltReq.setWalletType(
            Codec::Bridge::UtilsRequest::WalletType::LEGACY);
      }
      createWltReq.setLookup(lookup);
      createWltReq.setLabel("labl");
      createWltReq.setDescription("desc");

      auto rawReq = serializeCapnp(message);
      pushRequest(bridge, rawReq);

      //handle progress notifs
      auto walletData = progressWalletCreation(
         bridge, callbackId,
         "pass1", targetMs, 0, lookup, false, isBIP32);
      auto masterId = walletData.masterId;
      auto path = walletData.path;

      //validate reply
      auto result = waitOnReply();
      kj::ArrayPtr<const capnp::word> words(
         reinterpret_cast<const capnp::word*>(result->data.getPtr()),
         result->data.getSize() / sizeof(capnp::word));
      capnp::FlatArrayMessageReader reader(words);
      auto fromBridge = reader.getRoot<Codec::Bridge::FromBridge>();
      auto reply = fromBridge.getReply();
      if (!reply.getSuccess()) {
         return {};
      }
      if (reply.getReferenceId() != refId) {
         return {};
      }

      if (reply.which() != Codec::Bridge::RpcReply::UTILS) {
         return {};
      }

      auto utilsReply = reply.getUtils();
      if (utilsReply.which() != Codec::Bridge::UtilsReply::CREATE_WALLET) {
         return {};
      }
      return utilsReply.getCreateWallet();
   }

   std::vector<std::string> getWalletBackup(
      std::shared_ptr<Bridge::CppBridge> bridge, const std::string& walletId,
      const std::string& passphrase, Codec::Bridge::WalletBackup::Type backupType)
   {
      uint64_t refId = rand();
      auto callbackId = Cryptography::PRNG::fortuna.generateRandom(10).toHexStr();

      capnp::MallocMessageBuilder message;
      auto toBridge = message.initRoot<Codec::Bridge::ToBridge>();
      toBridge.setReferenceId(refId);
      auto request = toBridge.initWallet();
      request.setWalletId(walletId);
      auto reqBackup = request.initCreateBackupString();
      reqBackup.setPrivate(callbackId);
      auto rawReq = serializeCapnp(message);
      pushRequest(bridge, rawReq);

      //handle unlock and cleanup
      int count = 0;
      bool run = true;
      while (run) {
         auto result = waitOnReply();
         kj::ArrayPtr<const capnp::word> words(
            reinterpret_cast<const capnp::word*>(result->data.getPtr()),
            result->data.getSize() / sizeof(capnp::word));
         capnp::FlatArrayMessageReader reader(words);

         auto fromBridge = reader.getRoot<Codec::Bridge::FromBridge>();
         if (fromBridge.which() != Codec::Bridge::FromBridge::NOTIFICATION) {
            throw std::runtime_error("expected a notif");
         }

         auto notif = fromBridge.getNotification();
         if (notif.getCallbackId() != callbackId) {
            throw std::runtime_error("unexpected callback id");
         }

         switch (notif.which()) {
            case Codec::Bridge::Notification::UNLOCK_REQUEST:
            {
               capnp::MallocMessageBuilder notifMsg;
               auto notifBridge = notifMsg.initRoot<Codec::Bridge::ToBridge>();
               auto notifReply = notifBridge.initNotification();
               notifReply.setCounter(notif.getCounter());
               if (count == 0) {
                  notifReply.setSuccess(true);
                  notifReply.setUnlockRequest(passphrase);
               } else {
                  notifReply.setSuccess(false);
                  run = false;
               }

               auto rawNotif = serializeCapnp(notifMsg);
               pushRequest(bridge, rawNotif);
               ++count;
               break;
            }

            case Codec::Bridge::Notification::CLEANUP:
            {
               run = false;
               break;
            }

            default:
               throw std::runtime_error("unexpected notif type");
         }
      }

      //grab the reply
      auto result = waitOnReply();
      kj::ArrayPtr<const capnp::word> words(
         reinterpret_cast<const capnp::word*>(result->data.getPtr()),
         result->data.getSize() / sizeof(capnp::word));
      capnp::FlatArrayMessageReader reader(words);
      auto fromBridge = reader.getRoot<Codec::Bridge::FromBridge>();
      auto reply = fromBridge.getReply();
      if (reply.getSuccess() == false) {
         throw std::runtime_error(reply.getError());
      }
      if (reply.getReferenceId() != refId) {
         throw std::runtime_error("refId mismatch");
      }

      if (reply.which() != Codec::Bridge::RpcReply::WALLET) {
         throw std::runtime_error("which mismatch");
      }
      auto walletReply = reply.getWallet();

      if (walletReply.which() != Codec::Bridge::WalletReply::CREATE_BACKUP_STRING) {
         throw std::runtime_error("which mismatch");
      }
      auto capnBackup = walletReply.getCreateBackupString();

      //backup type
      if (capnBackup.getBackupType() != backupType) {
         throw std::runtime_error("backup type mismatch");
      }

      //root
      std::vector<std::string> lines;
      for (const auto& line : capnBackup.getRootClear()) {
         lines.emplace_back(line);
      }

      //chaincode
      if (capnBackup.hasChainClear()) {
         for (const auto& line : capnBackup.getChainClear()) {
            lines.emplace_back(line);
         }
      }
      return lines;
   }

   WalletData restoreWallet(
      std::shared_ptr<Bridge::CppBridge> bridge, const std::vector<std::string>& lines,
      const std::string& expectedWltId, Codec::Bridge::WalletBackup::Type backupType,
      const std::string& passphrase, std::chrono::milliseconds targetMs, uint32_t targetMB,
      bool merge, unsigned expectedLookup)
   {
      if (lines.size() < 2) {
         throw std::runtime_error("1");
      }

      bool isBIP32 =
         backupType == Codec::Bridge::WalletBackup::Type::ARMORY200_B ? true : false;

      //restore the wallet
      uint64_t refId = rand();
      auto callbackId = Cryptography::PRNG::fortuna.generateRandom(10).toHexStr();

      {
         capnp::MallocMessageBuilder message;
         auto toBridge = message.initRoot<Codec::Bridge::ToBridge>();
         toBridge.setReferenceId(refId);
         auto request = toBridge.initUtils();
         auto restoreWltReq = request.initRestoreWallet();

         restoreWltReq.setCallbackId(callbackId);

         size_t lineIndex = 0;
         if (lines.size() == 5) {
            restoreWltReq.setBackupId(lines[lineIndex++]);
         }

         auto rootLines = restoreWltReq.initRoot(2);
         rootLines.set(0, lines[lineIndex++]);
         rootLines.set(1, lines[lineIndex++]);

         if (lineIndex < lines.size()) {
            auto ccLines = restoreWltReq.initChaincode(2);
            ccLines.set(0, lines[lineIndex++]);
            ccLines.set(1, lines[lineIndex++]);
         }

         auto rawReq = serializeCapnp(message);
         pushRequest(bridge, rawReq);
      }

      //backup deser notifs
      bool restoringBackup = true;
      while (restoringBackup) {
         auto result = waitOnReply();
         kj::ArrayPtr<const capnp::word> words(
            reinterpret_cast<const capnp::word*>(result->data.getPtr()),
            result->data.getSize() / sizeof(capnp::word));
         capnp::FlatArrayMessageReader reader(words);
         auto fromBridge = reader.getRoot<Codec::Bridge::FromBridge>();
         if (fromBridge.which() != Codec::Bridge::FromBridge::NOTIFICATION) {
            throw std::runtime_error("2");
         }

         auto notif = fromBridge.getNotification();
         if (notif.getCallbackId() != callbackId) {
            throw std::runtime_error("3");
         }

         switch (notif.which())
         {
            case Codec::Bridge::Notification::RESTORE:
            {
               auto restoreNotif = notif.getRestore();
               switch (restoreNotif.which())
               {
                  case Codec::Bridge::Notification::RestorePrompt::CHECK_WALLET_ID:
                  {
                     //return merge decision
                     auto meta = restoreNotif.getCheckWalletId();
                     if (meta.getWalletId() != expectedWltId ||
                        meta.getBackupType() != backupType) {
                        throw std::runtime_error("4");
                     }

                     auto counter = notif.getCounter();
                     capnp::MallocMessageBuilder notifMsg;
                     auto toBridge = notifMsg.initRoot<Codec::Bridge::ToBridge>();
                     toBridge.setReferenceId(rand());

                     auto notifReply = toBridge.initNotification();
                     notifReply.setSuccess(true);
                     notifReply.setCounter(counter);
                     notifReply.setRestore(merge ?
                        Codec::Bridge::NotificationReply::RestoreMode::MERGE :
                        Codec::Bridge::NotificationReply::RestoreMode::OVERWRITE
                     );

                     auto rawReq = serializeCapnp(notifMsg);
                     pushRequest(bridge, rawReq);
                     restoringBackup = false;
                     break;
                  }

                  default:
                     throw std::runtime_error("5");
               }

               break;
            }

            default:
               throw std::runtime_error("6");
         }
      }

      //create wallet notifs
      auto wltData = progressWalletCreation(
         bridge, callbackId,
         passphrase, targetMs, targetMB,
         expectedLookup, lines.size() == 5, isBIP32, true);

      //validate reply
      auto result = waitOnReply();
      kj::ArrayPtr<const capnp::word> words(
         reinterpret_cast<const capnp::word*>(result->data.getPtr()),
         result->data.getSize() / sizeof(capnp::word));
      capnp::FlatArrayMessageReader reader(words);
      auto fromBridge = reader.getRoot<Codec::Bridge::FromBridge>();
      if (fromBridge.which() != Codec::Bridge::FromBridge::REPLY) {
         throw std::runtime_error("7");
      }

      auto reply = fromBridge.getReply();
      if (!reply.getSuccess() || reply.getReferenceId() != refId) {
         throw std::runtime_error("8");
      }
      return wltData;
   }

   ////////
   std::map<std::string, WalletData> loadWallets(
      std::shared_ptr<Bridge::CppBridge> bridge)
   {
      uint64_t refId = rand();

      capnp::MallocMessageBuilder message;
      auto toBridge = message.initRoot<Codec::Bridge::ToBridge>();
      toBridge.setReferenceId(refId);
      auto request = toBridge.initWalletManager();
      request.setLoadWallets();

      auto rawReq = serializeCapnp(message);
      pushRequest(bridge, rawReq);

      auto result = waitOnReply();
      kj::ArrayPtr<const capnp::word> words(
         reinterpret_cast<const capnp::word*>(result->data.getPtr()),
         result->data.getSize() / sizeof(capnp::word));
      capnp::FlatArrayMessageReader reader(words);
      auto fromBridge = reader.getRoot<Codec::Bridge::FromBridge>();
      auto reply = fromBridge.getReply();
      if (!reply.getSuccess() || reply.getReferenceId() != refId) {
         throw std::runtime_error({});
      }

      auto replyMgr = reply.getWalletManager();
      auto replyLoadWlts = replyMgr.getLoadWallets();

      std::map<std::string, WalletData> wltsData;
      for (const auto& capnWlt : replyLoadWlts) {
         wltsData.emplace(capnWlt.getWalletId(), capnToWalletData(capnWlt));
      }
      return wltsData;
   }

   std::map<std::string, WltListEntry> listWallets(
      std::shared_ptr<Bridge::CppBridge> bridge)
   {
      uint64_t refId = rand();

      capnp::MallocMessageBuilder message;
      auto toBridge = message.initRoot<Codec::Bridge::ToBridge>();
      toBridge.setReferenceId(refId);
      auto request = toBridge.initWalletManager();
      request.setListWallets();

      auto rawReq = serializeCapnp(message);
      pushRequest(bridge, rawReq);

      auto result = waitOnReply();
      kj::ArrayPtr<const capnp::word> words(
         reinterpret_cast<const capnp::word*>(result->data.getPtr()),
         result->data.getSize() / sizeof(capnp::word));
      capnp::FlatArrayMessageReader reader(words);
      auto fromBridge = reader.getRoot<Codec::Bridge::FromBridge>();
      auto reply = fromBridge.getReply();
      if (!reply.getSuccess() || reply.getReferenceId() != refId) {
         throw std::runtime_error({});
      }

      auto replyMgr = reply.getWalletManager();
      auto replyListWlts = replyMgr.getListWallets();

      std::map<std::string, WltListEntry> wltMap;
      for (auto capnEntry : replyListWlts) {
         std::vector<std::string> accountIds;
         for (auto accId : capnEntry.getAccountIds()) {
            accountIds.emplace_back(std::string{accId});
         }
         wltMap.emplace(std::string(capnEntry.getPath()),
            WltListEntry{
               capnEntry.getWalletId(),
               accountIds,
               (int)capnEntry.getState(),
               capnEntry.getStaged(),
               capnEntry.getWatchingOnly()}
            );
      }
      return wltMap;
   }

   std::set<std::string> getWalletAccountIds(
      std::shared_ptr<Bridge::CppBridge> bridge,
      const std::string& walletId)
   {
      uint64_t refId = rand();

      capnp::MallocMessageBuilder message;
      auto toBridge = message.initRoot<Codec::Bridge::ToBridge>();
      toBridge.setReferenceId(refId);
      auto request = toBridge.initWallet();
      request.setWalletId(walletId);
      request.setGetAccountIds();

      auto rawReq = serializeCapnp(message);
      pushRequest(bridge, rawReq);

      auto result = waitOnReply();
      kj::ArrayPtr<const capnp::word> words(
         reinterpret_cast<const capnp::word*>(result->data.getPtr()),
         result->data.getSize() / sizeof(capnp::word));
      capnp::FlatArrayMessageReader reader(words);
      auto fromBridge = reader.getRoot<Codec::Bridge::FromBridge>();
      auto reply = fromBridge.getReply();
      if (!reply.getSuccess() || reply.getReferenceId() != refId) {
         throw std::runtime_error({});
      }

      auto replyMgr = reply.getWallet();
      std::set<std::string> accIdSet;
      for (auto accId : replyMgr.getGetAccountIds()) {
         accIdSet.emplace(std::string(accId));
      }
      return accIdSet;
   }

   WalletData getWalletData(
      std::shared_ptr<Bridge::CppBridge> bridge,
      const std::string& walletId, const std::string& accId)
   {
      uint64_t refId = rand();

      capnp::MallocMessageBuilder message;
      auto toBridge = message.initRoot<Codec::Bridge::ToBridge>();
      toBridge.setReferenceId(refId);
      auto request = toBridge.initWallet();
      request.setWalletId(walletId);
      request.setAccountId(accId);
      request.setGetData();

      auto rawReq = serializeCapnp(message);
      pushRequest(bridge, rawReq);

      auto result = waitOnReply();
      kj::ArrayPtr<const capnp::word> words(
         reinterpret_cast<const capnp::word*>(result->data.getPtr()),
         result->data.getSize() / sizeof(capnp::word));
      capnp::FlatArrayMessageReader reader(words);
      auto fromBridge = reader.getRoot<Codec::Bridge::FromBridge>();
      auto reply = fromBridge.getReply();
      if (!reply.getSuccess() || reply.getReferenceId() != refId) {
         throw std::runtime_error({});
      }

      auto replyMgr = reply.getWallet();
      return capnToWalletData(replyMgr.getGetData());
   }

   bool changeWalletPassphrase(
      std::shared_ptr<Bridge::CppBridge> bridge,
      const std::string& walletId,
      const std::string& currentPass, const std::string& newPass)
   {
      auto callbackId = Cryptography::PRNG::fortuna.generateRandom(10).toHexStr();
      uint64_t refId = rand();

      //start passphrase change sequence
      {
         capnp::MallocMessageBuilder message;
         auto toBridge = message.initRoot<Codec::Bridge::ToBridge>();
         toBridge.setReferenceId(refId);
         auto request = toBridge.initWallet();
         request.setWalletId(walletId);

         auto passReq = request.initChangePassphrase();
         passReq.setCallbackId(callbackId);
         passReq.setPrivate();

         auto rawReq = serializeCapnp(message);
         pushRequest(bridge, rawReq);
      }

      //wait on passphrase prompt
      {
         auto rawPrompt = waitOnReply();
         kj::ArrayPtr<const capnp::word> words(
            reinterpret_cast<const capnp::word*>(rawPrompt->data.getPtr()),
            rawPrompt->data.getSize() / sizeof(capnp::word));
         capnp::FlatArrayMessageReader reader(words);
         auto fromBridge = reader.getRoot<Codec::Bridge::FromBridge>();
         if (fromBridge.which() != Codec::Bridge::FromBridge::NOTIFICATION) {
            return false;
         }

         auto notif = fromBridge.getNotification();
         if (notif.getCallbackId() != callbackId) {
            return false;
         }
         if (notif.which() != Codec::Bridge::Notification::UNLOCK_REQUEST) {
            return false;
         }
         auto counter = notif.getCounter();

         //push passphrase
         capnp::MallocMessageBuilder message;
         auto toBridge = message.initRoot<Codec::Bridge::ToBridge>();
         auto notifReply = toBridge.initNotification();
         notifReply.setSuccess(true);
         notifReply.setCounter(counter);
         notifReply.setUnlockRequest(currentPass);

         auto rawReq = serializeCapnp(message);
         pushRequest(bridge, rawReq);
      }

      //wait on new passphrase prompt
      {
         auto rawPrompt = waitOnReply();
         kj::ArrayPtr<const capnp::word> words(
            reinterpret_cast<const capnp::word*>(rawPrompt->data.getPtr()),
            rawPrompt->data.getSize() / sizeof(capnp::word));
         capnp::FlatArrayMessageReader reader(words);
         auto fromBridge = reader.getRoot<Codec::Bridge::FromBridge>();
         if (fromBridge.which() != Codec::Bridge::FromBridge::NOTIFICATION) {
            return false;
         }

         auto notif = fromBridge.getNotification();
         if (notif.getCallbackId() != callbackId) {
            return false;
         }
         if (notif.which() != Codec::Bridge::Notification::SET_PASSPHRASE) {
            return false;
         }
         auto counter = notif.getCounter();

         //push passphrase
         capnp::MallocMessageBuilder message;
         auto toBridge = message.initRoot<Codec::Bridge::ToBridge>();
         auto notifReply = toBridge.initNotification();
         notifReply.setSuccess(true);
         notifReply.setCounter(counter);
         auto capnSetPass = notifReply.initSetPassphrase();
         capnSetPass.setPassphrase(newPass);
         capnSetPass.setReuseKdf(true);

         auto rawReq = serializeCapnp(message);
         pushRequest(bridge, rawReq);
      }

      //expect success reply
      {
         auto rawPrompt = waitOnReply();
         kj::ArrayPtr<const capnp::word> words(
            reinterpret_cast<const capnp::word*>(rawPrompt->data.getPtr()),
            rawPrompt->data.getSize() / sizeof(capnp::word));
         capnp::FlatArrayMessageReader reader(words);
         auto fromBridge = reader.getRoot<Codec::Bridge::FromBridge>();
         if (fromBridge.which() != Codec::Bridge::FromBridge::REPLY) {
            return false;
         }

         auto reply = fromBridge.getReply();
         if (reply.getReferenceId() != refId || !reply.getSuccess()) {
            return false;
         }
      }

      //expect cleanup notif
      {
         auto rawPrompt = waitOnReply();
         kj::ArrayPtr<const capnp::word> words(
            reinterpret_cast<const capnp::word*>(rawPrompt->data.getPtr()),
            rawPrompt->data.getSize() / sizeof(capnp::word));
         capnp::FlatArrayMessageReader reader(words);
         auto fromBridge = reader.getRoot<Codec::Bridge::FromBridge>();
         if (fromBridge.which() != Codec::Bridge::FromBridge::NOTIFICATION) {
            return false;
         }

         auto notif = fromBridge.getNotification();
         if (notif.getCallbackId() != callbackId) {
            return false;
         }
         if (notif.which() != Codec::Bridge::Notification::CLEANUP) {
            return false;
         }
      }

      return true;
   }

   WalletData extendAddressPool(
      std::shared_ptr<Bridge::CppBridge> bridge,
      const std::string& walletId, const std::string& accountId,
      const std::string& dbId, unsigned count, bool isNew)
   {
      auto callbackId = Cryptography::PRNG::fortuna.generateRandom(10).toHexStr();
      uint64_t refId = rand();

      //start chain extension sequence
      {
         capnp::MallocMessageBuilder message;
         auto toBridge = message.initRoot<Codec::Bridge::ToBridge>();
         toBridge.setReferenceId(refId);
         auto request = toBridge.initWallet();
         request.setWalletId(walletId);
         request.setAccountId(accountId);

         auto extendReq = request.initExtendAddressPool();
         extendReq.setCallbackId(callbackId);
         extendReq.setCount(count);
         extendReq.setIsNew(isNew);

         auto rawReq = serializeCapnp(message);
         pushRequest(bridge, rawReq);
      }
      bool run = true;
      MsgPtr rawReply;
      unsigned notifCount = 0;
      unsigned lastKnownCount = 0;
      std::string refreshId;
      while (run) {
         auto rawPrompt = waitOnReply();
         kj::ArrayPtr<const capnp::word> words(
            reinterpret_cast<const capnp::word*>(rawPrompt->data.getPtr()),
            rawPrompt->data.getSize() / sizeof(capnp::word));
         capnp::FlatArrayMessageReader reader(words);
         auto fromBridge = reader.getRoot<Codec::Bridge::FromBridge>();

         switch (fromBridge.which())
         {
            case Codec::Bridge::FromBridge::NOTIFICATION:
            {
               auto notif = fromBridge.getNotification();
               switch (notif.which())
               {
                  case Codec::Bridge::Notification::WALLET_PROGRESS:
                  {
                     auto wltNotif = notif.getWalletProgress();
                     switch (wltNotif.which())
                     {
                        case Codec::Bridge::Notification::WalletProgress::EXTEND_CHAIN:
                        {
                           auto extNotif = wltNotif.getExtendChain();
                           if (extNotif.getTotal() != count) {
                              throw std::runtime_error("invalid total count");
                           }
                           ++notifCount;
                           lastKnownCount = extNotif.getCurrent();
                           break;
                        }

                        default:
                           throw std::runtime_error("invalid wallet progress which");
                     }
                     break;
                  }

                  case Codec::Bridge::Notification::REFRESH:
                  {
                     if (notif.getCallbackId() != "bdm_callback") {
                        throw std::runtime_error("invalid callbackId for refresh");
                     }
                     if (!refreshId.empty()) {
                        throw std::runtime_error("refresh already seen!");
                     }

                     auto refreshIds = notif.getRefresh();
                     if (refreshIds.size() != 1) {
                        throw std::runtime_error("unexpected refreshId count");
                     }
                     refreshId = refreshIds[0];
                     break;
                  }

                  default:
                     throw std::runtime_error("invalid notif which");
               }
               break;
            }

            case Codec::Bridge::FromBridge::REPLY:
            {
               auto reply = fromBridge.getReply();
               if (!reply.getSuccess() || reply.getReferenceId() != refId) {
                  throw std::runtime_error("request failed");
               }
               run = false;
               break;
            }

            default:
               throw std::runtime_error("invalid bridge which");
         }
      }

      if (notifCount < 3) {
         throw std::runtime_error("too few notifs");
      }
      if (lastKnownCount != count) {
         throw std::runtime_error("invalid count in final notif");
      }

      //expect cleanup notif
      {
         auto rawPrompt = waitOnReply();
         kj::ArrayPtr<const capnp::word> words(
            reinterpret_cast<const capnp::word*>(rawPrompt->data.getPtr()),
            rawPrompt->data.getSize() / sizeof(capnp::word));
         capnp::FlatArrayMessageReader reader(words);
         auto fromBridge = reader.getRoot<Codec::Bridge::FromBridge>();
         if (fromBridge.which() != Codec::Bridge::FromBridge::NOTIFICATION) {
            throw std::runtime_error("expected notif");
         }

         auto notif = fromBridge.getNotification();
         if (notif.getCallbackId() != callbackId) {
            throw std::runtime_error("unexpected callbackId");
         }
         if (notif.which() != Codec::Bridge::Notification::CLEANUP) {
            throw std::runtime_error("expected cleanup notif");
         }
      }

      //expect refresh notif
      while (refreshId.empty()) {
         auto rawPrompt = waitOnReply();
         kj::ArrayPtr<const capnp::word> words(
            reinterpret_cast<const capnp::word*>(rawPrompt->data.getPtr()),
            rawPrompt->data.getSize() / sizeof(capnp::word));
         capnp::FlatArrayMessageReader reader(words);
         auto fromBridge = reader.getRoot<Codec::Bridge::FromBridge>();
         if (fromBridge.which() != Codec::Bridge::FromBridge::NOTIFICATION) {
            throw std::runtime_error("expected notif");
         }

         auto notif = fromBridge.getNotification();
         if (notif.getCallbackId() == "progress") {
            continue;
         } else if (notif.getCallbackId() != "bdm_callback") {
            throw std::runtime_error("expected bdm notif");
         }
         if (notif.which() != Codec::Bridge::Notification::REFRESH) {
            throw std::runtime_error("expected refresh notif");
         }

         auto refreshIds = notif.getRefresh();
         if (refreshIds.size() != 1) {
            throw std::runtime_error("unexpected refreshId");
         }
         refreshId = refreshIds[0];
         break;
      }

      if (refreshId != dbId) {
         throw std::runtime_error("dbId mismatch");
      }

      //lastly, grab updated wallet data
      return getWalletData(bridge, walletId, accountId);
   }

   AddressData getAddress(
      std::shared_ptr<Bridge::CppBridge> bridge,
      const std::string& walletId,
      const std::string& accountId,
      uint32_t addressType=0)
   {
      uint64_t refId = rand();
      capnp::MallocMessageBuilder message;
      auto toBridge = message.initRoot<Codec::Bridge::ToBridge>();
      toBridge.setReferenceId(refId);
      auto request = toBridge.initWallet();
      request.setWalletId(walletId);
      request.setAccountId(accountId);
      auto reqAddr = request.initGetAddress();
      reqAddr.setNew();
      reqAddr.setType(addressType);
      auto rawReq = serializeCapnp(message);
      pushRequest(bridge, rawReq);

      auto result = waitOnReply();
      kj::ArrayPtr<const capnp::word> words(
         reinterpret_cast<const capnp::word*>(result->data.getPtr()),
         result->data.getSize() / sizeof(capnp::word));
      capnp::FlatArrayMessageReader reader(words);
      auto fromBridge = reader.getRoot<Codec::Bridge::FromBridge>();
      auto reply = fromBridge.getReply();
      if (reply.getSuccess() == false) {
         throw std::runtime_error("failure: " + std::string{reply.getError()});
      }
      if (reply.getReferenceId() != refId) {
         throw std::runtime_error("refId mismatch");
      }

      if (reply.which() != Codec::Bridge::RpcReply::WALLET) {
         throw std::runtime_error("which mismatch");
      }
      auto walletReply = reply.getWallet();

      if (walletReply.which() != Codec::Bridge::WalletReply::GET_ADDRESS) {
         throw std::runtime_error("which mismatch");
      }

      return capnToAddressData(walletReply.getGetAddress());
   }

   bool stageWallet(
      std::shared_ptr<Bridge::CppBridge> bridge,
      const std::string& walletId,
      bool stage)
   {
      uint64_t refId = rand();

      //request staging change
      {
         capnp::MallocMessageBuilder message;
         auto toBridge = message.initRoot<Codec::Bridge::ToBridge>();
         toBridge.setReferenceId(refId);
         auto request = toBridge.initWalletManager();
         auto stageReq = request.initStageWallet();

         stageReq.setWalletId(walletId);
         stageReq.setStage(stage);

         auto rawReq = serializeCapnp(message);
         pushRequest(bridge, rawReq);
      }

      //expect success
      auto result = waitOnReply();
      kj::ArrayPtr<const capnp::word> words(
         reinterpret_cast<const capnp::word*>(result->data.getPtr()),
         result->data.getSize() / sizeof(capnp::word));
      capnp::FlatArrayMessageReader reader(words);
      auto fromBridge = reader.getRoot<Codec::Bridge::FromBridge>();
      auto reply = fromBridge.getReply();
      if (reply.getReferenceId() != refId) {
         return false;
      }
      return reply.getSuccess();
   }

   /////////////////////////////////////////////////////////////////////////////
   // connect to db stuff
   bool waitOnConnection(Bridge::MessageId refId)
   {
      bool success = false;
      while (true) {
         auto reply = waitOnReply();
         kj::ArrayPtr<const capnp::word> words(
            reinterpret_cast<const capnp::word*>(reply->data.getPtr()),
            reply->data.getSize() / sizeof(capnp::word));
         capnp::FlatArrayMessageReader reader(words);

         auto fromBridge = reader.getRoot<Codec::Bridge::FromBridge>();
         switch (fromBridge.which()) {
            case Codec::Bridge::FromBridge::NOTIFICATION:
            {
               auto notif = fromBridge.getNotification();
               switch (notif.which()) {
                  case Codec::Bridge::Notification::SETUP_DONE:
                     success = true;
                     break;

                  case Codec::Bridge::Notification::DISCONNECTED:
                     success = false;
                     break;

                  default:
                     throw std::runtime_error("unexpected connection notif which");
               }
               break;
            }

            case Codec::Bridge::FromBridge::REPLY:
            {
               auto repCapnp = fromBridge.getReply();
               if (repCapnp.getReferenceId() != refId) {
                  throw std::runtime_error("referenceId mismatch");
               }
               if (repCapnp.getSuccess() == false) {
                  std::cout << std::string(repCapnp.getError()) << std::endl;
               }
               if (repCapnp.getSuccess() != success) {
                  throw std::runtime_error("connection state mismatch");
               }
               return success;
            }

            default:
               EXPECT_TRUE(false);
               break;
         }
      }
   }

   bool connectToIp(std::shared_ptr<Bridge::CppBridge> bridge,
      const std::string& ip, const std::string& port,
      const std::string& expectedPubkey)
   {
      uint64_t refId = rand();
      auto callbackId = Cryptography::PRNG::fortuna.generateRandom(4).toHexStr();
      {
         capnp::MallocMessageBuilder message;
         auto toBridge = message.initRoot<Codec::Bridge::ToBridge>();
         toBridge.setReferenceId(refId);
         auto request = toBridge.initSetup();
         auto connectReq = request.initConnectToIp();
         connectReq.setIp(ip);
         connectReq.setPort(port);
         connectReq.setCallbackId(callbackId);

         auto rawReq = serializeCapnp(message);
         pushRequest(bridge, rawReq);
      }

      //wait on key presentation
      auto reply = waitOnReply();
      kj::ArrayPtr<const capnp::word> words(
         reinterpret_cast<const capnp::word*>(reply->data.getPtr()),
         reply->data.getSize() / sizeof(capnp::word));
      capnp::FlatArrayMessageReader reader(words);

      auto fromBridge = reader.getRoot<Codec::Bridge::FromBridge>();
      if (fromBridge.which() != Codec::Bridge::FromBridge::NOTIFICATION) {
         return false;
      }

      auto notif = fromBridge.getNotification();
      if (notif.which() != Codec::Bridge::Notification::PRESENT_PUBKEY) {
         return false;
      }
      std::string presentedKey(notif.getPresentPubkey());

      //reply to request
      {
         capnp::MallocMessageBuilder message;
         auto toBridge = message.initRoot<Codec::Bridge::ToBridge>();
         toBridge.setReferenceId(refId);
         auto notifReply = toBridge.initNotification();
         notifReply.setCounter(notif.getCounter());
         notifReply.setPresentPubkey();
         notifReply.setSuccess(presentedKey == expectedPubkey);

         auto rawReq = serializeCapnp(message);
         pushRequest(bridge, rawReq);
      }
      return waitOnConnection(refId);
   }

   bool connectToPeer(std::shared_ptr<Bridge::CppBridge> bridge,
      const std::string& peerKey)
   {
      uint64_t refId = rand();
      capnp::MallocMessageBuilder message;
      auto toBridge = message.initRoot<Codec::Bridge::ToBridge>();
      toBridge.setReferenceId(refId);
      auto request = toBridge.initSetup();
      request.setConnectToPeer(peerKey);

      auto rawReq = serializeCapnp(message);
      pushRequest(bridge, rawReq);
      return waitOnConnection(refId);
   }

   bool automateDb(std::shared_ptr<Bridge::CppBridge> bridge,
      const std::filesystem::path& satoshiDir,
      const std::filesystem::path& dbDir)
   {
      uint64_t refId = rand();
      capnp::MallocMessageBuilder message;
      auto toBridge = message.initRoot<Codec::Bridge::ToBridge>();
      toBridge.setReferenceId(refId);
      auto request = toBridge.initSetup();
      auto autoDbReq = request.initAutomateDb();
      autoDbReq.setSatoshiPath(satoshiDir.string());
      autoDbReq.setDbDir(dbDir.string());

      auto rawReq = serializeCapnp(message);
      pushRequest(bridge, rawReq);
      return waitOnConnection(refId);
   }

   bool registerWallets(std::shared_ptr<Bridge::CppBridge> bridge)
   {
      uint64_t refId = rand();
      capnp::MallocMessageBuilder message;
      auto toBridge = message.initRoot<Codec::Bridge::ToBridge>();
      toBridge.setReferenceId(refId);
      auto request = toBridge.initService();
      request.setRegisterWallets();

      auto rawReq = serializeCapnp(message);
      pushRequest(bridge, rawReq);

      //expecting setup done notif
      auto reply = waitOnReply();
      kj::ArrayPtr<const capnp::word> words(
         reinterpret_cast<const capnp::word*>(reply->data.getPtr()),
         reply->data.getSize() / sizeof(capnp::word));
      capnp::FlatArrayMessageReader reader(words);

      auto fromBridge = reader.getRoot<Codec::Bridge::FromBridge>();
      if (fromBridge.which() != Codec::Bridge::FromBridge::NOTIFICATION) {
         return false;
      }

      auto notif = fromBridge.getNotification();
      if (notif.which() != Codec::Bridge::Notification::REGISTER_DONE) {
         std::cout << "expected register_done notif, instead got: " <<
            (int)notif.which() << std::endl;
         return false;
      }
      return true;
   }

   bool registerWallet(std::shared_ptr<Bridge::CppBridge> bridge,
      const std::string& walletId, const std::string& accountId,
      const std::string& dbId)
   {
      uint64_t refId = rand();
      capnp::MallocMessageBuilder message;
      auto toBridge = message.initRoot<Codec::Bridge::ToBridge>();
      toBridge.setReferenceId(refId);

      auto request = toBridge.initService();
      auto regReq = request.initRegisterWallet();
      regReq.setWalletId(walletId);
      regReq.setAccountId(accountId);
      regReq.setIsNew(false);

      auto rawReq = serializeCapnp(message);
      pushRequest(bridge, rawReq);

      //expecting refresh notif
      while (true) {
         auto reply = waitOnReply();
         kj::ArrayPtr<const capnp::word> words(
            reinterpret_cast<const capnp::word*>(reply->data.getPtr()),
            reply->data.getSize() / sizeof(capnp::word));
         capnp::FlatArrayMessageReader reader(words);

         auto fromBridge = reader.getRoot<Codec::Bridge::FromBridge>();
         if (fromBridge.which() != Codec::Bridge::FromBridge::NOTIFICATION) {
            continue;
         }

         auto notif = fromBridge.getNotification();
         switch (notif.which())
         {
            case Codec::Bridge::Notification::REFRESH:
            {
               auto refreshIds = notif.getRefresh();
               if (refreshIds.size() != 1) {
                  continue;
               }
               std::string idStr = refreshIds[0];
               if (idStr == dbId) {
                  return true;
               }
               break;
            }

            case Codec::Bridge::Notification::SCAN_PROGRESS:
               break;

            default:
               return false;
         }
      }
      return false;
   }

   int goOnline(std::shared_ptr<Bridge::CppBridge> bridge)
   {
      uint64_t refId = rand();
      capnp::MallocMessageBuilder message;
      auto toBridge = message.initRoot<Codec::Bridge::ToBridge>();
      toBridge.setReferenceId(refId);
      auto request = toBridge.initSetup();
      request.setGoOnline();
      auto rawReq = serializeCapnp(message);
      pushRequest(bridge, rawReq);

      bool run = true;
      int newBlock = -1;
      while (run) {
         auto reply = waitOnReply();
         kj::ArrayPtr<const capnp::word> words(
            reinterpret_cast<const capnp::word*>(reply->data.getPtr()),
            reply->data.getSize() / sizeof(capnp::word));
         capnp::FlatArrayMessageReader reader(words);

         auto fromBridge = reader.getRoot<Codec::Bridge::FromBridge>();
         if (fromBridge.which() != Codec::Bridge::FromBridge::NOTIFICATION) {
            return -2;
         }

         auto notif = fromBridge.getNotification();
         switch (notif.which()) {
            case Codec::Bridge::Notification::SCAN_PROGRESS:
               break;

            case Codec::Bridge::Notification::READY:
            {
               newBlock = notif.getReady();
               return newBlock;
            }

            case Codec::Bridge::Notification::NODE_STATUS:
            {
               //ignore
               break;
            }

            default:
               std::cout << "unexpected db init notif: " << notif.which() << std::endl;
               return -3;
         }
      }
      return -1;
   }

   int waitOnNewBlock()
   {
      auto reply = waitOnReply();
      kj::ArrayPtr<const capnp::word> words(
         reinterpret_cast<const capnp::word*>(reply->data.getPtr()),
         reply->data.getSize() / sizeof(capnp::word));
      capnp::FlatArrayMessageReader reader(words);

      auto fromBridge = reader.getRoot<Codec::Bridge::FromBridge>();
      if (fromBridge.which() != Codec::Bridge::FromBridge::NOTIFICATION) {
         return -2;
      }

      auto notif = fromBridge.getNotification();
      switch (notif.which()) {
         case Codec::Bridge::Notification::NEW_BLOCK:
         {
            return notif.getNewBlock();
         }

         default:
            std::cout << "unexpected db notif: " << notif.which() << std::endl;
            return -3;
      }
      return -1;
   }

   /////////////////////////////////////////////////////////////////////////////
   // balances
   std::vector<uint64_t> getWalletBalance(
      std::shared_ptr<Bridge::CppBridge> bridge,
      const std::string& wltId, const std::string& accId)
   {
      uint64_t refId = rand();
      capnp::MallocMessageBuilder message;
      auto toBridge = message.initRoot<Codec::Bridge::ToBridge>();
      toBridge.setReferenceId(refId);
      auto req = toBridge.initWallet();
      req.setWalletId(wltId);
      req.setAccountId(accId);
      req.setGetBalanceAndCount();
      auto rawReq = serializeCapnp(message);
      pushRequest(bridge, rawReq);

      //process reply
      auto resp = waitOnReply();
      kj::ArrayPtr<const capnp::word> words(
         reinterpret_cast<const capnp::word*>(resp->data.getPtr()),
         resp->data.getSize() / sizeof(capnp::word));
      capnp::FlatArrayMessageReader reader(words);

      auto fromBridge = reader.getRoot<Codec::Bridge::FromBridge>();
      if (fromBridge.which() != Codec::Bridge::FromBridge::REPLY) {
         return {};
      }
      auto reply = fromBridge.getReply();
      if (!reply.getSuccess()) {
         return {};
      }
      if (reply.which() != Codec::Bridge::RpcReply::WALLET) {
         return {};
      }

      auto walletReply = reply.getWallet();
      if (walletReply.which() != Codec::Bridge::WalletReply::GET_BALANCE_AND_COUNT) {
         return {};
      }
      auto balStruct = walletReply.getGetBalanceAndCount();
      return {
         balStruct.getFull(),
         balStruct.getSpendable(),
         balStruct.getUnconfirmed(),
         balStruct.getTxnCount()
      };
   }

   std::map<BinaryData, std::vector<uint64_t>> getAddrBalances(
      std::shared_ptr<Bridge::CppBridge> bridge,
      const std::string& wltId, const std::string& accId)
   {
      uint64_t refId = rand();
      capnp::MallocMessageBuilder message;
      auto toBridge = message.initRoot<Codec::Bridge::ToBridge>();
      toBridge.setReferenceId(refId);
      auto req = toBridge.initWallet();
      req.setWalletId(wltId);
      req.setAccountId(accId);
      req.setGetAddrCombinedList();
      auto rawReq = serializeCapnp(message);
      pushRequest(bridge, rawReq);

      //process reply
      auto resp = waitOnReply();
      kj::ArrayPtr<const capnp::word> words(
         reinterpret_cast<const capnp::word*>(resp->data.getPtr()),
         resp->data.getSize() / sizeof(capnp::word));
      capnp::FlatArrayMessageReader reader(words);

      auto fromBridge = reader.getRoot<Codec::Bridge::FromBridge>();
      if (fromBridge.which() != Codec::Bridge::FromBridge::REPLY) {
         return {};
      }
      auto reply = fromBridge.getReply();
      if (!reply.getSuccess()) {
         return {};
      }
      if (reply.which() != Codec::Bridge::RpcReply::WALLET) {
         return {};
      }

      auto walletReply = reply.getWallet();
      if (walletReply.which() != Codec::Bridge::WalletReply::GET_ADDR_COMBINED_LIST) {
         return {};
      }
      auto capnCombList = walletReply.getGetAddrCombinedList();
      auto capnBalances = capnCombList.getBalances();

      std::map<BinaryData, std::vector<uint64_t>> result;
      for (const auto& capnBal : capnBalances) {
         auto scrAddrCapn = capnBal.getScrAddr();
         BinaryData scrAddrRef{scrAddrCapn.begin(), scrAddrCapn.end()};
         auto iter = result.emplace(scrAddrRef, std::vector<uint64_t>{}).first;

         auto balCapn = capnBal.getBalances();
         iter->second.emplace_back(balCapn.getFull());
         iter->second.emplace_back(balCapn.getSpendable());
         iter->second.emplace_back(balCapn.getUnconfirmed());
         iter->second.emplace_back(balCapn.getTxnCount());
      }
      return result;
   }

   /////////////////////////////////////////////////////////////////////////////
   // ledger stuff
   std::string getLedgerDelegateId(std::shared_ptr<Bridge::CppBridge> bridge)
   {
      uint64_t refId = rand();
      capnp::MallocMessageBuilder message;
      auto toBridge = message.initRoot<Codec::Bridge::ToBridge>();
      toBridge.setReferenceId(refId);
      auto req = toBridge.initWalletManager();
      req.setGetMainLedgerDelegateId();
      auto rawReq = serializeCapnp(message);
      pushRequest(bridge, rawReq);

      //process reply
      auto resp = waitOnReply();
      kj::ArrayPtr<const capnp::word> words(
         reinterpret_cast<const capnp::word*>(resp->data.getPtr()),
         resp->data.getSize() / sizeof(capnp::word));
      capnp::FlatArrayMessageReader reader(words);

      auto fromBridge = reader.getRoot<Codec::Bridge::FromBridge>();
      if (fromBridge.which() != Codec::Bridge::FromBridge::REPLY) {
         return {};
      }
      auto reply = fromBridge.getReply();
      if (!reply.getSuccess()) {
         return {};
      }
      if (reply.which() != Codec::Bridge::RpcReply::WALLET_MANAGER) {
         return {};
      }
      auto mgrReply = reply.getWalletManager();
      return mgrReply.getGetMainLedgerDelegateId();
   }

   std::string getLedgerDelegateIdForWallet(
      std::shared_ptr<Bridge::CppBridge> bridge,
      const std::string& wltId, const std::string& accId)
   {
      uint64_t refId = rand();
      capnp::MallocMessageBuilder message;
      auto toBridge = message.initRoot<Codec::Bridge::ToBridge>();
      toBridge.setReferenceId(refId);
      auto req = toBridge.initWallet();
      req.setWalletId(wltId);
      req.setAccountId(accId);
      req.setGetLedgerDelegateId();
      auto rawReq = serializeCapnp(message);
      pushRequest(bridge, rawReq);

      //process reply
      auto resp = waitOnReply();
      kj::ArrayPtr<const capnp::word> words(
         reinterpret_cast<const capnp::word*>(resp->data.getPtr()),
         resp->data.getSize() / sizeof(capnp::word));
      capnp::FlatArrayMessageReader reader(words);

      auto fromBridge = reader.getRoot<Codec::Bridge::FromBridge>();
      if (fromBridge.which() != Codec::Bridge::FromBridge::REPLY) {
         return {};
      }
      auto reply = fromBridge.getReply();
      if (!reply.getSuccess()) {
         return {};
      }
      if (reply.which() != Codec::Bridge::RpcReply::WALLET) {
         return {};
      }
      auto serviceReply = reply.getWallet();
      return serviceReply.getGetLedgerDelegateId();
   }

   std::string getLedgerDelegateIdForScrAddr(
      std::shared_ptr<Bridge::CppBridge> bridge,
      const std::string& wltId, const std::string& accId,
      const BinaryData& scrAddr)
   {
      uint64_t refId = rand();
      capnp::MallocMessageBuilder message;
      auto toBridge = message.initRoot<Codec::Bridge::ToBridge>();
      toBridge.setReferenceId(refId);
      auto req = toBridge.initWallet();
      req.setWalletId(wltId);
      req.setAccountId(accId);
      req.setGetLedgerDelegateIdForScrAddr(capnp::Data::Builder(
         (uint8_t*)scrAddr.getPtr(), scrAddr.getSize()));
      auto rawReq = serializeCapnp(message);
      pushRequest(bridge, rawReq);

      //process reply
      auto resp = waitOnReply();
      kj::ArrayPtr<const capnp::word> words(
         reinterpret_cast<const capnp::word*>(resp->data.getPtr()),
         resp->data.getSize() / sizeof(capnp::word));
      capnp::FlatArrayMessageReader reader(words);

      auto fromBridge = reader.getRoot<Codec::Bridge::FromBridge>();
      if (fromBridge.which() != Codec::Bridge::FromBridge::REPLY) {
         return {};
      }
      auto reply = fromBridge.getReply();
      if (!reply.getSuccess()) {
         return {};
      }
      if (reply.which() != Codec::Bridge::RpcReply::WALLET) {
         return {};
      }
      auto serviceReply = reply.getWallet();
      return serviceReply.getGetLedgerDelegateIdForScrAddr();
   }

   size_t getLedgersPageCount(std::shared_ptr<Bridge::CppBridge> bridge,
      const std::string& delegateId)
   {
      uint64_t refId = rand();
      capnp::MallocMessageBuilder message;
      auto toBridge = message.initRoot<Codec::Bridge::ToBridge>();
      toBridge.setReferenceId(refId);
      auto req = toBridge.initDelegate();
      req.setId(delegateId);
      req.setGetPageCount();
      auto rawReq = serializeCapnp(message);
      pushRequest(bridge, rawReq);

      //process reply
      auto resp = waitOnReply();
      kj::ArrayPtr<const capnp::word> words(
         reinterpret_cast<const capnp::word*>(resp->data.getPtr()),
         resp->data.getSize() / sizeof(capnp::word));
      capnp::FlatArrayMessageReader reader(words);

      auto fromBridge = reader.getRoot<Codec::Bridge::FromBridge>();
      if (fromBridge.which() != Codec::Bridge::FromBridge::REPLY) {
         return SIZE_MAX;
      }
      auto reply = fromBridge.getReply();
      if (!reply.getSuccess()) {
         return SIZE_MAX;
      }
      if (reply.which() != Codec::Bridge::RpcReply::DELEGATE) {
         return SIZE_MAX;
      }
      auto delegateReply = reply.getDelegate();
      return delegateReply.getGetPageCount();
   }

   std::vector<Ledgers::Entry> getLedgersPage(
      std::shared_ptr<Bridge::CppBridge> bridge,
      const std::string& delegateId, uint32_t pageId)
   {
      uint64_t refId = rand();
      capnp::MallocMessageBuilder message;
      auto toBridge = message.initRoot<Codec::Bridge::ToBridge>();
      toBridge.setReferenceId(refId);
      auto req = toBridge.initDelegate();
      req.setId(delegateId);
      auto pageReq = req.initGetPages();
      pageReq.setFirst(pageId);
      pageReq.setLast(pageId);
      auto rawReq = serializeCapnp(message);
      pushRequest(bridge, rawReq);

      //process reply
      auto resp = waitOnReply();
      kj::ArrayPtr<const capnp::word> words(
         reinterpret_cast<const capnp::word*>(resp->data.getPtr()),
         resp->data.getSize() / sizeof(capnp::word));
      capnp::FlatArrayMessageReader reader(words);

      auto fromBridge = reader.getRoot<Codec::Bridge::FromBridge>();
      if (fromBridge.which() != Codec::Bridge::FromBridge::REPLY) {
         return {};
      }
      auto reply = fromBridge.getReply();
      if (!reply.getSuccess()) {
         return {};
      }
      if (reply.which() != Codec::Bridge::RpcReply::DELEGATE) {
         return {};
      }

      auto delegateReply = reply.getDelegate();
      auto result = capnToLedgers(delegateReply.getGetPages());
      if (result.size() != 1) {
         throw std::runtime_error("unexpected ledger size");
      }
      return result[0];
   }

   bool checkLedgers(const Ledgers::Entry& lhs, const TestChain::LedgerEntryValue& rhs)
   {
      return lhs.getValue() == rhs.balance &&
         lhs.getBlockNum() == rhs.height &&
         lhs.getIndex() == rhs.index &&
         lhs.getTxTime() == rhs.txTime &&
         lhs.getTxHash() == rhs.txHash &&
         lhs.isSentToSelf() == rhs.sts;
   }

   void checkBalances(
      const std::map<BinaryData, std::vector<uint64_t>>& balances,
      uint32_t height, bool reorg)
   {
      const auto& addrMap = reorg ?
         TestChain::testAddrBalances_Reorg[height] :
         TestChain::testAddrBalances[height];
      try {
         for (const auto& balPair : balances) {
            const auto& addrBal = addrMap.at(balPair.first);
            EXPECT_EQ(addrBal[0], balPair.second[0]) << balPair.first.toHexStr();
            EXPECT_EQ(addrBal[1], balPair.second[1]) << balPair.first.toHexStr();
            EXPECT_EQ(addrBal[2], balPair.second[2]) << balPair.first.toHexStr();
            EXPECT_EQ(addrBal[3], balPair.second[3]) << balPair.first.toHexStr();
         }
      } catch (const std::exception&) {
         ASSERT_TRUE(false);
      }
   }

   void printLedgers(const std::vector<Ledgers::Entry>& vec)
   {
      for (const auto& lhs : vec) {
         std::cout << "amount: " << lhs.getValue();
         std::cout << ", height: " << lhs.getBlockNum();
         std::cout << ", index: " << lhs.getIndex();
         std::cout << ", time: " << lhs.getTxTime();
         std::cout << ", sts: " << lhs.isSentToSelf() << std::endl;
      }
   }

   /////////////////////////////////////////////////////////////////////////////
   // wallet data lookups
   std::map<BinaryData, std::set<BinaryData>> getAddressBook(
      std::shared_ptr<Bridge::CppBridge> bridge,
      const std::string& walletId, const std::string& accountId)
   {
      uint64_t refId = rand();
      capnp::MallocMessageBuilder message;
      auto toBridge = message.initRoot<Codec::Bridge::ToBridge>();
      toBridge.setReferenceId(refId);
      auto req = toBridge.initWallet();
      req.setWalletId(walletId);
      req.setAccountId(accountId);
      req.setCreateAddressBook();
      auto rawReq = serializeCapnp(message);
      pushRequest(bridge, rawReq);

      //process reply
      auto resp = waitOnReply();
      kj::ArrayPtr<const capnp::word> words(
         reinterpret_cast<const capnp::word*>(resp->data.getPtr()),
         resp->data.getSize() / sizeof(capnp::word));
      capnp::FlatArrayMessageReader reader(words);

      auto fromBridge = reader.getRoot<Codec::Bridge::FromBridge>();
      if (fromBridge.which() != Codec::Bridge::FromBridge::REPLY) {
         return {};
      }
      auto reply = fromBridge.getReply();
      if (!reply.getSuccess()) {
         return {};
      }
      if (reply.which() != Codec::Bridge::RpcReply::WALLET) {
         return {};
      }

      auto walletReply = reply.getWallet();
      auto addrBookCapn = walletReply.getCreateAddressBook();
      std::map<BinaryData, std::set<BinaryData>> result;
      for (auto entry : addrBookCapn.getEntries()) {
         auto capnAddr = entry.getScrAddr();
         auto iter = result.emplace(
            BinaryData{capnAddr.begin(), capnAddr.size()},
            std::set<BinaryData>{}).first;
         auto& hashSet = iter->second;
         for (auto capnHash : entry.getTxHashes()) {
            hashSet.emplace(BinaryData{capnHash.begin(), capnHash.size()});
         }
      }
      return result;
   }

   const std::vector<UTXO> getUTXOs(
      std::shared_ptr<Bridge::CppBridge> bridge,
      const std::string& walletId, const std::string& accountId,
      int mode)
   {
      uint64_t refId = rand();
      capnp::MallocMessageBuilder message;
      auto toBridge = message.initRoot<Codec::Bridge::ToBridge>();
      toBridge.setReferenceId(refId);
      auto req = toBridge.initWallet();
      req.setWalletId(walletId);
      req.setAccountId(accountId);

      auto utxoReq = req.initGetUtxos();
      switch (mode)
      {
         case 0:
            utxoReq.setValue(UINT64_MAX);
            break;

         case 1:
            utxoReq.setZc();
            break;

         case 2:
            utxoReq.setRbf();
            break;

         default:
            throw std::runtime_error("invalid utxos fetch mode");
      }
      auto rawReq = serializeCapnp(message);
      pushRequest(bridge, rawReq);

      //process reply
      auto resp = waitOnReply();
      kj::ArrayPtr<const capnp::word> words(
         reinterpret_cast<const capnp::word*>(resp->data.getPtr()),
         resp->data.getSize() / sizeof(capnp::word));
      capnp::FlatArrayMessageReader reader(words);

      auto fromBridge = reader.getRoot<Codec::Bridge::FromBridge>();
      if (fromBridge.which() != Codec::Bridge::FromBridge::REPLY) {
         return {};
      }
      auto reply = fromBridge.getReply();
      if (!reply.getSuccess()) {
         return {};
      }
      if (reply.which() != Codec::Bridge::RpcReply::WALLET) {
         return {};
      }

      auto walletReply = reply.getWallet();
      auto utxoReply = walletReply.getGetUtxos();
      std::vector<UTXO> result;
      for (auto capnUtxo : utxoReply) {
         auto capnOutput = capnUtxo.getOutput();
         auto capnHash = capnOutput.getTxHash();
         BinaryData txHash{capnHash.begin(), capnHash.size()};

         auto capnScript = capnOutput.getScript();
         BinaryData script{capnScript.begin(), capnScript.size()};

         result.emplace_back(UTXO{
            capnOutput.getValue(), capnOutput.getTxHeight(),
            capnOutput.getTxIndex(), capnOutput.getTxOutIndex(),
            std::move(txHash), std::move(script)
         });
      }

      std::sort(result.begin(), result.end());
      return result;
   }

   /////////////////////////////////////////////////////////////////////////////
   // coin selection stuff
   const std::string getNewCoinSelector(
      std::shared_ptr<Bridge::CppBridge> bridge,
      const std::string& walletId, const std::string& accountId,
      uint32_t height)
   {
      uint64_t refId = rand();
      capnp::MallocMessageBuilder message;
      auto toBridge = message.initRoot<Codec::Bridge::ToBridge>();
      toBridge.setReferenceId(refId);
      auto req = toBridge.initWallet();
      req.setWalletId(walletId);
      req.setAccountId(accountId);
      req.setSetupNewCoinSelectionInstance(height);
      auto rawReq = serializeCapnp(message);
      pushRequest(bridge, rawReq);

      //process reply
      auto resp = waitOnReply();
      kj::ArrayPtr<const capnp::word> words(
         reinterpret_cast<const capnp::word*>(resp->data.getPtr()),
         resp->data.getSize() / sizeof(capnp::word));
      capnp::FlatArrayMessageReader reader(words);

      auto fromBridge = reader.getRoot<Codec::Bridge::FromBridge>();
      if (fromBridge.which() != Codec::Bridge::FromBridge::REPLY) {
         return {};
      }
      auto reply = fromBridge.getReply();
      if (!reply.getSuccess()) {
         return {};
      }
      if (reply.which() != Codec::Bridge::RpcReply::WALLET) {
         return {};
      }

      auto walletReply = reply.getWallet();
      return walletReply.getSetupNewCoinSelectionInstance();
   }

   bool cleanupCoinSelectionInstance(
      std::shared_ptr<Bridge::CppBridge> bridge,
      const std::string& csId)
   {
      uint64_t refId = rand();
      capnp::MallocMessageBuilder message;
      auto toBridge = message.initRoot<Codec::Bridge::ToBridge>();
      toBridge.setReferenceId(refId);
      auto req = toBridge.initCoinSelection();
      req.setId(csId);
      req.setCleanup();
      auto rawReq = serializeCapnp(message);
      pushRequest(bridge, rawReq);

      //process reply
      auto resp = waitOnReply();
      kj::ArrayPtr<const capnp::word> words(
         reinterpret_cast<const capnp::word*>(resp->data.getPtr()),
         resp->data.getSize() / sizeof(capnp::word));
      capnp::FlatArrayMessageReader reader(words);

      auto fromBridge = reader.getRoot<Codec::Bridge::FromBridge>();
      if (fromBridge.which() != Codec::Bridge::FromBridge::REPLY) {
         return false;
      }
      auto reply = fromBridge.getReply();
      return reply.getSuccess();
   }

   bool setNewCsRecipient(std::shared_ptr<Bridge::CppBridge> bridge,
      const std::string& csId, unsigned recId,
      const std::string& addr, uint64_t amount)
   {
      uint64_t refId = rand();
      capnp::MallocMessageBuilder message;
      auto toBridge = message.initRoot<Codec::Bridge::ToBridge>();
      toBridge.setReferenceId(refId);
      auto req = toBridge.initCoinSelection();
      req.setId(csId);
      auto recReq = req.initSetRecipient();
      recReq.setId(recId);
      recReq.setAddress(addr);
      recReq.setValue(amount);
      auto rawReq = serializeCapnp(message);
      pushRequest(bridge, rawReq);

      auto resp = waitOnReply();
      kj::ArrayPtr<const capnp::word> words(
         reinterpret_cast<const capnp::word*>(resp->data.getPtr()),
         resp->data.getSize() / sizeof(capnp::word));
      capnp::FlatArrayMessageReader reader(words);

      auto fromBridge = reader.getRoot<Codec::Bridge::FromBridge>();
      if (fromBridge.which() != Codec::Bridge::FromBridge::REPLY) {
         return false;
      }
      auto reply = fromBridge.getReply();
      return reply.getSuccess();
   }

   bool selectUtxos(std::shared_ptr<Bridge::CppBridge> bridge,
      const std::string& csId, uint32_t flags, float feeByte)
   {
      uint64_t refId = rand();
      capnp::MallocMessageBuilder message;
      auto toBridge = message.initRoot<Codec::Bridge::ToBridge>();
      toBridge.setReferenceId(refId);
      auto req = toBridge.initCoinSelection();
      req.setId(csId);
      auto selectReq = req.initSelectUtxos();
      selectReq.setFlags(flags);
      selectReq.setFeeByte(feeByte);
      auto rawReq = serializeCapnp(message);
      pushRequest(bridge, rawReq);

      //process reply
      auto resp = waitOnReply();
      kj::ArrayPtr<const capnp::word> words(
         reinterpret_cast<const capnp::word*>(resp->data.getPtr()),
         resp->data.getSize() / sizeof(capnp::word));
      capnp::FlatArrayMessageReader reader(words);

      auto fromBridge = reader.getRoot<Codec::Bridge::FromBridge>();
      if (fromBridge.which() != Codec::Bridge::FromBridge::REPLY) {
         return false;
      }
      auto reply = fromBridge.getReply();
      return reply.getSuccess();
   }

   bool processCustomUtxoList(std::shared_ptr<Bridge::CppBridge> bridge,
      const std::string& csId, const std::vector<UTXO>& utxos,
      uint32_t flags, uint64_t flatFee)
   {
      uint64_t refId = rand();
      capnp::MallocMessageBuilder message;
      auto toBridge = message.initRoot<Codec::Bridge::ToBridge>();
      toBridge.setReferenceId(refId);
      auto req = toBridge.initCoinSelection();
      req.setId(csId);
      auto customReq = req.initProcessCustomUtxoList();
      customReq.setFlatFee(flatFee);
      customReq.setFlags(flags);

      auto capnUtxos = customReq.initUtxos(utxos.size());
      for (unsigned i = 0; i < utxos.size(); i++) {
         auto capnUtxo = capnUtxos[i];
         const auto& utxo = utxos[i];

         capnUtxo.setValue(utxo.getAmount());
         capnUtxo.setTxHeight(utxo.getHeight());
         capnUtxo.setTxIndex(utxo.getTxIndex());
         capnUtxo.setTxOutIndex(utxo.getTxOutIndex());
         capnUtxo.setTxHash(capnp::Data::Builder(
            (uint8_t*)utxo.getTxHash().getPtr(), utxo.getTxHash().getSize()));
         capnUtxo.setScript(capnp::Data::Builder(
            (uint8_t*)utxo.getScript().getPtr(), utxo.getScript().getSize()));
      }
      auto rawReq = serializeCapnp(message);
      pushRequest(bridge, rawReq);

      //process reply
      auto resp = waitOnReply();
      kj::ArrayPtr<const capnp::word> words(
         reinterpret_cast<const capnp::word*>(resp->data.getPtr()),
         resp->data.getSize() / sizeof(capnp::word));
      capnp::FlatArrayMessageReader reader(words);

      auto fromBridge = reader.getRoot<Codec::Bridge::FromBridge>();
      if (fromBridge.which() != Codec::Bridge::FromBridge::REPLY) {
         return false;
      }
      auto reply = fromBridge.getReply();
      return reply.getSuccess();
   }

   std::vector<UTXO> getUtxoSelection(
      std::shared_ptr<Bridge::CppBridge> bridge,
      const std::string& csId)
   {
      uint64_t refId = rand();
      capnp::MallocMessageBuilder message;
      auto toBridge = message.initRoot<Codec::Bridge::ToBridge>();
      toBridge.setReferenceId(refId);
      auto req = toBridge.initCoinSelection();
      req.setId(csId);
      req.setGetUtxoSelection();
      auto rawReq = serializeCapnp(message);
      pushRequest(bridge, rawReq);

      //process reply
      auto resp = waitOnReply();
      kj::ArrayPtr<const capnp::word> words(
         reinterpret_cast<const capnp::word*>(resp->data.getPtr()),
         resp->data.getSize() / sizeof(capnp::word));
      capnp::FlatArrayMessageReader reader(words);

      auto fromBridge = reader.getRoot<Codec::Bridge::FromBridge>();
      if (fromBridge.which() != Codec::Bridge::FromBridge::REPLY) {
         return {};
      }
      auto reply = fromBridge.getReply();
      if (!reply.getSuccess()) {
         return {};
      }
      if (reply.which() != Codec::Bridge::RpcReply::COIN_SELECTION) {
         return {};
      }

      auto csReply = reply.getCoinSelection();
      auto utxoReply = csReply.getGetUtxoSelection();
      std::vector<UTXO> result;
      for (auto capnUtxo : utxoReply) {
         auto capnOutput = capnUtxo.getOutput();
         auto capnHash = capnOutput.getTxHash();
         BinaryData txHash{capnHash.begin(), capnHash.size()};

         auto capnScript = capnOutput.getScript();
         BinaryData script{capnScript.begin(), capnScript.size()};

         result.emplace_back(UTXO{
            capnOutput.getValue(), capnOutput.getTxHeight(),
            capnOutput.getTxIndex(), capnOutput.getTxOutIndex(),
            std::move(txHash), std::move(script)
         });
      }
      return result;
   }

   uint64_t getFlatFee(
      std::shared_ptr<Bridge::CppBridge> bridge,
      const std::string& csId)
   {
      uint64_t refId = rand();
      capnp::MallocMessageBuilder message;
      auto toBridge = message.initRoot<Codec::Bridge::ToBridge>();
      toBridge.setReferenceId(refId);
      auto req = toBridge.initCoinSelection();
      req.setId(csId);
      req.setGetFlatFee();
      auto rawReq = serializeCapnp(message);
      pushRequest(bridge, rawReq);

      //process reply
      auto resp = waitOnReply();
      kj::ArrayPtr<const capnp::word> words(
         reinterpret_cast<const capnp::word*>(resp->data.getPtr()),
         resp->data.getSize() / sizeof(capnp::word));
      capnp::FlatArrayMessageReader reader(words);

      auto fromBridge = reader.getRoot<Codec::Bridge::FromBridge>();
      if (fromBridge.which() != Codec::Bridge::FromBridge::REPLY) {
         return UINT64_MAX;
      }
      auto reply = fromBridge.getReply();
      if (!reply.getSuccess()) {
         return UINT64_MAX;
      }
      if (reply.which() != Codec::Bridge::RpcReply::COIN_SELECTION) {
         return UINT64_MAX;
      }
      return reply.getCoinSelection().getGetFlatFee();
   }

   /////////////////////////////////////////////////////////////////////////////
   // tx signing
   std::string getNewSigner(std::shared_ptr<Bridge::CppBridge> bridge)
   {
      uint64_t refId = rand();
      capnp::MallocMessageBuilder message;
      auto toBridge = message.initRoot<Codec::Bridge::ToBridge>();
      toBridge.setReferenceId(refId);
      auto req = toBridge.initSigner();
      req.setGetNew();
      auto rawReq = serializeCapnp(message);
      pushRequest(bridge, rawReq);

      //process reply
      auto resp = waitOnReply();
      kj::ArrayPtr<const capnp::word> words(
         reinterpret_cast<const capnp::word*>(resp->data.getPtr()),
         resp->data.getSize() / sizeof(capnp::word));
      capnp::FlatArrayMessageReader reader(words);

      auto fromBridge = reader.getRoot<Codec::Bridge::FromBridge>();
      if (fromBridge.which() != Codec::Bridge::FromBridge::REPLY) {
         return {};
      }
      auto reply = fromBridge.getReply();
      if (!reply.getSuccess()) {
         return {};
      }
      if (reply.which() != Codec::Bridge::RpcReply::SIGNER) {
         return {};
      }

      auto signerReply = reply.getSigner();
      return signerReply.getGetNew();
   }

   bool setSignerVersion(
      std::shared_ptr<Bridge::CppBridge> bridge, const std::string& signerId,
      uint32_t version)
   {
      uint64_t refId = rand();
      capnp::MallocMessageBuilder message;
      auto toBridge = message.initRoot<Codec::Bridge::ToBridge>();
      toBridge.setReferenceId(refId);
      auto req = toBridge.initSigner();
      req.setId(signerId);
      req.setSetVersion(version);
      auto rawReq = serializeCapnp(message);
      pushRequest(bridge, rawReq);

      //process reply
      auto resp = waitOnReply();
      kj::ArrayPtr<const capnp::word> words(
         reinterpret_cast<const capnp::word*>(resp->data.getPtr()),
         resp->data.getSize() / sizeof(capnp::word));
      capnp::FlatArrayMessageReader reader(words);

      auto fromBridge = reader.getRoot<Codec::Bridge::FromBridge>();
      if (fromBridge.which() != Codec::Bridge::FromBridge::REPLY) {
         return false;
      }
      auto reply = fromBridge.getReply();
      return reply.getSuccess();
   }

   std::map<BinaryData, BinaryData> getTxsByHash(
      std::shared_ptr<Bridge::CppBridge> bridge,
      const std::set<BinaryData>& hashSet)
   {
      uint64_t refId = rand();
      capnp::MallocMessageBuilder message;
      auto toBridge = message.initRoot<Codec::Bridge::ToBridge>();
      toBridge.setReferenceId(refId);
      auto req = toBridge.initService();
      auto txsReq = req.initGetTxsByHash(hashSet.size());
      unsigned i=0;
      for (const auto& hash : hashSet) {
         txsReq.set(i++, capnp::Data::Builder(
            (uint8_t*)hash.getPtr(), hash.getSize()));
      }
      auto rawReq = serializeCapnp(message);
      pushRequest(bridge, rawReq);

      //process reply
      auto resp = waitOnReply();
      kj::ArrayPtr<const capnp::word> words(
         reinterpret_cast<const capnp::word*>(resp->data.getPtr()),
         resp->data.getSize() / sizeof(capnp::word));
      capnp::FlatArrayMessageReader reader(words);

      auto fromBridge = reader.getRoot<Codec::Bridge::FromBridge>();
      if (fromBridge.which() != Codec::Bridge::FromBridge::REPLY) {
         return {};
      }
      auto reply = fromBridge.getReply();
      if (!reply.getSuccess()) {
         return {};
      }
      if (reply.which() != Codec::Bridge::RpcReply::SERVICE) {
         return {};
      }

      auto serviceReply = reply.getService();
      auto capnTxs = serviceReply.getGetTxsByHash();
      std::map<BinaryData, BinaryData> result;
      for (auto capnTx : capnTxs) {
         auto capnHash = capnTx.getHash();
         BinaryDataRef hash{capnHash.begin(), capnHash.size()};

         auto capnRaw = capnTx.getRaw();
         BinaryDataRef raw{capnRaw.begin(), capnRaw.size()};
         result.emplace(hash, raw);
      }
      return result;
   }

   bool addSpenderByOutpoint(
      std::shared_ptr<Bridge::CppBridge> bridge, const std::string& signerId,
      const BinaryData& hash, uint32_t index, uint32_t sequence)
   {
      uint64_t refId = rand();
      capnp::MallocMessageBuilder message;
      auto toBridge = message.initRoot<Codec::Bridge::ToBridge>();
      toBridge.setReferenceId(refId);
      auto req = toBridge.initSigner();
      req.setId(signerId);

      auto spenderReq = req.initAddSpenderByOutpoint();
      spenderReq.setHash(capnp::Data::Builder(
         (uint8_t*)hash.getPtr(), hash.getSize()));
      spenderReq.setTxOutId(index);
      spenderReq.setSequence(sequence);
      auto rawReq = serializeCapnp(message);
      pushRequest(bridge, rawReq);

      //process reply
      auto resp = waitOnReply();
      kj::ArrayPtr<const capnp::word> words(
         reinterpret_cast<const capnp::word*>(resp->data.getPtr()),
         resp->data.getSize() / sizeof(capnp::word));
      capnp::FlatArrayMessageReader reader(words);

      auto fromBridge = reader.getRoot<Codec::Bridge::FromBridge>();
      if (fromBridge.which() != Codec::Bridge::FromBridge::REPLY) {
         return false;
      }
      auto reply = fromBridge.getReply();
      return reply.getSuccess();
   }

   bool populateUtxo(
      std::shared_ptr<Bridge::CppBridge> bridge, const std::string& signerId,
      const BinaryData& hash, uint32_t index,
      uint64_t value, const BinaryData& script)
   {
      uint64_t refId = rand();
      capnp::MallocMessageBuilder message;
      auto toBridge = message.initRoot<Codec::Bridge::ToBridge>();
      toBridge.setReferenceId(refId);
      auto req = toBridge.initSigner();
      req.setId(signerId);

      auto utxoReq = req.initPopulateUtxo();
      utxoReq.setHash(capnp::Data::Builder(
         (uint8_t*)hash.getPtr(), hash.getSize()));
      utxoReq.setTxOutId(index);
      utxoReq.setScript(capnp::Data::Builder(
         (uint8_t*)script.getPtr(), script.getSize()));
      utxoReq.setValue(value);
      auto rawReq = serializeCapnp(message);
      pushRequest(bridge, rawReq);

      //process reply
      auto resp = waitOnReply();
      kj::ArrayPtr<const capnp::word> words(
         reinterpret_cast<const capnp::word*>(resp->data.getPtr()),
         resp->data.getSize() / sizeof(capnp::word));
      capnp::FlatArrayMessageReader reader(words);

      auto fromBridge = reader.getRoot<Codec::Bridge::FromBridge>();
      if (fromBridge.which() != Codec::Bridge::FromBridge::REPLY) {
         return false;
      }
      auto reply = fromBridge.getReply();
      return reply.getSuccess();
   }

   bool addSupportingTx(
      std::shared_ptr<Bridge::CppBridge> bridge, const std::string& signerId,
      const BinaryData& rawTx)
   {
      uint64_t refId = rand();
      capnp::MallocMessageBuilder message;
      auto toBridge = message.initRoot<Codec::Bridge::ToBridge>();
      toBridge.setReferenceId(refId);
      auto req = toBridge.initSigner();
      req.setId(signerId);
      req.setAddSupportingTx(capnp::Data::Builder(
         (uint8_t*)rawTx.getPtr(), rawTx.getSize()));
      auto rawReq = serializeCapnp(message);
      pushRequest(bridge, rawReq);

      //process reply
      auto resp = waitOnReply();
      kj::ArrayPtr<const capnp::word> words(
         reinterpret_cast<const capnp::word*>(resp->data.getPtr()),
         resp->data.getSize() / sizeof(capnp::word));
      capnp::FlatArrayMessageReader reader(words);

      auto fromBridge = reader.getRoot<Codec::Bridge::FromBridge>();
      if (fromBridge.which() != Codec::Bridge::FromBridge::REPLY) {
         return false;
      }
      auto reply = fromBridge.getReply();
      return reply.getSuccess();
   }

   bool addSignerRecipient(
      std::shared_ptr<Bridge::CppBridge> bridge, const std::string& signerId,
      const BinaryData& script, uint64_t value)
   {
      uint64_t refId = rand();
      capnp::MallocMessageBuilder message;
      auto toBridge = message.initRoot<Codec::Bridge::ToBridge>();
      toBridge.setReferenceId(refId);
      auto req = toBridge.initSigner();
      req.setId(signerId);

      auto recipientReq = req.initAddRecipient();
      recipientReq.setScript(capnp::Data::Builder(
         (uint8_t*)script.getPtr(), script.getSize()));
      recipientReq.setValue(value);
      auto rawReq = serializeCapnp(message);
      pushRequest(bridge, rawReq);

      //process reply
      auto resp = waitOnReply();
      kj::ArrayPtr<const capnp::word> words(
         reinterpret_cast<const capnp::word*>(resp->data.getPtr()),
         resp->data.getSize() / sizeof(capnp::word));
      capnp::FlatArrayMessageReader reader(words);

      auto fromBridge = reader.getRoot<Codec::Bridge::FromBridge>();
      if (fromBridge.which() != Codec::Bridge::FromBridge::REPLY) {
         return false;
      }
      auto reply = fromBridge.getReply();
      return reply.getSuccess();
   }

   bool resolveSigner(
      std::shared_ptr<Bridge::CppBridge> bridge, const std::string& signerId,
      const std::string& walletId)
   {
      uint64_t refId = rand();
      capnp::MallocMessageBuilder message;
      auto toBridge = message.initRoot<Codec::Bridge::ToBridge>();
      toBridge.setReferenceId(refId);
      auto req = toBridge.initSigner();
      req.setId(signerId);
      req.setResolve(walletId);
      auto rawReq = serializeCapnp(message);
      pushRequest(bridge, rawReq);

      //process reply
      auto resp = waitOnReply();
      kj::ArrayPtr<const capnp::word> words(
         reinterpret_cast<const capnp::word*>(resp->data.getPtr()),
         resp->data.getSize() / sizeof(capnp::word));
      capnp::FlatArrayMessageReader reader(words);

      auto fromBridge = reader.getRoot<Codec::Bridge::FromBridge>();
      if (fromBridge.which() != Codec::Bridge::FromBridge::REPLY) {
         return false;
      }
      auto reply = fromBridge.getReply();
      return reply.getSuccess();
   }

   bool signTx(
      std::shared_ptr<Bridge::CppBridge> bridge, const std::string& signerId,
      const std::string& walletId, const std::string& passphrase)
   {
      uint64_t refId = rand();
      auto callbackId = Cryptography::PRNG::fortuna.generateRandom(10).toHexStr();
      capnp::MallocMessageBuilder message;
      auto toBridge = message.initRoot<Codec::Bridge::ToBridge>();
      toBridge.setReferenceId(refId);
      auto req = toBridge.initSigner();
      req.setId(signerId);
      auto signReq = req.initSignTx();
      signReq.setWalletId(walletId);
      signReq.setCallbackId(callbackId);
      auto rawReq = serializeCapnp(message);
      pushRequest(bridge, rawReq);

      //handle unlock request
      bool hasCleanedUp = false;
      bool hasReply = false;
      bool success = false;
      while (!hasCleanedUp || !hasReply) {
         auto resp = waitOnReply();
         kj::ArrayPtr<const capnp::word> words(
            reinterpret_cast<const capnp::word*>(resp->data.getPtr()),
            resp->data.getSize() / sizeof(capnp::word));
         capnp::FlatArrayMessageReader reader(words);

         auto fromBridge = reader.getRoot<Codec::Bridge::FromBridge>();
         switch (fromBridge.which())
         {
            case Codec::Bridge::FromBridge::NOTIFICATION:
            {
               auto notif = fromBridge.getNotification();
               if (notif.getCallbackId() != callbackId) {
                  return false;
               }

               switch (notif.which())
               {
                  case Codec::Bridge::Notification::UNLOCK_REQUEST:
                  {
                     //reply with passphrase
                     auto counter = notif.getCounter();
                     capnp::MallocMessageBuilder passReply;
                     auto toBridge = passReply.initRoot<Codec::Bridge::ToBridge>();
                     auto notifReply = toBridge.initNotification();
                     notifReply.setSuccess(true);
                     notifReply.setCounter(counter);
                     notifReply.setUnlockRequest(passphrase);

                     auto rawRep = serializeCapnp(passReply);
                     pushRequest(bridge, rawRep);
                     break;
                  }

                  case Codec::Bridge::Notification::CLEANUP:
                  {
                     hasCleanedUp = true;
                     break;
                  }

                  default:
                     return false;
               }
               break;
            }

            case Codec::Bridge::FromBridge::REPLY:
            {
               auto reply = fromBridge.getReply();
               success = reply.getSuccess();
               hasReply = true;
               break;
            }

            default:
               EXPECT_TRUE(false);
               break;
         }
      }
      return success;
   }

   struct InputState
   {
      const bool isValid = false;
      const uint32_t sigCount = UINT32_MAX;
   };

   InputState getInputState(
      std::shared_ptr<Bridge::CppBridge> bridge, const std::string& signerId,
      uint32_t inputId)
   {
      uint64_t refId = rand();
      capnp::MallocMessageBuilder message;
      auto toBridge = message.initRoot<Codec::Bridge::ToBridge>();
      toBridge.setReferenceId(refId);
      auto req = toBridge.initSigner();
      req.setId(signerId);
      req.setGetSignedStateForInput(inputId);
      auto rawReq = serializeCapnp(message);
      pushRequest(bridge, rawReq);

      //process reply
      auto resp = waitOnReply();
      kj::ArrayPtr<const capnp::word> words(
         reinterpret_cast<const capnp::word*>(resp->data.getPtr()),
         resp->data.getSize() / sizeof(capnp::word));
      capnp::FlatArrayMessageReader reader(words);

      auto fromBridge = reader.getRoot<Codec::Bridge::FromBridge>();
      if (fromBridge.which() != Codec::Bridge::FromBridge::REPLY) {
         return {};
      }
      auto reply = fromBridge.getReply();

      if (reply.getSuccess() == false) {
         return {};
      }
      if (reply.which() != Codec::Bridge::RpcReply::SIGNER) {
         return {};
      }

      auto inputStateCapn = reply.getSigner().getGetSignedStateForInput();
      return InputState{ inputStateCapn.getIsValid(), inputStateCapn.getSigCount() };
   }

   BinaryData getSignedTx(
      std::shared_ptr<Bridge::CppBridge> bridge, const std::string& signerId)
   {
      uint64_t refId = rand();
      capnp::MallocMessageBuilder message;
      auto toBridge = message.initRoot<Codec::Bridge::ToBridge>();
      toBridge.setReferenceId(refId);
      auto req = toBridge.initSigner();
      req.setId(signerId);
      req.setGetSignedTx();
      auto rawReq = serializeCapnp(message);
      pushRequest(bridge, rawReq);

      //process reply
      auto resp = waitOnReply();
      kj::ArrayPtr<const capnp::word> words(
         reinterpret_cast<const capnp::word*>(resp->data.getPtr()),
         resp->data.getSize() / sizeof(capnp::word));
      capnp::FlatArrayMessageReader reader(words);

      auto fromBridge = reader.getRoot<Codec::Bridge::FromBridge>();
      if (fromBridge.which() != Codec::Bridge::FromBridge::REPLY) {
         return {};
      }
      auto reply = fromBridge.getReply();

      if (reply.getSuccess() == false) {
         return {};
      }
      if (reply.which() != Codec::Bridge::RpcReply::SIGNER) {
         return {};
      }

      auto signedTxCapn = reply.getSigner().getGetSignedTx();
      return BinaryData{ signedTxCapn.begin(), signedTxCapn.size() };
   }

   bool cleanupSigner(std::shared_ptr<Bridge::CppBridge> bridge,
      const std::string& signerId)
   {
      uint64_t refId = rand();
      auto callbackId = Cryptography::PRNG::fortuna.generateRandom(10).toHexStr();
      capnp::MallocMessageBuilder message;
      auto toBridge = message.initRoot<Codec::Bridge::ToBridge>();
      toBridge.setReferenceId(refId);
      auto req = toBridge.initSigner();
      req.setId(signerId);
      req.setCleanup();
      auto rawReq = serializeCapnp(message);
      pushRequest(bridge, rawReq);

      //process reply
      auto resp = waitOnReply();
      kj::ArrayPtr<const capnp::word> words(
         reinterpret_cast<const capnp::word*>(resp->data.getPtr()),
         resp->data.getSize() / sizeof(capnp::word));
      capnp::FlatArrayMessageReader reader(words);

      auto fromBridge = reader.getRoot<Codec::Bridge::FromBridge>();
      if (fromBridge.which() != Codec::Bridge::FromBridge::REPLY) {
         return false;
      }
      auto reply = fromBridge.getReply();
      return reply.getSuccess();
   }

   BinaryData createAndSignTx(
      std::shared_ptr<Bridge::CppBridge> bridge,
      const std::string& walletId, const std::string& accountId,
      const std::vector<UTXO>& utxos,
      const std::map<std::string, uint64_t>& recipients,
      const BinaryData& changeScrAddr, uint64_t fee, bool rbf,
      const std::string& passphrase)
   {
      /* coin selection leg */

      //create a coin selection instance
      auto csId = getNewCoinSelector(bridge, walletId, accountId, 5);
      if (csId.empty()) {
         throw std::runtime_error("failed to create coin selection instance");
      }

      //set recipient
      auto recipientPrivKey = Cryptography::ECDSA::createNewPrivateKey();
      auto recipientPubKey = Cryptography::ECDSA::computePublicKey(
         recipientPrivKey, true);
      auto hash160 =  BtcUtils::getHash160(recipientPubKey);
      auto recipientAddr = BtcUtils::scrAddrToSegWitAddress(hash160);

      //arbitrary value, used for coin selection grouping, just needs to be unique
      unsigned recipientId = 100;
      uint64_t totalSpent = 0;
      for (const auto& recipPair : recipients) {
         if (!setNewCsRecipient(bridge, csId,
            recipientId++, recipPair.first, recipPair.second)) {
            throw std::runtime_error("failed to add recipient");
         }
         totalSpent += recipPair.second;
      }

      //get coin selection
      if (utxos.empty()) {
         if (!selectUtxos(bridge, csId, 0, (float)fee)) {
            throw std::runtime_error("selectUtxos error");
         }
      } else {
         if (!processCustomUtxoList(bridge, csId, utxos,
            rbf ? USE_FULL_CUSTOM_LIST : 0, fee)) {
            throw std::runtime_error("processCustomUtxoList error");
         }
      }

      auto utxoSelection = getUtxoSelection(bridge, csId);
      if (utxoSelection.empty()) {
         throw std::runtime_error("empty utxo selection");
      }

      uint64_t totalInputs = 0;
      std::set<BinaryData> supportingTxHashes;
      for (const auto& utxo : utxoSelection) {
         totalInputs += utxo.getAmount();
         supportingTxHashes.emplace(utxo.getTxHash());
      }

      auto totalFee = getFlatFee(bridge, csId);
      if (totalFee == UINT64_MAX || totalFee == 0) {
         throw std::runtime_error("invalid fee");
      }
      if (totalInputs < totalSpent + totalFee) {
         throw std::runtime_error("fee overflow");
      }

      //cleanup coin selection instance
      if (!cleanupCoinSelectionInstance(bridge, csId)) {
         throw std::runtime_error("cs cleanup error");
      }

      /* signer leg */

      //get the supporting tx
      auto supportingTxMap = getTxsByHash(bridge, supportingTxHashes);
      for (const auto& utxo : utxoSelection) {
         auto iter = supportingTxMap.find(utxo.getTxHash());
         if (iter == supportingTxMap.end()) {
            throw std::runtime_error("missing supporting tx");
         }
         if (iter->second.empty()) {
            throw std::runtime_error("empty supporting tx");
         }
      }

      //create signer
      auto signerId = getNewSigner(bridge);
      if (signerId.empty()) {
         throw std::runtime_error("failed to create signer instance");
      }

      //set version
      if (!setSignerVersion(bridge, signerId, 2)) {
         throw std::runtime_error("failed to set version");
      }

      //set inputs
      uint32_t sequence = UINT32_MAX;
      if (rbf) {
         sequence -= 2;
         if (!utxos.empty()) {
            //rbf and a set of utxos, we are probably replacing a tx,
            //sequence should be lower than original zc
            --sequence;
         }
      }
      for (const auto& utxo : utxoSelection) {
         if (!addSpenderByOutpoint(
            bridge, signerId,
            utxo.getTxHash(), utxo.getTxOutIndex(), sequence)) {
            throw std::runtime_error("failed to add spender");
         }
         if (!populateUtxo(
            bridge, signerId,
            utxo.getTxHash(), utxo.getTxOutIndex(),
            utxo.getAmount(), utxo.getScript())) {
            throw std::runtime_error("failed to populate utxo");
         }

         auto rawTx = supportingTxMap.at(utxo.getTxHash());
         if (!addSupportingTx(bridge, signerId, rawTx)) {
            throw std::runtime_error("failed to add supporting tx");
         }
      }

      //set recipients
      for (const auto& recipPair : recipients) {
         auto recipientScrAddr = BtcUtils::getScrAddrForAddrStr(recipPair.first);
         if (!addSignerRecipient(bridge, signerId,
            BtcUtils::getTxOutScriptForScrAddr(recipientScrAddr), recipPair.second)) {
            throw std::runtime_error("failed to add recipient to signer");
         }
      }

      //add change output
      uint64_t changeAmount = totalInputs - totalSpent - totalFee;
      if (!addSignerRecipient(bridge, signerId,
         BtcUtils::getTxOutScriptForScrAddr(changeScrAddr), changeAmount
      ))
      for (unsigned i = 0; i < utxoSelection.size(); i++) {
         auto inputState = getInputState(bridge, signerId, i);
         if (inputState.isValid) {
            throw std::runtime_error("input state should be invalid");
         }
         if (inputState.sigCount != 0) {
            throw std::runtime_error("sig count should be 0");
         }
      }

      //resolve
      if (!resolveSigner(bridge, signerId, walletId)) {
         throw std::runtime_error("failed to resolve signer");
      }
      for (unsigned i = 0; i < utxoSelection.size(); i++) {
         auto inputState = getInputState(bridge, signerId, i);
         if (inputState.isValid) {
            throw std::runtime_error("input state should be invalid");
         }
         if (inputState.sigCount != 0) {
            throw std::runtime_error("sig count should be 0");
         }
      }

      //sign
      if (!signTx(bridge, signerId, walletId, passphrase)) {
         throw std::runtime_error("failed to sign tx");
      }
      for (unsigned i = 0; i < utxoSelection.size(); i++) {
         auto inputState = getInputState(bridge, signerId, i);
         if (!inputState.isValid) {
            throw std::runtime_error("input state should be valid");
         }
         if (inputState.sigCount != 1) {
            throw std::runtime_error("sig count should be 1");
         }
      }

      //get signed tx
      auto signedTx = getSignedTx(bridge, signerId);

      //cleanup signer
      if (!cleanupSigner(bridge, signerId)) {
         throw std::runtime_error("failed to cleanup signer");
      }
      return signedTx;
   }

   /////////////////////////////////////////////////////////////////////////////
   // zc stuff
   void broadcastTx(std::shared_ptr<Bridge::CppBridge> bridge,
      const BinaryData& rawTx)
   {
      uint64_t refId = rand();
      capnp::MallocMessageBuilder message;
      auto toBridge = message.initRoot<Codec::Bridge::ToBridge>();
      toBridge.setReferenceId(refId);
      auto req = toBridge.initService();
      auto broadcastReq = req.initBroadcastTx();
      broadcastReq.setViaP2p();
      auto rawTxs = broadcastReq.initRawTxs(1);
      rawTxs.set(0, capnp::Data::Builder(
         (uint8_t*)rawTx.getPtr(), rawTx.getSize()));
      auto rawReq = serializeCapnp(message);
      pushRequest(bridge, rawReq);
   }

   std::vector<Ledgers::Entry> waitOnZc()
   {
      auto resp = waitOnReply();
      kj::ArrayPtr<const capnp::word> words(
         reinterpret_cast<const capnp::word*>(resp->data.getPtr()),
         resp->data.getSize() / sizeof(capnp::word));
      capnp::FlatArrayMessageReader reader(words);

      auto fromBridge = reader.getRoot<Codec::Bridge::FromBridge>();
      if (fromBridge.which() != Codec::Bridge::FromBridge::NOTIFICATION) {
         throw std::runtime_error("not a notif");
      }

      auto notif = fromBridge.getNotification();
      if (notif.which() != Codec::Bridge::Notification::ZERO_CONFS) {
         auto error = notif.getError();
         std::cout << std::string(error) << std::endl;
         throw std::runtime_error("not a zc");
      }
      return capnToLedgers(notif.getZeroConfs());
   }

   std::vector<BinaryData> waitOnInvalidatedZCs()
   {
      auto resp = waitOnReply();
      kj::ArrayPtr<const capnp::word> words(
         reinterpret_cast<const capnp::word*>(resp->data.getPtr()),
         resp->data.getSize() / sizeof(capnp::word));
      capnp::FlatArrayMessageReader reader(words);

      auto fromBridge = reader.getRoot<Codec::Bridge::FromBridge>();
      if (fromBridge.which() != Codec::Bridge::FromBridge::NOTIFICATION) {
         throw std::runtime_error("not a notif");
      }

      auto notif = fromBridge.getNotification();
      if (notif.which() != Codec::Bridge::Notification::INVALIDATED_ZCS) {
         auto error = notif.getError();
         std::cout << std::string(error) << std::endl;
         throw std::runtime_error("not an invalidated zc");
      }
      std::vector<BinaryData> result;
      for (auto hash : notif.getInvalidatedZcs()) {
         result.emplace_back(BinaryData{hash.begin(), hash.size()});
      }
      return result;
   }
}

////////////////////////////////////////////////////////////////////////////////
// WalletManagerTests
////////////////////////////////////////////////////////////////////////////////
class WalletManagerTests : public ::testing::Test
{
protected:
   virtual void SetUp(void)
   {
      homedir_ = std::filesystem::path("./fakehomedir");
      FileUtils::removeDirectory(homedir_);
      std::filesystem::create_directory(homedir_);

      Config::parseArgs({
         "--offline",
         "--datadir=./fakehomedir" },
         Config::ProcessType::DB);
   }

   virtual void TearDown(void)
   {
      Config::reset();
      FileUtils::removeDirectory(homedir_);
   }

   std::filesystem::path homedir_;
};

////////////////////////////////////////////////////////////////////////////////
TEST_F(WalletManagerTests, ListStageLoad)
{
   /*
   covers the following scenarios:
      1: wallet without control passphrase
      2: with control passphrase, keep encrypted
      3: with control passphrase, decrypted
      4: with control passphrase, decrypted but unstaged
   */
   std::vector<std::filesystem::path> walletFiles;

   //wallet 1
   {
      Wallets::IO::CreateWalletParams params{
         homedir_,
         {1ms, 0, SecureBinaryData::fromString("privpass1")},
         {},
         nullptr, 4
      };

      std::unique_ptr<Seeds::ClearTextSeed> seed(
         new Seeds::ClearTextSeed_Armory());
      auto assetWlt = Wallets::AssetWallet_Single::createFromSeed(
         std::move(seed), params);
      walletFiles.emplace_back(assetWlt->getDbFilename());
   }

   //wallet 2, 3, 4
   for (unsigned i=2; i<5; i++) {
      Wallets::IO::CreateWalletParams params{
         homedir_,
         {1ms, 0, SecureBinaryData::fromString("privpass" + std::to_string(i))},
         {1ms, 0, SecureBinaryData::fromString("controlpass" + std::to_string(i))},
         nullptr, 4
      };

      std::unique_ptr<Seeds::ClearTextSeed> seed(
         new Seeds::ClearTextSeed_Armory());
      auto assetWlt = Wallets::AssetWallet_Single::createFromSeed(
         std::move(seed), params);
      walletFiles.emplace_back(assetWlt->getDbFilename());
   }

   //list wallets
   Bridge::WalletManager mgr{homedir_};
   auto theList = mgr.listWallets();
   ASSERT_EQ(theList.size(), 4);
   for (const auto& path : walletFiles) {
      ASSERT_NE(theList.find(path.filename().string()), theList.end());
   }

   auto checkState = [&walletFiles](
      const std::map<std::string, std::shared_ptr<Bridge::WalletFileInfo>>& theList,
      unsigned intId, Bridge::WalletLoadState expLoadState, bool expectedStaged)->bool
   {
      auto listEntry = theList.at(walletFiles[intId].filename().string());
      if (listEntry->state() != expLoadState) {
         return false;
      }
      if (listEntry->state() == Bridge::WalletLoadState::Ready) {
         if (!listEntry->hasAccountIds()) {
            return false;
         } else {
            auto accIds = listEntry->getAccountIds();
            if (accIds.begin()->toHexStr() != legacyAccId) {
               return false;
            }
         }
         if (listEntry->walletId().empty()) {
            return false;
         }
      } else {
         if (listEntry->hasAccountIds()) {
            return false;
         }
      }
      return listEntry->staged() == expectedStaged;
   };

   EXPECT_TRUE(checkState(theList, 0, Bridge::WalletLoadState::Ready, true));
   EXPECT_TRUE(checkState(theList, 1, Bridge::WalletLoadState::Encrypted, false));
   EXPECT_TRUE(checkState(theList, 2, Bridge::WalletLoadState::Encrypted, false));
   EXPECT_TRUE(checkState(theList, 3, Bridge::WalletLoadState::Encrypted, false));

   //unlock wallets 3 & 4, fail for wtl2
   {
      try {
         mgr.unlockControlHeader(walletFiles[2].filename().string(), [](
            const std::set<Wallets::EncryptionKeyId>&)->Passphrase::Result {
               return { SecureBinaryData::fromString("controlpass3"), true };
            }
         );

         mgr.unlockControlHeader(walletFiles[3].filename().string(), [](
            const std::set<Wallets::EncryptionKeyId>&)->Passphrase::Result {
               return { SecureBinaryData::fromString("controlpass4"), true };
            }
         );
      } catch (const std::exception& e) {
         ASSERT_TRUE(false) << e.what();
      }

      unsigned count = 0;
      auto failUnlockLbd = [&count](
         const std::set<Wallets::EncryptionKeyId>&)->Passphrase::Result {
         if (count++ < 2) {
            return { Cryptography::PRNG::fortuna.generateRandom(10), true };
         }
         //give up after 2 tries
         return { {}, false };
      };
      try {
         mgr.unlockControlHeader(walletFiles[1].filename().string(), failUnlockLbd);
         ASSERT_TRUE(false);
      } catch (const Wallets::Encryption::DecryptedDataContainerException& e) {
         ASSERT_EQ(e.what(), std::string{"unlock request rejected"}) << e.what();
      }
   }

   //recheck the list
   theList = mgr.listWallets();
   ASSERT_EQ(theList.size(), 4);
   for (const auto& path : walletFiles) {
      ASSERT_NE(theList.find(path.filename().string()), theList.end());
   }
   EXPECT_TRUE(checkState(theList, 0, Bridge::WalletLoadState::Ready, true));
   EXPECT_TRUE(checkState(theList, 1, Bridge::WalletLoadState::Encrypted, false));
   EXPECT_TRUE(checkState(theList, 2, Bridge::WalletLoadState::Ready, true));
   EXPECT_TRUE(checkState(theList, 3, Bridge::WalletLoadState::Ready, true));

   //unstage wlt4
   {
      auto wlt4Info = theList.at(walletFiles[3].filename().string());
      ASSERT_TRUE(mgr.stageWallet(wlt4Info->walletId(), false));
   }

   //recheck the list
   theList = mgr.listWallets();
   ASSERT_EQ(theList.size(), 4);
   for (const auto& path : walletFiles) {
      ASSERT_NE(theList.find(path.filename().string()), theList.end());
   }
   EXPECT_TRUE(checkState(theList, 0, Bridge::WalletLoadState::Ready, true));
   EXPECT_TRUE(checkState(theList, 1, Bridge::WalletLoadState::Encrypted, false));
   EXPECT_TRUE(checkState(theList, 2, Bridge::WalletLoadState::Ready, true));
   EXPECT_TRUE(checkState(theList, 3, Bridge::WalletLoadState::Ready, false));

   //load wallets
   mgr.loadWallets();

   //check loaded wallets
   auto checkHasWallet = [&theList, &walletFiles, &mgr](unsigned intId)->bool
   {
      auto entry = theList.at(walletFiles[intId].filename().string());
      try {
         return mgr.hasWallet(entry->walletId());
      } catch (const std::exception&) {
         return false;
      }
      
   };

   EXPECT_TRUE(checkHasWallet(0));
   EXPECT_FALSE(checkHasWallet(1));
   EXPECT_TRUE(checkHasWallet(2));
   EXPECT_FALSE(checkHasWallet(3));

   //check list only has wlt1
   theList = mgr.listWallets();
   ASSERT_EQ(theList.size(), 2);

   ASSERT_EQ(theList.find(walletFiles[0].filename().string()), theList.end());
   ASSERT_NE(theList.find(walletFiles[1].filename().string()), theList.end());
   ASSERT_EQ(theList.find(walletFiles[2].filename().string()), theList.end());
   ASSERT_NE(theList.find(walletFiles[3].filename().string()), theList.end());
}

////////////////////////////////////////////////////////////////////////////////
TEST_F(WalletManagerTests, ListWO)
{
   std::vector<std::filesystem::path> wltPaths;

   //wallet 1
   {
      Wallets::IO::CreateWalletParams params{
         homedir_,
         {1ms, 0, SecureBinaryData::fromString("privpass1")},
         {1ms, 0, SecureBinaryData::fromString("ctrlPass")},
         nullptr, 4
      };

      std::unique_ptr<Seeds::ClearTextSeed> seed(
         new Seeds::ClearTextSeed_Armory());
      auto assetWlt = Wallets::AssetWallet_Single::createFromSeed(
         std::move(seed), params);
      wltPaths.emplace_back(assetWlt->getDbFilename());
   }

   {
      auto woWltPath = Wallets::AssetWallet::forkWatchingOnly({wltPaths[0],
         [](const std::set<Wallets::EncryptionKeyId>&)->Passphrase::Result
         {
            return { SecureBinaryData::fromString("ctrlPass"), true };
         }},
         {1ms, 0, SecureBinaryData::fromString("woPass")}
      );
      ASSERT_FALSE(woWltPath.empty());
      ASSERT_TRUE(FileUtils::pathExists(woWltPath, 0));
      ASSERT_NE(woWltPath, wltPaths[0]);
      wltPaths.emplace_back(woWltPath);
   }

   //list wallets
   Bridge::WalletManager mgr{homedir_};
   auto theList = mgr.listWallets();
   ASSERT_EQ(theList.size(), 2);
   for (const auto& path : wltPaths) {
      ASSERT_NE(theList.find(path.filename().string()), theList.end());
   }

   auto checkState = [&wltPaths](
      const std::map<std::string, std::shared_ptr<Bridge::WalletFileInfo>>& theList,
      unsigned intId, Bridge::WalletLoadState expLoadState,
      bool expectedStaged, bool isWO=false)->bool
   {
      auto listEntry = theList.at(wltPaths[intId].filename().string());
      if (listEntry->state() != expLoadState) {
         return false;
      }
      if (listEntry->state() == Bridge::WalletLoadState::Ready) {
         if (!listEntry->hasAccountIds()) {
            return false;
         } else {
            auto accIds = listEntry->getAccountIds();
            if (accIds.begin()->toHexStr() != legacyAccId) {
               return false;
            }
         }
         if (listEntry->walletId().empty()) {
            return false;
         }
      } else {
         if (listEntry->hasAccountIds()) {
            return false;
         }
      }
      if (isWO != listEntry->isWatchingOnly()) {
         return false;
      }
      return listEntry->staged() == expectedStaged;
   };

   EXPECT_TRUE(checkState(theList, 0,
      Bridge::WalletLoadState::Encrypted, false));
   EXPECT_TRUE(checkState(theList, 1,
      Bridge::WalletLoadState::Encrypted, false));

   //unlock the wallets
   {
      try {
         mgr.unlockControlHeader(wltPaths[0].filename().string(), [](
            const std::set<Wallets::EncryptionKeyId>&)->Passphrase::Result {
               return { SecureBinaryData::fromString("ctrlPass"), true };
            }
         );

         mgr.unlockControlHeader(wltPaths[1].filename().string(), [](
            const std::set<Wallets::EncryptionKeyId>&)->Passphrase::Result {
               return { SecureBinaryData::fromString("woPass"), true };
            }
         );
      } catch (const std::exception& e) {
         ASSERT_TRUE(false) << e.what();
      }
   }

   //recheck the list
   theList = mgr.listWallets();
   ASSERT_EQ(theList.size(), 2);
   for (const auto& path : wltPaths) {
      ASSERT_NE(theList.find(path.filename().string()), theList.end());
   }

   std::vector<std::string> wltIds;
   for (const auto& entry : theList) {
      wltIds.emplace_back(entry.second->walletId());
   }
   ASSERT_EQ(wltIds.size(), 2);
   ASSERT_EQ(wltIds[0], wltIds[1]);

   EXPECT_TRUE(checkState(theList, 0,
      Bridge::WalletLoadState::Ready, true, false));
   EXPECT_TRUE(checkState(theList, 1,
      Bridge::WalletLoadState::Ready, true, true));
}

////////////////////////////////////////////////////////////////////////////////
TEST_F(WalletManagerTests, Migrate_Legacy)
{
   //copy test legacy wallet to datadir
   const auto& wltId = "28m472Xbm"sv;
   std::string fileName = std::string{"armory_"sv} +
      std::string{wltId} +
      std::string{"_.wallet"sv};
   const std::filesystem::path wltPath{fileName};
   std::filesystem::copy(
      "input_files/legacy.wallet"sv,
      homedir_ / wltPath
   );

   //list wallets
   Bridge::WalletManager mgr{homedir_};
   auto theList = mgr.listWallets();
   ASSERT_EQ(theList.size(), 1);

   //sanity check
   {
      auto iter = theList.find(wltPath.filename().string());
      ASSERT_NE(iter, theList.end());
      EXPECT_EQ(iter->second->state(), Bridge::WalletLoadState::Legacy);
      EXPECT_EQ(iter->second->walletId(), wltId);
      EXPECT_FALSE(iter->second->staged());
   }

   //migrate the wallet
   auto passFunc = [](const std::set<Wallets::EncryptionKeyId>&)
   ->Passphrase::Result
   {
      return { SecureBinaryData::fromString("testnet"), true };
   };
   mgr.migrateWallet(wltPath.filename().string(), passFunc,
      Wallets::IO::CreateWalletParams{
         homedir_,
         {1ms, 0, {}},
         {1ms, 0, {}},
         nullptr, 0
      });

   theList = mgr.listWallets();
   ASSERT_EQ(theList.size(), 2);

   //check legacy wallet
   {
      auto iter = theList.find(wltPath.filename().string());
      ASSERT_NE(iter, theList.end());
      EXPECT_EQ(iter->second->state(), Bridge::WalletLoadState::Legacy);
      EXPECT_EQ(iter->second->walletId(), wltId);
      EXPECT_FALSE(iter->second->staged());
   }

   //check migrated wallet
   {
      auto iter = theList.begin();
      while (iter != theList.end()) {
         if (iter->first != wltPath.filename().string()) {
            break;
         }
         ++iter;
      }
      ASSERT_NE(iter, theList.end());
      EXPECT_EQ(iter->second->state(), Bridge::WalletLoadState::Ready);
      EXPECT_EQ(iter->second->walletId(), wltId);
      EXPECT_TRUE(iter->second->staged());
   }

   //load wallets
   mgr.loadWallets();

   //check migrated wallet
   try {
      auto wltContainer = mgr.getWalletContainer(wltId);
      ASSERT_NE(wltContainer, nullptr);
      EXPECT_EQ(wltContainer->getHighestUsedIndex(), 2);
      EXPECT_EQ(wltContainer->getAccountId().toHexStr(), "f6e10000");

      auto wltPtr = wltContainer->getWalletPtr();
      EXPECT_EQ(wltPtr->getLabel(), "legacy1");
      EXPECT_EQ(wltPtr->getDescription(), "migration test");

      auto addrAccPtr = wltContainer->getAddressAccount();
      auto addrMap = addrAccPtr->getAddressHashMap();
      EXPECT_EQ(addrMap.size(), 104 * 3);

      //TODO: check addresses
   } catch (const std::exception& e) {
      ASSERT_TRUE(false) << e.what();
   }

   //TODO: check backup string, with version (should be 1.35c)
}

////////////////////////////////////////////////////////////////////////////////
TEST_F(WalletManagerTests, Migrate_Reject)
{
   //copy test legacy wallet to datadir
   const auto& wltId = "28m472Xbm"sv;
   std::string fileName = std::string{"armory_"sv} +
      std::string{wltId} +
      std::string{"_.wallet"sv};
   const std::filesystem::path wltPath{fileName};
   std::filesystem::copy(
      "input_files/legacy.wallet"sv,
      homedir_ / wltPath
   );

   //list wallets
   Bridge::WalletManager mgr{homedir_};
   auto theList = mgr.listWallets();
   ASSERT_EQ(theList.size(), 1);

   //sanity check
   {
      auto iter = theList.find(wltPath.filename().string());
      ASSERT_NE(iter, theList.end());
      EXPECT_EQ(iter->second->state(), Bridge::WalletLoadState::Legacy);
      EXPECT_EQ(iter->second->walletId(), wltId);
      EXPECT_FALSE(iter->second->staged());
   }

   //reject the unlock
   auto passFunc = [](const std::set<Wallets::EncryptionKeyId>&)
   ->Passphrase::Result
   {
      return { SecureBinaryData::fromString("testnet"), false };
   };

   try {
      mgr.migrateWallet(wltPath.filename().string(), passFunc,
         Wallets::IO::CreateWalletParams{
            homedir_,
            {1ms, 0, {}},
            {1ms, 0, {}},
            nullptr, 0
         });
      ASSERT_TRUE(false);
   } catch (const std::exception& e) {
      EXPECT_EQ(e.what(), std::string{"rejected migration"});
   }
}

////////////////////////////////////////////////////////////////////////////////
// WalletManagerWebsocketsTests
////////////////////////////////////////////////////////////////////////////////
class WalletManagerWebsocketsTests : public ::testing::Test
{
protected:
   void initBDM()
   {
      theBDMt_ = new BlockDataManagerThread();
      iface_ = theBDMt_->bdm()->getIFace();
   }

   void createWallet()
   {
      Wallets::IO::CreateWalletParams params{
         homedir_,
         Passphrase::SetNew{1ms, 0, {}},
         Passphrase::SetNew{1ms, 0, {}},
         nullptr, 0
      };

      //create empty WO wallet
      auto wltWO = Wallets::AssetWallet_Single::createBlank("walletWO1"sv, params);
      wltWO->setupImportAccount();

      auto pubKeyB = Cryptography::ECDSA::computePublicKey(TestChain::privKeyAddrB);
      wltWO->importPublicKey(pubKeyB, AddressEntryType(
         AddressEntryType::P2PKH | AddressEntryType::Uncompressed));

      auto pubKeyC = Cryptography::ECDSA::computePublicKey(TestChain::privKeyAddrC);
      wltWO->importPublicKey(pubKeyC, AddressEntryType(
         AddressEntryType::P2PKH | AddressEntryType::Uncompressed));

      auto pubKeyD = Cryptography::ECDSA::computePublicKey(TestChain::privKeyAddrD);
      wltWO->importPublicKey(pubKeyD, AddressEntryType(
         AddressEntryType::P2PKH | AddressEntryType::Uncompressed));

      auto pubKeyE = Cryptography::ECDSA::computePublicKey(TestChain::privKeyAddrE);
      wltWO->importPublicKey(pubKeyE, AddressEntryType(
         AddressEntryType::P2PKH | AddressEntryType::Uncompressed));
   }

   virtual void SetUp()
   {
      FileUtils::removeDirectory(blkdir_);
      FileUtils::removeDirectory(homedir_);
      FileUtils::removeDirectory(ldbdir_);

      FileUtils::createDirectory(blkdir_ / "blocks");
      FileUtils::createDirectory(homedir_);
      FileUtils::createDirectory(ldbdir_);

      Config::DBSettings::setServiceType(SERVICE_UNITTEST_WITHWS);

      // Put the first 5 blocks into the blkdir
      blk0dat_ = FileUtils::getBlkFilename(blkdir_ / "blocks", 0);
      TestUtils::setBlocks({ "0", "1", "2", "3", "4", "5" }, blk0dat_);

      WebSocketServer::init();
      Config::parseArgs({
         "--datadir=./fakehomedir",
         "--dbdir=./ldbtestdir",
         "--satoshi-datadir=./blkfiletest",
         "--db-type=DB_BARE",
         "--thread-count=3",
         "--public"},
         Config::ProcessType::DB);

      //setup auth peers for server and client
      authPeersPassLbd_ = [](const std::set<Wallets::EncryptionKeyId>&)
      ->Passphrase::Result
      {
         return { {}, true };
      };

      auto createWltLbd = []()->std::unique_ptr<Passphrase::Params>
      {
         return std::make_unique<Passphrase::Params>(
            1ms, 0, SecureBinaryData{});
      };

      Wallets::AuthorizedPeers::createWallet({
         homedir_ / SERVER_AUTH_PEER_FILENAME, {createWltLbd}});
      Wallets::AuthorizedPeers serverPeers(
         {homedir_ / SERVER_AUTH_PEER_FILENAME, authPeersPassLbd_});

      Wallets::AuthorizedPeers::createWallet({
         homedir_ / CLIENT_AUTH_PEER_FILENAME, {createWltLbd}});
      Wallets::AuthorizedPeers clientPeers(
         {homedir_ / CLIENT_AUTH_PEER_FILENAME, authPeersPassLbd_});

      //share public keys between client and server
      auto& serverPubkey = serverPeers.getOwnPublicKey();

      std::stringstream serverAddr;
      serverAddr << "127.0.0.1:" << Config::NetworkSettings::dbPort();
      clientPeers.addPeer(serverPubkey, {serverAddr.str()}, {}, true);

      serverPubkey_ = BinaryData(serverPubkey.pubkey, 33);
      serverAddr_ = serverAddr.str();

      createWallet();
      initBDM();
      auto nodePtr = std::dynamic_pointer_cast<NodeUnitTest>(
         Config::NetworkSettings::bitcoinNodes().first);
      nodePtr->setBDM(theBDMt_->bdm());
      hexMagicBytes = Config::BitcoinSettings::getMagicBytes().toHexStr();
   }

   /////////////////////////////////////////////////////////////////////////////
   virtual void TearDown()
   {
      WebSocketServer::shutdown();
      WebSocketServer::waitOnShutdown();
      theBDMt_->shutdown();

      delete theBDMt_;
      theBDMt_ = nullptr;

      FileUtils::removeDirectory(blkdir_);
      FileUtils::removeDirectory(homedir_);
      FileUtils::removeDirectory(ldbdir_);
      Config::reset();
   }

   /////////////////////////////////////////////////////////////////////////////
   BlockDataManagerThread *theBDMt_;
   Passphrase::UnlockFunc authPeersPassLbd_;
   LMDBBlockDatabase* iface_;

   std::filesystem::path blkdir_{"./blkfiletest"sv};
   std::filesystem::path homedir_{"./fakehomedir"sv};
   std::filesystem::path ldbdir_{"./ldbtestdir"sv};
   std::filesystem::path blk0dat_;

   BinaryData serverPubkey_;
   std::string serverAddr_;
   std::string hexMagicBytes;
};

////
TEST_F(WalletManagerWebsocketsTests, Connect)
{
   NotifQueue queue;
   auto notifFunc = [&queue](BinaryData notifData)
   {
      queue.push_back(notifData);
   };
   auto mgr = std::make_shared<Bridge::WalletManager>(homedir_);
   mgr->setBdvCallback(notifFunc);

   //list wallets
   auto theList = mgr->listWallets();
   ASSERT_EQ(theList.size(), 1);
   auto wltId = theList.begin()->second->walletId();
   mgr->loadWallets();

   WebSocketServer::initAuthPeers({
      homedir_ / SERVER_AUTH_PEER_FILENAME, authPeersPassLbd_});
   WebSocketServer::start(theBDMt_->bdm(), true);

   //create bdv ptr, connect to db
   auto clientPeers = std::make_shared<Wallets::AuthorizedPeers>(
      Wallets::IO::ReadOnlyFileParams{
         homedir_ / CLIENT_AUTH_PEER_FILENAME, authPeersPassLbd_});
   auto bdvPtr = Bridge::setupClientConnection(clientPeers,
      Config::NetworkSettings::dbIP(), Config::NetworkSettings::dbPort(),
      Config::NetworkSettings::oneWayAuth(), {},
      mgr->getBdvCallback());
   mgr->setBdvPtr(bdvPtr);

   //expecting setupDone notif
   {
      auto notifBd = queue.pop();
      kj::ArrayPtr<const capnp::word> words(
         reinterpret_cast<const capnp::word*>(notifBd.getPtr()),
         notifBd.getSize() / sizeof(capnp::word));
      capnp::FlatArrayMessageReader reader(words);
      auto fromBridge = reader.getRoot<Codec::Bridge::FromBridge>();
      auto notif = fromBridge.getNotification();
      ASSERT_EQ(notif.which(), Codec::Bridge::Notification::SETUP_DONE);
   }

   //register wallet
   mgr->registerWallets();

   //expecting registerDone notif
   {
      auto notifBd = queue.pop();
      kj::ArrayPtr<const capnp::word> words(
         reinterpret_cast<const capnp::word*>(notifBd.getPtr()),
         notifBd.getSize() / sizeof(capnp::word));
      capnp::FlatArrayMessageReader reader(words);
      auto fromBridge = reader.getRoot<Codec::Bridge::FromBridge>();
      auto notif = fromBridge.getNotification();
      ASSERT_EQ(notif.which(), Codec::Bridge::Notification::REGISTER_DONE);
   }

   //start blockchain db & go online
   theBDMt_->start(Config::DBSettings::initMode());
   bdvPtr->goOnline();

   //expecting ready notif
   int newBlockVal = 0;
   bool run = true;
   while (run) {
      auto notifBd = queue.pop();
      kj::ArrayPtr<const capnp::word> words(
         reinterpret_cast<const capnp::word*>(notifBd.getPtr()),
         notifBd.getSize() / sizeof(capnp::word));
      capnp::FlatArrayMessageReader reader(words);
      auto fromBridge = reader.getRoot<Codec::Bridge::FromBridge>();
      auto notif = fromBridge.getNotification();

      switch (notif.which()) {
         case Codec::Bridge::Notification::READY:
         {
            newBlockVal = notif.getReady();
            run = false;
            break;
         }

         case Codec::Bridge::Notification::SCAN_PROGRESS:
            break;

         default:
            EXPECT_TRUE(false) << (int)notif.which();
      }
   }
   ASSERT_EQ(newBlockVal, 5);

   //check wallet balances
   try {
      auto wltCont = mgr->getWalletContainer(wltId);
      ASSERT_NE(wltCont, nullptr);

      auto addrBalances = wltCont->getAddrBalanceMap();
      ASSERT_EQ(addrBalances.size(), 4);

      for (const auto& mgrBal : addrBalances) {
         auto addrBal = TestChain::testAddrBalances[5].at(mgrBal.first);
         EXPECT_EQ(addrBal[0], mgrBal.second.fullBalance);
         EXPECT_EQ(addrBal[1], mgrBal.second.spendableBalance);
         EXPECT_EQ(addrBal[2], mgrBal.second.unconfirmedBalance);
         EXPECT_EQ(addrBal[3], mgrBal.second.txCount);
      }
   } catch (const std::exception& e) {
      std::cout << e.what() << std::endl;
      ASSERT_TRUE(false);
   }

   //cleanup
   bdvPtr->unregisterFromDB();
}

////////////////////////////////////////////////////////////////////////////////
// BridgeWalletTests
class BridgeWalletTests : public ::testing::Test
{
protected:
   //setup
   virtual void SetUp(void)
   {
      std::srand(std::time({}));
      homedir = std::filesystem::path("./fakehomedir");
      FileUtils::removeDirectory(homedir);
      std::filesystem::create_directory(homedir);

      Config::parseArgs({
         "--offline",
         "--datadir=./fakehomedir" },
         Config::ProcessType::Bridge);

      replyQueue.clear();
      bridge_ = std::make_shared<Bridge::CppBridge>();
      bridge_->setWriteLambda([](MsgPtr payload) {
         std::unique_lock<std::mutex> lock(commsMutex);
         replyQueue.emplace_back(std::move(payload));
         commsCV.notify_all();
      });
   }

   virtual void TearDown(void)
   {
      bridge_.reset();
      Config::reset();
      FileUtils::removeDirectory(homedir);
   }

   //helpers
   bool unlockWallet(const std::string& path, const std::string& passphrase)
   {
      uint64_t refId = rand();
      auto callbackId = Cryptography::PRNG::fortuna.generateRandom(10).toHexStr();

      //request unlock
      {
         capnp::MallocMessageBuilder message;
         auto toBridge = message.initRoot<Codec::Bridge::ToBridge>();
         toBridge.setReferenceId(refId);
         auto request = toBridge.initWalletManager();
         auto unlockReq = request.initUnlockControlHeader();

         unlockReq.setCallbackId(callbackId);
         unlockReq.setWalletPath(path);

         auto rawReq = serializeCapnp(message);
         pushRequest(bridge_, rawReq);
      }

      //wait on passphrase prompt
      auto rawPrompt = waitOnReply();
      kj::ArrayPtr<const capnp::word> words(
         reinterpret_cast<const capnp::word*>(rawPrompt->data.getPtr()),
         rawPrompt->data.getSize() / sizeof(capnp::word));
      capnp::FlatArrayMessageReader reader(words);
      auto fromBridge = reader.getRoot<Codec::Bridge::FromBridge>();
      if (fromBridge.which() != Codec::Bridge::FromBridge::NOTIFICATION) {
         return false;
      }

      auto notif = fromBridge.getNotification();
      if (notif.getCallbackId() != callbackId) {
         return false;
      }
      if (notif.which() != Codec::Bridge::Notification::UNLOCK_REQUEST) {
         return false;
      }
      auto counter = notif.getCounter();

      //push passphrase
      {
         capnp::MallocMessageBuilder message;
         auto toBridge = message.initRoot<Codec::Bridge::ToBridge>();
         auto notifReply = toBridge.initNotification();
         notifReply.setSuccess(true);
         notifReply.setCounter(counter);
         notifReply.setUnlockRequest(passphrase);

         auto rawReq = serializeCapnp(message);
         pushRequest(bridge_, rawReq);
      }

      //expect success reply
      bool success = false;
      {
         auto rawReply = waitOnReply();
         kj::ArrayPtr<const capnp::word> words(
            reinterpret_cast<const capnp::word*>(rawReply->data.getPtr()),
            rawReply->data.getSize() / sizeof(capnp::word));
         capnp::FlatArrayMessageReader reader(words);
         auto fromBridge = reader.getRoot<Codec::Bridge::FromBridge>();
         if (fromBridge.which() != Codec::Bridge::FromBridge::REPLY) {
            return false;
         }

         auto rpcReply = fromBridge.getReply();
         if (rpcReply.getReferenceId() != refId) {
            success = false;
         } else {
            success = rpcReply.getSuccess();
         }
      }

      //expect prompt cleanup
      {
         auto rawReply = waitOnReply();
         kj::ArrayPtr<const capnp::word> words(
            reinterpret_cast<const capnp::word*>(rawReply->data.getPtr()),
            rawReply->data.getSize() / sizeof(capnp::word));
         capnp::FlatArrayMessageReader reader(words);
         auto fromBridge = reader.getRoot<Codec::Bridge::FromBridge>();
         if (fromBridge.which() != Codec::Bridge::FromBridge::NOTIFICATION) {
            return false;
         }
   
         auto notif = fromBridge.getNotification();
         if (notif.getCallbackId() != callbackId) {
            return false;
         }
         if (notif.which() != Codec::Bridge::Notification::CLEANUP) {
            return false;
         }
      }

      return success;
   }

   int failToUnlockWallet(const std::string& path, unsigned count)
   {
      uint64_t refId = rand();
      auto callbackId = Cryptography::PRNG::fortuna.generateRandom(10).toHexStr();

      //request unlock
      {
         capnp::MallocMessageBuilder message;
         auto toBridge = message.initRoot<Codec::Bridge::ToBridge>();
         toBridge.setReferenceId(refId);
         auto request = toBridge.initWalletManager();
         auto unlockReq = request.initUnlockControlHeader();

         unlockReq.setCallbackId(callbackId);
         unlockReq.setWalletPath(path);

         auto rawReq = serializeCapnp(message);
         pushRequest(bridge_, rawReq);
      }

      unsigned attempts = 0;
      while (attempts <= count) {
         //wait on passphrase prompt
         auto rawPrompt = waitOnReply();
         kj::ArrayPtr<const capnp::word> words(
            reinterpret_cast<const capnp::word*>(rawPrompt->data.getPtr()),
            rawPrompt->data.getSize() / sizeof(capnp::word));
         capnp::FlatArrayMessageReader reader(words);
         auto fromBridge = reader.getRoot<Codec::Bridge::FromBridge>();
         if (fromBridge.which() != Codec::Bridge::FromBridge::NOTIFICATION) {
            return -1;
         }

         auto notif = fromBridge.getNotification();
         if (notif.getCallbackId() != callbackId) {
            return -2;
         }
         if (notif.which() != Codec::Bridge::Notification::UNLOCK_REQUEST) {
            return -3;
         }
         auto counter = notif.getCounter();

         //push passphrase
         {
            capnp::MallocMessageBuilder message;
            auto toBridge = message.initRoot<Codec::Bridge::ToBridge>();
            auto notifReply = toBridge.initNotification();
            notifReply.setCounter(counter);

            if (attempts != count) {
               notifReply.setSuccess(true);
               auto badPass = Cryptography::PRNG::fortuna.generateRandom(10).toHexStr();
               notifReply.setUnlockRequest(badPass);
            } else {
               notifReply.setSuccess(false);
            }

            auto rawReq = serializeCapnp(message);
            pushRequest(bridge_, rawReq);
         }
         ++attempts;
      }

      //expect failure notif
      {
         auto rawReply = waitOnReply();
         kj::ArrayPtr<const capnp::word> words(
            reinterpret_cast<const capnp::word*>(rawReply->data.getPtr()),
            rawReply->data.getSize() / sizeof(capnp::word));
         capnp::FlatArrayMessageReader reader(words);
         auto fromBridge = reader.getRoot<Codec::Bridge::FromBridge>();
         if (fromBridge.which() != Codec::Bridge::FromBridge::REPLY) {
            return -4;
         }

         auto rpcReply = fromBridge.getReply();
         if (rpcReply.getReferenceId() != refId) {
            return -5;
         }
         if (rpcReply.getSuccess() != false) {
            return -6;
         }

         std::string errorStr = rpcReply.getError();
         if (errorStr != std::string{"unlock request rejected"}) {
            return -7;
         }
      }

      //expect prompt cleanup
      {
         auto rawReply = waitOnReply();
         kj::ArrayPtr<const capnp::word> words(
            reinterpret_cast<const capnp::word*>(rawReply->data.getPtr()),
            rawReply->data.getSize() / sizeof(capnp::word));
         capnp::FlatArrayMessageReader reader(words);
         auto fromBridge = reader.getRoot<Codec::Bridge::FromBridge>();
         if (fromBridge.which() != Codec::Bridge::FromBridge::NOTIFICATION) {
            return -8;
         }
   
         auto notif = fromBridge.getNotification();
         if (notif.getCallbackId() != callbackId) {
            return -9;
         }
         if (notif.which() != Codec::Bridge::Notification::CLEANUP) {
            return -10;
         }
      }
      return attempts;
   }

   std::chrono::milliseconds testKDFUnlock(const std::string& walletId)
   {
      uint64_t refId = rand();
      capnp::MallocMessageBuilder message;
      auto toBridge = message.initRoot<Codec::Bridge::ToBridge>();
      toBridge.setReferenceId(refId);
      auto request = toBridge.initWallet();
      request.setWalletId(walletId);
      request.setGetUnlockTime();

      auto rawReq = serializeCapnp(message);
      pushRequest(bridge_, rawReq);

      auto result = waitOnReply();
      kj::ArrayPtr<const capnp::word> words(
         reinterpret_cast<const capnp::word*>(result->data.getPtr()),
         result->data.getSize() / sizeof(capnp::word));
      capnp::FlatArrayMessageReader reader(words);
      auto fromBridge = reader.getRoot<Codec::Bridge::FromBridge>();
      auto reply = fromBridge.getReply();
      if (!reply.getSuccess() || reply.getReferenceId() != refId) {
         return {};
      }
      return std::chrono::milliseconds(reply.getWallet().getGetUnlockTime());
   }

public:
   std::filesystem::path homedir;
   std::shared_ptr<Bridge::CppBridge> bridge_;
};

////////////////////////////////////////////////////////////////////////////////
TEST_F(BridgeWalletTests, ListStageLoad)
{
   /*
   This test covers the same scenario as WalletManagerTests.ListStageLoad,
   but through CppBridge rather than WalletManager directly
   */
   std::vector<std::pair<std::filesystem::path, std::string>> walletFiles;

   //wallet 1
   {
      Wallets::IO::CreateWalletParams params{
         homedir,
         {1ms, 0, SecureBinaryData::fromString("privpass1")},
         {},
         nullptr, 4
      };

      std::unique_ptr<Seeds::ClearTextSeed> seed(
         new Seeds::ClearTextSeed_Armory());
      auto assetWlt = Wallets::AssetWallet_Single::createFromSeed(
         std::move(seed), params);
      walletFiles.emplace_back(
         std::make_pair(assetWlt->getDbFilename(), assetWlt->getID()));
   }

   //wallet 2, 3, 4
   for (unsigned i=2; i<5; i++) {
      Wallets::IO::CreateWalletParams params{
         homedir,
         {1ms, 0, SecureBinaryData::fromString("privpass" + std::to_string(i))},
         {1ms, 0, SecureBinaryData::fromString("controlpass" + std::to_string(i))},
         nullptr, 4
      };

      std::unique_ptr<Seeds::ClearTextSeed> seed(
         new Seeds::ClearTextSeed_Armory());
      auto assetWlt = Wallets::AssetWallet_Single::createFromSeed(
         std::move(seed), params);
      walletFiles.emplace_back(
         std::make_pair(assetWlt->getDbFilename(), assetWlt->getID()));
   }

   //list wallets
   try {
      auto wltList = listWallets(bridge_);
      ASSERT_EQ(wltList.size(), 4);
      auto checkWltList = [&wltList, &walletFiles](
         unsigned intId, int expectedState, bool expectedStaged)->bool
      {
         auto entry = walletFiles[intId];
         auto path = entry.first.filename().string();
         auto capnEntry = wltList.at(path);

         if (capnEntry.loadState != expectedState) {
            return false;
         }

         if (expectedState == CapnWalletState_Ready) {
            if (capnEntry.walletId != entry.second) {
               return false;
            }
         }
         return capnEntry.staged == expectedStaged;
      };
      EXPECT_TRUE(checkWltList(0, CapnWalletState_Ready, true));
      EXPECT_TRUE(checkWltList(1, CapnWalletState_Encrypted, false));
      EXPECT_TRUE(checkWltList(2, CapnWalletState_Encrypted, false));
      EXPECT_TRUE(checkWltList(3, CapnWalletState_Encrypted, false));
   } catch (const std::exception& e) {
      ASSERT_TRUE(false) << e.what();
   }

   //unlock wallets 3 & 4
   ASSERT_TRUE(unlockWallet(walletFiles[2].first.filename().string(), "controlpass3"));
   ASSERT_TRUE(unlockWallet(walletFiles[3].first.filename().string(), "controlpass4"));

   //list wallets again, check for unlocks
   try {
      auto wltList = listWallets(bridge_);
      ASSERT_EQ(wltList.size(), 4);
      auto checkWltList = [&wltList, &walletFiles](
         unsigned intId, int expectedState, bool expectedStaged)->bool
      {
         auto entry = walletFiles[intId];
         auto path = entry.first.filename().string();
         auto capnEntry = wltList.at(path);

         if (capnEntry.loadState != expectedState) {
            return false;
         }

         if (expectedState == CapnWalletState_Ready) {
            if (capnEntry.walletId != entry.second) {
               return false;
            }
         }
         return capnEntry.staged == expectedStaged;
      };
      EXPECT_TRUE(checkWltList(0, CapnWalletState_Ready, true));
      EXPECT_TRUE(checkWltList(1, CapnWalletState_Encrypted, false));
      EXPECT_TRUE(checkWltList(2, CapnWalletState_Ready, true));
      EXPECT_TRUE(checkWltList(3, CapnWalletState_Ready, true));
   } catch (const std::exception& e) {
      ASSERT_TRUE(false) << e.what();
   }

   //fail to unlock wallet 2
   ASSERT_EQ(failToUnlockWallet(walletFiles[1].first.filename().string(), 2), 3);

   //unstage wallet 4
   stageWallet(bridge_, walletFiles[3].second, false);

   //list wallets again, check for staging
   try {
      auto wltList = listWallets(bridge_);
      ASSERT_EQ(wltList.size(), 4);
      auto checkWltList = [&wltList, &walletFiles](
         unsigned intId, int expectedState, bool expectedStaged)->bool
      {
         auto entry = walletFiles[intId];
         auto path = entry.first.filename().string();
         auto capnEntry = wltList.at(path);

         if (capnEntry.loadState != expectedState) {
            return false;
         }

         if (expectedState == CapnWalletState_Ready) {
            if (capnEntry.walletId != entry.second) {
               return false;
            }
         }
         return capnEntry.staged == expectedStaged;
      };
      EXPECT_TRUE(checkWltList(0, CapnWalletState_Ready, true));
      EXPECT_TRUE(checkWltList(1, CapnWalletState_Encrypted, false));
      EXPECT_TRUE(checkWltList(2, CapnWalletState_Ready, true));
      EXPECT_TRUE(checkWltList(3, CapnWalletState_Ready, false));
   } catch (const std::exception& e) {
      ASSERT_TRUE(false) << e.what();
   }

   //load wallets
   auto wallets = loadWallets(bridge_);
   ASSERT_EQ(wallets.size(), 2);
   EXPECT_NE(wallets.find(walletFiles[0].second), wallets.end());
   EXPECT_NE(wallets.find(walletFiles[2].second), wallets.end());

   //list wallets one last time
   //list wallets again, check for staging
   try {
      auto wltList = listWallets(bridge_);
      ASSERT_EQ(wltList.size(), 2);
      auto checkWltList = [&wltList, &walletFiles](
         unsigned intId, int expectedState, bool expectedStaged)->bool
      {
         auto entry = walletFiles[intId];
         auto path = entry.first.filename().string();
         auto capnEntry = wltList.at(path);

         if (capnEntry.loadState != expectedState) {
            return false;
         }

         if (expectedState == CapnWalletState_Ready) {
            if (capnEntry.walletId != entry.second) {
               return false;
            }
         }
         return capnEntry.staged == expectedStaged;
      };
      EXPECT_TRUE(checkWltList(1, CapnWalletState_Encrypted, false));
      EXPECT_TRUE(checkWltList(3, CapnWalletState_Ready, false));
   } catch (const std::exception& e) {
      ASSERT_TRUE(false) << e.what();
   }
}

TEST_F(BridgeWalletTests, ListWO)
{
   std::vector<std::pair<std::filesystem::path, std::string>> wltPaths;
   std::string walletId;

   //wallet 1
   {
      Wallets::IO::CreateWalletParams params{
         homedir,
         {1ms, 0, SecureBinaryData::fromString("privpass1")},
         {1ms, 0, SecureBinaryData::fromString("ctrlPass")},
         nullptr, 4
      };

      std::unique_ptr<Seeds::ClearTextSeed> seed(
         new Seeds::ClearTextSeed_Armory());
      auto assetWlt = Wallets::AssetWallet_Single::createFromSeed(
         std::move(seed), params);
      walletId = assetWlt->getID();
      wltPaths.emplace_back(std::make_pair(
         assetWlt->getDbFilename(), walletId));
   }

   {
      const auto& fullWltPath = wltPaths.begin()->first;
      auto woWltPath = Wallets::AssetWallet::forkWatchingOnly({fullWltPath,
         [](const std::set<Wallets::EncryptionKeyId>&)->Passphrase::Result
         {
            return { SecureBinaryData::fromString("ctrlPass"), true };
         }},
         {1ms, 0, SecureBinaryData::fromString("woPass")}
      );
      ASSERT_FALSE(woWltPath.empty());
      ASSERT_TRUE(FileUtils::pathExists(woWltPath, 0));
      ASSERT_NE(woWltPath, fullWltPath);
      wltPaths.emplace_back(std::make_pair(
         woWltPath, walletId));
   }

   //list wallets
   try {
      auto wltList = listWallets(bridge_);
      ASSERT_EQ(wltList.size(), 2);
      auto checkWltList = [&wltList, &wltPaths](
         unsigned intId, int expectedState, bool expectedStaged)->bool
      {
         auto entry = wltPaths[intId];
         auto path = entry.first.filename().string();
         auto capnEntry = wltList.at(path);

         if (capnEntry.loadState != expectedState) {
            return false;
         }

         if (expectedState == CapnWalletState_Ready) {
            if (capnEntry.walletId != entry.second) {
               return false;
            }
         }

         //encrypted wallets show up as not WO (can't be determined yet)
         if (capnEntry.isWO) {
            return false;
         }
         return capnEntry.staged == expectedStaged;
      };
      EXPECT_TRUE(checkWltList(0, CapnWalletState_Encrypted, false));
      EXPECT_TRUE(checkWltList(1, CapnWalletState_Encrypted, false));
   } catch (const std::exception& e) {
      ASSERT_TRUE(false) << e.what();
   }

   //unlock them
   ASSERT_TRUE(unlockWallet(wltPaths[0].first.filename().string(), "ctrlPass"));
   ASSERT_TRUE(unlockWallet(wltPaths[1].first.filename().string(), "woPass"));

   //list again
   try {
      std::vector<std::string> wltIds;
      auto wltList = listWallets(bridge_);
      ASSERT_EQ(wltList.size(), 2);
      auto checkWltList = [&wltList, &wltPaths, &wltIds](
         unsigned intId, int expectedState,
         bool expectedStaged, bool isWO)->bool
      {
         auto entry = wltPaths[intId];
         auto path = entry.first.filename().string();
         auto capnEntry = wltList.at(path);

         if (capnEntry.loadState != expectedState) {
            return false;
         }

         if (expectedState == CapnWalletState_Ready) {
            if (capnEntry.walletId != entry.second) {
               return false;
            }
            wltIds.emplace_back(capnEntry.walletId);
         }

         if (isWO != capnEntry.isWO) {
            return false;
         }
         return capnEntry.staged == expectedStaged;
      };
      EXPECT_TRUE(checkWltList(0, CapnWalletState_Ready, true, false));
      EXPECT_TRUE(checkWltList(1, CapnWalletState_Ready, true, true));

      ASSERT_EQ(wltIds.size(), 2);
      ASSERT_EQ(wltIds[0], wltIds[1]);
   } catch (const std::exception& e) {
      ASSERT_TRUE(false) << e.what();
   }
}

////////////////////////////////////////////////////////////////////////////////
TEST_F(BridgeWalletTests, CreateWallet)
{
   //create the wallet
   uint64_t refId = rand();
   auto callbackId = Cryptography::PRNG::fortuna.generateRandom(10).toHexStr();

   capnp::MallocMessageBuilder message;
   auto toBridge = message.initRoot<Codec::Bridge::ToBridge>();
   toBridge.setReferenceId(refId);
   auto request = toBridge.initUtils();
   auto createWltReq = request.initCreateWallet();

   createWltReq.setCallbackId(callbackId);
   createWltReq.setWalletType(Codec::Bridge::UtilsRequest::WalletType::LEGACY);
   createWltReq.setLookup(100);
   createWltReq.setLabel("labl");
   createWltReq.setDescription("desc");

   auto rawReq = serializeCapnp(message);
   pushRequest(bridge_, rawReq);

   //handle progress notifs
   std::string masterId;
   std::filesystem::path path;
   try {
      auto walletData = progressWalletCreation(
         bridge_, callbackId,
         "pass1", 500ms, 128, 100);
      masterId = walletData.masterId;
      path = walletData.path;
   } catch (const std::exception& e) {
      ASSERT_TRUE(false) << e.what();
   }

   //validate reply
   auto result = waitOnReply();
   kj::ArrayPtr<const capnp::word> words(
      reinterpret_cast<const capnp::word*>(result->data.getPtr()),
      result->data.getSize() / sizeof(capnp::word));
   capnp::FlatArrayMessageReader reader(words);
   auto fromBridge = reader.getRoot<Codec::Bridge::FromBridge>();
   auto reply = fromBridge.getReply();
   ASSERT_TRUE(reply.getSuccess());
   ASSERT_EQ(reply.getReferenceId(), refId);

   ASSERT_EQ(reply.which(), Codec::Bridge::RpcReply::UTILS);
   auto utilsReply = reply.getUtils();
   ASSERT_EQ(utilsReply.which(), Codec::Bridge::UtilsReply::CREATE_WALLET);
   std::string wltId = utilsReply.getCreateWallet();
   ASSERT_FALSE(wltId.empty());

   //get the wallet data & validate it
   auto wltData = getWalletData(bridge_, wltId, {});
   EXPECT_EQ(wltData.walletId, wltId);
   EXPECT_FALSE(wltData.accountId.empty());
   EXPECT_EQ(wltData.path.filename().string(), path);
   EXPECT_EQ(wltData.masterId, masterId);

   EXPECT_EQ(wltData.label, "labl");
   EXPECT_EQ(wltData.desc, "desc");

   EXPECT_TRUE(wltData.encrypted);
   EXPECT_FALSE(wltData.watchingOnly);
   EXPECT_EQ(wltData.addresses.size(), 1);
   EXPECT_EQ(wltData.kdfMemReq, 128);

   //request KDF unlock time
   auto unlockTime = testKDFUnlock(wltId);
   EXPECT_GE(unlockTime, 500ms) << unlockTime.count();

   //grab backup string
   auto backup = getWalletBackup(bridge_, wltId, "pass1",
      Codec::Bridge::WalletBackup::Type::ARMORY200_A);
   ASSERT_EQ(backup.size(), 2);
   ASSERT_EQ(backup[0].size(), 46);
   ASSERT_EQ(backup[1].size(), 46);
}

TEST_F(BridgeWalletTests, CreateWallet_BIP32)
{
   //create the wallet
   uint64_t refId = rand();
   auto callbackId = Cryptography::PRNG::fortuna.generateRandom(10).toHexStr();

   capnp::MallocMessageBuilder message;
   auto toBridge = message.initRoot<Codec::Bridge::ToBridge>();
   toBridge.setReferenceId(refId);
   auto request = toBridge.initUtils();
   auto createWltReq = request.initCreateWallet();

   createWltReq.setCallbackId(callbackId);
   createWltReq.setWalletType(Codec::Bridge::UtilsRequest::WalletType::STRUCTURED_BIP32);
   createWltReq.setLookup(100);
   createWltReq.setLabel("labl");
   createWltReq.setDescription("desc");

   auto rawReq = serializeCapnp(message);
   pushRequest(bridge_, rawReq);

   //handle progress notifs
   std::string masterId;
   std::filesystem::path path;
   try {
      auto walletData = progressWalletCreation(
         bridge_, callbackId,
         "pass1", 500ms, 128, 100, false, true);
      masterId = walletData.masterId;
      path = walletData.path;
   } catch (const std::exception& e) {
      ASSERT_TRUE(false) << e.what();
   }

   //validate reply
   auto result = waitOnReply();
   kj::ArrayPtr<const capnp::word> words(
      reinterpret_cast<const capnp::word*>(result->data.getPtr()),
      result->data.getSize() / sizeof(capnp::word));
   capnp::FlatArrayMessageReader reader(words);
   auto fromBridge = reader.getRoot<Codec::Bridge::FromBridge>();
   auto reply = fromBridge.getReply();
   ASSERT_TRUE(reply.getSuccess());
   ASSERT_EQ(reply.getReferenceId(), refId);

   ASSERT_EQ(reply.which(), Codec::Bridge::RpcReply::UTILS);
   auto utilsReply = reply.getUtils();
   ASSERT_EQ(utilsReply.which(), Codec::Bridge::UtilsReply::CREATE_WALLET);
   std::string wltId = utilsReply.getCreateWallet();
   ASSERT_FALSE(wltId.empty());

   //get the wallet data & validate it
   auto wltData = getWalletData(bridge_, wltId, {});
   EXPECT_EQ(wltData.walletId, wltId);
   EXPECT_FALSE(wltData.accountId.empty());
   EXPECT_EQ(wltData.path.filename().string(), path);
   EXPECT_EQ(wltData.masterId, masterId);

   EXPECT_EQ(wltData.label, "labl");
   EXPECT_EQ(wltData.desc, "desc");

   EXPECT_TRUE(wltData.encrypted);
   EXPECT_FALSE(wltData.watchingOnly);
   EXPECT_EQ(wltData.addresses.size(), 1);
   EXPECT_EQ(wltData.kdfMemReq, 128);

   //request KDF unlock time
   auto unlockTime = testKDFUnlock(wltId);
   EXPECT_GE(unlockTime, 500ms) << unlockTime.count();

   //grab backup string
   auto backup = getWalletBackup(bridge_, wltId, "pass1",
      Codec::Bridge::WalletBackup::Type::ARMORY200_B);
   ASSERT_EQ(backup.size(), 2);
   ASSERT_EQ(backup[0].size(), 46);
   ASSERT_EQ(backup[1].size(), 46);
}

TEST_F(BridgeWalletTests, DeleteWallet)
{
   std::string masterId;
   std::filesystem::path path;
   std::string wltId;

   {
      //create the wallet
      uint64_t refId = rand();
      auto callbackId = Cryptography::PRNG::fortuna.generateRandom(10).toHexStr();

      capnp::MallocMessageBuilder message;
      auto toBridge = message.initRoot<Codec::Bridge::ToBridge>();
      toBridge.setReferenceId(refId);
      auto request = toBridge.initUtils();
      auto createWltReq = request.initCreateWallet();

      createWltReq.setCallbackId(callbackId);
      createWltReq.setLookup(100);
      createWltReq.setLabel("labl");
      createWltReq.setDescription("desc");

      auto rawReq = serializeCapnp(message);
      pushRequest(bridge_, rawReq);

      //handle progress notifs
      try {
         auto walletData = progressWalletCreation(
            bridge_, callbackId,
            "pass1", 500ms, 0, 100);
         masterId = walletData.masterId;
         path = walletData.path;
      } catch (const std::exception& e) {
         ASSERT_TRUE(false) << e.what();
      }

      //validate reply
      auto result = waitOnReply();
      kj::ArrayPtr<const capnp::word> words(
         reinterpret_cast<const capnp::word*>(result->data.getPtr()),
         result->data.getSize() / sizeof(capnp::word));
      capnp::FlatArrayMessageReader reader(words);
      auto fromBridge = reader.getRoot<Codec::Bridge::FromBridge>();
      auto reply = fromBridge.getReply();
      ASSERT_TRUE(reply.getSuccess());
      ASSERT_EQ(reply.getReferenceId(), refId);

      ASSERT_EQ(reply.which(), Codec::Bridge::RpcReply::UTILS);
      auto utilsReply = reply.getUtils();
      ASSERT_EQ(utilsReply.which(), Codec::Bridge::UtilsReply::CREATE_WALLET);
      wltId = utilsReply.getCreateWallet();
   }

   //get the wallet data & validate it
   ASSERT_FALSE(wltId.empty());
   auto wltData = getWalletData(bridge_, wltId, {});
   EXPECT_EQ(wltData.walletId, wltId);
   EXPECT_FALSE(wltData.accountId.empty());
   ASSERT_FALSE(path.empty());
   EXPECT_EQ(wltData.path.filename().string(), path);
   EXPECT_EQ(wltData.masterId, masterId);

   EXPECT_EQ(wltData.label, "labl");
   EXPECT_EQ(wltData.desc, "desc");

   EXPECT_TRUE(wltData.encrypted);
   EXPECT_FALSE(wltData.watchingOnly);
   EXPECT_EQ(wltData.addresses.size(), 1);
   EXPECT_GE(wltData.kdfMemReq, 8);

   //check wallet path
   auto fullWltPath = homedir / path;
   ASSERT_TRUE(FileUtils::pathExists(fullWltPath, 0));

   //delete said wallet
   {
      uint64_t refId = rand();
      capnp::MallocMessageBuilder message;
      auto toBridge = message.initRoot<Codec::Bridge::ToBridge>();
      toBridge.setReferenceId(refId);
      auto request = toBridge.initWalletManager();
      request.setDeleteWallet(wltId);
      auto rawReq = serializeCapnp(message);
      pushRequest(bridge_, rawReq);

      //validate reply
      auto result = waitOnReply();
      kj::ArrayPtr<const capnp::word> words(
         reinterpret_cast<const capnp::word*>(result->data.getPtr()),
         result->data.getSize() / sizeof(capnp::word));
      capnp::FlatArrayMessageReader reader(words);
      auto fromBridge = reader.getRoot<Codec::Bridge::FromBridge>();
      auto reply = fromBridge.getReply();
      ASSERT_TRUE(reply.getSuccess());
      ASSERT_EQ(reply.getReferenceId(), refId);
      ASSERT_FALSE(FileUtils::pathExists(fullWltPath, 0));
   }
}

TEST_F(BridgeWalletTests, CreateWallet_Reject)
{
   //create the wallet
   uint64_t refId = rand();
   auto callbackId = Cryptography::PRNG::fortuna.generateRandom(10).toHexStr();

   capnp::MallocMessageBuilder message;
   auto toBridge = message.initRoot<Codec::Bridge::ToBridge>();
   toBridge.setReferenceId(refId);
   auto request = toBridge.initUtils();
   auto createWltReq = request.initCreateWallet();

   createWltReq.setCallbackId(callbackId);
   createWltReq.setLookup(100);
   createWltReq.setLabel("labl");
   createWltReq.setDescription("desc");

   auto rawReq = serializeCapnp(message);
   pushRequest(bridge_, rawReq);

   //handle progress notifs
   try {
      auto walletData = progressWalletCreation(
         bridge_, callbackId,
         std::string{}, 500ms, 128, 100);
      ASSERT_FALSE(walletData.masterId.empty());
      ASSERT_FALSE(walletData.path.empty());

      //check file is cleaned up
      EXPECT_FALSE(FileUtils::pathExists(
         std::filesystem::path{"./fakehomedir"} / walletData.path, 0));
   } catch (const std::exception& e) {
      ASSERT_TRUE(false) << e.what();
   }

   //validate reply
   auto result = waitOnReply();
   kj::ArrayPtr<const capnp::word> words(
      reinterpret_cast<const capnp::word*>(result->data.getPtr()),
      result->data.getSize() / sizeof(capnp::word));
   capnp::FlatArrayMessageReader reader(words);
   auto fromBridge = reader.getRoot<Codec::Bridge::FromBridge>();
   auto reply = fromBridge.getReply();
   ASSERT_FALSE(reply.getSuccess());
   ASSERT_EQ(reply.getReferenceId(), refId);
   std::string errorStr = reply.getError();
   EXPECT_EQ(errorStr, std::string{"passphrase request was rejected"});
}

////////////////////////////////////////////////////////////////////////////////
TEST_F(BridgeWalletTests, RestoreWallet_Legacy)
{
   const std::string walletId{"292AxMD9H"};
   const std::vector<std::string> lines {
      "oiow rfta wueg hewo  wuaj jawj rddi uufu  tusi",
      "idnt enrd sjgo tgfi  esni eutw ktna ustg  arfe",
      "jdtf fink jshs ewda  kkor daet kgtr eiha  ejgd",
      "uaew ggod ngjk ejuu  rugf kufg awnn ofas  rhtf"
   };
   const std::string passphrase{"privPassTest"};

   //restore the wallet
   auto restoreData = restoreWallet(bridge_, lines, walletId,
      Codec::Bridge::WalletBackup::Type::LEGACY135_A,
      passphrase, 300ms, 32, false, 500);

   //get the wallet data & validate it
   auto wltData = getWalletData(bridge_, walletId, {});
   EXPECT_EQ(wltData.walletId, walletId);
   EXPECT_FALSE(wltData.accountId.empty());
   EXPECT_EQ(wltData.path.filename(), restoreData.path);
   EXPECT_EQ(wltData.masterId, restoreData.masterId);

   EXPECT_TRUE(wltData.encrypted);
   EXPECT_FALSE(wltData.watchingOnly);
   EXPECT_EQ(wltData.addresses.size(), 1);
   EXPECT_EQ(wltData.lookup, 500);
   EXPECT_EQ(wltData.kdfMemReq, 32);

   //request KDF unlock time
   auto unlockTime = testKDFUnlock(walletId);
   EXPECT_GE(unlockTime, 300ms) << unlockTime.count();

   //grab backup strings via callback
   {
      uint64_t refId = rand();
      auto callbackId = Cryptography::PRNG::fortuna.generateRandom(10).toHexStr();

      capnp::MallocMessageBuilder message;
      auto toBridge = message.initRoot<Codec::Bridge::ToBridge>();
      toBridge.setReferenceId(refId);
      auto request = toBridge.initWallet();
      request.setWalletId(walletId);
      auto reqBackup = request.initCreateBackupString();
      reqBackup.setPrivate(callbackId);
      auto rawReq = serializeCapnp(message);
      pushRequest(bridge_, rawReq);

      //wait for unlock request
      auto result = waitOnReply();
      kj::ArrayPtr<const capnp::word> words(
         reinterpret_cast<const capnp::word*>(result->data.getPtr()),
         result->data.getSize() / sizeof(capnp::word));
      capnp::FlatArrayMessageReader reader(words);
      auto fromBridge = reader.getRoot<Codec::Bridge::FromBridge>();
      auto notif = fromBridge.getNotification();
      ASSERT_EQ(notif.getCallbackId(), callbackId);
      ASSERT_EQ(notif.which(), Codec::Bridge::Notification::UNLOCK_REQUEST);

      //reply with passphrase
      capnp::MallocMessageBuilder notifMsg;
      auto notifBridge = notifMsg.initRoot<Codec::Bridge::ToBridge>();
      auto notifReply = notifBridge.initNotification();
      notifReply.setSuccess(true);
      notifReply.setCounter(notif.getCounter());
      notifReply.setUnlockRequest(passphrase);

      auto rawNotif = serializeCapnp(notifMsg);
      pushRequest(bridge_, rawNotif);

      //cleanup notif
      result = waitOnReply();
      words = kj::ArrayPtr<const capnp::word>(
         reinterpret_cast<const capnp::word*>(result->data.getPtr()),
         result->data.getSize() / sizeof(capnp::word));
      reader = capnp::FlatArrayMessageReader(words);
      fromBridge = reader.getRoot<Codec::Bridge::FromBridge>();
      notif = fromBridge.getNotification();
      ASSERT_EQ(notif.which(), Codec::Bridge::Notification::CLEANUP);

      //check the backup
      auto backup = waitOnReply();
      words = kj::ArrayPtr<const capnp::word>(
         reinterpret_cast<const capnp::word*>(backup->data.getPtr()),
         backup->data.getSize() / sizeof(capnp::word));
      capnp::FlatArrayMessageReader replyReader(words);
      fromBridge = replyReader.getRoot<Codec::Bridge::FromBridge>();
      ASSERT_EQ(fromBridge.which(), Codec::Bridge::FromBridge::REPLY);
      auto reply = fromBridge.getReply();
      ASSERT_TRUE(reply.getSuccess());
      ASSERT_EQ(reply.getReferenceId(), refId);

      ASSERT_EQ(reply.which(), Codec::Bridge::RpcReply::WALLET);
      auto walletReply = reply.getWallet();
      ASSERT_EQ(walletReply.which(), Codec::Bridge::WalletReply::CREATE_BACKUP_STRING);

      auto capnBackup = walletReply.getCreateBackupString();
      auto rootLines = capnBackup.getRootClear();
      ASSERT_EQ(rootLines.size(), 2);
      ASSERT_EQ(rootLines[0], lines[0]);
      ASSERT_EQ(rootLines[1], lines[1]);

      auto chainLines = capnBackup.getChainClear();
      ASSERT_EQ(chainLines.size(), 2);
      ASSERT_EQ(chainLines[0], lines[2]);
      ASSERT_EQ(chainLines[1], lines[3]);
   }

   //grab backup strings via passphrase
   try {
      auto capnLines = getWalletBackup(bridge_, walletId, passphrase,
         Codec::Bridge::WalletBackup::Type::LEGACY135_A);
      ASSERT_EQ(capnLines.size(), 4);

      for (unsigned i = 0; i < 4; i++) {
         ASSERT_EQ(lines[i], capnLines[i]);
      }
   } catch (const std::exception& e) {
      ASSERT_TRUE(false) << e.what();
   }
}

TEST_F(BridgeWalletTests, RestoreWallet_BIP32)
{
   const std::string walletId{"2tkj1x7cb"};
   const std::vector<std::string> lines {
      "reah edre tttd rekh  skhj ride anag weht  eeaw",
      "shdu ksuj fetd fguo  skwk twnw tsdr gkwg  gawh"
   };
   const std::string passphrase{"privPassTest"};

   //restore the wallet
   auto restoreData = restoreWallet(bridge_, lines, walletId,
      Codec::Bridge::WalletBackup::Type::ARMORY200_B,
      passphrase, 0ms, 32, false, 500);

   //get the wallet data & validate it
   auto wltData = getWalletData(bridge_, walletId, {});
   EXPECT_EQ(wltData.walletId, walletId);
   EXPECT_FALSE(wltData.accountId.empty());
   EXPECT_EQ(wltData.path.filename(), restoreData.path);
   EXPECT_EQ(wltData.masterId, restoreData.masterId);

   EXPECT_TRUE(wltData.encrypted);
   EXPECT_FALSE(wltData.watchingOnly);
   EXPECT_EQ(wltData.addresses.size(), 1);
   EXPECT_EQ(wltData.lookup, 499);
   EXPECT_EQ(wltData.kdfMemReq, 32);

   //grab backup strings via passphrase
   try {
      auto capnLines = getWalletBackup(bridge_, walletId, passphrase,
         Codec::Bridge::WalletBackup::Type::ARMORY200_B);
      ASSERT_EQ(capnLines.size(), 2);

      for (unsigned i = 0; i < 2; i++) {
         ASSERT_EQ(lines[i], capnLines[i]);
      }
   } catch (const std::exception& e) {
      ASSERT_TRUE(false) << e.what();
   }
}

TEST_F(BridgeWalletTests, RestoreWallet_LegacyWO)
{
   const std::string walletId{"292AxMD9H"};
   const std::vector<std::string> lines {
      "wswg egsh oghk aaaj eu",
      "kdrk ouid agee jttn tfaa ruun twsu jfgu nasj",
      "otrr egjh farw dfuo gddh ugki fhrt dgeh geth",
      "jdtf fink jshs ewda kkor daet kgtr eiha ejgd",
      "uaew ggod ngjk ejuu rugf kufg awnn ofas rhtf",
   };
   const std::string passphrase{"privPassTest"};

   //restore the wallet
   auto restoreData = restoreWallet(bridge_, lines, walletId,
      Codec::Bridge::WalletBackup::Type::LEGACY135_A,
      passphrase, 300ms, 32, false, 500);

   //get the wallet data & validate it
   auto wltData = getWalletData(bridge_, walletId, {});
   EXPECT_EQ(wltData.walletId, walletId);
   EXPECT_FALSE(wltData.accountId.empty());
   EXPECT_EQ(wltData.path.filename(), restoreData.path);
   EXPECT_EQ(wltData.masterId, restoreData.masterId);

   EXPECT_FALSE(wltData.encrypted);
   EXPECT_TRUE(wltData.watchingOnly);
   EXPECT_EQ(wltData.addresses.size(), 1);
   EXPECT_EQ(wltData.lookup, 500);
   EXPECT_EQ(wltData.kdfMemReq, 0);

   //grab backup strings
   {
      uint64_t refId = rand();

      capnp::MallocMessageBuilder message;
      auto toBridge = message.initRoot<Codec::Bridge::ToBridge>();
      toBridge.setReferenceId(refId);
      auto request = toBridge.initWallet();
      request.setWalletId(walletId);
      auto reqBackup = request.initCreateBackupString();
      reqBackup.setPublic();
      auto rawReq = serializeCapnp(message);
      pushRequest(bridge_, rawReq);

      //check the backup
      auto backup = waitOnReply();
      auto words = kj::ArrayPtr<const capnp::word>(
         reinterpret_cast<const capnp::word*>(backup->data.getPtr()),
         backup->data.getSize() / sizeof(capnp::word));
      capnp::FlatArrayMessageReader replyReader(words);
      auto fromBridge = replyReader.getRoot<Codec::Bridge::FromBridge>();
      ASSERT_EQ(fromBridge.which(), Codec::Bridge::FromBridge::REPLY);
      auto reply = fromBridge.getReply();
      ASSERT_TRUE(reply.getSuccess());
      ASSERT_EQ(reply.getReferenceId(), refId);

      ASSERT_EQ(reply.which(), Codec::Bridge::RpcReply::WALLET);
      auto walletReply = reply.getWallet();
      ASSERT_EQ(walletReply.which(), Codec::Bridge::WalletReply::CREATE_BACKUP_STRING);

      auto capnBackup = walletReply.getCreateBackupString();
      auto rootLines = capnBackup.getRootClear();
      ASSERT_EQ(rootLines.size(), 2);
      ASSERT_EQ(rootLines[0], lines[1]);
      ASSERT_EQ(rootLines[1], lines[2]);

      auto chainLines = capnBackup.getChainClear();
      ASSERT_EQ(chainLines.size(), 2);
      ASSERT_EQ(chainLines[0], lines[3]);
      ASSERT_EQ(chainLines[1], lines[4]);

      //backupid
      auto backupId = capnBackup.getBackupId();
      ASSERT_EQ(backupId, lines[0]);
   }
}

TEST_F(BridgeWalletTests, RestoreMerge)
{
   //create the wallet
   uint64_t refId = rand();
   auto callbackId = Cryptography::PRNG::fortuna.generateRandom(10).toHexStr();
   std::string passphrase{"pass2"};
   std::string passphrase2{"pass3"};
   unsigned lookup = 46;

   capnp::MallocMessageBuilder message;
   auto toBridge = message.initRoot<Codec::Bridge::ToBridge>();
   toBridge.setReferenceId(refId);
   auto request = toBridge.initUtils();
   auto createWltReq = request.initCreateWallet();

   createWltReq.setCallbackId(callbackId);
   createWltReq.setLookup(lookup);
   createWltReq.setLabel("labl2");
   createWltReq.setDescription("desc2");

   auto rawReq = serializeCapnp(message);
   pushRequest(bridge_, rawReq);

   //handle progress notifs
   std::string masterId;
   std::filesystem::path path;
   try {
      auto walletData = progressWalletCreation(
         bridge_, callbackId,
         passphrase, 500ms, 128, lookup);
      masterId = walletData.masterId;
      path = walletData.path;
   } catch (const std::exception& e) {
      ASSERT_TRUE(false) << e.what();
   }

   //validate reply
   auto result = waitOnReply();
   kj::ArrayPtr<const capnp::word> words(
      reinterpret_cast<const capnp::word*>(result->data.getPtr()),
      result->data.getSize() / sizeof(capnp::word));
   capnp::FlatArrayMessageReader reader(words);
   auto fromBridge = reader.getRoot<Codec::Bridge::FromBridge>();
   auto reply = fromBridge.getReply();
   ASSERT_TRUE(reply.getSuccess());
   ASSERT_EQ(reply.getReferenceId(), refId);

   ASSERT_EQ(reply.which(), Codec::Bridge::RpcReply::UTILS);
   auto utilsReply = reply.getUtils();
   ASSERT_EQ(utilsReply.which(), Codec::Bridge::UtilsReply::CREATE_WALLET);
   std::string wltId = utilsReply.getCreateWallet();
   ASSERT_FALSE(wltId.empty());

   //get the wallet data & validate it
   auto wltData = getWalletData(bridge_, wltId, {});
   EXPECT_EQ(wltData.walletId, wltId);
   EXPECT_FALSE(wltData.accountId.empty());
   EXPECT_EQ(wltData.path.filename().string(), path);
   EXPECT_EQ(wltData.masterId, masterId);

   EXPECT_EQ(wltData.label, "labl2");
   EXPECT_EQ(wltData.desc, "desc2");

   EXPECT_TRUE(wltData.encrypted);
   EXPECT_FALSE(wltData.watchingOnly);
   EXPECT_EQ(wltData.addresses.size(), 1);
   EXPECT_EQ(wltData.lookup, lookup);
   EXPECT_EQ(wltData.kdfMemReq, 128);

   //grab 3 addresses
   std::vector<AddressData> addresses;
   addresses.emplace_back(*wltData.addresses.begin());
   for (unsigned i=0; i<3; i++) {
      addresses.emplace_back(getAddress(bridge_, wltId, wltData.accountId));
   }

   for (unsigned i=0; i<4; i++) {
      const auto& addr = addresses[i];
      ASSERT_EQ(addr.index, i);
      ASSERT_FALSE(addr.hash.empty());
   }

   //grab the backup strings
   std::vector<std::string> lines;
   try {
      lines = getWalletBackup(bridge_, wltId, passphrase,
         Codec::Bridge::WalletBackup::Type::ARMORY200_A);
   } catch (const std::exception& e) {
      ASSERT_TRUE(false) << e.what();
   }

   //restore merge the wallet
   ASSERT_EQ(lines.size(), 2);
   for (const auto& line : lines) {
      ASSERT_FALSE(line.empty());
   }
   auto restoreData = restoreWallet(bridge_, lines, wltId,
      Codec::Bridge::WalletBackup::Type::ARMORY200_A,
      passphrase2, 1ms, 0, true,
      //the legacy armory account always starts with asset 0
      lookup - 1
   );
   ASSERT_EQ(restoreData.masterId, masterId);
   ASSERT_EQ(restoreData.path, path);

   //validate restored wallet state
   auto wltData2 = getWalletData(bridge_, wltId, {});
   EXPECT_EQ(wltData2.walletId, wltId);
   EXPECT_EQ(wltData2.accountId, wltData.accountId);
   EXPECT_EQ(wltData2.path.filename().string(), path);
   EXPECT_EQ(wltData2.masterId, masterId);

   EXPECT_EQ(wltData2.label, "labl2");
   EXPECT_EQ(wltData2.desc, "desc2");

   EXPECT_TRUE(wltData2.encrypted);
   EXPECT_FALSE(wltData2.watchingOnly);
   EXPECT_EQ(wltData2.addresses.size(), 4);
   EXPECT_EQ(wltData2.lookup, lookup);

   for (const auto& addr : wltData2.addresses) {
      ASSERT_LT(addr.index, 4);
      const auto& addrI = addresses[addr.index];
      ASSERT_EQ(addr.hash, addrI.hash);
   }

   //check it unlocks with passphrase2
   std::vector<std::string> lines2;
   try {
      lines2 = getWalletBackup(bridge_, wltId, passphrase2,
         Codec::Bridge::WalletBackup::Type::ARMORY200_A);
   } catch (const std::exception& e) {
      ASSERT_TRUE(false) << e.what();
   }
   EXPECT_EQ(lines, lines2);
}

////////////////////////////////////////////////////////////////////////////////
TEST_F(BridgeWalletTests, Migrate_Legacy)
{
   //copy test legacy wallet to datadir
   const std::string wltId{"28m472Xbm"sv};
   const std::string fileName = std::string{"armory_"sv} +
      std::string{wltId} +
      std::string{"_.wallet"sv};
   const std::filesystem::path wltPath{fileName};
   std::filesystem::copy(
      "input_files/legacy.wallet"sv,
      homedir / wltPath
   );

   //list the wallet
   try {
      auto wltList = listWallets(bridge_);
      ASSERT_EQ(wltList.size(), 1);

      auto wltInfoLegacy = wltList.at(fileName);
      ASSERT_EQ(wltInfoLegacy.walletId, wltId);
      ASSERT_FALSE(wltInfoLegacy.staged);
      ASSERT_EQ(wltInfoLegacy.loadState, 1);
      ASSERT_EQ(wltInfoLegacy.accountIds.size(), 0);
   } catch (const std::exception& e) {
      ASSERT_TRUE(false) << e.what();
   }

   //migrate it
   auto callbackId = Cryptography::PRNG::fortuna.generateRandom(10).toHexStr();
   uint64_t refId = rand();
   {
      capnp::MallocMessageBuilder message;
      auto toBridge = message.initRoot<Codec::Bridge::ToBridge>();
      toBridge.setReferenceId(refId);
      auto mgrRequest = toBridge.initWalletManager();
      auto migrationRequest = mgrRequest.initMigrateWallet();
      migrationRequest.setWalletPath(fileName);
      migrationRequest.setCallbackId(callbackId);
      auto rawReq = serializeCapnp(message);
      pushRequest(bridge_, rawReq);
   }

   /* handle migration callbacks */

   //unlock notif
   {
      auto result = waitOnReply();
      kj::ArrayPtr<const capnp::word> words(
         reinterpret_cast<const capnp::word*>(result->data.getPtr()),
         result->data.getSize() / sizeof(capnp::word));
      capnp::FlatArrayMessageReader reader(words);
      auto fromBridge = reader.getRoot<Codec::Bridge::FromBridge>();
      ASSERT_EQ(fromBridge.which(), Codec::Bridge::FromBridge::NOTIFICATION);

      auto notif = fromBridge.getNotification();
      ASSERT_EQ (notif.getCallbackId(), callbackId);
      auto counter = notif.getCounter();

      ASSERT_EQ(notif.which(), Codec::Bridge::Notification::UNLOCK_REQUEST);

      capnp::MallocMessageBuilder message;
      auto toBridge = message.initRoot<Codec::Bridge::ToBridge>();
      auto notifReply = toBridge.initNotification();
      notifReply.setSuccess(true);
      notifReply.setCounter(counter);
      notifReply.setUnlockRequest("testnet");
      auto rawReq = serializeCapnp(message);
      pushRequest(bridge_, rawReq);
   }

   //progress new wallet creation
   auto wltData = progressWalletCreation(
      bridge_, callbackId, "newpass", 250ms, 32, 104);

   //validate success
   auto result = waitOnReply();
   kj::ArrayPtr<const capnp::word> words(
      reinterpret_cast<const capnp::word*>(result->data.getPtr()),
      result->data.getSize() / sizeof(capnp::word));
   capnp::FlatArrayMessageReader reader(words);
   auto fromBridge = reader.getRoot<Codec::Bridge::FromBridge>();
   ASSERT_EQ(fromBridge.which(), Codec::Bridge::FromBridge::REPLY);
   auto reply = fromBridge.getReply();
   ASSERT_EQ(reply.getReferenceId(), refId);
   ASSERT_TRUE(reply.getSuccess());

   ASSERT_EQ(reply.which(), Codec::Bridge::RpcReply::WALLET_MANAGER);
   auto mgrReply = reply.getWalletManager();
   ASSERT_EQ(mgrReply.which(), Codec::Bridge::WalletManagerReply::MIGRATE_WALLET);
   std::string migratedId(mgrReply.getMigrateWallet());

   /* list wallets again, migrated wallet should be staged */
   try {
      auto wltList = listWallets(bridge_);
      ASSERT_EQ(wltList.size(), 2);
      for (const auto& wltInfo : wltList) {
         ASSERT_EQ(wltInfo.second.walletId, wltId);
         if (wltInfo.first == fileName) {
            ASSERT_FALSE(wltInfo.second.staged);
            ASSERT_EQ(wltInfo.second.loadState, CapnWalletState_Legacy);
            ASSERT_EQ(wltInfo.second.accountIds.size(), 0);
         } else {
            ASSERT_TRUE(wltInfo.second.staged);
            ASSERT_EQ(wltInfo.second.loadState, CapnWalletState_Ready);
            ASSERT_EQ(wltInfo.second.accountIds.size(), 1);
            ASSERT_EQ(wltInfo.second.accountIds[0], legacyAccId);
         }
      }
   } catch (const std::exception& e) {
      ASSERT_TRUE(false) << e.what();
   }

   {
      auto wallets = loadWallets(bridge_);
      ASSERT_EQ(wallets.size(), 1);
      auto iter = wallets.find(wltId);
      ASSERT_NE(iter, wallets.end());

      ASSERT_TRUE(iter->second.encrypted);
      ASSERT_FALSE(iter->second.watchingOnly);
      EXPECT_EQ(iter->second.kdfMemReq, 32);
      EXPECT_EQ(iter->second.lookup, 104);
      EXPECT_EQ(iter->second.addresses.size(), 3);
   }
}

TEST_F(BridgeWalletTests, ImportWallet_Legacy)
{
   std::filesystem::path legacyWalletFile{"input_files/legacy.wallet"sv};
   const std::string walletId{"28m472Xbm"sv};

   /* import the wallet file */
   uint64_t refId = rand();
   capnp::MallocMessageBuilder message;
   auto toBridge = message.initRoot<Codec::Bridge::ToBridge>();
   toBridge.setReferenceId(refId);
   auto request = toBridge.initUtils();
   request.setImportWallet(legacyWalletFile.string());
   auto rawReq = serializeCapnp(message);
   pushRequest(bridge_, rawReq);

   auto result = waitOnReply();
   kj::ArrayPtr<const capnp::word> words(
      reinterpret_cast<const capnp::word*>(result->data.getPtr()),
      result->data.getSize() / sizeof(capnp::word));
   capnp::FlatArrayMessageReader reader(words);
   auto fromBridge = reader.getRoot<Codec::Bridge::FromBridge>();
   auto reply = fromBridge.getReply();
   ASSERT_TRUE(reply.getSuccess());
   ASSERT_EQ(reply.getReferenceId(), refId);

   /* validate the reply */
   auto utilsReply = reply.getUtils();
   auto wltReply = utilsReply.getImportWallet();

   //type
   EXPECT_EQ(wltReply.which(),
      Codec::Bridge::WalletImportPreview::LEGACY);

   //id
   std::string capnWltId(wltReply.getWalletId());
   EXPECT_EQ(capnWltId, walletId);

   //label
   std::string capnLabel(wltReply.getLabel());
   EXPECT_EQ(capnLabel, "legacy1");

   //description
   std::string capnDesc(wltReply.getDescription());
   EXPECT_EQ(capnDesc, "migration test");

   //address count & index
   EXPECT_EQ(wltReply.getHighestUsedIndex(), 2);
   EXPECT_EQ(wltReply.getAddressCount(), 104);

   //priv keys
   EXPECT_FALSE(wltReply.getWatchingOnly());
   EXPECT_TRUE(wltReply.getEncrypted());
   EXPECT_EQ(wltReply.getKdfMem(), 32 * 1024 * 1024);

   //version
   EXPECT_EQ(wltReply.getSeedVersion(), "1.35");

   /* check wallet was imported to the manager */
   try {
      auto wltList = listWallets(bridge_);
      ASSERT_EQ(wltList.size(), 1);

      auto wltInfoLegacy = wltList.at(legacyWalletFile.filename().string());
      ASSERT_EQ(wltInfoLegacy.walletId, walletId);
      ASSERT_FALSE(wltInfoLegacy.staged);
      ASSERT_EQ(wltInfoLegacy.loadState, 1);
      ASSERT_EQ(wltInfoLegacy.accountIds.size(), 0);
   } catch (const std::exception& e) {
      ASSERT_TRUE(false) << e.what();
   }

   /* migrate the wallet */
   auto callbackId = Cryptography::PRNG::fortuna.generateRandom(10).toHexStr();
   refId = rand();
   {
      capnp::MallocMessageBuilder message;
      auto toBridge = message.initRoot<Codec::Bridge::ToBridge>();
      toBridge.setReferenceId(refId);
      auto mgrRequest = toBridge.initWalletManager();
      auto migrationRequest = mgrRequest.initMigrateWallet();
      migrationRequest.setWalletPath(legacyWalletFile.filename().string());
      migrationRequest.setCallbackId(callbackId);
      auto rawReq = serializeCapnp(message);
      pushRequest(bridge_, rawReq);
   }

   /* handle migration callbacks */

   //unlock notif
   {
      auto result = waitOnReply();
      kj::ArrayPtr<const capnp::word> words(
         reinterpret_cast<const capnp::word*>(result->data.getPtr()),
         result->data.getSize() / sizeof(capnp::word));
      capnp::FlatArrayMessageReader reader(words);
      auto fromBridge = reader.getRoot<Codec::Bridge::FromBridge>();
      ASSERT_EQ(fromBridge.which(), Codec::Bridge::FromBridge::NOTIFICATION);

      auto notif = fromBridge.getNotification();
      ASSERT_EQ (notif.getCallbackId(), callbackId);
      auto counter = notif.getCounter();

      ASSERT_EQ(notif.which(), Codec::Bridge::Notification::UNLOCK_REQUEST);

      capnp::MallocMessageBuilder message;
      auto toBridge = message.initRoot<Codec::Bridge::ToBridge>();
      auto notifReply = toBridge.initNotification();
      notifReply.setSuccess(true);
      notifReply.setCounter(counter);
      notifReply.setUnlockRequest("testnet");
      auto rawReq = serializeCapnp(message);
      pushRequest(bridge_, rawReq);
   }

   //progress new wallet creation
   auto wltData = progressWalletCreation(
      bridge_, callbackId, "newpass", 250ms, 32, 104);

   //validate success
   result = waitOnReply();
   words = kj::ArrayPtr<const capnp::word>{
      reinterpret_cast<const capnp::word*>(result->data.getPtr()),
      result->data.getSize() / sizeof(capnp::word)};
   reader = capnp::FlatArrayMessageReader{words};
   fromBridge = reader.getRoot<Codec::Bridge::FromBridge>();
   ASSERT_EQ(fromBridge.which(), Codec::Bridge::FromBridge::REPLY);
   reply = fromBridge.getReply();
   ASSERT_EQ(reply.getReferenceId(), refId);
   ASSERT_TRUE(reply.getSuccess());

   ASSERT_EQ(reply.which(), Codec::Bridge::RpcReply::WALLET_MANAGER);
   auto mgrReply = reply.getWalletManager();
   ASSERT_EQ(mgrReply.which(), Codec::Bridge::WalletManagerReply::MIGRATE_WALLET);
   std::string migratedId(mgrReply.getMigrateWallet());

   /* list wallets again, migrated wallet should be staged */
   try {
      auto wltList = listWallets(bridge_);
      ASSERT_EQ(wltList.size(), 2);
      for (const auto& wltInfo : wltList) {
         ASSERT_EQ(wltInfo.second.walletId, walletId);
         if (wltInfo.first == legacyWalletFile.filename()) {
            ASSERT_FALSE(wltInfo.second.staged);
            ASSERT_EQ(wltInfo.second.loadState, CapnWalletState_Legacy);
            ASSERT_EQ(wltInfo.second.accountIds.size(), 0);
         } else {
            ASSERT_TRUE(wltInfo.second.staged);
            ASSERT_EQ(wltInfo.second.loadState, CapnWalletState_Ready);
            ASSERT_EQ(wltInfo.second.accountIds.size(), 1);
            ASSERT_EQ(wltInfo.second.accountIds[0], legacyAccId);
         }
      }
   } catch (const std::exception& e) {
      ASSERT_TRUE(false) << e.what();
   }

   {
      auto wallets = loadWallets(bridge_);
      ASSERT_EQ(wallets.size(), 1);
      auto iter = wallets.find(walletId);
      ASSERT_NE(iter, wallets.end());

      ASSERT_TRUE(iter->second.encrypted);
      ASSERT_FALSE(iter->second.watchingOnly);
      EXPECT_EQ(iter->second.kdfMemReq, 32);
      EXPECT_EQ(iter->second.lookup, 104);
      EXPECT_EQ(iter->second.addresses.size(), 3);
   }
}

////////////////////////////////////////////////////////////////////////////////
TEST_F(BridgeWalletTests, ChangeWalletPassphrase)
{
   /* 1. create an encrypted wallet */
   std::filesystem::path walletPath;
   std::string walletId;

   std::string currentPass{"privPass1"};
   std::string newPass{"newPrivPass2"};

   //wallet 1
   {
      Wallets::IO::CreateWalletParams params{
         homedir,
         {500ms, 0, SecureBinaryData::fromString(currentPass)},
         {1ms, 0, {}},
         nullptr, 4
      };

      std::unique_ptr<Seeds::ClearTextSeed> seed(
         new Seeds::ClearTextSeed_Armory());
      auto assetWlt = Wallets::AssetWallet_Single::createFromSeed(
         std::move(seed), params);
      walletPath = assetWlt->getDbFilename();
      walletId = assetWlt->getID();
   }
   ASSERT_FALSE(walletId.empty());

   /* 2. load it into bridge */
   auto wltList = listWallets(bridge_);
   ASSERT_EQ(wltList.size(), 1);
   ASSERT_EQ(wltList.begin()->first, walletPath.filename().string());

   {
      const auto& wltEntry = wltList.begin()->second;
      ASSERT_EQ(wltEntry.loadState, (int)Bridge::WalletLoadState::Ready);
      ASSERT_TRUE(wltEntry.staged);
      EXPECT_EQ(wltEntry.walletId, walletId);
   }

   auto wallets = loadWallets(bridge_);
   ASSERT_EQ(wallets.size(), 1);
   ASSERT_EQ(wallets.begin()->first, walletId);

   /* 3. check current passphrase, grab backup */
   auto now = std::chrono::system_clock::now();
   auto wltBackupLines = getWalletBackup(bridge_, walletId, currentPass,
      Codec::Bridge::WalletBackup::Type::ARMORY200_A);
   auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
      std::chrono::system_clock::now() - now);
   EXPECT_GE(elapsed, 500ms);
   ASSERT_EQ(wltBackupLines.size(), 2);
   ASSERT_EQ(wltBackupLines[0].size(), 46);
   ASSERT_EQ(wltBackupLines[1].size(), 46);
   ASSERT_NE(wltBackupLines[0], wltBackupLines[1]);

   /* 4. change its passphrase */
   EXPECT_TRUE(changeWalletPassphrase(bridge_, walletId,
      currentPass, newPass));

   /* 5. check current passphrase fails to unlock wallet */
   try {
      auto newLines = getWalletBackup(bridge_, walletId, currentPass,
         Codec::Bridge::WalletBackup::Type::ARMORY200_A);
      ASSERT_TRUE(false);
   } catch (const std::exception& e) {
      EXPECT_EQ(e.what(), std::string{"unlock request rejected"});
   }

   /* 6. check new passphrase unlocks wallet */
   try {
      auto now = std::chrono::system_clock::now();
      auto newLines = getWalletBackup(bridge_, walletId, newPass,
         Codec::Bridge::WalletBackup::Type::ARMORY200_A);
      auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
         std::chrono::system_clock::now() - now);
      EXPECT_GE(elapsed, 500ms);
      ASSERT_EQ(newLines.size(), 2);
      ASSERT_EQ(newLines[0], wltBackupLines[0]);
      ASSERT_EQ(newLines[1], wltBackupLines[1]);
   } catch (const std::exception& e) {
      ASSERT_TRUE(false);
   }
}

////////////////////////////////////////////////////////////////////////////////
TEST_F(BridgeWalletTests, ExtendAddressChain)
{
   /*
   NOTE: bridge is offline in this test. It covers the graceful handling of
   attempted wallet registration after generating a batch of fresh addresses
   */
   /* 1. create a wallet */
   std::filesystem::path walletPath;
   std::string walletId;
   std::string accountId;
   std::string dbId;

   //wallet 1
   {
      Wallets::IO::CreateWalletParams params{
         homedir,
         {1ms, 0, SecureBinaryData::fromString("pass1")},
         {1ms, 0, {}},
         nullptr, 4
      };

      std::unique_ptr<Seeds::ClearTextSeed> seed(
         new Seeds::ClearTextSeed_Armory());
      auto assetWlt = Wallets::AssetWallet_Single::createFromSeed(
         std::move(seed), params);
      walletPath = assetWlt->getDbFilename();
      walletId = assetWlt->getID();
   }
   ASSERT_FALSE(walletId.empty());

   /* 2. load it into bridge */
   auto wltList = listWallets(bridge_);
   ASSERT_EQ(wltList.size(), 1);
   ASSERT_EQ(wltList.begin()->first, walletPath.filename().string());

   {
      const auto& wltEntry = wltList.begin()->second;
      ASSERT_EQ(
         wltEntry.loadState,
         (int)Bridge::WalletLoadState::Ready
      );
      ASSERT_TRUE(wltEntry.staged);
      EXPECT_EQ(wltEntry.walletId, walletId);
   }

   {
      auto wallets = loadWallets(bridge_);
      ASSERT_EQ(wallets.size(), 1);
      auto wltIter = wallets.begin();
      ASSERT_EQ(wltIter->first, walletId);
      ASSERT_EQ(wltIter->second.lookup, 4);
      ASSERT_EQ(wltIter->second.useCount, -1);
      accountId = wltIter->second.accountId;
      dbId = wltIter->second.dbId;
   }

   /* 3. extend its address chain */
   try {
      auto wltData = extendAddressPool(bridge_,
         walletId, accountId, dbId, 10000, false);
      ASSERT_EQ(wltData.walletId, walletId);
      ASSERT_EQ(wltData.accountId, accountId);
      EXPECT_EQ(wltData.useCount, -1);
      EXPECT_EQ(wltData.lookup, 10004);
   } catch (const std::runtime_error& e) {
      ASSERT_TRUE(false) << e.what();
   }

   /* grab wallet data explicitly, check chain again */
   auto walletData = getWalletData(bridge_, walletId, accountId);
   ASSERT_EQ(walletData.walletId, walletId);
   ASSERT_EQ(walletData.accountId, accountId);
   EXPECT_EQ(walletData.useCount, -1);
   EXPECT_EQ(walletData.lookup, 10004);
}

////////////////////////////////////////////////////////////////////////////////
TEST_F(BridgeWalletTests, ForkWO)
{
   std::filesystem::path walletPath;
   std::string walletId;
   std::string accountId;
   std::string dbId;

   //wallet 1
   {
      Wallets::IO::CreateWalletParams params{
         homedir,
         {1ms, 0, SecureBinaryData::fromString("pass1")},
         {1ms, 0, {}},
         nullptr, 4
      };

      std::unique_ptr<Seeds::ClearTextSeed> seed(
         new Seeds::ClearTextSeed_Armory());
      auto assetWlt = Wallets::AssetWallet_Single::createFromSeed(
         std::move(seed), params);
      walletPath = assetWlt->getDbFilename();
      walletId = assetWlt->getID();
   }
   ASSERT_FALSE(walletId.empty());

   /* 2. load it into bridge */
   auto wltList = listWallets(bridge_);
   ASSERT_EQ(wltList.size(), 1);
   ASSERT_EQ(wltList.begin()->first, walletPath.filename().string());

   {
      const auto& wltEntry = wltList.begin()->second;
      ASSERT_EQ(
         wltEntry.loadState,
         (int)Bridge::WalletLoadState::Ready
      );
      ASSERT_TRUE(wltEntry.staged);
      EXPECT_EQ(wltEntry.walletId, walletId);
   }

   {
      auto wallets = loadWallets(bridge_);
      ASSERT_EQ(wallets.size(), 1);
      auto wltIter = wallets.begin();
      ASSERT_EQ(wltIter->first, walletId);
      ASSERT_EQ(wltIter->second.lookup, 4);
      ASSERT_EQ(wltIter->second.useCount, -1);
      accountId = wltIter->second.accountId;
      dbId = wltIter->second.dbId;
   }

   /* 3. fork it */
   uint64_t refId = rand();
   {
      auto callbackId = Cryptography::PRNG::fortuna.generateRandom(10).toHexStr();

      capnp::MallocMessageBuilder message;
      auto toBridge = message.initRoot<Codec::Bridge::ToBridge>();
      toBridge.setReferenceId(refId);
      auto request = toBridge.initWallet();
      request.setWalletId(walletId);
      request.setForkWatchingOnly(callbackId);

      auto rawReq = serializeCapnp(message);
      pushRequest(bridge_, rawReq);

      //deal with notifications
      bool run = true;
      while (run) {
         auto result = waitOnReply();
         kj::ArrayPtr<const capnp::word> words(
            reinterpret_cast<const capnp::word*>(result->data.getPtr()),
            result->data.getSize() / sizeof(capnp::word));
         capnp::FlatArrayMessageReader reader(words);
         auto fromBridge = reader.getRoot<Codec::Bridge::FromBridge>();

         ASSERT_EQ(fromBridge.which(), Codec::Bridge::FromBridge::NOTIFICATION);
         auto notif = fromBridge.getNotification();
         ASSERT_EQ(notif.getCallbackId(), callbackId);
         switch (notif.which())
         {
            case Codec::Bridge::Notification::SET_PASSPHRASE:
            {
               auto counter = notif.getCounter();

               //push passphrase
               capnp::MallocMessageBuilder message;
               auto toBridge = message.initRoot<Codec::Bridge::ToBridge>();
               auto notifReply = toBridge.initNotification();
               notifReply.setSuccess(true);
               notifReply.setCounter(counter);
               auto capnSetPass = notifReply.initSetPassphrase();
               capnSetPass.setPassphrase("woCtrlPass");
               capnSetPass.setReuseKdf(true);

               auto rawReq = serializeCapnp(message);
               pushRequest(bridge_, rawReq);
               break;
            }

            case Codec::Bridge::Notification::CLEANUP:
            {
               run = false;
               break;
            }

            /*case Bridge::Notification::WALLET_PROGRESS:
            {
               auto prog = notif.getWalletProgress();
               switch (prog.which())
               {
               }
               break;
            }*/

            default:
               ASSERT_TRUE(false);
         }
      }
   }

   //handle reply
   auto result = waitOnReply();
   kj::ArrayPtr<const capnp::word> words(
      reinterpret_cast<const capnp::word*>(result->data.getPtr()),
      result->data.getSize() / sizeof(capnp::word));
   capnp::FlatArrayMessageReader reader(words);
   auto fromBridge = reader.getRoot<Codec::Bridge::FromBridge>();
   ASSERT_EQ(fromBridge.which(), Codec::Bridge::FromBridge::REPLY);

   auto reply = fromBridge.getReply();
   ASSERT_EQ(reply.getReferenceId(), refId);
   ASSERT_TRUE(reply.getSuccess());
   std::filesystem::path woWltPath{
      std::string{reply.getWallet().getForkWatchingOnly()}};
   ASSERT_NE(woWltPath.filename(), walletPath.filename());

   /* unload full wallet */
   {
      uint64_t msgId = rand();
      capnp::MallocMessageBuilder message;
      auto toBridge = message.initRoot<Codec::Bridge::ToBridge>();
      toBridge.setReferenceId(msgId);
      auto request = toBridge.initWalletManager();
      request.setUnloadWallet(walletId);

      auto rawReq = serializeCapnp(message);
      pushRequest(bridge_, rawReq);

      auto result = waitOnReply();
      kj::ArrayPtr<const capnp::word> words(
         reinterpret_cast<const capnp::word*>(result->data.getPtr()),
         result->data.getSize() / sizeof(capnp::word));
      capnp::FlatArrayMessageReader reader(words);
      auto fromBridge = reader.getRoot<Codec::Bridge::FromBridge>();

      auto reply = fromBridge.getReply();
      ASSERT_EQ(reply.getReferenceId(), msgId);
      ASSERT_TRUE(reply.getSuccess());
   }

   /* load WO wallet */
   wltList = listWallets(bridge_);
   ASSERT_EQ(wltList.size(), 1);
   ASSERT_EQ(wltList.begin()->first, woWltPath.filename().string());
   ASSERT_EQ(wltList.begin()->second.loadState, CapnWalletState_Encrypted);

   ASSERT_TRUE(unlockWallet(woWltPath.filename().string(), "woCtrlPass"));
   wltList = listWallets(bridge_);
   ASSERT_EQ(wltList.size(), 1);
   ASSERT_EQ(wltList.begin()->first, woWltPath.filename().string());
   ASSERT_EQ(wltList.begin()->second.loadState, CapnWalletState_Ready);

   {
      auto wallets = loadWallets(bridge_);
      ASSERT_EQ(wallets.size(), 1);
      auto wltIter = wallets.begin();
      ASSERT_EQ(wltIter->first, walletId);
      ASSERT_EQ(wltIter->second.lookup, 4);
      ASSERT_EQ(wltIter->second.useCount, -1);
      ASSERT_EQ(wltIter->second.path, woWltPath);
      ASSERT_EQ(wltIter->second.accountId, accountId);
      ASSERT_NE(dbId, wltIter->second.dbId);
   }
}

////////////////////////////////////////////////////////////////////////////////
// BridgeWalletsWithDBTests
class BridgeWalletsWithDBTests : public ::testing::Test
{
protected:
   void initBDM()
   {
      theBDMt_ = new BlockDataManagerThread();
      iface_ = theBDMt_->bdm()->getIFace();
   }

   void prepareWallets()
   {
      auto pubKeyB = Cryptography::ECDSA::computePublicKey(TestChain::privKeyAddrB);
      auto pubKeyC = Cryptography::ECDSA::computePublicKey(TestChain::privKeyAddrC);
      auto pubKeyD = Cryptography::ECDSA::computePublicKey(TestChain::privKeyAddrD);
      auto pubKeyE = Cryptography::ECDSA::computePublicKey(TestChain::privKeyAddrE);

      walletId_ = createWOWallet(homedir_, {
         pubKeyB, pubKeyC, pubKeyD, pubKeyE}
      );
   }

   /////////////////////////////////////////////////////////////////////////////
   virtual void SetUp()
   {
      FileUtils::removeDirectory(blkdir_);
      FileUtils::removeDirectory(homedir_);
      FileUtils::removeDirectory(ldbdir_);

      FileUtils::createDirectory(blkdir_ / "blocks");
      FileUtils::createDirectory(homedir_);
      FileUtils::createDirectory(ldbdir_);

      Config::DBSettings::setServiceType(SERVICE_UNITTEST_WITHWS);

      // Put the first 5 blocks into the blkdir
      blk0dat_ = FileUtils::getBlkFilename(blkdir_ / "blocks", 0);
      TestUtils::setBlocks({ "0", "1", "2", "3", "4", "5" }, blk0dat_);

      WebSocketServer::init();
      Config::parseArgs({
         "--datadir=./fakehomedir",
         "--dbdir=./ldbtestdir",
         "--satoshi-datadir=./blkfiletest",
         "--db-type=DB_BARE",
         "--thread-count=3",
         "--public"},
         Config::ProcessType::DB);
      prepareWallets();

      //setup auth peers for server and client
      authPeersPassLbd_ = [](const std::set<Wallets::EncryptionKeyId>&)
      ->Passphrase::Result
      {
         return { {}, true };
      };

      auto createWltLbd = []()->std::unique_ptr<Passphrase::Params>
      {
         return std::make_unique<Passphrase::Params>(
            1ms, 0, SecureBinaryData{});
      };

      Wallets::AuthorizedPeers::createWallet({
         homedir_ / SERVER_AUTH_PEER_FILENAME, {createWltLbd}});
      Wallets::AuthorizedPeers serverPeers(
         {homedir_ / SERVER_AUTH_PEER_FILENAME, authPeersPassLbd_});

      //share public keys between client and server
      BinaryDataRef pubkeyref{
         serverPeers.getOwnPublicKey().pubkey, BIP151PUBKEYSIZE};
      Wallets::PeerKey servPK{pubkeyref, true, true};
      serverPubkey_ = servPK.toHumanReadable();

      replyQueue.clear();
      bridge_ = std::make_shared<Bridge::CppBridge>();
      bridge_->setWriteLambda([](MsgPtr payload) {
         std::unique_lock<std::mutex> lock(commsMutex);
         replyQueue.emplace_back(std::move(payload));
         commsCV.notify_all();
      });

      initBDM();
      auto nodePtr = std::dynamic_pointer_cast<NodeUnitTest>(
         Config::NetworkSettings::bitcoinNodes().first);
      nodePtr->setBDM(theBDMt_->bdm());
      hexMagicBytes = Config::BitcoinSettings::getMagicBytes().toHexStr();
   }

   /////////////////////////////////////////////////////////////////////////////
   virtual void TearDown()
   {
      bridge_.reset();
      WebSocketServer::shutdown();
      WebSocketServer::waitOnShutdown();
      theBDMt_->shutdown();

      delete theBDMt_;
      theBDMt_ = nullptr;

      FileUtils::removeDirectory(blkdir_);
      FileUtils::removeDirectory(homedir_);
      FileUtils::removeDirectory(ldbdir_);
      Config::reset();
   }

protected:
   BlockDataManagerThread *theBDMt_;
   Passphrase::UnlockFunc authPeersPassLbd_;
   LMDBBlockDatabase* iface_;

   std::filesystem::path blkdir_{"./blkfiletest"sv};
   std::filesystem::path homedir_{"./fakehomedir"sv};
   std::filesystem::path ldbdir_{"./ldbtestdir"sv};
   std::filesystem::path blk0dat_;

   std::string hexMagicBytes;
   std::shared_ptr<Bridge::CppBridge> bridge_;
   std::string walletId_;
   std::string serverPubkey_;
};

TEST_F(BridgeWalletsWithDBTests, Connect)
{
   auto wltList = listWallets(bridge_);
   ASSERT_EQ(wltList.size(), 1);
   auto wltId = wltList.begin()->second.walletId;
   ASSERT_FALSE(wltId.empty());
   ASSERT_EQ(wltId, walletId_);
   auto wallets = loadWallets(bridge_);
   ASSERT_EQ(wallets.size(), 1);

   ASSERT_EQ(wallets.begin()->second.walletId, wltId);
   auto accountId = wallets.begin()->second.accountId;
   ASSERT_FALSE(accountId.empty());

   WebSocketServer::initAuthPeers({
      homedir_ / SERVER_AUTH_PEER_FILENAME, authPeersPassLbd_});
   WebSocketServer::start(theBDMt_->bdm(), true);

   ASSERT_TRUE(connectToIp(bridge_, "127.0.0.1", "9001", serverPubkey_));
   ASSERT_TRUE(registerWallets(bridge_));

   //start db, go online and wait on ready notif
   theBDMt_->start(Config::DBSettings::initMode());
   ASSERT_EQ(goOnline(bridge_), 5);

   //check balances
   auto balances = getAddrBalances(bridge_, wltId, accountId);
   ASSERT_EQ(balances.size(), 4);

   try {
      for (const auto& balPair : balances) {
         const auto& addrBal = TestChain::testAddrBalances[5].at(balPair.first);
         EXPECT_EQ(addrBal[0], balPair.second[0]);
         EXPECT_EQ(addrBal[1], balPair.second[1]);
         EXPECT_EQ(addrBal[2], balPair.second[2]);
      }
   } catch (const std::exception&) {
      ASSERT_TRUE(false);
   }
}

TEST_F(BridgeWalletsWithDBTests, CycleConnection)
{
   auto wltList = listWallets(bridge_);
   ASSERT_EQ(wltList.size(), 1);
   auto wltId = wltList.begin()->second.walletId;
   ASSERT_FALSE(wltId.empty());
   ASSERT_EQ(wltId, walletId_);
   auto wallets = loadWallets(bridge_);
   ASSERT_EQ(wallets.size(), 1);

   ASSERT_EQ(wallets.begin()->second.walletId, wltId);
   auto accountId = wallets.begin()->second.accountId;
   ASSERT_FALSE(accountId.empty());

   WebSocketServer::initAuthPeers({
      homedir_ / SERVER_AUTH_PEER_FILENAME, authPeersPassLbd_});
   WebSocketServer::start(theBDMt_->bdm(), true);

   ASSERT_FALSE(connectToIp(bridge_, "127.0.0.1", "9001", {}));
   ASSERT_TRUE(connectToIp(bridge_, "127.0.0.1", "9001", serverPubkey_));
   ASSERT_TRUE(registerWallets(bridge_));

   //start db, go online and wait on ready notif
   theBDMt_->start(Config::DBSettings::initMode());
   ASSERT_EQ(goOnline(bridge_), 5);

   //check balances
   auto balances = getAddrBalances(bridge_, wltId, accountId);
   ASSERT_EQ(balances.size(), 4);

   try {
      for (const auto& balPair : balances) {
         const auto& addrBal = TestChain::testAddrBalances[5].at(balPair.first);
         EXPECT_EQ(addrBal[0], balPair.second[0]);
         EXPECT_EQ(addrBal[1], balPair.second[1]);
         EXPECT_EQ(addrBal[2], balPair.second[2]);
      }
   } catch (const std::exception&) {
      ASSERT_TRUE(false);
   }

   //kill BDM
   WebSocketServer::shutdown();

   //wait on disconnected notif
   auto reply = waitOnReply();
   kj::ArrayPtr<const capnp::word> words(
      reinterpret_cast<const capnp::word*>(reply->data.getPtr()),
      reply->data.getSize() / sizeof(capnp::word));
   capnp::FlatArrayMessageReader reader(words);

   auto fromBridge = reader.getRoot<Codec::Bridge::FromBridge>();
   ASSERT_EQ(fromBridge.which(), Codec::Bridge::FromBridge::NOTIFICATION);
   auto notif = fromBridge.getNotification();
   ASSERT_EQ(notif.which(), Codec::Bridge::Notification::DISCONNECTED);

   //restart BDM
   WebSocketServer::waitOnShutdown();
   theBDMt_->shutdown();
   delete theBDMt_;

   {
      WebSocketServer::init();
      Config::reset();
      Config::DBSettings::setServiceType(SERVICE_UNITTEST_WITHWS);
      Config::parseArgs({
         "--datadir=./fakehomedir",
         "--dbdir=./ldbtestdir",
         "--satoshi-datadir=./blkfiletest",
         "--db-type=DB_BARE",
         "--thread-count=3",
         "--public"},
         Config::ProcessType::DB);
      initBDM();
      auto nodePtr = std::dynamic_pointer_cast<NodeUnitTest>(
         Config::NetworkSettings::bitcoinNodes().first);
      nodePtr->setBDM(theBDMt_->bdm());
   }

   WebSocketServer::initAuthPeers({
      homedir_ / SERVER_AUTH_PEER_FILENAME, authPeersPassLbd_});
   WebSocketServer::start(theBDMt_->bdm(), true);

   ASSERT_TRUE(connectToIp(bridge_, "127.0.0.1", "9001", serverPubkey_));
   ASSERT_TRUE(registerWallets(bridge_));
   theBDMt_->start(Config::DBSettings::initMode());
   ASSERT_EQ(goOnline(bridge_), 5);

   //check balances
   balances = getAddrBalances(bridge_, wltId, accountId);
   ASSERT_EQ(balances.size(), 4);

   try {
      for (const auto& balPair : balances) {
         const auto& addrBal = TestChain::testAddrBalances[5].at(balPair.first);
         EXPECT_EQ(addrBal[0], balPair.second[0]);
         EXPECT_EQ(addrBal[1], balPair.second[1]);
         EXPECT_EQ(addrBal[2], balPair.second[2]);
      }
   } catch (const std::exception&) {
      ASSERT_TRUE(false);
   }
}

////////////////////////////////////////////////////////////////////////////////
TEST_F(BridgeWalletsWithDBTests, DeleteWallet)
{
   //create a fresh wallet
   auto wltId = createWallet(homedir_);

   auto wltList = listWallets(bridge_);
   ASSERT_EQ(wltList.size(), 2);
   auto wallets = loadWallets(bridge_);
   ASSERT_EQ(wallets.size(), 2);

   std::string accountId, importAccId, dbId;
   for (const auto& wltData : wallets) {
      if (wltData.first != wltId) {
         ASSERT_EQ(wltData.first, walletId_);
         importAccId = wltData.second.accountId;
         ASSERT_FALSE(importAccId.empty());
      } else {
         ASSERT_EQ(wltData.first, wltId);
         accountId = wltData.second.accountId;
         dbId = wltData.second.dbId;
         ASSERT_FALSE(accountId.empty());
      }
   }

   WebSocketServer::initAuthPeers({
      homedir_ / SERVER_AUTH_PEER_FILENAME, authPeersPassLbd_});
   WebSocketServer::start(theBDMt_->bdm(), true);

   ASSERT_TRUE(connectToIp(bridge_, "127.0.0.1", "9001", serverPubkey_));
   ASSERT_TRUE(registerWallets(bridge_));

   //start db, go online and wait on ready notif
   theBDMt_->start(Config::DBSettings::initMode());
   ASSERT_EQ(goOnline(bridge_), 5);

   //check balances
   auto balances = getAddrBalances(bridge_, walletId_, importAccId);
   ASSERT_EQ(balances.size(), 4);
   try {
      for (const auto& balPair : balances) {
         const auto& addrBal = TestChain::testAddrBalances[5].at(balPair.first);
         EXPECT_EQ(addrBal[0], balPair.second[0]);
         EXPECT_EQ(addrBal[1], balPair.second[1]);
         EXPECT_EQ(addrBal[2], balPair.second[2]);
      }
   } catch (const std::exception&) {
      ASSERT_TRUE(false);
   }

   balances = getAddrBalances(bridge_, wltId, accountId);
   ASSERT_EQ(balances.size(), 0);

   //get the wallet path, check it
   std::filesystem::path wltPath;
   for (const auto& entry : wltList) {
      if (entry.second.walletId == wltId) {
         wltPath = homedir_ / entry.first;
         break;
      }
   }
   ASSERT_FALSE(wltPath.empty());
   ASSERT_TRUE(FileUtils::pathExists(wltPath, 0));

   //delete said wallet
   uint64_t refId = rand();
   capnp::MallocMessageBuilder message;
   auto toBridge = message.initRoot<Codec::Bridge::ToBridge>();
   toBridge.setReferenceId(refId);
   auto request = toBridge.initWalletManager();
   request.setDeleteWallet(wltId);
   auto rawReq = serializeCapnp(message);
   pushRequest(bridge_, rawReq);

   //validate reply
   int count = 0;
   while (count < 2) {
      auto result = waitOnReply();
      kj::ArrayPtr<const capnp::word> words(
         reinterpret_cast<const capnp::word*>(result->data.getPtr()),
         result->data.getSize() / sizeof(capnp::word));
      capnp::FlatArrayMessageReader reader(words);
      auto fromBridge = reader.getRoot<Codec::Bridge::FromBridge>();
      switch (fromBridge.which())
      {
         case Codec::Bridge::FromBridge::REPLY:
         {
            auto reply = fromBridge.getReply();
            ASSERT_TRUE(reply.getSuccess());
            ASSERT_EQ(reply.getReferenceId(), refId);
            ++count;
            break;
         }

         case Codec::Bridge::FromBridge::NOTIFICATION:
         {
            auto notif = fromBridge.getNotification();
            ASSERT_EQ(notif.which(), Codec::Bridge::Notification::REFRESH);
            ++count;
            break;
         }

         default:
            ASSERT_TRUE(false);
            break;
      }
   }
   ASSERT_FALSE(FileUtils::pathExists(wltPath, 0));
}

////////////////////////////////////////////////////////////////////////////////
TEST_F(BridgeWalletsWithDBTests, ExtendAddressChain)
{
   //create a fresh wallet
   auto wltId = createWallet(homedir_);

   auto wltList = listWallets(bridge_);
   ASSERT_EQ(wltList.size(), 2);
   auto wallets = loadWallets(bridge_);
   ASSERT_EQ(wallets.size(), 2);

   std::string accountId, importAccId, dbId;
   for (const auto& wltData : wallets) {
      if (wltData.first != wltId) {
         ASSERT_EQ(wltData.first, walletId_);
         importAccId = wltData.second.accountId;
         ASSERT_FALSE(importAccId.empty());
      } else {
         ASSERT_EQ(wltData.first, wltId);
         accountId = wltData.second.accountId;
         dbId = wltData.second.dbId;
         ASSERT_FALSE(accountId.empty());
      }
   }

   WebSocketServer::initAuthPeers({
      homedir_ / SERVER_AUTH_PEER_FILENAME, authPeersPassLbd_});
   WebSocketServer::start(theBDMt_->bdm(), true);

   ASSERT_TRUE(connectToIp(bridge_, "127.0.0.1", "9001", serverPubkey_));
   ASSERT_TRUE(registerWallets(bridge_));

   //start db, go online and wait on ready notif
   theBDMt_->start(Config::DBSettings::initMode());
   ASSERT_EQ(goOnline(bridge_), 5);

   //check balances
   auto balances = getAddrBalances(bridge_, walletId_, importAccId);
   ASSERT_EQ(balances.size(), 4);
   try {
      for (const auto& balPair : balances) {
         const auto& addrBal = TestChain::testAddrBalances[5].at(balPair.first);
         EXPECT_EQ(addrBal[0], balPair.second[0]);
         EXPECT_EQ(addrBal[1], balPair.second[1]);
         EXPECT_EQ(addrBal[2], balPair.second[2]);
      }
   } catch (const std::exception&) {
      ASSERT_TRUE(false);
   }

   balances = getAddrBalances(bridge_, wltId, accountId);
   ASSERT_EQ(balances.size(), 0);

   /* extend address chain */
   try {
      auto wltData = extendAddressPool(bridge_,
         wltId, accountId, dbId, 10000, false);
      ASSERT_EQ(wltData.walletId, wltId);
      ASSERT_EQ(wltData.accountId, accountId);
      EXPECT_EQ(wltData.useCount, -1);
      EXPECT_EQ(wltData.lookup, 10004);
   } catch (const std::runtime_error& e) {
      ASSERT_TRUE(false) << e.what();
   }

   /* grab wallet data explicitly, check chain again */
   auto walletData = getWalletData(bridge_, wltId, accountId);
   ASSERT_EQ(walletData.walletId, wltId);
   ASSERT_EQ(walletData.accountId, accountId);
   EXPECT_EQ(walletData.useCount, -1);
   EXPECT_EQ(walletData.lookup, 10004);
}

TEST_F(BridgeWalletsWithDBTests, AddNewAddress)
{
   /*
   This test covers the edge case where a wallet does not have enough
   computed addresses to serve a getAddress request, in which event it
   needs to trigger the address generation flow first
   */

   //create a fresh wallet
   auto wltId = createWallet(homedir_);

   auto wltList = listWallets(bridge_);
   ASSERT_EQ(wltList.size(), 2);
   auto wallets = loadWallets(bridge_);
   ASSERT_EQ(wallets.size(), 2);

   std::string accountId, importAccId, dbId;
   for (const auto& wltData : wallets) {
      if (wltData.first != wltId) {
         ASSERT_EQ(wltData.first, walletId_);
         importAccId = wltData.second.accountId;
         ASSERT_FALSE(importAccId.empty());
      } else {
         ASSERT_EQ(wltData.first, wltId);
         accountId = wltData.second.accountId;
         dbId = wltData.second.dbId;
         ASSERT_FALSE(accountId.empty());
      }
   }

   WebSocketServer::initAuthPeers({
      homedir_ / SERVER_AUTH_PEER_FILENAME, authPeersPassLbd_});
   WebSocketServer::start(theBDMt_->bdm(), true);

   ASSERT_TRUE(connectToIp(bridge_, "127.0.0.1", "9001", serverPubkey_));
   ASSERT_TRUE(registerWallets(bridge_));

   //start db, go online and wait on ready notif
   theBDMt_->start(Config::DBSettings::initMode());
   ASSERT_EQ(goOnline(bridge_), 5);

   //check balances
   auto balances = getAddrBalances(bridge_, walletId_, importAccId);
   ASSERT_EQ(balances.size(), 4);
   try {
      for (const auto& balPair : balances) {
         const auto& addrBal = TestChain::testAddrBalances[5].at(balPair.first);
         EXPECT_EQ(addrBal[0], balPair.second[0]);
         EXPECT_EQ(addrBal[1], balPair.second[1]);
         EXPECT_EQ(addrBal[2], balPair.second[2]);
      }
   } catch (const std::exception&) {
      ASSERT_TRUE(false);
   }

   balances = getAddrBalances(bridge_, wltId, accountId);
   ASSERT_EQ(balances.size(), 0);

   /* get 4 addresses, wallet should have the data for that */
   {
      auto walletData = getWalletData(bridge_, wltId, accountId);
      ASSERT_EQ(walletData.walletId, wltId);
      ASSERT_EQ(walletData.accountId, accountId);
      EXPECT_EQ(walletData.useCount, -1);
      EXPECT_EQ(walletData.lookup, 4);
   }

   for (unsigned i=0; i<4; i++) {
      getAddress(bridge_, wltId, accountId);
   }

   {
      auto walletData = getWalletData(bridge_, wltId, accountId);
      ASSERT_EQ(walletData.walletId, wltId);
      ASSERT_EQ(walletData.accountId, accountId);
      EXPECT_EQ(walletData.useCount, 3);
      EXPECT_EQ(walletData.lookup, 4);
   }

   /* request a new address, it should trigger address creation */
   {
      uint64_t refId = rand();
      capnp::MallocMessageBuilder message;
      auto toBridge = message.initRoot<Codec::Bridge::ToBridge>();
      toBridge.setReferenceId(refId);
      auto request = toBridge.initWallet();
      request.setWalletId(wltId);
      request.setAccountId(accountId);
      auto reqAddr = request.initGetAddress();
      reqAddr.setNew();
      auto rawReq = serializeCapnp(message);
      pushRequest(bridge_, rawReq);

      bool refreshSeen = false;
      bool done = false;
      while (!done || !refreshSeen) {
         auto result = waitOnReply();
         kj::ArrayPtr<const capnp::word> words(
            reinterpret_cast<const capnp::word*>(result->data.getPtr()),
            result->data.getSize() / sizeof(capnp::word));
         capnp::FlatArrayMessageReader reader(words);
         auto fromBridge = reader.getRoot<Codec::Bridge::FromBridge>();

         switch (fromBridge.which())
         {
            case Codec::Bridge::FromBridge::REPLY:
            {
               auto reply = fromBridge.getReply();
               ASSERT_TRUE(reply.getSuccess());
               ASSERT_EQ(reply.getReferenceId(), refId);
               ASSERT_EQ(reply.which(), Codec::Bridge::RpcReply::WALLET);

               auto walletReply = reply.getWallet();
               ASSERT_EQ(walletReply.which(), Codec::Bridge::WalletReply::GET_ADDRESS);
               done = true;
               break;
            }

            case Codec::Bridge::FromBridge::NOTIFICATION:
            {
               auto notif = fromBridge.getNotification();
               if (notif.which() == Codec::Bridge::Notification::REFRESH) {
                  ASSERT_FALSE(refreshSeen);
                  auto refreshNotif = notif.getRefresh();
                  ASSERT_EQ(refreshNotif.size(), 1);
                  ASSERT_EQ(refreshNotif[0], dbId);
                  refreshSeen = true;
               }
               break;
            }

            default:
               ASSERT_TRUE(false);
               break;
         }
      }
   }

   {
      //grab wallet data explicitly, check chain again
      auto walletData = getWalletData(bridge_, wltId, accountId);
      ASSERT_EQ(walletData.walletId, wltId);
      ASSERT_EQ(walletData.accountId, accountId);
      EXPECT_EQ(walletData.useCount, 4);
      EXPECT_EQ(walletData.lookup, 104);
   }
}

////////////////////////////////////////////////////////////////////////////////
// BridgeChainDataTests
class BridgeChainDataTests : public ::testing::Test
{
protected:
   void initBDM()
   {
      theBDMt_ = new BlockDataManagerThread();
      iface_ = theBDMt_->bdm()->getIFace();
   }

   void prepareWallets()
   {
      auto pubKeyB = Cryptography::ECDSA::computePublicKey(TestChain::privKeyAddrB);
      auto pubKeyC = Cryptography::ECDSA::computePublicKey(TestChain::privKeyAddrC);
      auto pubKeyD = Cryptography::ECDSA::computePublicKey(TestChain::privKeyAddrD);
      auto pubKeyE = Cryptography::ECDSA::computePublicKey(TestChain::privKeyAddrE);
      auto pubKeyF = Cryptography::ECDSA::computePublicKey(TestChain::privKeyAddrF);

      walletId_BCDE_ = createImportWallet(homedir_, {
         TestChain::privKeyAddrB,
         TestChain::privKeyAddrC,
         TestChain::privKeyAddrD,
         TestChain::privKeyAddrE
      });
      walletId_BC_ = createWOWallet(homedir_,
         { pubKeyB, pubKeyC }
      );
      walletId_DE_ = createWOWallet(homedir_,
         { pubKeyD, pubKeyE }
      );
      walletId_AFLB_ = createWOWallet(homedir_,
         { pubKeyF }, {
            TestChain::scrAddrA,
            TestChain::lb1ScrAddr, TestChain::lb1ScrAddrP2SH,
            TestChain::lb2ScrAddr, TestChain::lb2ScrAddrP2SH
         }
      );
   }

   void loadWallets(const std::set<std::string>& walletIds)
   {
      ASSERT_FALSE(walletId_BCDE_.empty());
      ASSERT_FALSE(walletId_BC_.empty());
      ASSERT_FALSE(walletId_DE_.empty());
      ASSERT_FALSE(walletId_AFLB_.empty());

      auto wltList = listWallets(bridge_);
      ASSERT_EQ(wltList.size(), 4);

      for (auto& wltData : wltList) {
         auto iter = walletIds.find(wltData.second.walletId);
         if (iter == walletIds.end()) {
            stageWallet(bridge_, wltData.second.walletId, false);
         }

         if (wltData.second.walletId == walletId_BCDE_) {
            accountId_BCDE_ = *wltData.second.accountIds.begin();
         } else if (wltData.second.walletId == walletId_BC_) {
            accountId_BC_ = *wltData.second.accountIds.begin();
         } else if (wltData.second.walletId == walletId_DE_) {
            accountId_DE_ = *wltData.second.accountIds.begin();
         } else if (wltData.second.walletId == walletId_AFLB_) {
            accountId_AFLB_ = *wltData.second.accountIds.begin();
         }
      }

      auto wallets = ::loadWallets(bridge_);
      auto wltIdsCopy = walletIds;
      for (const auto& wltPair : wallets) {
         auto iter = wltIdsCopy.find(wltPair.second.walletId);
         if (iter != wltIdsCopy.end()) {
            wltIdsCopy.erase(iter);
         }
      }
      ASSERT_TRUE(wltIdsCopy.empty());

      ASSERT_FALSE(accountId_BCDE_.empty());
      ASSERT_FALSE(accountId_BC_.empty());
      ASSERT_FALSE(accountId_DE_.empty());
      ASSERT_FALSE(accountId_AFLB_.empty());
   }

   std::map<std::string, std::string> loadWallet(const std::string& walletId)
   {
      auto wltList = listWallets(bridge_);
      for (auto& wltData : wltList) {
         if (walletId == wltData.second.walletId) {
            stageWallet(bridge_, wltData.second.walletId, true);
         }
      }

      std::map<std::string, std::string> result;
      auto wallets = ::loadWallets(bridge_);
      for (const auto& wltData : wallets) {
         if (wltData.second.walletId == walletId) {
            result.emplace(wltData.second.accountId, wltData.second.dbId);
         }
      }
      return result;
   }

   ////////
   virtual void SetUp()
   {
      FileUtils::removeDirectory(blkdir_);
      FileUtils::removeDirectory(homedir_);
      FileUtils::removeDirectory(ldbdir_);

      FileUtils::createDirectory(blkdir_ / "blocks");
      FileUtils::createDirectory(homedir_);
      FileUtils::createDirectory(ldbdir_);

      Config::DBSettings::setServiceType(SERVICE_UNITTEST_WITHWS);

      // Put the first 5 blocks into the blkdir
      blk0dat_ = FileUtils::getBlkFilename(blkdir_ / "blocks", 0);
      TestUtils::setBlocks({ "0", "1", "2", "3" }, blk0dat_);

      WebSocketServer::init();
      Config::parseArgs({
         "--datadir=./fakehomedir",
         "--dbdir=./ldbtestdir",
         "--satoshi-datadir=./blkfiletest",
         "--db-type=DB_BARE",
         "--thread-count=3",
         "--public"},
         Config::ProcessType::DB);
      prepareWallets();

      //setup auth peers for server and client
      authPeersPassLbd_ = [](const std::set<Wallets::EncryptionKeyId>&)
      ->Passphrase::Result
      {
         return { {}, true };
      };

      auto createWltLbd = []()->std::unique_ptr<Passphrase::Params>
      {
         return std::make_unique<Passphrase::Params>(
            1ms, 0, SecureBinaryData{});
      };

      Wallets::AuthorizedPeers::createWallet({
         homedir_ / SERVER_AUTH_PEER_FILENAME, {createWltLbd}});
      Wallets::AuthorizedPeers serverPeers(
         {homedir_ / SERVER_AUTH_PEER_FILENAME, authPeersPassLbd_});
      Wallets::PeerKey servPK{serverPeers.getOwnPublicKey(), true, true};
      serverPubkey_ = servPK.toHumanReadable();

      replyQueue.clear();
      bridge_ = std::make_shared<Bridge::CppBridge>();
      bridge_->setWriteLambda([](MsgPtr payload) {
         std::unique_lock<std::mutex> lock(commsMutex);
         replyQueue.emplace_back(std::move(payload));
         commsCV.notify_all();
      });

      initBDM();
      nodePtr_ = std::dynamic_pointer_cast<NodeUnitTest>(
         Config::NetworkSettings::bitcoinNodes().first);
      nodePtr_->setBDM(theBDMt_->bdm());
      hexMagicBytes = Config::BitcoinSettings::getMagicBytes().toHexStr();
   }

   ////////
   virtual void TearDown()
   {
      bridge_.reset();
      WebSocketServer::shutdown();
      WebSocketServer::waitOnShutdown();
      theBDMt_->shutdown();

      delete theBDMt_;
      theBDMt_ = nullptr;

      FileUtils::removeDirectory(blkdir_);
      FileUtils::removeDirectory(homedir_);
      FileUtils::removeDirectory(ldbdir_);
      Config::reset();
   }

protected:
   BlockDataManagerThread *theBDMt_;
   Passphrase::UnlockFunc authPeersPassLbd_;
   LMDBBlockDatabase* iface_;

   std::filesystem::path blkdir_{"./blkfiletest"sv};
   std::filesystem::path homedir_{"./fakehomedir"sv};
   std::filesystem::path ldbdir_{"./ldbtestdir"sv};
   std::filesystem::path blk0dat_;

   std::string serverPubkey_;
   std::string hexMagicBytes;
   std::shared_ptr<Bridge::CppBridge> bridge_;
   std::shared_ptr<NodeUnitTest> nodePtr_;

   std::string walletId_BCDE_;
   std::string walletId_BC_;
   std::string walletId_DE_;
   std::string walletId_AFLB_;

   std::string accountId_BCDE_;
   std::string accountId_BC_;
   std::string accountId_DE_;
   std::string accountId_AFLB_;
};

////////////////////////////////////////////////////////////////////////////////
// ledgers
TEST_F(BridgeChainDataTests, Check5Blocks_BCDE)
{
   loadWallets({walletId_BCDE_});

   TestUtils::setBlocks({ "0", "1", "2", "3", "4", "5" }, blk0dat_);
   WebSocketServer::initAuthPeers({
      homedir_ / SERVER_AUTH_PEER_FILENAME, authPeersPassLbd_});
   WebSocketServer::start(theBDMt_->bdm(), true);

   ASSERT_TRUE(connectToIp(bridge_, "127.0.0.1", "9001", serverPubkey_));
   ASSERT_TRUE(registerWallets(bridge_));

   //start db, go online and wait on ready notif
   theBDMt_->start(Config::DBSettings::initMode());
   ASSERT_EQ(goOnline(bridge_), 5);

   //check wallet balance
   auto wltBal = getWalletBalance(bridge_, walletId_BCDE_, accountId_BCDE_);
   ASSERT_EQ(wltBal.size(), 4);
   EXPECT_EQ(wltBal[0], TestChain::wltBal_BCDE[5][0]);
   EXPECT_EQ(wltBal[1], TestChain::wltBal_BCDE[5][1]);
   EXPECT_EQ(wltBal[2], TestChain::wltBal_BCDE[5][2]);
   EXPECT_EQ(wltBal[3], TestChain::wltBal_BCDE[5][3]);

   //check address balances
   auto balances = getAddrBalances(bridge_, walletId_BCDE_, accountId_BCDE_);
   ASSERT_EQ(balances.size(), 4);
   checkBalances(balances, 5, false);

   //check ledgers
   auto delegateId = getLedgerDelegateId(bridge_);
   ASSERT_FALSE(delegateId.empty());

   auto pageCount = getLedgersPageCount(bridge_, delegateId);
   EXPECT_EQ(pageCount, 1);

   auto ledgers = getLedgersPage(bridge_, delegateId, 0);
   ASSERT_EQ(ledgers.size(), 15);

   for (unsigned i = 0; i < ledgers.size(); i++) {
      EXPECT_TRUE(checkLedgers(ledgers[i], TestChain::ledgersBCDE[i])) << i;
   }
}

TEST_F(BridgeChainDataTests, ChangeFilters_ALFB_BCDE)
{
   loadWallets({walletId_BCDE_, walletId_AFLB_});

   TestUtils::setBlocks({ "0", "1", "2", "3", "4", "5" }, blk0dat_);
   WebSocketServer::initAuthPeers({
      homedir_ / SERVER_AUTH_PEER_FILENAME, authPeersPassLbd_});
   WebSocketServer::start(theBDMt_->bdm(), true);

   ASSERT_TRUE(connectToIp(bridge_, "127.0.0.1", "9001", serverPubkey_));
   ASSERT_TRUE(registerWallets(bridge_));

   //start db, go online and wait on ready notif
   theBDMt_->start(Config::DBSettings::initMode());
   ASSERT_EQ(goOnline(bridge_), 5);

   //check main ledger
   auto delegateId = getLedgerDelegateId(bridge_);
   ASSERT_FALSE(delegateId.empty());

   auto pageCount = getLedgersPageCount(bridge_, delegateId);
   EXPECT_EQ(pageCount, 1);

   auto ledgers = getLedgersPage(bridge_, delegateId, 0);
   ASSERT_EQ(ledgers.size(), 26);

   std::vector<TestChain::LedgerEntryValue> combinedLedgers;
   combinedLedgers.insert(combinedLedgers.end(),
      TestChain::ledgersBCDE.begin(), TestChain::ledgersBCDE.end());
   combinedLedgers.insert(combinedLedgers.end(),
      TestChain::ledgersAFLB.begin(), TestChain::ledgersAFLB.end());
   ASSERT_EQ(combinedLedgers.size(), 26);
   std::sort(combinedLedgers.begin(), combinedLedgers.end());

   for (unsigned i = 0; i < ledgers.size(); i++) {
      EXPECT_TRUE(
         checkLedgers(ledgers[i],
            combinedLedgers[i])) << i;
   }

   //remove AFLB from filter
   uint64_t refId = rand();

   capnp::MallocMessageBuilder message;
   auto toBridge = message.initRoot<Codec::Bridge::ToBridge>();
   toBridge.setReferenceId(refId);
   auto request = toBridge.initWalletManager();
   auto filterReq = request.initUpdateMainLedgerFilter(1);
   auto wltId0 = filterReq[0];
   wltId0.setWalletId(walletId_BCDE_);
   wltId0.setAccountId(accountId_BCDE_);

   auto rawReq = serializeCapnp(message);
   pushRequest(bridge_, rawReq);

   auto result = waitOnReply();
   kj::ArrayPtr<const capnp::word> words(
      reinterpret_cast<const capnp::word*>(result->data.getPtr()),
      result->data.getSize() / sizeof(capnp::word));
   capnp::FlatArrayMessageReader reader(words);
   auto fromBridge = reader.getRoot<Codec::Bridge::FromBridge>();
   ASSERT_EQ(fromBridge.which(), Codec::Bridge::FromBridge::NOTIFICATION);
   auto notif = fromBridge.getNotification();
   ASSERT_EQ(notif.which(), Codec::Bridge::Notification::REFRESH);
   auto notifIds = notif.getRefresh();
   ASSERT_EQ(notifIds.size(), 1);
   ASSERT_EQ(std::string(notifIds[0]), std::string{"wallet_filter_changed"});

   //check main ledger again
   ledgers = getLedgersPage(bridge_, delegateId, 0);
   ASSERT_EQ(ledgers.size(), 15);

   for (unsigned i = 0; i < ledgers.size(); i++) {
      EXPECT_TRUE(checkLedgers(ledgers[i], TestChain::ledgersBCDE[i])) << i;
   }
}

TEST_F(BridgeChainDataTests, BlocksOutOfOrder_BCDE)
{
   /*
   This implements the same test conditions as
   BlockDir.HeadersFirstForwardUpdate
   */

   loadWallets({walletId_BCDE_});

   TestUtils::setBlocks({ "0", "1", "2", "4", "5", "4A" }, blk0dat_);
   WebSocketServer::initAuthPeers({
      homedir_ / SERVER_AUTH_PEER_FILENAME, authPeersPassLbd_});
   WebSocketServer::start(theBDMt_->bdm(), true);

   ASSERT_TRUE(connectToIp(bridge_, "127.0.0.1", "9001", serverPubkey_));
   ASSERT_TRUE(registerWallets(bridge_));

   //start db, go online and wait on ready notif
   theBDMt_->start(Config::DBSettings::initMode());
   ASSERT_EQ(goOnline(bridge_), 2);

   //check wallet balance
   auto wltBal = getWalletBalance(bridge_, walletId_BCDE_, accountId_BCDE_);
   ASSERT_EQ(wltBal.size(), 4);
   EXPECT_EQ(wltBal[0], TestChain::wltBal_BCDE[2][0]);
   EXPECT_EQ(wltBal[1], TestChain::wltBal_BCDE[2][1]);
   EXPECT_EQ(wltBal[2], TestChain::wltBal_BCDE[2][2]);
   EXPECT_EQ(wltBal[3], TestChain::wltBal_BCDE[2][3]);

   //check address balances
   auto balances = getAddrBalances(bridge_, walletId_BCDE_, accountId_BCDE_);
   ASSERT_EQ(balances.size(), 1);
   checkBalances(balances, 2, false);

   //check ledgers
   auto delegateId = getLedgerDelegateId(bridge_);
   ASSERT_FALSE(delegateId.empty());

   auto pageCount = getLedgersPageCount(bridge_, delegateId);
   EXPECT_EQ(pageCount, 1);

   auto ledgers = getLedgersPage(bridge_, delegateId, 0);
   ASSERT_EQ(ledgers.size(), 4);

   for (unsigned i = 0; i < ledgers.size(); i++) {
      EXPECT_TRUE(checkLedgers(ledgers[i], TestChain::ledgersBCDE[i + 11])) << i;
   }

   /* add block 3 */
   TestUtils::appendBlocks({ "3" }, blk0dat_);
   nodePtr_->notifyNewBlock();
   ASSERT_EQ(waitOnNewBlock(), 5);

   //check wallet balance
   wltBal = getWalletBalance(bridge_, walletId_BCDE_, accountId_BCDE_);
   ASSERT_EQ(wltBal.size(), 4);
   EXPECT_EQ(wltBal[0], TestChain::wltBal_BCDE[5][0]);
   EXPECT_EQ(wltBal[1], TestChain::wltBal_BCDE[5][1]);
   EXPECT_EQ(wltBal[2], TestChain::wltBal_BCDE[5][2]);
   EXPECT_EQ(wltBal[3], TestChain::wltBal_BCDE[5][3]);

   //check address balances
   balances = getAddrBalances(bridge_, walletId_BCDE_, accountId_BCDE_);
   ASSERT_EQ(balances.size(), 4);
   checkBalances(balances, 5, false);

   //check ledgers
   ledgers = getLedgersPage(bridge_, delegateId, 0);
   ASSERT_EQ(ledgers.size(), 15);

   for (unsigned i = 0; i < ledgers.size(); i++) {
      EXPECT_TRUE(checkLedgers(ledgers[i], TestChain::ledgersBCDE[i])) << i;
   }
}

////////////////////////////////////////////////////////////////////////////////
// ledgers add block
TEST_F(BridgeChainDataTests, AddBlocks_BCDE)
{
   loadWallets({walletId_BCDE_});

   WebSocketServer::initAuthPeers({
      homedir_ / SERVER_AUTH_PEER_FILENAME, authPeersPassLbd_});
   WebSocketServer::start(theBDMt_->bdm(), true);

   //connect to db
   ASSERT_TRUE(connectToIp(bridge_, "127.0.0.1", "9001", serverPubkey_));
   ASSERT_TRUE(registerWallets(bridge_));

   //start db, go online and wait on ready notif
   theBDMt_->start(Config::DBSettings::initMode());
   ASSERT_EQ(goOnline(bridge_), 3);

   /* block 3 */

   //wallet balance
   auto wltBal = getWalletBalance(bridge_, walletId_BCDE_, accountId_BCDE_);
   ASSERT_EQ(wltBal.size(), 4);
   EXPECT_EQ(wltBal[0], TestChain::wltBal_BCDE[3][0]);
   EXPECT_EQ(wltBal[1], TestChain::wltBal_BCDE[3][1]);
   EXPECT_EQ(wltBal[2], TestChain::wltBal_BCDE[3][2]);
   EXPECT_EQ(wltBal[3], TestChain::wltBal_BCDE[3][3]);

   //addr balances
   auto balances = getAddrBalances(bridge_, walletId_BCDE_, accountId_BCDE_);
   ASSERT_EQ(balances.size(), 4);
   checkBalances(balances, 3, false);

   //ledgers
   auto delegateId = getLedgerDelegateId(bridge_);
   ASSERT_FALSE(delegateId.empty());

   auto pageCount = getLedgersPageCount(bridge_, delegateId);
   EXPECT_EQ(pageCount, 1);

   auto ledgersAt3Blocks = getLedgersPage(bridge_, delegateId, 0);
   ASSERT_EQ(ledgersAt3Blocks.size(), 9);

   for (unsigned i = 0; i < ledgersAt3Blocks.size(); i++) {
      EXPECT_TRUE(
         checkLedgers(ledgersAt3Blocks[i],
         TestChain::ledgersBCDE[i + 6]));
   }

   /* block 4 */
   TestUtils::setBlocks({ "0", "1", "2", "3", "4" }, blk0dat_);
   nodePtr_->notifyNewBlock();
   ASSERT_EQ(waitOnNewBlock(), 4);

   //wallet balance
   wltBal = getWalletBalance(bridge_, walletId_BCDE_, accountId_BCDE_);
   ASSERT_EQ(wltBal.size(), 4);
   EXPECT_EQ(wltBal[0], TestChain::wltBal_BCDE[4][0]);
   EXPECT_EQ(wltBal[1], TestChain::wltBal_BCDE[4][1]);
   EXPECT_EQ(wltBal[2], TestChain::wltBal_BCDE[4][2]);
   EXPECT_EQ(wltBal[3], TestChain::wltBal_BCDE[4][3]);

   //addr balances
   balances = getAddrBalances(bridge_, walletId_BCDE_, accountId_BCDE_);
   ASSERT_EQ(balances.size(), 4);
   checkBalances(balances, 4, false);

   //ledgers
   pageCount = getLedgersPageCount(bridge_, delegateId);
   EXPECT_EQ(pageCount, 1);

   auto ledgersAt4Blocks = getLedgersPage(bridge_, delegateId, 0);
   ASSERT_EQ(ledgersAt4Blocks.size(), 12);

   for (unsigned i = 0; i < ledgersAt4Blocks.size(); i++) {
      EXPECT_TRUE(
         checkLedgers(ledgersAt4Blocks[i],
         TestChain::ledgersBCDE[i + 3]));
   }

   /* block 5 */
   TestUtils::setBlocks({ "0", "1", "2", "3", "4", "5" }, blk0dat_);
   nodePtr_->notifyNewBlock();
   ASSERT_EQ(waitOnNewBlock(), 5);

   //wallet balance
   wltBal = getWalletBalance(bridge_, walletId_BCDE_, accountId_BCDE_);
   ASSERT_EQ(wltBal.size(), 4);
   EXPECT_EQ(wltBal[0], TestChain::wltBal_BCDE[5][0]);
   EXPECT_EQ(wltBal[1], TestChain::wltBal_BCDE[5][1]);
   EXPECT_EQ(wltBal[2], TestChain::wltBal_BCDE[5][2]);
   EXPECT_EQ(wltBal[3], TestChain::wltBal_BCDE[5][3]);

   //addr balances
   balances = getAddrBalances(bridge_, walletId_BCDE_, accountId_BCDE_);
   ASSERT_EQ(balances.size(), 4);
   checkBalances(balances, 5, false);

   //ledgers
   pageCount = getLedgersPageCount(bridge_, delegateId);
   EXPECT_EQ(pageCount, 1);

   auto ledgersAt5Blocks = getLedgersPage(bridge_, delegateId, 0);
   ASSERT_EQ(ledgersAt5Blocks.size(), 15);

   for (unsigned i = 0; i < ledgersAt5Blocks.size(); i++) {
      EXPECT_TRUE(
         checkLedgers(ledgersAt5Blocks[i],
         TestChain::ledgersBCDE[i])) << i;
   }
}

TEST_F(BridgeChainDataTests, AddBlocks_BC_DE)
{
   // load the 2 wallets
   loadWallets({walletId_BC_, walletId_DE_});

   //connect to db
   WebSocketServer::initAuthPeers({
      homedir_ / SERVER_AUTH_PEER_FILENAME, authPeersPassLbd_});
   WebSocketServer::start(theBDMt_->bdm(), true);

   ASSERT_TRUE(connectToIp(bridge_, "127.0.0.1", "9001", serverPubkey_));
   ASSERT_TRUE(registerWallets(bridge_));

   //start db, go online and wait on ready notif
   theBDMt_->start(Config::DBSettings::initMode());
   ASSERT_EQ(goOnline(bridge_), 3);

   /* block 3 */

   //balances
   auto balances1 = getAddrBalances(bridge_, walletId_BC_, accountId_BC_);
   ASSERT_EQ(balances1.size(), 2);
   checkBalances(balances1, 3, false);
   auto balances2 = getAddrBalances(bridge_, walletId_DE_, accountId_DE_);
   ASSERT_EQ(balances2.size(), 2);
   checkBalances(balances2, 3, false);

   //ledgers
   auto delegateId = getLedgerDelegateId(bridge_);
   auto delegateIdWlt1 = getLedgerDelegateIdForWallet(
      bridge_, walletId_BC_, accountId_BC_);
   auto delegateIdWlt2 = getLedgerDelegateIdForWallet(
      bridge_, walletId_DE_, accountId_DE_);
   ASSERT_FALSE(delegateIdWlt1.empty());
   ASSERT_FALSE(delegateIdWlt2.empty());

   auto pageCount = getLedgersPageCount(bridge_, delegateId);
   EXPECT_EQ(pageCount, 1);
   pageCount = getLedgersPageCount(bridge_, delegateIdWlt1);
   EXPECT_EQ(pageCount, 1);
   pageCount = getLedgersPageCount(bridge_, delegateIdWlt2);
   EXPECT_EQ(pageCount, 1);

   //wlt1 ledgers
   auto ledgersAtBlocks = getLedgersPage(bridge_, delegateIdWlt1, 0);
   ASSERT_EQ(ledgersAtBlocks.size(), 7);

   for (unsigned i = 0; i < ledgersAtBlocks.size(); i++) {
      EXPECT_TRUE(
         checkLedgers(ledgersAtBlocks[i],
            TestChain::ledgersBC[i + 3]));
   }

   //wlt2 ledgers
   ledgersAtBlocks = getLedgersPage(bridge_, delegateIdWlt2, 0);
   ASSERT_EQ(ledgersAtBlocks.size(), 3);

   for (unsigned i = 0; i < ledgersAtBlocks.size(); i++) {
      EXPECT_TRUE(
         checkLedgers(ledgersAtBlocks[i],
            TestChain::ledgersDE[i + 3]));
   }

   //combined ledgers
   ledgersAtBlocks = getLedgersPage(bridge_, delegateId, 0);
   ASSERT_EQ(ledgersAtBlocks.size(), 10);

   std::vector<TestChain::LedgerEntryValue> combinedLedgers;
   combinedLedgers.insert(combinedLedgers.end(),
      TestChain::ledgersBC.begin() + 3, TestChain::ledgersBC.end());
   combinedLedgers.insert(combinedLedgers.end(),
      TestChain::ledgersDE.begin() + 3, TestChain::ledgersDE.end());
   ASSERT_EQ(combinedLedgers.size(), 10);
   std::sort(combinedLedgers.begin(), combinedLedgers.end());

   for (unsigned i = 0; i < ledgersAtBlocks.size(); i++) {
      EXPECT_TRUE(
         checkLedgers(ledgersAtBlocks[i],
         combinedLedgers[i])) << i;
   }

   /* block 4 */
   TestUtils::setBlocks({ "0", "1", "2", "3", "4" }, blk0dat_);
   nodePtr_->notifyNewBlock();
   ASSERT_EQ(waitOnNewBlock(), 4);

   //balances
   balances1 = getAddrBalances(bridge_, walletId_BC_, accountId_BC_);
   ASSERT_EQ(balances1.size(), 2);
   checkBalances(balances1, 4, false);
   balances2 = getAddrBalances(bridge_, walletId_DE_, accountId_DE_);
   ASSERT_EQ(balances2.size(), 2);
   checkBalances(balances2, 4, false);

   //wlt1 ledgers
   ledgersAtBlocks = getLedgersPage(bridge_, delegateIdWlt1, 0);
   ASSERT_EQ(ledgersAtBlocks.size(), 8);

   for (unsigned i = 0; i < ledgersAtBlocks.size(); i++) {
      EXPECT_TRUE(
         checkLedgers(ledgersAtBlocks[i],
            TestChain::ledgersBC[i + 2]));
   }

   //wlt2 ledgers
   ledgersAtBlocks = getLedgersPage(bridge_, delegateIdWlt2, 0);
   ASSERT_EQ(ledgersAtBlocks.size(), 5);

   for (unsigned i = 0; i < ledgersAtBlocks.size(); i++) {
      EXPECT_TRUE(
         checkLedgers(ledgersAtBlocks[i],
            TestChain::ledgersDE[i + 1]));
   }

   //combined ledgers
   ledgersAtBlocks = getLedgersPage(bridge_, delegateId, 0);
   ASSERT_EQ(ledgersAtBlocks.size(), 13);

   combinedLedgers.clear();
   combinedLedgers.insert(combinedLedgers.end(),
      TestChain::ledgersBC.begin() + 2, TestChain::ledgersBC.end());
   combinedLedgers.insert(combinedLedgers.end(),
      TestChain::ledgersDE.begin() + 1, TestChain::ledgersDE.end());
   ASSERT_EQ(combinedLedgers.size(), 13);
   std::sort(combinedLedgers.begin(), combinedLedgers.end());

   for (unsigned i = 0; i < ledgersAtBlocks.size(); i++) {
      EXPECT_TRUE(
         checkLedgers(ledgersAtBlocks[i],
            combinedLedgers[i])) << i;
   }

   /* block 5 */
   TestUtils::setBlocks({ "0", "1", "2", "3", "4", "5" }, blk0dat_);
   nodePtr_->notifyNewBlock();
   ASSERT_EQ(waitOnNewBlock(), 5);

   //balances
   balances1 = getAddrBalances(bridge_, walletId_BC_, accountId_BC_);
   ASSERT_EQ(balances1.size(), 2);
   checkBalances(balances1, 5, false);
   balances2 = getAddrBalances(bridge_, walletId_DE_, accountId_DE_);
   ASSERT_EQ(balances2.size(), 2);
   checkBalances(balances2, 5, false);

   //wlt1 ledgers
   ledgersAtBlocks = getLedgersPage(bridge_, delegateIdWlt1, 0);
   ASSERT_EQ(ledgersAtBlocks.size(), 10);

   for (unsigned i = 0; i < ledgersAtBlocks.size(); i++) {
      EXPECT_TRUE(
         checkLedgers(ledgersAtBlocks[i],
            TestChain::ledgersBC[i]));
   }

   //wlt2 ledgers
   ledgersAtBlocks = getLedgersPage(bridge_, delegateIdWlt2, 0);
   ASSERT_EQ(ledgersAtBlocks.size(), 6);

   for (unsigned i = 0; i < ledgersAtBlocks.size(); i++) {
      EXPECT_TRUE(
         checkLedgers(ledgersAtBlocks[i],
            TestChain::ledgersDE[i]));
   }

   //combined ledgers
   ledgersAtBlocks = getLedgersPage(bridge_, delegateId, 0);
   ASSERT_EQ(ledgersAtBlocks.size(), 16);

   combinedLedgers.clear();
   combinedLedgers.insert(combinedLedgers.end(),
      TestChain::ledgersBC.begin(), TestChain::ledgersBC.end());
   combinedLedgers.insert(combinedLedgers.end(),
      TestChain::ledgersDE.begin(), TestChain::ledgersDE.end());
   ASSERT_EQ(combinedLedgers.size(), 16);
   std::sort(combinedLedgers.begin(), combinedLedgers.end());

   for (unsigned i = 0; i < ledgersAtBlocks.size(); i++) {
      EXPECT_TRUE(
         checkLedgers(ledgersAtBlocks[i],
            combinedLedgers[i])) << i;
   }
}

TEST_F(BridgeChainDataTests, AddBlocks_BCDE_AFLB)
{
   // load the 2 wallets
   loadWallets({walletId_BCDE_, walletId_AFLB_});

   //connect to db
   WebSocketServer::initAuthPeers({
      homedir_ / SERVER_AUTH_PEER_FILENAME, authPeersPassLbd_});
   WebSocketServer::start(theBDMt_->bdm(), true);

   ASSERT_TRUE(connectToIp(bridge_, "127.0.0.1", "9001", serverPubkey_));
   ASSERT_TRUE(registerWallets(bridge_));

   //start db, go online and wait on ready notif
   theBDMt_->start(Config::DBSettings::initMode());
   ASSERT_EQ(goOnline(bridge_), 3);

   /* block 3 */

   //wlt balance
   auto wltBal = getWalletBalance(bridge_, walletId_BCDE_, accountId_BCDE_);
   ASSERT_EQ(wltBal.size(), 4);
   EXPECT_EQ(wltBal[0], TestChain::wltBal_BCDE[3][0]);
   EXPECT_EQ(wltBal[1], TestChain::wltBal_BCDE[3][1]);
   EXPECT_EQ(wltBal[2], TestChain::wltBal_BCDE[3][2]);
   EXPECT_EQ(wltBal[3], TestChain::wltBal_BCDE[3][3]);

   wltBal = getWalletBalance(bridge_, walletId_AFLB_, accountId_AFLB_);
   ASSERT_EQ(wltBal.size(), 4);
   EXPECT_EQ(wltBal[0], TestChain::wltBal_AFLB[3][0]);
   EXPECT_EQ(wltBal[1], TestChain::wltBal_AFLB[3][1]);
   EXPECT_EQ(wltBal[2], TestChain::wltBal_AFLB[3][2]);
   EXPECT_EQ(wltBal[3], TestChain::wltBal_AFLB[3][3]);

   //address balances
   auto balances1 = getAddrBalances(bridge_, walletId_BCDE_, accountId_BCDE_);
   ASSERT_EQ(balances1.size(), 4);
   checkBalances(balances1, 3, false);
   auto balances2 = getAddrBalances(bridge_, walletId_AFLB_, accountId_AFLB_);
   ASSERT_EQ(balances2.size(), 6);
   checkBalances(balances2, 3, false);

   //setup wallet ledger delegates
   auto delegateId = getLedgerDelegateId(bridge_);
   auto delegateIdWlt1 = getLedgerDelegateIdForWallet(
      bridge_, walletId_BCDE_, accountId_BCDE_);
   auto delegateIdWlt2 = getLedgerDelegateIdForWallet(
      bridge_, walletId_AFLB_, accountId_AFLB_);
   ASSERT_FALSE(delegateIdWlt1.empty());
   ASSERT_FALSE(delegateIdWlt2.empty());

   auto pageCount = getLedgersPageCount(bridge_, delegateId);
   EXPECT_EQ(pageCount, 1);
   pageCount = getLedgersPageCount(bridge_, delegateIdWlt1);
   EXPECT_EQ(pageCount, 1);
   pageCount = getLedgersPageCount(bridge_, delegateIdWlt2);
   EXPECT_EQ(pageCount, 1);

   //setup address ledger delegates
   auto delegateScrAddrA = getLedgerDelegateIdForScrAddr(bridge_,
      walletId_AFLB_, accountId_AFLB_, TestChain::scrAddrA);
   ASSERT_FALSE(delegateScrAddrA.empty());
   auto delegateScrAddrB = getLedgerDelegateIdForScrAddr(bridge_,
      walletId_BCDE_, accountId_BCDE_, TestChain::scrAddrB);
   ASSERT_FALSE(delegateScrAddrB.empty());
   auto delegateScrAddrC = getLedgerDelegateIdForScrAddr(bridge_,
      walletId_BCDE_, accountId_BCDE_, TestChain::scrAddrC);
   ASSERT_FALSE(delegateScrAddrC.empty());
   auto delegateScrAddrD = getLedgerDelegateIdForScrAddr(bridge_,
      walletId_BCDE_, accountId_BCDE_, TestChain::scrAddrD);
   ASSERT_FALSE(delegateScrAddrD.empty());
   auto delegateScrAddrE = getLedgerDelegateIdForScrAddr(bridge_,
      walletId_BCDE_, accountId_BCDE_, TestChain::scrAddrE);
   ASSERT_FALSE(delegateScrAddrE.empty());
   auto delegateScrAddrF = getLedgerDelegateIdForScrAddr(bridge_,
      walletId_AFLB_, accountId_AFLB_, TestChain::scrAddrF);
   ASSERT_FALSE(delegateScrAddrF.empty());
   auto delegateScrAddrLB1 = getLedgerDelegateIdForScrAddr(bridge_,
      walletId_AFLB_, accountId_AFLB_, TestChain::lb1ScrAddr);
   ASSERT_FALSE(delegateScrAddrLB1.empty());
   auto delegateP2SHLB1 = getLedgerDelegateIdForScrAddr(bridge_,
      walletId_AFLB_, accountId_AFLB_, TestChain::lb1ScrAddrP2SH);
   ASSERT_FALSE(delegateP2SHLB1.empty());
   auto delegateScrAddrLB2 = getLedgerDelegateIdForScrAddr(bridge_,
      walletId_AFLB_, accountId_AFLB_, TestChain::lb2ScrAddr);
   ASSERT_FALSE(delegateScrAddrLB2.empty());
   auto delegateP2SHLB2 = getLedgerDelegateIdForScrAddr(bridge_,
      walletId_AFLB_, accountId_AFLB_, TestChain::lb2ScrAddrP2SH);
   ASSERT_FALSE(delegateP2SHLB2.empty());

   pageCount = getLedgersPageCount(bridge_, delegateScrAddrA);
   EXPECT_EQ(pageCount, 1);
   pageCount = getLedgersPageCount(bridge_, delegateScrAddrB);
   EXPECT_EQ(pageCount, 1);
   pageCount = getLedgersPageCount(bridge_, delegateScrAddrC);
   EXPECT_EQ(pageCount, 1);
   pageCount = getLedgersPageCount(bridge_, delegateScrAddrD);
   EXPECT_EQ(pageCount, 1);
   pageCount = getLedgersPageCount(bridge_, delegateScrAddrE);
   EXPECT_EQ(pageCount, 1);
   pageCount = getLedgersPageCount(bridge_, delegateScrAddrF);
   EXPECT_EQ(pageCount, 1);
   pageCount = getLedgersPageCount(bridge_, delegateScrAddrLB1);
   EXPECT_EQ(pageCount, 1);
   pageCount = getLedgersPageCount(bridge_, delegateP2SHLB1);
   EXPECT_EQ(pageCount, 1);
   pageCount = getLedgersPageCount(bridge_, delegateScrAddrLB2);
   EXPECT_EQ(pageCount, 1);
   pageCount = getLedgersPageCount(bridge_, delegateP2SHLB2);
   EXPECT_EQ(pageCount, 1);

   //wlt1 ledgers
   auto ledgersAtBlocks = getLedgersPage(bridge_, delegateIdWlt1, 0);
   ASSERT_EQ(ledgersAtBlocks.size(), 9);

   for (unsigned i = 0; i < ledgersAtBlocks.size(); i++) {
      EXPECT_TRUE(
         checkLedgers(ledgersAtBlocks[i],
            TestChain::ledgersBCDE[i + 6]));
   }

   //wlt2 ledgers
   ledgersAtBlocks = getLedgersPage(bridge_, delegateIdWlt2, 0);
   ASSERT_EQ(ledgersAtBlocks.size(), 7);

   for (unsigned i = 0; i < ledgersAtBlocks.size(); i++) {
      EXPECT_TRUE(
         checkLedgers(ledgersAtBlocks[i],
            TestChain::ledgersAFLB[i + 4]));
   }

   //combined ledgers
   ledgersAtBlocks = getLedgersPage(bridge_, delegateId, 0);
   ASSERT_EQ(ledgersAtBlocks.size(), 16);

   std::vector<TestChain::LedgerEntryValue> combinedLedgers;
   combinedLedgers.insert(combinedLedgers.end(),
      TestChain::ledgersBCDE.begin() + 6, TestChain::ledgersBCDE.end());
   combinedLedgers.insert(combinedLedgers.end(),
      TestChain::ledgersAFLB.begin() + 4, TestChain::ledgersAFLB.end());
   ASSERT_EQ(combinedLedgers.size(), 16);
   std::sort(combinedLedgers.begin(), combinedLedgers.end());

   for (unsigned i = 0; i < ledgersAtBlocks.size(); i++) {
      EXPECT_TRUE(
         checkLedgers(ledgersAtBlocks[i],
         combinedLedgers[i])) << i;
   }

   /* address ledgers */

   //A
   ledgersAtBlocks = getLedgersPage(bridge_, delegateScrAddrA, 0);
   ASSERT_EQ(ledgersAtBlocks.size(), 1);
   for (unsigned i = 0; i < ledgersAtBlocks.size(); i++) {
      EXPECT_TRUE(
         checkLedgers(ledgersAtBlocks[i],
            TestChain::ledgersA[i]));
   }

   //B
   ledgersAtBlocks = getLedgersPage(bridge_, delegateScrAddrB, 0);
   ASSERT_EQ(ledgersAtBlocks.size(), 5);
   for (unsigned i = 0; i < ledgersAtBlocks.size(); i++) {
      EXPECT_TRUE(
         checkLedgers(ledgersAtBlocks[i],
            TestChain::ledgersB[i + 2]));
   }

   //C
   ledgersAtBlocks = getLedgersPage(bridge_, delegateScrAddrC, 0);
   ASSERT_EQ(ledgersAtBlocks.size(), 2);
   for (unsigned i = 0; i < ledgersAtBlocks.size(); i++) {
      EXPECT_TRUE(
         checkLedgers(ledgersAtBlocks[i],
            TestChain::ledgersC[i + 2]));
   }

   //D
   ledgersAtBlocks = getLedgersPage(bridge_, delegateScrAddrD, 0);
   ASSERT_EQ(ledgersAtBlocks.size(), 1);
   for (unsigned i = 0; i < ledgersAtBlocks.size(); i++) {
      EXPECT_TRUE(
         checkLedgers(ledgersAtBlocks[i],
            TestChain::ledgersD[i + 3]));
   }

   //E
   ledgersAtBlocks = getLedgersPage(bridge_, delegateScrAddrE, 0);
   ASSERT_EQ(ledgersAtBlocks.size(), 2);
   for (unsigned i = 0; i < ledgersAtBlocks.size(); i++) {
      EXPECT_TRUE(
         checkLedgers(ledgersAtBlocks[i],
            TestChain::ledgersE[i]));
   }

   //F
   ledgersAtBlocks = getLedgersPage(bridge_, delegateScrAddrF, 0);
   ASSERT_EQ(ledgersAtBlocks.size(), 3);
   for (unsigned i = 0; i < ledgersAtBlocks.size(); i++) {
      EXPECT_TRUE(
         checkLedgers(ledgersAtBlocks[i],
            TestChain::ledgersF[i + 2]));
   }

   //LB1
   ledgersAtBlocks = getLedgersPage(bridge_, delegateScrAddrLB1, 0);
   ASSERT_EQ(ledgersAtBlocks.size(), 1);
   for (unsigned i = 0; i < ledgersAtBlocks.size(); i++) {
      EXPECT_TRUE(
         checkLedgers(ledgersAtBlocks[i],
            TestChain::ledgersLB1[i + 1]));
   }

   //LB1_P2SH
   ledgersAtBlocks = getLedgersPage(bridge_, delegateP2SHLB1, 0);
   ASSERT_EQ(ledgersAtBlocks.size(), 2);
   for (unsigned i = 0; i < ledgersAtBlocks.size(); i++) {
      EXPECT_TRUE(
         checkLedgers(ledgersAtBlocks[i],
            TestChain::ledgersLB1_P2SH[i + 1]));
   }

   //LB2
   ledgersAtBlocks = getLedgersPage(bridge_, delegateScrAddrLB2, 0);
   ASSERT_EQ(ledgersAtBlocks.size(), 3);
   for (unsigned i = 0; i < ledgersAtBlocks.size(); i++) {
      EXPECT_TRUE(
         checkLedgers(ledgersAtBlocks[i],
            TestChain::ledgersLB2[i + 1])) << i;
   }

   //LB2_P2SH
   ledgersAtBlocks = getLedgersPage(bridge_, delegateP2SHLB2, 0);
   ASSERT_EQ(ledgersAtBlocks.size(), 1);
   for (unsigned i = 0; i < ledgersAtBlocks.size(); i++) {
      EXPECT_TRUE(
         checkLedgers(ledgersAtBlocks[i],
            TestChain::ledgersLB2_P2SH[i + 1]));
   }

   /* block 4 */
   TestUtils::setBlocks({ "0", "1", "2", "3", "4" }, blk0dat_);
   nodePtr_->notifyNewBlock();
   ASSERT_EQ(waitOnNewBlock(), 4);

   //wlt balance
   wltBal = getWalletBalance(bridge_, walletId_BCDE_, accountId_BCDE_);
   ASSERT_EQ(wltBal.size(), 4);
   EXPECT_EQ(wltBal[0], TestChain::wltBal_BCDE[4][0]);
   EXPECT_EQ(wltBal[1], TestChain::wltBal_BCDE[4][1]);
   EXPECT_EQ(wltBal[2], TestChain::wltBal_BCDE[4][2]);
   EXPECT_EQ(wltBal[3], TestChain::wltBal_BCDE[4][3]);

   wltBal = getWalletBalance(bridge_, walletId_AFLB_, accountId_AFLB_);
   ASSERT_EQ(wltBal.size(), 4);
   EXPECT_EQ(wltBal[0], TestChain::wltBal_AFLB[4][0]);
   EXPECT_EQ(wltBal[1], TestChain::wltBal_AFLB[4][1]);
   EXPECT_EQ(wltBal[2], TestChain::wltBal_AFLB[4][2]);
   EXPECT_EQ(wltBal[3], TestChain::wltBal_AFLB[4][3]);

   //addr balances
   balances1 = getAddrBalances(bridge_, walletId_BCDE_, accountId_BCDE_);
   ASSERT_EQ(balances1.size(), 4);
   checkBalances(balances1, 4, false);
   balances2 = getAddrBalances(bridge_, walletId_AFLB_, accountId_AFLB_);
   ASSERT_EQ(balances2.size(), 6);
   checkBalances(balances2, 4, false);

   //wlt1 ledgers
   ledgersAtBlocks = getLedgersPage(bridge_, delegateIdWlt1, 0);
   ASSERT_EQ(ledgersAtBlocks.size(), 12);

   for (unsigned i = 0; i < ledgersAtBlocks.size(); i++) {
      EXPECT_TRUE(
         checkLedgers(ledgersAtBlocks[i],
            TestChain::ledgersBCDE[i + 3]));
   }

   //wlt2 ledgers
   ledgersAtBlocks = getLedgersPage(bridge_, delegateIdWlt2, 0);
   ASSERT_EQ(ledgersAtBlocks.size(), 10);

   for (unsigned i = 0; i < ledgersAtBlocks.size(); i++) {
      EXPECT_TRUE(
         checkLedgers(ledgersAtBlocks[i],
            TestChain::ledgersAFLB[i + 1]));
   }

   //combined ledgers
   ledgersAtBlocks = getLedgersPage(bridge_, delegateId, 0);
   ASSERT_EQ(ledgersAtBlocks.size(), 22);

   combinedLedgers.clear();
   combinedLedgers.insert(combinedLedgers.end(),
      TestChain::ledgersBCDE.begin() + 3, TestChain::ledgersBCDE.end());
   combinedLedgers.insert(combinedLedgers.end(),
      TestChain::ledgersAFLB.begin() + 1, TestChain::ledgersAFLB.end());
   ASSERT_EQ(combinedLedgers.size(), 22);
   std::sort(combinedLedgers.begin(), combinedLedgers.end());

   for (unsigned i = 0; i < ledgersAtBlocks.size(); i++) {
      EXPECT_TRUE(
         checkLedgers(ledgersAtBlocks[i],
            combinedLedgers[i])) << i;
   }

   /* address ledgers */

   //A
   ledgersAtBlocks = getLedgersPage(bridge_, delegateScrAddrA, 0);
   ASSERT_EQ(ledgersAtBlocks.size(), 1);
   for (unsigned i = 0; i < ledgersAtBlocks.size(); i++) {
      EXPECT_TRUE(
         checkLedgers(ledgersAtBlocks[i],
            TestChain::ledgersA[i]));
   }

   //B
   ledgersAtBlocks = getLedgersPage(bridge_, delegateScrAddrB, 0);
   ASSERT_EQ(ledgersAtBlocks.size(), 5);
   for (unsigned i = 0; i < ledgersAtBlocks.size(); i++) {
      EXPECT_TRUE(
         checkLedgers(ledgersAtBlocks[i],
            TestChain::ledgersB[i + 2]));
   }

   //C
   ledgersAtBlocks = getLedgersPage(bridge_, delegateScrAddrC, 0);
   ASSERT_EQ(ledgersAtBlocks.size(), 3);
   for (unsigned i = 0; i < ledgersAtBlocks.size(); i++) {
      EXPECT_TRUE(
         checkLedgers(ledgersAtBlocks[i],
            TestChain::ledgersC[i + 1]));
   }

   //D
   ledgersAtBlocks = getLedgersPage(bridge_, delegateScrAddrD, 0);
   ASSERT_EQ(ledgersAtBlocks.size(), 3);
   for (unsigned i = 0; i < ledgersAtBlocks.size(); i++) {
      EXPECT_TRUE(
         checkLedgers(ledgersAtBlocks[i],
            TestChain::ledgersD[i + 1]));
   }

   //E
   ledgersAtBlocks = getLedgersPage(bridge_, delegateScrAddrE, 0);
   ASSERT_EQ(ledgersAtBlocks.size(), 2);
   for (unsigned i = 0; i < ledgersAtBlocks.size(); i++) {
      EXPECT_TRUE(
         checkLedgers(ledgersAtBlocks[i],
            TestChain::ledgersE[i]));
   }

   //F
   ledgersAtBlocks = getLedgersPage(bridge_, delegateScrAddrF, 0);
   ASSERT_EQ(ledgersAtBlocks.size(), 4);
   for (unsigned i = 0; i < ledgersAtBlocks.size(); i++) {
      EXPECT_TRUE(
         checkLedgers(ledgersAtBlocks[i],
            TestChain::ledgersF[i + 1]));
   }

   //LB1
   ledgersAtBlocks = getLedgersPage(bridge_, delegateScrAddrLB1, 0);
   ASSERT_EQ(ledgersAtBlocks.size(), 2);
   for (unsigned i = 0; i < ledgersAtBlocks.size(); i++) {
      EXPECT_TRUE(
         checkLedgers(ledgersAtBlocks[i],
            TestChain::ledgersLB1[i]));
   }

   //LB1_P2SH
   ledgersAtBlocks = getLedgersPage(bridge_, delegateP2SHLB1, 0);
   ASSERT_EQ(ledgersAtBlocks.size(), 3);
   for (unsigned i = 0; i < ledgersAtBlocks.size(); i++) {
      EXPECT_TRUE(
         checkLedgers(ledgersAtBlocks[i],
            TestChain::ledgersLB1_P2SH[i]));
   }

   //LB2
   ledgersAtBlocks = getLedgersPage(bridge_, delegateScrAddrLB2, 0);
   ASSERT_EQ(ledgersAtBlocks.size(), 4);
   for (unsigned i = 0; i < ledgersAtBlocks.size(); i++) {
      EXPECT_TRUE(
         checkLedgers(ledgersAtBlocks[i],
            TestChain::ledgersLB2[i])) << i;
   }

   //LB2_P2SH
   ledgersAtBlocks = getLedgersPage(bridge_, delegateP2SHLB2, 0);
   ASSERT_EQ(ledgersAtBlocks.size(), 2);
   for (unsigned i = 0; i < ledgersAtBlocks.size(); i++) {
      EXPECT_TRUE(
         checkLedgers(ledgersAtBlocks[i],
            TestChain::ledgersLB2_P2SH[i]));
   }

   /* block 5 */
   TestUtils::setBlocks({ "0", "1", "2", "3", "4", "5" }, blk0dat_);
   nodePtr_->notifyNewBlock();
   ASSERT_EQ(waitOnNewBlock(), 5);

   //wlt balance
   wltBal = getWalletBalance(bridge_, walletId_BCDE_, accountId_BCDE_);
   ASSERT_EQ(wltBal.size(), 4);
   EXPECT_EQ(wltBal[0], TestChain::wltBal_BCDE[5][0]);
   EXPECT_EQ(wltBal[1], TestChain::wltBal_BCDE[5][1]);
   EXPECT_EQ(wltBal[2], TestChain::wltBal_BCDE[5][2]);
   EXPECT_EQ(wltBal[3], TestChain::wltBal_BCDE[5][3]);

   wltBal = getWalletBalance(bridge_, walletId_AFLB_, accountId_AFLB_);
   ASSERT_EQ(wltBal.size(), 4);
   EXPECT_EQ(wltBal[0], TestChain::wltBal_AFLB[5][0]);
   EXPECT_EQ(wltBal[1], TestChain::wltBal_AFLB[5][1]);
   EXPECT_EQ(wltBal[2], TestChain::wltBal_AFLB[5][2]);
   EXPECT_EQ(wltBal[3], TestChain::wltBal_AFLB[5][3]);

   //addr balances
   balances1 = getAddrBalances(bridge_, walletId_BCDE_, accountId_BCDE_);
   ASSERT_EQ(balances1.size(), 4);
   checkBalances(balances1, 5, false);
   balances2 = getAddrBalances(bridge_, walletId_AFLB_, accountId_AFLB_);
   ASSERT_EQ(balances2.size(), 6);
   checkBalances(balances2, 5, false);

   //wlt1 ledgers
   ledgersAtBlocks = getLedgersPage(bridge_, delegateIdWlt1, 0);
   ASSERT_EQ(ledgersAtBlocks.size(), 15);

   for (unsigned i = 0; i < ledgersAtBlocks.size(); i++) {
      EXPECT_TRUE(
         checkLedgers(ledgersAtBlocks[i],
            TestChain::ledgersBCDE[i]));
   }

   //wlt2 ledgers
   ledgersAtBlocks = getLedgersPage(bridge_, delegateIdWlt2, 0);
   ASSERT_EQ(ledgersAtBlocks.size(), 11);

   for (unsigned i = 0; i < ledgersAtBlocks.size(); i++) {
      EXPECT_TRUE(
         checkLedgers(ledgersAtBlocks[i],
            TestChain::ledgersAFLB[i]));
   }

   //combined ledgers
   ledgersAtBlocks = getLedgersPage(bridge_, delegateId, 0);
   ASSERT_EQ(ledgersAtBlocks.size(), 26);

   combinedLedgers.clear();
   combinedLedgers.insert(combinedLedgers.end(),
      TestChain::ledgersBCDE.begin(), TestChain::ledgersBCDE.end());
   combinedLedgers.insert(combinedLedgers.end(),
      TestChain::ledgersAFLB.begin(), TestChain::ledgersAFLB.end());
   ASSERT_EQ(combinedLedgers.size(), 26);
   std::sort(combinedLedgers.begin(), combinedLedgers.end());

   for (unsigned i = 0; i < ledgersAtBlocks.size(); i++) {
      EXPECT_TRUE(
         checkLedgers(ledgersAtBlocks[i],
            combinedLedgers[i])) << i;
   }

   /* address ledgers */

   //A
   ledgersAtBlocks = getLedgersPage(bridge_, delegateScrAddrA, 0);
   ASSERT_EQ(ledgersAtBlocks.size(), 1);
   for (unsigned i = 0; i < ledgersAtBlocks.size(); i++) {
      EXPECT_TRUE(
         checkLedgers(ledgersAtBlocks[i],
            TestChain::ledgersA[i]));
   }

   //B
   ledgersAtBlocks = getLedgersPage(bridge_, delegateScrAddrB, 0);
   ASSERT_EQ(ledgersAtBlocks.size(), 7);
   for (unsigned i = 0; i < ledgersAtBlocks.size(); i++) {
      EXPECT_TRUE(
         checkLedgers(ledgersAtBlocks[i],
            TestChain::ledgersB[i]));
   }

   //C
   ledgersAtBlocks = getLedgersPage(bridge_, delegateScrAddrC, 0);
   ASSERT_EQ(ledgersAtBlocks.size(), 4);
   for (unsigned i = 0; i < ledgersAtBlocks.size(); i++) {
      EXPECT_TRUE(
         checkLedgers(ledgersAtBlocks[i],
            TestChain::ledgersC[i]));
   }

   //D
   ledgersAtBlocks = getLedgersPage(bridge_, delegateScrAddrD, 0);
   ASSERT_EQ(ledgersAtBlocks.size(), 4);
   for (unsigned i = 0; i < ledgersAtBlocks.size(); i++) {
      EXPECT_TRUE(
         checkLedgers(ledgersAtBlocks[i],
            TestChain::ledgersD[i]));
   }

   //E
   ledgersAtBlocks = getLedgersPage(bridge_, delegateScrAddrE, 0);
   ASSERT_EQ(ledgersAtBlocks.size(), 2);
   for (unsigned i = 0; i < ledgersAtBlocks.size(); i++) {
      EXPECT_TRUE(
         checkLedgers(ledgersAtBlocks[i],
            TestChain::ledgersE[i]));
   }

   //F
   ledgersAtBlocks = getLedgersPage(bridge_, delegateScrAddrF, 0);
   ASSERT_EQ(ledgersAtBlocks.size(), 5);
   for (unsigned i = 0; i < ledgersAtBlocks.size(); i++) {
      EXPECT_TRUE(
         checkLedgers(ledgersAtBlocks[i],
            TestChain::ledgersF[i]));
   }

   //LB1
   ledgersAtBlocks = getLedgersPage(bridge_, delegateScrAddrLB1, 0);
   ASSERT_EQ(ledgersAtBlocks.size(), 2);
   for (unsigned i = 0; i < ledgersAtBlocks.size(); i++) {
      EXPECT_TRUE(
         checkLedgers(ledgersAtBlocks[i],
            TestChain::ledgersLB1[i]));
   }

   //LB1_P2SH
   ledgersAtBlocks = getLedgersPage(bridge_, delegateP2SHLB1, 0);
   ASSERT_EQ(ledgersAtBlocks.size(), 3);
   for (unsigned i = 0; i < ledgersAtBlocks.size(); i++) {
      EXPECT_TRUE(
         checkLedgers(ledgersAtBlocks[i],
            TestChain::ledgersLB1_P2SH[i]));
   }

   //LB2
   ledgersAtBlocks = getLedgersPage(bridge_, delegateScrAddrLB2, 0);
   ASSERT_EQ(ledgersAtBlocks.size(), 4);
   for (unsigned i = 0; i < ledgersAtBlocks.size(); i++) {
      EXPECT_TRUE(
         checkLedgers(ledgersAtBlocks[i],
            TestChain::ledgersLB2[i]));
   }

   //LB2_P2SH
   ledgersAtBlocks = getLedgersPage(bridge_, delegateP2SHLB2, 0);
   ASSERT_EQ(ledgersAtBlocks.size(), 2);
   for (unsigned i = 0; i < ledgersAtBlocks.size(); i++) {
      EXPECT_TRUE(
         checkLedgers(ledgersAtBlocks[i],
            TestChain::ledgersLB2_P2SH[i]));
   }
}

////////////////////////////////////////////////////////////////////////////////
// ledgers reorg
TEST_F(BridgeChainDataTests, Reorg_BCDE)
{
   loadWallets({walletId_BCDE_});

   //connect to db
   WebSocketServer::initAuthPeers({
      homedir_ / SERVER_AUTH_PEER_FILENAME, authPeersPassLbd_});
   WebSocketServer::start(theBDMt_->bdm(), true);

   ASSERT_TRUE(connectToIp(bridge_, "127.0.0.1", "9001", serverPubkey_));
   ASSERT_TRUE(registerWallets(bridge_));

   //start db, go online and wait on ready notif
   theBDMt_->start(Config::DBSettings::initMode());
   ASSERT_EQ(goOnline(bridge_), 3);

   /* block 3 */

   //wlt balance
   auto wltBal = getWalletBalance(bridge_, walletId_BCDE_, accountId_BCDE_);
   ASSERT_EQ(wltBal.size(), 4);
   EXPECT_EQ(wltBal[0], TestChain::wltBal_BCDE[3][0]);
   EXPECT_EQ(wltBal[1], TestChain::wltBal_BCDE[3][1]);
   EXPECT_EQ(wltBal[2], TestChain::wltBal_BCDE[3][2]);
   EXPECT_EQ(wltBal[3], TestChain::wltBal_BCDE[3][3]);

   //addr balances
   auto balances = getAddrBalances(bridge_, walletId_BCDE_, accountId_BCDE_);
   ASSERT_EQ(balances.size(), 4);
   checkBalances(balances, 3, false);

   //ledgers
   auto delegateId = getLedgerDelegateId(bridge_);
   ASSERT_FALSE(delegateId.empty());

   auto pageCount = getLedgersPageCount(bridge_, delegateId);
   EXPECT_EQ(pageCount, 1);

   auto ledgersAt3Blocks = getLedgersPage(bridge_, delegateId, 0);
   ASSERT_EQ(ledgersAt3Blocks.size(), 9);

   for (unsigned i = 0; i < ledgersAt3Blocks.size(); i++) {
      EXPECT_TRUE(
         checkLedgers(ledgersAt3Blocks[i],
         TestChain::ledgersBCDE[i + 6]));
   }

   /* block 4A */
   TestUtils::setBlocks({ "0", "1", "2", "3", "4A" }, blk0dat_);
   nodePtr_->notifyNewBlock();
   ASSERT_EQ(waitOnNewBlock(), 4);

   //wlt balance
   wltBal = getWalletBalance(bridge_, walletId_BCDE_, accountId_BCDE_);
   ASSERT_EQ(wltBal.size(), 4);
   EXPECT_EQ(wltBal[0], TestChain::wltBal_BCDE_Reorg[4][0]);
   EXPECT_EQ(wltBal[1], TestChain::wltBal_BCDE_Reorg[4][1]);
   EXPECT_EQ(wltBal[2], TestChain::wltBal_BCDE_Reorg[4][2]);
   EXPECT_EQ(wltBal[3], TestChain::wltBal_BCDE_Reorg[4][3]);

   //addr balances
   balances = getAddrBalances(bridge_, walletId_BCDE_, accountId_BCDE_);
   ASSERT_EQ(balances.size(), 4);
   checkBalances(balances, 4, true);

   //ledgers
   pageCount = getLedgersPageCount(bridge_, delegateId);
   EXPECT_EQ(pageCount, 1);

   auto ledgersAt4Blocks = getLedgersPage(bridge_, delegateId, 0);
   ASSERT_EQ(ledgersAt4Blocks.size(), 9);

   for (unsigned i = 0; i < ledgersAt4Blocks.size(); i++) {
      EXPECT_TRUE(
         checkLedgers(ledgersAt4Blocks[i],
         TestChain::ledgersBCDE_Reorg[i + 2]));
   }

   /* block 5 */
   TestUtils::setBlocks({ "0", "1", "2", "3", "4A", "4", "5" }, blk0dat_);
   nodePtr_->notifyNewBlock();
   ASSERT_EQ(waitOnNewBlock(), 5);

   //wlt balance
   wltBal = getWalletBalance(bridge_, walletId_BCDE_, accountId_BCDE_);
   ASSERT_EQ(wltBal.size(), 4);
   EXPECT_EQ(wltBal[0], TestChain::wltBal_BCDE[5][0]);
   EXPECT_EQ(wltBal[1], TestChain::wltBal_BCDE[5][1]);
   EXPECT_EQ(wltBal[2], TestChain::wltBal_BCDE[5][2]);
   EXPECT_EQ(wltBal[3], TestChain::wltBal_BCDE[5][3]);

   //addr balances
   balances = getAddrBalances(bridge_, walletId_BCDE_, accountId_BCDE_);
   ASSERT_EQ(balances.size(), 4);
   checkBalances(balances, 5, false);

   //ledgers
   pageCount = getLedgersPageCount(bridge_, delegateId);
   EXPECT_EQ(pageCount, 1);

   auto ledgersAt5Blocks = getLedgersPage(bridge_, delegateId, 0);
   ASSERT_EQ(ledgersAt5Blocks.size(), 15);

   for (unsigned i = 0; i < ledgersAt5Blocks.size(); i++) {
      EXPECT_TRUE(
         checkLedgers(ledgersAt5Blocks[i],
         TestChain::ledgersBCDE[i]));
   }

   /* block 5A */
   TestUtils::setBlocks({ "0", "1", "2", "3", "4A", "4", "5", "5A" }, blk0dat_);
   nodePtr_->notifyNewBlock();
   ASSERT_EQ(waitOnNewBlock(), 5);

   //wlt balance
   wltBal = getWalletBalance(bridge_, walletId_BCDE_, accountId_BCDE_);
   ASSERT_EQ(wltBal.size(), 4);
   EXPECT_EQ(wltBal[0], TestChain::wltBal_BCDE_Reorg[5][0]);
   EXPECT_EQ(wltBal[1], TestChain::wltBal_BCDE_Reorg[5][1]);
   EXPECT_EQ(wltBal[2], TestChain::wltBal_BCDE_Reorg[5][2]);
   EXPECT_EQ(wltBal[3], TestChain::wltBal_BCDE_Reorg[5][3]);

   //addr balances
   balances = getAddrBalances(bridge_, walletId_BCDE_, accountId_BCDE_);
   ASSERT_EQ(balances.size(), 4);
   checkBalances(balances, 5, true);

   //ledgers
   pageCount = getLedgersPageCount(bridge_, delegateId);
   EXPECT_EQ(pageCount, 1);

   ledgersAt5Blocks = getLedgersPage(bridge_, delegateId, 0);
   ASSERT_EQ(ledgersAt5Blocks.size(), 11);

   for (unsigned i = 0; i < ledgersAt5Blocks.size(); i++) {
      EXPECT_TRUE(
         checkLedgers(ledgersAt5Blocks[i],
         TestChain::ledgersBCDE_Reorg[i]));
   }
}

TEST_F(BridgeChainDataTests, Reorg_BCDE_DifferentOrder)
{
   loadWallets({walletId_BCDE_});

   //connect to db
   WebSocketServer::initAuthPeers({
      homedir_ / SERVER_AUTH_PEER_FILENAME, authPeersPassLbd_});
   WebSocketServer::start(theBDMt_->bdm(), true);

   ASSERT_TRUE(connectToIp(bridge_, "127.0.0.1", "9001", serverPubkey_));
   ASSERT_TRUE(registerWallets(bridge_));

   //start db, go online and wait on ready notif
   theBDMt_->start(Config::DBSettings::initMode());
   ASSERT_EQ(goOnline(bridge_), 3);

   /* block 3 */

   //wlt balance
   auto wltBal = getWalletBalance(bridge_, walletId_BCDE_, accountId_BCDE_);
   ASSERT_EQ(wltBal.size(), 4);
   EXPECT_EQ(wltBal[0], TestChain::wltBal_BCDE[3][0]);
   EXPECT_EQ(wltBal[1], TestChain::wltBal_BCDE[3][1]);
   EXPECT_EQ(wltBal[2], TestChain::wltBal_BCDE[3][2]);
   EXPECT_EQ(wltBal[3], TestChain::wltBal_BCDE[3][3]);

   //addr balances
   auto balances = getAddrBalances(bridge_, walletId_BCDE_, accountId_BCDE_);
   ASSERT_EQ(balances.size(), 4);
   checkBalances(balances, 3, false);

   //ledgers
   auto delegateId = getLedgerDelegateId(bridge_);
   ASSERT_FALSE(delegateId.empty());

   auto pageCount = getLedgersPageCount(bridge_, delegateId);
   EXPECT_EQ(pageCount, 1);

   auto ledgersAt3Blocks = getLedgersPage(bridge_, delegateId, 0);
   ASSERT_EQ(ledgersAt3Blocks.size(), 9);

   for (unsigned i = 0; i < ledgersAt3Blocks.size(); i++) {
      EXPECT_TRUE(
         checkLedgers(ledgersAt3Blocks[i],
         TestChain::ledgersBCDE[i + 6]));
   }

   /* block 4, 5 */
   TestUtils::setBlocks({ "0", "1", "2", "3", "4", "5" }, blk0dat_);
   nodePtr_->notifyNewBlock();
   ASSERT_EQ(waitOnNewBlock(), 5);

   //wlt balance
   wltBal = getWalletBalance(bridge_, walletId_BCDE_, accountId_BCDE_);
   ASSERT_EQ(wltBal.size(), 4);
   EXPECT_EQ(wltBal[0], TestChain::wltBal_BCDE[5][0]);
   EXPECT_EQ(wltBal[1], TestChain::wltBal_BCDE[5][1]);
   EXPECT_EQ(wltBal[2], TestChain::wltBal_BCDE[5][2]);
   EXPECT_EQ(wltBal[3], TestChain::wltBal_BCDE[5][3]);

   //addr balances
   balances = getAddrBalances(bridge_, walletId_BCDE_, accountId_BCDE_);
   ASSERT_EQ(balances.size(), 4);
   checkBalances(balances, 5, false);

   //ledgers
   pageCount = getLedgersPageCount(bridge_, delegateId);
   EXPECT_EQ(pageCount, 1);

   auto ledgersAt5Blocks = getLedgersPage(bridge_, delegateId, 0);
   ASSERT_EQ(ledgersAt5Blocks.size(), 15);

   for (unsigned i = 0; i < ledgersAt5Blocks.size(); i++) {
      EXPECT_TRUE(
         checkLedgers(ledgersAt5Blocks[i],
         TestChain::ledgersBCDE[i]));
   }

   /* block 4A, 5A */
   TestUtils::setBlocks({ "0", "1", "2", "3", "4", "5", "4A", "5A" }, blk0dat_);
   nodePtr_->notifyNewBlock();
   ASSERT_EQ(waitOnNewBlock(), 5);

   //wlt balance
   wltBal = getWalletBalance(bridge_, walletId_BCDE_, accountId_BCDE_);
   ASSERT_EQ(wltBal.size(), 4);
   EXPECT_EQ(wltBal[0], TestChain::wltBal_BCDE_Reorg[5][0]);
   EXPECT_EQ(wltBal[1], TestChain::wltBal_BCDE_Reorg[5][1]);
   EXPECT_EQ(wltBal[2], TestChain::wltBal_BCDE_Reorg[5][2]);
   EXPECT_EQ(wltBal[3], TestChain::wltBal_BCDE_Reorg[5][3]);

   //addr balances
   balances = getAddrBalances(bridge_, walletId_BCDE_, accountId_BCDE_);
   ASSERT_EQ(balances.size(), 4);
   checkBalances(balances, 5, true);

   //ledgers
   pageCount = getLedgersPageCount(bridge_, delegateId);
   EXPECT_EQ(pageCount, 1);

   ledgersAt5Blocks = getLedgersPage(bridge_, delegateId, 0);
   ASSERT_EQ(ledgersAt5Blocks.size(), 11);

   for (unsigned i = 0; i < ledgersAt5Blocks.size(); i++) {
      EXPECT_TRUE(
         checkLedgers(ledgersAt5Blocks[i],
         TestChain::ledgersBCDE_Reorg[i]));
   }
}

TEST_F(BridgeChainDataTests, Reorg_BC_DE)
{
   loadWallets({walletId_BC_, walletId_DE_});

   //connect to db
   WebSocketServer::initAuthPeers({
      homedir_ / SERVER_AUTH_PEER_FILENAME, authPeersPassLbd_});
   WebSocketServer::start(theBDMt_->bdm(), true);

   ASSERT_TRUE(connectToIp(bridge_, "127.0.0.1", "9001", serverPubkey_));
   ASSERT_TRUE(registerWallets(bridge_));

   //start db, go online and wait on ready notif
   theBDMt_->start(Config::DBSettings::initMode());
   ASSERT_EQ(goOnline(bridge_), 3);

   /* block 3 */

   //balances
   auto balances1 = getAddrBalances(bridge_, walletId_BC_, accountId_BC_);
   ASSERT_EQ(balances1.size(), 2);
   checkBalances(balances1, 3, false);
   auto balances2 = getAddrBalances(bridge_, walletId_DE_, accountId_DE_);
   ASSERT_EQ(balances2.size(), 2);
   checkBalances(balances2, 3, false);

   //ledgers
   auto delegateId = getLedgerDelegateId(bridge_);
   auto delegateIdWlt1 = getLedgerDelegateIdForWallet(
      bridge_, walletId_BC_, accountId_BC_);
   auto delegateIdWlt2 = getLedgerDelegateIdForWallet(
      bridge_, walletId_DE_, accountId_DE_);
   ASSERT_FALSE(delegateIdWlt1.empty());
   ASSERT_FALSE(delegateIdWlt2.empty());

   auto pageCount = getLedgersPageCount(bridge_, delegateId);
   EXPECT_EQ(pageCount, 1);
   pageCount = getLedgersPageCount(bridge_, delegateIdWlt1);
   EXPECT_EQ(pageCount, 1);
   pageCount = getLedgersPageCount(bridge_, delegateIdWlt2);
   EXPECT_EQ(pageCount, 1);

   //wlt1 ledgers
   auto ledgersAtBlocks = getLedgersPage(bridge_, delegateIdWlt1, 0);
   ASSERT_EQ(ledgersAtBlocks.size(), 7);

   for (unsigned i = 0; i < ledgersAtBlocks.size(); i++) {
      EXPECT_TRUE(
         checkLedgers(ledgersAtBlocks[i],
            TestChain::ledgersBC[i + 3]));
   }

   //wlt2 ledgers
   ledgersAtBlocks = getLedgersPage(bridge_, delegateIdWlt2, 0);
   ASSERT_EQ(ledgersAtBlocks.size(), 3);

   for (unsigned i = 0; i < ledgersAtBlocks.size(); i++) {
      EXPECT_TRUE(
         checkLedgers(ledgersAtBlocks[i],
            TestChain::ledgersDE[i + 3]));
   }

   //combined ledgers
   ledgersAtBlocks = getLedgersPage(bridge_, delegateId, 0);
   ASSERT_EQ(ledgersAtBlocks.size(), 10);

   std::vector<TestChain::LedgerEntryValue> combinedLedgers;
   combinedLedgers.insert(combinedLedgers.end(),
      TestChain::ledgersBC.begin() + 3, TestChain::ledgersBC.end());
   combinedLedgers.insert(combinedLedgers.end(),
      TestChain::ledgersDE.begin() + 3, TestChain::ledgersDE.end());
   ASSERT_EQ(combinedLedgers.size(), 10);
   std::sort(combinedLedgers.begin(), combinedLedgers.end());

   for (unsigned i = 0; i < ledgersAtBlocks.size(); i++) {
      EXPECT_TRUE(
         checkLedgers(ledgersAtBlocks[i],
         combinedLedgers[i])) << i;
   }

   /* block 4A */
   TestUtils::setBlocks({ "0", "1", "2", "3", "4A" }, blk0dat_);
   nodePtr_->notifyNewBlock();
   ASSERT_EQ(waitOnNewBlock(), 4);

   //balances
   balances1 = getAddrBalances(bridge_, walletId_BC_, accountId_BC_);
   ASSERT_EQ(balances1.size(), 2);
   checkBalances(balances1, 4, true);
   balances2 = getAddrBalances(bridge_, walletId_DE_, accountId_DE_);
   ASSERT_EQ(balances2.size(), 2);
   checkBalances(balances2, 4, true);

   //wlt1 ledgers
   ledgersAtBlocks = getLedgersPage(bridge_, delegateIdWlt1, 0);
   ASSERT_EQ(ledgersAtBlocks.size(), 7);

   for (unsigned i = 0; i < ledgersAtBlocks.size(); i++) {
      EXPECT_TRUE(
         checkLedgers(ledgersAtBlocks[i],
            TestChain::ledgersBC_Reorg[i]));
   }

   //wlt2 ledgers
   ledgersAtBlocks = getLedgersPage(bridge_, delegateIdWlt2, 0);
   ASSERT_EQ(ledgersAtBlocks.size(), 3);

   for (unsigned i = 0; i < ledgersAtBlocks.size(); i++) {
      EXPECT_TRUE(
         checkLedgers(ledgersAtBlocks[i],
            TestChain::ledgersDE_Reorg[i + 2]));
   }

   //combined ledgers
   ledgersAtBlocks = getLedgersPage(bridge_, delegateId, 0);
   ASSERT_EQ(ledgersAtBlocks.size(), 10);

   combinedLedgers.clear();
   combinedLedgers.insert(combinedLedgers.end(),
      TestChain::ledgersBC_Reorg.begin(), TestChain::ledgersBC_Reorg.end());
   combinedLedgers.insert(combinedLedgers.end(),
      TestChain::ledgersDE_Reorg.begin() + 2, TestChain::ledgersDE_Reorg.end());
   ASSERT_EQ(combinedLedgers.size(), 10);
   std::sort(combinedLedgers.begin(), combinedLedgers.end());

   for (unsigned i = 0; i < ledgersAtBlocks.size(); i++) {
      EXPECT_TRUE(
         checkLedgers(ledgersAtBlocks[i],
            combinedLedgers[i])) << i;
   }

   /* block 5 */
   TestUtils::setBlocks({ "0", "1", "2", "3", "4A", "4", "5" }, blk0dat_);
   nodePtr_->notifyNewBlock();
   ASSERT_EQ(waitOnNewBlock(), 5);

   //balances
   balances1 = getAddrBalances(bridge_, walletId_BC_, accountId_BC_);
   ASSERT_EQ(balances1.size(), 2);
   checkBalances(balances1, 5, false);
   balances2 = getAddrBalances(bridge_, walletId_DE_, accountId_DE_);
   ASSERT_EQ(balances2.size(), 2);
   checkBalances(balances2, 5, false);

   //wlt1 ledgers
   ledgersAtBlocks = getLedgersPage(bridge_, delegateIdWlt1, 0);
   ASSERT_EQ(ledgersAtBlocks.size(), 10);

   for (unsigned i = 0; i < ledgersAtBlocks.size(); i++) {
      EXPECT_TRUE(
         checkLedgers(ledgersAtBlocks[i],
            TestChain::ledgersBC[i]));
   }

   //wlt2 ledgers
   ledgersAtBlocks = getLedgersPage(bridge_, delegateIdWlt2, 0);
   ASSERT_EQ(ledgersAtBlocks.size(), 6);

   for (unsigned i = 0; i < ledgersAtBlocks.size(); i++) {
      EXPECT_TRUE(
         checkLedgers(ledgersAtBlocks[i],
            TestChain::ledgersDE[i]));
   }

   //combined ledgers
   ledgersAtBlocks = getLedgersPage(bridge_, delegateId, 0);
   ASSERT_EQ(ledgersAtBlocks.size(), 16);

   combinedLedgers.clear();
   combinedLedgers.insert(combinedLedgers.end(),
      TestChain::ledgersBC.begin(), TestChain::ledgersBC.end());
   combinedLedgers.insert(combinedLedgers.end(),
      TestChain::ledgersDE.begin(), TestChain::ledgersDE.end());
   ASSERT_EQ(combinedLedgers.size(), 16);
   std::sort(combinedLedgers.begin(), combinedLedgers.end());

   for (unsigned i = 0; i < ledgersAtBlocks.size(); i++) {
      EXPECT_TRUE(
         checkLedgers(ledgersAtBlocks[i],
            combinedLedgers[i])) << i;
   }

   /* block 5A */
   TestUtils::setBlocks({ "0", "1", "2", "3", "4A", "4", "5", "5A" }, blk0dat_);
   nodePtr_->notifyNewBlock();
   ASSERT_EQ(waitOnNewBlock(), 5);

   //balances
   balances1 = getAddrBalances(bridge_, walletId_BC_, accountId_BC_);
   ASSERT_EQ(balances1.size(), 2);
   checkBalances(balances1, 5, true);
   balances2 = getAddrBalances(bridge_, walletId_DE_, accountId_DE_);
   ASSERT_EQ(balances2.size(), 2);
   checkBalances(balances2, 5, true);

   //wlt1 ledgers
   ledgersAtBlocks = getLedgersPage(bridge_, delegateIdWlt1, 0);
   ASSERT_EQ(ledgersAtBlocks.size(), 7);

   for (unsigned i = 0; i < ledgersAtBlocks.size(); i++) {
      EXPECT_TRUE(
         checkLedgers(ledgersAtBlocks[i],
            TestChain::ledgersBC_Reorg[i]));
   }

   //wlt2 ledgers
   ledgersAtBlocks = getLedgersPage(bridge_, delegateIdWlt2, 0);
   ASSERT_EQ(ledgersAtBlocks.size(), 5);

   for (unsigned i = 0; i < ledgersAtBlocks.size(); i++) {
      EXPECT_TRUE(
         checkLedgers(ledgersAtBlocks[i],
            TestChain::ledgersDE_Reorg[i])) << i;
   }

   //combined ledgers
   ledgersAtBlocks = getLedgersPage(bridge_, delegateId, 0);
   ASSERT_EQ(ledgersAtBlocks.size(), 12);

   combinedLedgers.clear();
   combinedLedgers.insert(combinedLedgers.end(),
      TestChain::ledgersBC_Reorg.begin(), TestChain::ledgersBC_Reorg.end());
   combinedLedgers.insert(combinedLedgers.end(),
      TestChain::ledgersDE_Reorg.begin(), TestChain::ledgersDE_Reorg.end());
   ASSERT_EQ(combinedLedgers.size(), 12);
   std::sort(combinedLedgers.begin(), combinedLedgers.end());

   for (unsigned i = 0; i < ledgersAtBlocks.size(); i++) {
      EXPECT_TRUE(
         checkLedgers(ledgersAtBlocks[i],
            combinedLedgers[i])) << i;
   }
}

TEST_F(BridgeChainDataTests, Reorg_BCDE_AFLB)
{
   loadWallets({walletId_BCDE_, walletId_AFLB_});

   //connect to db
   WebSocketServer::initAuthPeers({
      homedir_ / SERVER_AUTH_PEER_FILENAME, authPeersPassLbd_});
   WebSocketServer::start(theBDMt_->bdm(), true);

   ASSERT_TRUE(connectToIp(bridge_, "127.0.0.1", "9001", serverPubkey_));
   ASSERT_TRUE(registerWallets(bridge_));

   //start db, go online and wait on ready notif
   theBDMt_->start(Config::DBSettings::initMode());
   ASSERT_EQ(goOnline(bridge_), 3);

   /* block 3 */

   //wlt balance
   auto wltBal = getWalletBalance(bridge_, walletId_BCDE_, accountId_BCDE_);
   ASSERT_EQ(wltBal.size(), 4);
   EXPECT_EQ(wltBal[0], TestChain::wltBal_BCDE[3][0]);
   EXPECT_EQ(wltBal[1], TestChain::wltBal_BCDE[3][1]);
   EXPECT_EQ(wltBal[2], TestChain::wltBal_BCDE[3][2]);
   EXPECT_EQ(wltBal[3], TestChain::wltBal_BCDE[3][3]);

   wltBal = getWalletBalance(bridge_, walletId_AFLB_, accountId_AFLB_);
   ASSERT_EQ(wltBal.size(), 4);
   EXPECT_EQ(wltBal[0], TestChain::wltBal_AFLB[3][0]);
   EXPECT_EQ(wltBal[1], TestChain::wltBal_AFLB[3][1]);
   EXPECT_EQ(wltBal[2], TestChain::wltBal_AFLB[3][2]);
   EXPECT_EQ(wltBal[3], TestChain::wltBal_AFLB[3][3]);

   //addr balances
   auto balances1 = getAddrBalances(bridge_, walletId_BCDE_, accountId_BCDE_);
   ASSERT_EQ(balances1.size(), 4);
   checkBalances(balances1, 3, false);
   auto balances2 = getAddrBalances(bridge_, walletId_AFLB_, accountId_AFLB_);
   ASSERT_EQ(balances2.size(), 6);
   checkBalances(balances2, 3, false);

   //setup wallet ledger delegates
   auto delegateId = getLedgerDelegateId(bridge_);
   auto delegateIdWlt1 = getLedgerDelegateIdForWallet(
      bridge_, walletId_BCDE_, accountId_BCDE_);
   auto delegateIdWlt2 = getLedgerDelegateIdForWallet(
      bridge_, walletId_AFLB_, accountId_AFLB_);
   ASSERT_FALSE(delegateIdWlt1.empty());
   ASSERT_FALSE(delegateIdWlt2.empty());

   auto pageCount = getLedgersPageCount(bridge_, delegateId);
   EXPECT_EQ(pageCount, 1);
   pageCount = getLedgersPageCount(bridge_, delegateIdWlt1);
   EXPECT_EQ(pageCount, 1);
   pageCount = getLedgersPageCount(bridge_, delegateIdWlt2);
   EXPECT_EQ(pageCount, 1);

   //setup address ledger delegates
   auto delegateScrAddrA = getLedgerDelegateIdForScrAddr(bridge_,
      walletId_AFLB_, accountId_AFLB_, TestChain::scrAddrA);
   ASSERT_FALSE(delegateScrAddrA.empty());
   auto delegateScrAddrB = getLedgerDelegateIdForScrAddr(bridge_,
      walletId_BCDE_, accountId_BCDE_, TestChain::scrAddrB);
   ASSERT_FALSE(delegateScrAddrB.empty());
   auto delegateScrAddrC = getLedgerDelegateIdForScrAddr(bridge_,
      walletId_BCDE_, accountId_BCDE_, TestChain::scrAddrC);
   ASSERT_FALSE(delegateScrAddrC.empty());
   auto delegateScrAddrD = getLedgerDelegateIdForScrAddr(bridge_,
      walletId_BCDE_, accountId_BCDE_, TestChain::scrAddrD);
   ASSERT_FALSE(delegateScrAddrD.empty());
   auto delegateScrAddrE = getLedgerDelegateIdForScrAddr(bridge_,
      walletId_BCDE_, accountId_BCDE_, TestChain::scrAddrE);
   ASSERT_FALSE(delegateScrAddrE.empty());
   auto delegateScrAddrF = getLedgerDelegateIdForScrAddr(bridge_,
      walletId_AFLB_, accountId_AFLB_, TestChain::scrAddrF);
   ASSERT_FALSE(delegateScrAddrF.empty());
   auto delegateScrAddrLB1 = getLedgerDelegateIdForScrAddr(bridge_,
      walletId_AFLB_, accountId_AFLB_, TestChain::lb1ScrAddr);
   ASSERT_FALSE(delegateScrAddrLB1.empty());
   auto delegateP2SHLB1 = getLedgerDelegateIdForScrAddr(bridge_,
      walletId_AFLB_, accountId_AFLB_, TestChain::lb1ScrAddrP2SH);
   ASSERT_FALSE(delegateP2SHLB1.empty());
   auto delegateScrAddrLB2 = getLedgerDelegateIdForScrAddr(bridge_,
      walletId_AFLB_, accountId_AFLB_, TestChain::lb2ScrAddr);
   ASSERT_FALSE(delegateScrAddrLB2.empty());
   auto delegateP2SHLB2 = getLedgerDelegateIdForScrAddr(bridge_,
      walletId_AFLB_, accountId_AFLB_, TestChain::lb2ScrAddrP2SH);
   ASSERT_FALSE(delegateP2SHLB2.empty());

   pageCount = getLedgersPageCount(bridge_, delegateScrAddrA);
   EXPECT_EQ(pageCount, 1);
   pageCount = getLedgersPageCount(bridge_, delegateScrAddrB);
   EXPECT_EQ(pageCount, 1);
   pageCount = getLedgersPageCount(bridge_, delegateScrAddrC);
   EXPECT_EQ(pageCount, 1);
   pageCount = getLedgersPageCount(bridge_, delegateScrAddrD);
   EXPECT_EQ(pageCount, 1);
   pageCount = getLedgersPageCount(bridge_, delegateScrAddrE);
   EXPECT_EQ(pageCount, 1);
   pageCount = getLedgersPageCount(bridge_, delegateScrAddrF);
   EXPECT_EQ(pageCount, 1);
   pageCount = getLedgersPageCount(bridge_, delegateScrAddrLB1);
   EXPECT_EQ(pageCount, 1);
   pageCount = getLedgersPageCount(bridge_, delegateP2SHLB1);
   EXPECT_EQ(pageCount, 1);
   pageCount = getLedgersPageCount(bridge_, delegateScrAddrLB2);
   EXPECT_EQ(pageCount, 1);
   pageCount = getLedgersPageCount(bridge_, delegateP2SHLB2);
   EXPECT_EQ(pageCount, 1);

   //wlt1 ledgers
   auto ledgersAtBlocks = getLedgersPage(bridge_, delegateIdWlt1, 0);
   ASSERT_EQ(ledgersAtBlocks.size(), 9);

   for (unsigned i = 0; i < ledgersAtBlocks.size(); i++) {
      EXPECT_TRUE(
         checkLedgers(ledgersAtBlocks[i],
            TestChain::ledgersBCDE[i + 6]));
   }

   //wlt2 ledgers
   ledgersAtBlocks = getLedgersPage(bridge_, delegateIdWlt2, 0);
   ASSERT_EQ(ledgersAtBlocks.size(), 7);

   for (unsigned i = 0; i < ledgersAtBlocks.size(); i++) {
      EXPECT_TRUE(
         checkLedgers(ledgersAtBlocks[i],
            TestChain::ledgersAFLB[i + 4]));
   }

   //combined ledgers
   ledgersAtBlocks = getLedgersPage(bridge_, delegateId, 0);
   ASSERT_EQ(ledgersAtBlocks.size(), 16);

   std::vector<TestChain::LedgerEntryValue> combinedLedgers;
   combinedLedgers.insert(combinedLedgers.end(),
      TestChain::ledgersBCDE.begin() + 6, TestChain::ledgersBCDE.end());
   combinedLedgers.insert(combinedLedgers.end(),
      TestChain::ledgersAFLB.begin() + 4, TestChain::ledgersAFLB.end());
   ASSERT_EQ(combinedLedgers.size(), 16);
   std::sort(combinedLedgers.begin(), combinedLedgers.end());

   for (unsigned i = 0; i < ledgersAtBlocks.size(); i++) {
      EXPECT_TRUE(
         checkLedgers(ledgersAtBlocks[i],
         combinedLedgers[i])) << i;
   }

   /* address ledgers */

   //A
   ledgersAtBlocks = getLedgersPage(bridge_, delegateScrAddrA, 0);
   ASSERT_EQ(ledgersAtBlocks.size(), 1);
   for (unsigned i = 0; i < ledgersAtBlocks.size(); i++) {
      EXPECT_TRUE(
         checkLedgers(ledgersAtBlocks[i],
            TestChain::ledgersA[i]));
   }

   //B
   ledgersAtBlocks = getLedgersPage(bridge_, delegateScrAddrB, 0);
   ASSERT_EQ(ledgersAtBlocks.size(), 5);
   for (unsigned i = 0; i < ledgersAtBlocks.size(); i++) {
      EXPECT_TRUE(
         checkLedgers(ledgersAtBlocks[i],
            TestChain::ledgersB[i + 2]));
   }

   //C
   ledgersAtBlocks = getLedgersPage(bridge_, delegateScrAddrC, 0);
   ASSERT_EQ(ledgersAtBlocks.size(), 2);
   for (unsigned i = 0; i < ledgersAtBlocks.size(); i++) {
      EXPECT_TRUE(
         checkLedgers(ledgersAtBlocks[i],
            TestChain::ledgersC[i + 2]));
   }

   //D
   ledgersAtBlocks = getLedgersPage(bridge_, delegateScrAddrD, 0);
   ASSERT_EQ(ledgersAtBlocks.size(), 1);
   for (unsigned i = 0; i < ledgersAtBlocks.size(); i++) {
      EXPECT_TRUE(
         checkLedgers(ledgersAtBlocks[i],
            TestChain::ledgersD[i + 3]));
   }

   //E
   ledgersAtBlocks = getLedgersPage(bridge_, delegateScrAddrE, 0);
   ASSERT_EQ(ledgersAtBlocks.size(), 2);
   for (unsigned i = 0; i < ledgersAtBlocks.size(); i++) {
      EXPECT_TRUE(
         checkLedgers(ledgersAtBlocks[i],
            TestChain::ledgersE[i]));
   }

   //F
   ledgersAtBlocks = getLedgersPage(bridge_, delegateScrAddrF, 0);
   ASSERT_EQ(ledgersAtBlocks.size(), 3);
   for (unsigned i = 0; i < ledgersAtBlocks.size(); i++) {
      EXPECT_TRUE(
         checkLedgers(ledgersAtBlocks[i],
            TestChain::ledgersF[i + 2]));
   }

   //LB1
   ledgersAtBlocks = getLedgersPage(bridge_, delegateScrAddrLB1, 0);
   ASSERT_EQ(ledgersAtBlocks.size(), 1);
   for (unsigned i = 0; i < ledgersAtBlocks.size(); i++) {
      EXPECT_TRUE(
         checkLedgers(ledgersAtBlocks[i],
            TestChain::ledgersLB1[i + 1]));
   }

   //LB1_P2SH
   ledgersAtBlocks = getLedgersPage(bridge_, delegateP2SHLB1, 0);
   ASSERT_EQ(ledgersAtBlocks.size(), 2);
   for (unsigned i = 0; i < ledgersAtBlocks.size(); i++) {
      EXPECT_TRUE(
         checkLedgers(ledgersAtBlocks[i],
            TestChain::ledgersLB1_P2SH[i + 1]));
   }

   //LB2
   ledgersAtBlocks = getLedgersPage(bridge_, delegateScrAddrLB2, 0);
   ASSERT_EQ(ledgersAtBlocks.size(), 3);
   for (unsigned i = 0; i < ledgersAtBlocks.size(); i++) {
      EXPECT_TRUE(
         checkLedgers(ledgersAtBlocks[i],
            TestChain::ledgersLB2[i + 1])) << i;
   }

   //LB2_P2SH
   ledgersAtBlocks = getLedgersPage(bridge_, delegateP2SHLB2, 0);
   ASSERT_EQ(ledgersAtBlocks.size(), 1);
   for (unsigned i = 0; i < ledgersAtBlocks.size(); i++) {
      EXPECT_TRUE(
         checkLedgers(ledgersAtBlocks[i],
            TestChain::ledgersLB2_P2SH[i + 1]));
   }

   /* block 4A */
   TestUtils::setBlocks({ "0", "1", "2", "3", "4A" }, blk0dat_);
   nodePtr_->notifyNewBlock();
   ASSERT_EQ(waitOnNewBlock(), 4);

   //wlt balance
   wltBal = getWalletBalance(bridge_, walletId_BCDE_, accountId_BCDE_);
   ASSERT_EQ(wltBal.size(), 4);
   EXPECT_EQ(wltBal[0], TestChain::wltBal_BCDE_Reorg[4][0]);
   EXPECT_EQ(wltBal[1], TestChain::wltBal_BCDE_Reorg[4][1]);
   EXPECT_EQ(wltBal[2], TestChain::wltBal_BCDE_Reorg[4][2]);
   EXPECT_EQ(wltBal[3], TestChain::wltBal_BCDE_Reorg[4][3]);

   wltBal = getWalletBalance(bridge_, walletId_AFLB_, accountId_AFLB_);
   ASSERT_EQ(wltBal.size(), 4);
   EXPECT_EQ(wltBal[0], TestChain::wltBal_AFLB_Reorg[4][0]);
   EXPECT_EQ(wltBal[1], TestChain::wltBal_AFLB_Reorg[4][1]);
   EXPECT_EQ(wltBal[2], TestChain::wltBal_AFLB_Reorg[4][2]);
   EXPECT_EQ(wltBal[3], TestChain::wltBal_AFLB_Reorg[4][3]);

   //addr balances
   balances1 = getAddrBalances(bridge_, walletId_BCDE_, accountId_BCDE_);
   ASSERT_EQ(balances1.size(), 4);
   checkBalances(balances1, 4, true);
   balances2 = getAddrBalances(bridge_, walletId_AFLB_, accountId_AFLB_);
   ASSERT_EQ(balances2.size(), 6);
   checkBalances(balances2, 4, true);

   //wlt1 ledgers
   ledgersAtBlocks = getLedgersPage(bridge_, delegateIdWlt1, 0);
   ASSERT_EQ(ledgersAtBlocks.size(), 9);

   for (unsigned i = 0; i < ledgersAtBlocks.size(); i++) {
      EXPECT_TRUE(
         checkLedgers(ledgersAtBlocks[i],
            TestChain::ledgersBCDE_Reorg[i + 2]));
   }

   //wlt2 ledgers
   ledgersAtBlocks = getLedgersPage(bridge_, delegateIdWlt2, 0);
   ASSERT_EQ(ledgersAtBlocks.size(), 8);

   for (unsigned i = 0; i < ledgersAtBlocks.size(); i++) {
      EXPECT_TRUE(
         checkLedgers(ledgersAtBlocks[i],
            TestChain::ledgersAFLB_Reorg[i + 2]));
   }

   //combined ledgers
   ledgersAtBlocks = getLedgersPage(bridge_, delegateId, 0);
   ASSERT_EQ(ledgersAtBlocks.size(), 17);

   combinedLedgers.clear();
   combinedLedgers.insert(combinedLedgers.end(),
      TestChain::ledgersBCDE_Reorg.begin() + 2, TestChain::ledgersBCDE_Reorg.end());
   combinedLedgers.insert(combinedLedgers.end(),
      TestChain::ledgersAFLB_Reorg.begin() + 2, TestChain::ledgersAFLB_Reorg.end());
   ASSERT_EQ(combinedLedgers.size(), 17);
   std::sort(combinedLedgers.begin(), combinedLedgers.end());

   for (unsigned i = 0; i < ledgersAtBlocks.size(); i++) {
      EXPECT_TRUE(
         checkLedgers(ledgersAtBlocks[i],
            combinedLedgers[i])) << i;
   }

   /* address ledgers */

   //A
   ledgersAtBlocks = getLedgersPage(bridge_, delegateScrAddrA, 0);
   ASSERT_EQ(ledgersAtBlocks.size(), 1);
   for (unsigned i = 0; i < ledgersAtBlocks.size(); i++) {
      EXPECT_TRUE(
         checkLedgers(ledgersAtBlocks[i],
            TestChain::ledgersA_Reorg[i]));
   }

   //B
   ledgersAtBlocks = getLedgersPage(bridge_, delegateScrAddrB, 0);
   ASSERT_EQ(ledgersAtBlocks.size(), 5);
   for (unsigned i = 0; i < ledgersAtBlocks.size(); i++) {
      EXPECT_TRUE(
         checkLedgers(ledgersAtBlocks[i],
            TestChain::ledgersB_Reorg[i]));
   }

   //C
   ledgersAtBlocks = getLedgersPage(bridge_, delegateScrAddrC, 0);
   ASSERT_EQ(ledgersAtBlocks.size(), 2);
   for (unsigned i = 0; i < ledgersAtBlocks.size(); i++) {
      EXPECT_TRUE(
         checkLedgers(ledgersAtBlocks[i],
            TestChain::ledgersC_Reorg[i]));
   }

   //D
   ledgersAtBlocks = getLedgersPage(bridge_, delegateScrAddrD, 0);
   ASSERT_EQ(ledgersAtBlocks.size(), 1);
   for (unsigned i = 0; i < ledgersAtBlocks.size(); i++) {
      EXPECT_TRUE(
         checkLedgers(ledgersAtBlocks[i],
            TestChain::ledgersD_Reorg[i + 2]));
   }

   //E
   ledgersAtBlocks = getLedgersPage(bridge_, delegateScrAddrE, 0);
   ASSERT_EQ(ledgersAtBlocks.size(), 2);
   for (unsigned i = 0; i < ledgersAtBlocks.size(); i++) {
      EXPECT_TRUE(
         checkLedgers(ledgersAtBlocks[i],
            TestChain::ledgersE_Reorg[i]));
   }

   //F
   ledgersAtBlocks = getLedgersPage(bridge_, delegateScrAddrF, 0);
   ASSERT_EQ(ledgersAtBlocks.size(), 4);
   for (unsigned i = 0; i < ledgersAtBlocks.size(); i++) {
      EXPECT_TRUE(
         checkLedgers(ledgersAtBlocks[i],
            TestChain::ledgersF_Reorg[i + 1]));
   }

   //LB1
   ledgersAtBlocks = getLedgersPage(bridge_, delegateScrAddrLB1, 0);
   ASSERT_EQ(ledgersAtBlocks.size(), 1);
   for (unsigned i = 0; i < ledgersAtBlocks.size(); i++) {
      EXPECT_TRUE(
         checkLedgers(ledgersAtBlocks[i],
            TestChain::ledgersLB1_Reorg[i + 1]));
   }

   //LB1_P2SH
   ledgersAtBlocks = getLedgersPage(bridge_, delegateP2SHLB1, 0);
   ASSERT_EQ(ledgersAtBlocks.size(), 2);
   for (unsigned i = 0; i < ledgersAtBlocks.size(); i++) {
      EXPECT_TRUE(
         checkLedgers(ledgersAtBlocks[i],
            TestChain::ledgersLB1_P2SH_Reorg[i]));
   }

   //LB2
   ledgersAtBlocks = getLedgersPage(bridge_, delegateScrAddrLB2, 0);
   ASSERT_EQ(ledgersAtBlocks.size(), 3);
   for (unsigned i = 0; i < ledgersAtBlocks.size(); i++) {
      EXPECT_TRUE(
         checkLedgers(ledgersAtBlocks[i],
            TestChain::ledgersLB2_Reorg[i])) << i;
   }

   //LB2_P2SH
   ledgersAtBlocks = getLedgersPage(bridge_, delegateP2SHLB2, 0);
   ASSERT_EQ(ledgersAtBlocks.size(), 1);
   for (unsigned i = 0; i < ledgersAtBlocks.size(); i++) {
      EXPECT_TRUE(
         checkLedgers(ledgersAtBlocks[i],
            TestChain::ledgersLB2_P2SH_Reorg[i + 1]));
   }

   /* block 5 */
   TestUtils::setBlocks({ "0", "1", "2", "3", "4A", "4", "5" }, blk0dat_);
   nodePtr_->notifyNewBlock();
   ASSERT_EQ(waitOnNewBlock(), 5);

   //wlt balance
   wltBal = getWalletBalance(bridge_, walletId_BCDE_, accountId_BCDE_);
   ASSERT_EQ(wltBal.size(), 4);
   EXPECT_EQ(wltBal[0], TestChain::wltBal_BCDE[5][0]);
   EXPECT_EQ(wltBal[1], TestChain::wltBal_BCDE[5][1]);
   EXPECT_EQ(wltBal[2], TestChain::wltBal_BCDE[5][2]);
   EXPECT_EQ(wltBal[3], TestChain::wltBal_BCDE[5][3]);

   wltBal = getWalletBalance(bridge_, walletId_AFLB_, accountId_AFLB_);
   ASSERT_EQ(wltBal.size(), 4);
   EXPECT_EQ(wltBal[0], TestChain::wltBal_AFLB[5][0]);
   EXPECT_EQ(wltBal[1], TestChain::wltBal_AFLB[5][1]);
   EXPECT_EQ(wltBal[2], TestChain::wltBal_AFLB[5][2]);
   EXPECT_EQ(wltBal[3], TestChain::wltBal_AFLB[5][3]);

   //balances
   balances1 = getAddrBalances(bridge_, walletId_BCDE_, accountId_BCDE_);
   ASSERT_EQ(balances1.size(), 4);
   checkBalances(balances1, 5, false);
   balances2 = getAddrBalances(bridge_, walletId_AFLB_, accountId_AFLB_);
   ASSERT_EQ(balances2.size(), 6);
   checkBalances(balances2, 5, false);

   //wlt1 ledgers
   ledgersAtBlocks = getLedgersPage(bridge_, delegateIdWlt1, 0);
   ASSERT_EQ(ledgersAtBlocks.size(), 15);

   for (unsigned i = 0; i < ledgersAtBlocks.size(); i++) {
      EXPECT_TRUE(
         checkLedgers(ledgersAtBlocks[i],
            TestChain::ledgersBCDE[i]));
   }

   //wlt2 ledgers
   ledgersAtBlocks = getLedgersPage(bridge_, delegateIdWlt2, 0);
   ASSERT_EQ(ledgersAtBlocks.size(), 11);

   for (unsigned i = 0; i < ledgersAtBlocks.size(); i++) {
      EXPECT_TRUE(
         checkLedgers(ledgersAtBlocks[i],
            TestChain::ledgersAFLB[i]));
   }

   //combined ledgers
   ledgersAtBlocks = getLedgersPage(bridge_, delegateId, 0);
   ASSERT_EQ(ledgersAtBlocks.size(), 26);

   combinedLedgers.clear();
   combinedLedgers.insert(combinedLedgers.end(),
      TestChain::ledgersBCDE.begin(), TestChain::ledgersBCDE.end());
   combinedLedgers.insert(combinedLedgers.end(),
      TestChain::ledgersAFLB.begin(), TestChain::ledgersAFLB.end());
   ASSERT_EQ(combinedLedgers.size(), 26);
   std::sort(combinedLedgers.begin(), combinedLedgers.end());

   for (unsigned i = 0; i < ledgersAtBlocks.size(); i++) {
      EXPECT_TRUE(
         checkLedgers(ledgersAtBlocks[i],
            combinedLedgers[i])) << i;
   }

   /* address ledgers */

   //A
   ledgersAtBlocks = getLedgersPage(bridge_, delegateScrAddrA, 0);
   ASSERT_EQ(ledgersAtBlocks.size(), 1);
   for (unsigned i = 0; i < ledgersAtBlocks.size(); i++) {
      EXPECT_TRUE(
         checkLedgers(ledgersAtBlocks[i],
            TestChain::ledgersA[i]));
   }

   //B
   ledgersAtBlocks = getLedgersPage(bridge_, delegateScrAddrB, 0);
   ASSERT_EQ(ledgersAtBlocks.size(), 7);
   for (unsigned i = 0; i < ledgersAtBlocks.size(); i++) {
      EXPECT_TRUE(
         checkLedgers(ledgersAtBlocks[i],
            TestChain::ledgersB[i]));
   }

   //C
   ledgersAtBlocks = getLedgersPage(bridge_, delegateScrAddrC, 0);
   ASSERT_EQ(ledgersAtBlocks.size(), 4);
   for (unsigned i = 0; i < ledgersAtBlocks.size(); i++) {
      EXPECT_TRUE(
         checkLedgers(ledgersAtBlocks[i],
            TestChain::ledgersC[i]));
   }

   //D
   ledgersAtBlocks = getLedgersPage(bridge_, delegateScrAddrD, 0);
   ASSERT_EQ(ledgersAtBlocks.size(), 4);
   for (unsigned i = 0; i < ledgersAtBlocks.size(); i++) {
      EXPECT_TRUE(
         checkLedgers(ledgersAtBlocks[i],
            TestChain::ledgersD[i]));
   }

   //E
   ledgersAtBlocks = getLedgersPage(bridge_, delegateScrAddrE, 0);
   ASSERT_EQ(ledgersAtBlocks.size(), 2);
   for (unsigned i = 0; i < ledgersAtBlocks.size(); i++) {
      EXPECT_TRUE(
         checkLedgers(ledgersAtBlocks[i],
            TestChain::ledgersE[i]));
   }

   //F
   ledgersAtBlocks = getLedgersPage(bridge_, delegateScrAddrF, 0);
   ASSERT_EQ(ledgersAtBlocks.size(), 5);
   for (unsigned i = 0; i < ledgersAtBlocks.size(); i++) {
      EXPECT_TRUE(
         checkLedgers(ledgersAtBlocks[i],
            TestChain::ledgersF[i]));
   }

   //LB1
   ledgersAtBlocks = getLedgersPage(bridge_, delegateScrAddrLB1, 0);
   ASSERT_EQ(ledgersAtBlocks.size(), 2);
   for (unsigned i = 0; i < ledgersAtBlocks.size(); i++) {
      EXPECT_TRUE(
         checkLedgers(ledgersAtBlocks[i],
            TestChain::ledgersLB1[i]));
   }

   //LB1_P2SH
   ledgersAtBlocks = getLedgersPage(bridge_, delegateP2SHLB1, 0);
   ASSERT_EQ(ledgersAtBlocks.size(), 3);
   for (unsigned i = 0; i < ledgersAtBlocks.size(); i++) {
      EXPECT_TRUE(
         checkLedgers(ledgersAtBlocks[i],
            TestChain::ledgersLB1_P2SH[i]));
   }

   //LB2
   ledgersAtBlocks = getLedgersPage(bridge_, delegateScrAddrLB2, 0);
   ASSERT_EQ(ledgersAtBlocks.size(), 4);
   for (unsigned i = 0; i < ledgersAtBlocks.size(); i++) {
      EXPECT_TRUE(
         checkLedgers(ledgersAtBlocks[i],
            TestChain::ledgersLB2[i]));
   }

   //LB2_P2SH
   ledgersAtBlocks = getLedgersPage(bridge_, delegateP2SHLB2, 0);
   ASSERT_EQ(ledgersAtBlocks.size(), 2);
   for (unsigned i = 0; i < ledgersAtBlocks.size(); i++) {
      EXPECT_TRUE(
         checkLedgers(ledgersAtBlocks[i],
            TestChain::ledgersLB2_P2SH[i]));
   }

   /* block 5A */
   TestUtils::setBlocks({ "0", "1", "2", "3", "4A", "4", "5", "5A" }, blk0dat_);
   nodePtr_->notifyNewBlock();
   ASSERT_EQ(waitOnNewBlock(), 5);

   //wlt balance
   wltBal = getWalletBalance(bridge_, walletId_BCDE_, accountId_BCDE_);
   ASSERT_EQ(wltBal.size(), 4);
   EXPECT_EQ(wltBal[0], TestChain::wltBal_BCDE_Reorg[5][0]);
   EXPECT_EQ(wltBal[1], TestChain::wltBal_BCDE_Reorg[5][1]);
   EXPECT_EQ(wltBal[2], TestChain::wltBal_BCDE_Reorg[5][2]);
   EXPECT_EQ(wltBal[3], TestChain::wltBal_BCDE_Reorg[5][3]);

   wltBal = getWalletBalance(bridge_, walletId_AFLB_, accountId_AFLB_);
   ASSERT_EQ(wltBal.size(), 4);
   EXPECT_EQ(wltBal[0], TestChain::wltBal_AFLB_Reorg[5][0]);
   EXPECT_EQ(wltBal[1], TestChain::wltBal_AFLB_Reorg[5][1]);
   EXPECT_EQ(wltBal[2], TestChain::wltBal_AFLB_Reorg[5][2]);
   EXPECT_EQ(wltBal[3], TestChain::wltBal_AFLB_Reorg[5][3]);

   //addr balances
   balances1 = getAddrBalances(bridge_, walletId_BCDE_, accountId_BCDE_);
   ASSERT_EQ(balances1.size(), 4);
   checkBalances(balances1, 5, true);
   balances2 = getAddrBalances(bridge_, walletId_AFLB_, accountId_AFLB_);
   ASSERT_EQ(balances2.size(), 6);
   checkBalances(balances2, 5, true);

   //wlt1 ledgers
   ledgersAtBlocks = getLedgersPage(bridge_, delegateIdWlt1, 0);
   ASSERT_EQ(ledgersAtBlocks.size(), 11);

   for (unsigned i = 0; i < ledgersAtBlocks.size(); i++) {
      EXPECT_TRUE(
         checkLedgers(ledgersAtBlocks[i],
            TestChain::ledgersBCDE_Reorg[i]));
   }

   //wlt2 ledgers
   ledgersAtBlocks = getLedgersPage(bridge_, delegateIdWlt2, 0);
   ASSERT_EQ(ledgersAtBlocks.size(), 10);

   for (unsigned i = 0; i < ledgersAtBlocks.size(); i++) {
      EXPECT_TRUE(
         checkLedgers(ledgersAtBlocks[i],
            TestChain::ledgersAFLB_Reorg[i])) << i;
   }

   //combined ledgers
   ledgersAtBlocks = getLedgersPage(bridge_, delegateId, 0);
   ASSERT_EQ(ledgersAtBlocks.size(), 21);

   combinedLedgers.clear();
   combinedLedgers.insert(combinedLedgers.end(),
      TestChain::ledgersBCDE_Reorg.begin(), TestChain::ledgersBCDE_Reorg.end());
   combinedLedgers.insert(combinedLedgers.end(),
      TestChain::ledgersAFLB_Reorg.begin(), TestChain::ledgersAFLB_Reorg.end());
   ASSERT_EQ(combinedLedgers.size(), 21);
   std::sort(combinedLedgers.begin(), combinedLedgers.end());

   for (unsigned i = 0; i < ledgersAtBlocks.size(); i++) {
      EXPECT_TRUE(
         checkLedgers(ledgersAtBlocks[i],
            combinedLedgers[i])) << i;
   }

   /* address ledgers */

   //A
   ledgersAtBlocks = getLedgersPage(bridge_, delegateScrAddrA, 0);
   ASSERT_EQ(ledgersAtBlocks.size(), 1);
   for (unsigned i = 0; i < ledgersAtBlocks.size(); i++) {
      EXPECT_TRUE(
         checkLedgers(ledgersAtBlocks[i],
            TestChain::ledgersA_Reorg[i]));
   }

   //B
   ledgersAtBlocks = getLedgersPage(bridge_, delegateScrAddrB, 0);
   ASSERT_EQ(ledgersAtBlocks.size(), 5);
   for (unsigned i = 0; i < ledgersAtBlocks.size(); i++) {
      EXPECT_TRUE(
         checkLedgers(ledgersAtBlocks[i],
            TestChain::ledgersB_Reorg[i]));
   }

   //C
   ledgersAtBlocks = getLedgersPage(bridge_, delegateScrAddrC, 0);
   ASSERT_EQ(ledgersAtBlocks.size(), 2);
   for (unsigned i = 0; i < ledgersAtBlocks.size(); i++) {
      EXPECT_TRUE(
         checkLedgers(ledgersAtBlocks[i],
            TestChain::ledgersC_Reorg[i]));
   }

   //D
   ledgersAtBlocks = getLedgersPage(bridge_, delegateScrAddrD, 0);
   ASSERT_EQ(ledgersAtBlocks.size(), 3);
   for (unsigned i = 0; i < ledgersAtBlocks.size(); i++) {
      EXPECT_TRUE(
         checkLedgers(ledgersAtBlocks[i],
            TestChain::ledgersD_Reorg[i]));
   }

   //E
   ledgersAtBlocks = getLedgersPage(bridge_, delegateScrAddrE, 0);
   ASSERT_EQ(ledgersAtBlocks.size(), 2);
   for (unsigned i = 0; i < ledgersAtBlocks.size(); i++) {
      EXPECT_TRUE(
         checkLedgers(ledgersAtBlocks[i],
            TestChain::ledgersE_Reorg[i]));
   }

   //F
   ledgersAtBlocks = getLedgersPage(bridge_, delegateScrAddrF, 0);
   ASSERT_EQ(ledgersAtBlocks.size(), 5);
   for (unsigned i = 0; i < ledgersAtBlocks.size(); i++) {
      EXPECT_TRUE(
         checkLedgers(ledgersAtBlocks[i],
            TestChain::ledgersF_Reorg[i]));
   }

   //LB1
   ledgersAtBlocks = getLedgersPage(bridge_, delegateScrAddrLB1, 0);
   ASSERT_EQ(ledgersAtBlocks.size(), 2);
   for (unsigned i = 0; i < ledgersAtBlocks.size(); i++) {
      EXPECT_TRUE(
         checkLedgers(ledgersAtBlocks[i],
            TestChain::ledgersLB1_Reorg[i]));
   }

   //LB1_P2SH
   ledgersAtBlocks = getLedgersPage(bridge_, delegateP2SHLB1, 0);
   ASSERT_EQ(ledgersAtBlocks.size(), 2);
   for (unsigned i = 0; i < ledgersAtBlocks.size(); i++) {
      EXPECT_TRUE(
         checkLedgers(ledgersAtBlocks[i],
            TestChain::ledgersLB1_P2SH_Reorg[i]));
   }

   //LB2
   ledgersAtBlocks = getLedgersPage(bridge_, delegateScrAddrLB2, 0);
   ASSERT_EQ(ledgersAtBlocks.size(), 3);
   for (unsigned i = 0; i < ledgersAtBlocks.size(); i++) {
      EXPECT_TRUE(
         checkLedgers(ledgersAtBlocks[i],
            TestChain::ledgersLB2_Reorg[i])) << i;
   }

   //LB2_P2SH
   ledgersAtBlocks = getLedgersPage(bridge_, delegateP2SHLB2, 0);
   ASSERT_EQ(ledgersAtBlocks.size(), 2);
   for (unsigned i = 0; i < ledgersAtBlocks.size(); i++) {
      EXPECT_TRUE(
         checkLedgers(ledgersAtBlocks[i],
            TestChain::ledgersLB2_P2SH_Reorg[i]));
   }
}

TEST_F(BridgeChainDataTests, Reorg_SpendBeforeBranchPoint)
{
   /*
   Amazingly enough, our test chain does not provide a case of a reorg
   where an output that lives before the branch point is spent across
   both branches. This test covers that.
   */

   /* start with classic 6 blocks */
   loadWallets({walletId_BCDE_});

   TestUtils::setBlocks({ "0", "1", "2", "3", "4", "5" }, blk0dat_);
   WebSocketServer::initAuthPeers({
      homedir_ / SERVER_AUTH_PEER_FILENAME, authPeersPassLbd_});
   WebSocketServer::start(theBDMt_->bdm(), true);

   ASSERT_TRUE(connectToIp(bridge_, "127.0.0.1", "9001", serverPubkey_));
   ASSERT_TRUE(registerWallets(bridge_));

   //start db, go online and wait on ready notif
   theBDMt_->start(Config::DBSettings::initMode());
   ASSERT_EQ(goOnline(bridge_), 5);

   //check wallet balance
   auto wltBal = getWalletBalance(bridge_, walletId_BCDE_, accountId_BCDE_);
   ASSERT_EQ(wltBal.size(), 4);
   EXPECT_EQ(wltBal[0], TestChain::wltBal_BCDE[5][0]);
   EXPECT_EQ(wltBal[1], TestChain::wltBal_BCDE[5][1]);
   EXPECT_EQ(wltBal[2], TestChain::wltBal_BCDE[5][2]);
   EXPECT_EQ(wltBal[3], TestChain::wltBal_BCDE[5][3]);

   //check address balances
   auto balances = getAddrBalances(bridge_, walletId_BCDE_, accountId_BCDE_);
   ASSERT_EQ(balances.size(), 4);
   checkBalances(balances, 5, false);

   //check ledgers
   auto delegateId = getLedgerDelegateId(bridge_);
   ASSERT_FALSE(delegateId.empty());

   auto pageCount = getLedgersPageCount(bridge_, delegateId);
   EXPECT_EQ(pageCount, 1);

   auto ledgers = getLedgersPage(bridge_, delegateId, 0);
   ASSERT_EQ(ledgers.size(), 15);

   for (unsigned i = 0; i < ledgers.size(); i++) {
      EXPECT_TRUE(checkLedgers(ledgers[i], TestChain::ledgersBCDE[i])) << i;
   }

   /* create 2 txs that spends the same output */
   auto utxos = getUTXOs(bridge_, walletId_BCDE_, accountId_BCDE_, 0);

   //look for output 4|2:0
   UTXO theUtxo;
   for (const auto& utxo : utxos) {
      if (utxo.txHash == TestChain::hash42) {
         theUtxo = utxo;
         break;
      }
   }
   ASSERT_TRUE(theUtxo.isInitialized());

   //txA
   auto recipientPrivKeyA = Cryptography::ECDSA::createNewPrivateKey();
   auto recipientPubKeyA = Cryptography::ECDSA::computePublicKey(
      recipientPrivKeyA, true);
   auto hash160A =  BtcUtils::getHash160(recipientPubKeyA);
   auto recipientAddrA = BtcUtils::scrAddrToSegWitAddress(hash160A);

   auto txA = createAndSignTx(
      bridge_, walletId_BCDE_, accountId_BCDE_,
      { theUtxo }, {{ recipientAddrA, 3 * COIN }},
      TestChain::scrAddrB, 2, false,
      "privPass1"
   );
   ASSERT_FALSE(txA.empty());
   Tx txAObj{txA};

   //txB
   auto recipientPrivKeyB = Cryptography::ECDSA::createNewPrivateKey();
   auto recipientPubKeyB = Cryptography::ECDSA::computePublicKey(
      recipientPrivKeyB, true);
   auto hash160B =  BtcUtils::getHash160(recipientPubKeyB);
   auto recipientAddrB = BtcUtils::scrAddrToSegWitAddress(hash160B);

   auto txB = createAndSignTx(
      bridge_, walletId_BCDE_, accountId_BCDE_,
      { theUtxo }, {{ recipientAddrB, 4 * COIN }},
      TestChain::scrAddrB, 2, false,
      "privPass1"
   );
   ASSERT_FALSE(txB.empty());
   Tx txBObj{txB};
   ASSERT_NE(txAObj.getThisHash(), txBObj.getThisHash());

   //broadcast txA
   broadcastTx(bridge_, txA);
   waitOnZc();

   /* validate effect of zc txA */

   //check wallet balance
   Types::Amount txAAmount = 3UL * COIN + 2UL;

   wltBal = getWalletBalance(bridge_, walletId_BCDE_, accountId_BCDE_);
   ASSERT_EQ(wltBal.size(), 4);
   EXPECT_EQ(wltBal[0], TestChain::wltBal_BCDE[5][0] - txAAmount);
   EXPECT_EQ(wltBal[1], TestChain::wltBal_BCDE[5][1] - 5 * COIN);
   EXPECT_EQ(wltBal[2], TestChain::wltBal_BCDE[5][2] - txAAmount);
   EXPECT_EQ(wltBal[3], TestChain::wltBal_BCDE[5][3] + 1);

   //check address balances
   try {
      auto balances = getAddrBalances(bridge_, walletId_BCDE_, accountId_BCDE_);
      auto scrAddrDBal = balances.at(TestChain::scrAddrD);
      auto testAddrDBal = TestChain::testAddrBalances[5].at(TestChain::scrAddrD);
      EXPECT_EQ(scrAddrDBal[0], testAddrDBal[0] - 5UL * COIN);
      EXPECT_EQ(scrAddrDBal[1], testAddrDBal[1] - 5UL * COIN);
      EXPECT_EQ(scrAddrDBal[2], testAddrDBal[2] - 5UL * COIN);
      EXPECT_EQ(scrAddrDBal[3], testAddrDBal[3] + 1);
   } catch (const std::out_of_range&) {
      ASSERT_TRUE(false);
   }

   //check ledgers
   ledgers = getLedgersPage(bridge_, delegateId, 0);
   ASSERT_EQ(ledgers.size(), 16);

   auto ledger0 = ledgers[0];
   ASSERT_EQ(ledger0.getTxHash(), txAObj.getThisHash());
   ASSERT_EQ(ledger0.getBlockNum(), UINT32_MAX);
   EXPECT_EQ(ledger0.getValue(), Types::Value(txAAmount) * -1L);

   for (unsigned i = 0; i < TestChain::ledgersBCDE.size(); i++) {
      EXPECT_TRUE(checkLedgers(ledgers[i + 1], TestChain::ledgersBCDE[i])) << i;
   }

   //mine txA
   DBTestUtils::mineNewBlock(theBDMt_, TestChain::addrA, 1);
   ASSERT_EQ(waitOnNewBlock(), 6);

   /* validate effect of mined txA */

   //check wallet balance
   wltBal = getWalletBalance(bridge_, walletId_BCDE_, accountId_BCDE_);
   ASSERT_EQ(wltBal.size(), 4);
   EXPECT_EQ(wltBal[0], TestChain::wltBal_BCDE[5][0] - txAAmount);
   EXPECT_EQ(wltBal[1], TestChain::wltBal_BCDE[5][1] - txAAmount);
   EXPECT_EQ(wltBal[2], TestChain::wltBal_BCDE[5][2] - txAAmount);
   EXPECT_EQ(wltBal[3], TestChain::wltBal_BCDE[5][3] + 1);

   //check address balances
   try {
      auto balances = getAddrBalances(bridge_, walletId_BCDE_, accountId_BCDE_);
      auto scrAddrDBal = balances.at(TestChain::scrAddrD);
      auto testAddrDBal = TestChain::testAddrBalances[5].at(TestChain::scrAddrD);
      EXPECT_EQ(scrAddrDBal[0], testAddrDBal[0] - 5UL * COIN);
      EXPECT_EQ(scrAddrDBal[1], testAddrDBal[1] - 5UL * COIN);
      EXPECT_EQ(scrAddrDBal[2], testAddrDBal[2] - 5UL * COIN);
      EXPECT_EQ(scrAddrDBal[3], testAddrDBal[3] + 1);
   } catch (const std::out_of_range&) {
      ASSERT_TRUE(false);
   }

   //check ledgers
   ledgers = getLedgersPage(bridge_, delegateId, 0);
   ASSERT_EQ(ledgers.size(), 16);

   ledger0 = ledgers[0];
   ASSERT_EQ(ledger0.getTxHash(), txAObj.getThisHash());
   ASSERT_EQ(ledger0.getBlockNum(), 6);
   EXPECT_EQ(ledger0.getValue(), Types::Value(txAAmount) * -1L);

   for (unsigned i = 0; i < TestChain::ledgersBCDE.size(); i++) {
      EXPECT_TRUE(checkLedgers(ledgers[i + 1], TestChain::ledgersBCDE[i])) << i;
   }

   /* reorg from block 5 */

   auto branchPoint = theBDMt_->bdm()->blockchain()->getHeaderByHeight(5);
   auto topA = theBDMt_->bdm()->blockchain()->top();
   ASSERT_EQ(branchPoint->getUniqueID(), 6);
   nodePtr_->setReorgBranchPoint(branchPoint);
   nodePtr_->mineNewBlock(theBDMt_->bdm(), 1, TestChain::addrA, 100.0);
   ASSERT_EQ(waitOnNewBlock(), 6);

   auto topB = theBDMt_->bdm()->blockchain()->top();
   ASSERT_NE(topA->getThisHash(), topB->getThisHash());
   ASSERT_EQ(topA->getPrevHash(), branchPoint->getThisHash());
   ASSERT_EQ(topB->getPrevHash(), branchPoint->getThisHash());
   ASSERT_FALSE(topA->isMainBranch());

   //check ledgers
   ledgers = getLedgersPage(bridge_, delegateId, 0);
   ASSERT_EQ(ledgers.size(), 15);

   for (unsigned i = 0; i < ledgers.size(); i++) {
      EXPECT_TRUE(checkLedgers(ledgers[i], TestChain::ledgersBCDE[i])) << i;
   }

   //broadcast txB
   Types::Amount txBAmount = 4UL * COIN + 2UL;
   broadcastTx(bridge_, txB);
   auto zcLedgers = waitOnZc();
   ASSERT_EQ(zcLedgers.size(), 1);
   ASSERT_EQ(zcLedgers[0].getTxHash(), txBObj.getThisHash());
   ASSERT_EQ(zcLedgers[0].getValue(), Types::Value(txBAmount) * -1L);

   /* validate effect of zc txB */

   //check wallet balance
   wltBal = getWalletBalance(bridge_, walletId_BCDE_, accountId_BCDE_);
   ASSERT_EQ(wltBal.size(), 4);
   EXPECT_EQ(wltBal[0], TestChain::wltBal_BCDE[5][0] - txBAmount);
   EXPECT_EQ(wltBal[1], TestChain::wltBal_BCDE[5][1] - 5 * COIN);
   EXPECT_EQ(wltBal[2], TestChain::wltBal_BCDE[5][2] - txBAmount);
   EXPECT_EQ(wltBal[3], TestChain::wltBal_BCDE[5][3] + 1);

   //check address balances
   try {
      auto balances = getAddrBalances(bridge_, walletId_BCDE_, accountId_BCDE_);
      auto scrAddrDBal = balances.at(TestChain::scrAddrD);
      auto testAddrDBal = TestChain::testAddrBalances[5].at(TestChain::scrAddrD);
      EXPECT_EQ(scrAddrDBal[0], testAddrDBal[0] - 5UL * COIN);
      EXPECT_EQ(scrAddrDBal[1], testAddrDBal[1] - 5UL * COIN);
      EXPECT_EQ(scrAddrDBal[2], testAddrDBal[2] - 5UL * COIN);
      EXPECT_EQ(scrAddrDBal[3], testAddrDBal[3] + 1);
   } catch (const std::out_of_range&) {
      ASSERT_TRUE(false);
   }

   //ledgers
   ledgers = getLedgersPage(bridge_, delegateId, 0);
   ASSERT_EQ(ledgers.size(), 16);

   ledger0 = ledgers[0];
   ASSERT_EQ(ledger0.getTxHash(), txBObj.getThisHash());
   ASSERT_EQ(ledger0.getBlockNum(), UINT32_MAX);
   EXPECT_EQ(ledger0.getValue(), Types::Value(txBAmount) * -1L);

   for (unsigned i = 0; i < TestChain::ledgersBCDE.size(); i++) {
      EXPECT_TRUE(checkLedgers(ledgers[i + 1], TestChain::ledgersBCDE[i])) << i;
   }

   //mine txB
   DBTestUtils::mineNewBlock(theBDMt_, TestChain::addrA, 1);
   ASSERT_EQ(waitOnNewBlock(), 7);

   /* validate effect of mined txB */

   //check wallet balance
   wltBal = getWalletBalance(bridge_, walletId_BCDE_, accountId_BCDE_);
   ASSERT_EQ(wltBal.size(), 4);
   EXPECT_EQ(wltBal[0], TestChain::wltBal_BCDE[5][0] - txBAmount);
   EXPECT_EQ(wltBal[1], TestChain::wltBal_BCDE[5][1] - txBAmount);
   EXPECT_EQ(wltBal[2], TestChain::wltBal_BCDE[5][2] - txBAmount);
   EXPECT_EQ(wltBal[3], TestChain::wltBal_BCDE[5][3] + 1);

   //check address balances
   try {
      auto balances = getAddrBalances(bridge_, walletId_BCDE_, accountId_BCDE_);
      auto scrAddrDBal = balances.at(TestChain::scrAddrD);
      auto testAddrDBal = TestChain::testAddrBalances[5].at(TestChain::scrAddrD);
      EXPECT_EQ(scrAddrDBal[0], testAddrDBal[0] - 5UL * COIN);
      EXPECT_EQ(scrAddrDBal[1], testAddrDBal[1] - 5UL * COIN);
      EXPECT_EQ(scrAddrDBal[2], testAddrDBal[2] - 5UL * COIN);
      EXPECT_EQ(scrAddrDBal[3], testAddrDBal[3] + 1);
   } catch (const std::out_of_range&) {
      ASSERT_TRUE(false);
   }

   //ledgers
   ledgers = getLedgersPage(bridge_, delegateId, 0);
   ASSERT_EQ(ledgers.size(), 16);

   ledger0 = ledgers[0];
   ASSERT_EQ(ledger0.getTxHash(), txBObj.getThisHash());
   ASSERT_EQ(ledger0.getBlockNum(), 7);
   EXPECT_EQ(ledger0.getValue(), Types::Value(txBAmount) * -1L);

   for (unsigned i = 0; i < TestChain::ledgersBCDE.size(); i++) {
      EXPECT_TRUE(checkLedgers(ledgers[i + 1], TestChain::ledgersBCDE[i])) << i;
   }
}

////////////////////////////////////////////////////////////////////////////////
// helper data
TEST_F(BridgeChainDataTests, AddressBook)
{
   loadWallets({walletId_BCDE_});

   TestUtils::setBlocks({ "0", "1", "2", "3", "4", "5" }, blk0dat_);
   WebSocketServer::initAuthPeers({
      homedir_ / SERVER_AUTH_PEER_FILENAME, authPeersPassLbd_});
   WebSocketServer::start(theBDMt_->bdm(), true);

   ASSERT_TRUE(connectToIp(bridge_, "127.0.0.1", "9001", serverPubkey_));
   ASSERT_TRUE(registerWallets(bridge_));

   //start db, go online and wait on ready notif
   theBDMt_->start(Config::DBSettings::initMode());
   ASSERT_EQ(goOnline(bridge_), 5);

   //check balances
   auto balances = getAddrBalances(bridge_, walletId_BCDE_, accountId_BCDE_);
   ASSERT_EQ(balances.size(), 4);
   checkBalances(balances, 5, false);

   //check ledgers
   auto delegateId = getLedgerDelegateId(bridge_);
   ASSERT_FALSE(delegateId.empty());

   auto pageCount = getLedgersPageCount(bridge_, delegateId);
   EXPECT_EQ(pageCount, 1);

   auto ledgers = getLedgersPage(bridge_, delegateId, 0);
   ASSERT_EQ(ledgers.size(), 15);

   for (unsigned i = 0; i < ledgers.size(); i++) {
      EXPECT_TRUE(checkLedgers(ledgers[i], TestChain::ledgersBCDE[i])) << i;
   }

   //grab address book
   auto addrBook = getAddressBook(bridge_, walletId_BCDE_, accountId_BCDE_);

   try {
      auto hashes = addrBook.at(TestChain::scrAddrB);
      ASSERT_EQ(hashes.size(), TestChain::ledgersB.size());
      for (const auto& entry : TestChain::ledgersB) {
         ASSERT_NE(hashes.find(entry.txHash), hashes.end());
      }
   } catch (const std::range_error&) {
      ASSERT_TRUE(false);
   }

   try {
      auto hashes = addrBook.at(TestChain::scrAddrC);
      ASSERT_EQ(hashes.size(), TestChain::ledgersC.size());
      for (const auto& entry : TestChain::ledgersC) {
         ASSERT_NE(hashes.find(entry.txHash), hashes.end());
      }
   } catch (const std::range_error&) {
      ASSERT_TRUE(false);
   }

   try {
      auto hashes = addrBook.at(TestChain::scrAddrD);
      ASSERT_EQ(hashes.size(), TestChain::ledgersD.size());
      for (const auto& entry : TestChain::ledgersD) {
         ASSERT_NE(hashes.find(entry.txHash), hashes.end());
      }
   } catch (const std::range_error&) {
      ASSERT_TRUE(false);
   }

   try {
      auto hashes = addrBook.at(TestChain::scrAddrE);
      ASSERT_EQ(hashes.size(), TestChain::ledgersE.size());
      for (const auto& entry : TestChain::ledgersE) {
         ASSERT_NE(hashes.find(entry.txHash), hashes.end());
      }
   } catch (const std::range_error&) {
      ASSERT_TRUE(false);
   }
}

TEST_F(BridgeChainDataTests, getUTXOs)
{
   loadWallets({walletId_BCDE_, walletId_AFLB_});

   TestUtils::setBlocks({ "0", "1", "2", "3", "4", "5" }, blk0dat_);
   WebSocketServer::initAuthPeers({
      homedir_ / SERVER_AUTH_PEER_FILENAME, authPeersPassLbd_});
   WebSocketServer::start(theBDMt_->bdm(), true);

   ASSERT_TRUE(connectToIp(bridge_, "127.0.0.1", "9001", serverPubkey_));
   ASSERT_TRUE(registerWallets(bridge_));

   //start db, go online and wait on ready notif
   theBDMt_->start(Config::DBSettings::initMode());
   ASSERT_EQ(goOnline(bridge_), 5);

   //grab BCDE utxos
   auto utxos = getUTXOs(bridge_, walletId_BCDE_, accountId_BCDE_, 0);
   ASSERT_EQ(utxos.size(), 8);

   //Block 4, tx 2, output 0, 5 COINS
   auto utxo = utxos[0];
   ASSERT_EQ(utxo.getHeight(), 4);
   ASSERT_EQ(utxo.getTxIndex(), 2);
   ASSERT_EQ(utxo.getTxOutIndex(), 0);
   ASSERT_EQ(utxo.getAmount(), 5 * COIN);
   ASSERT_EQ(utxo.getTxHash(), TestChain::hash42);

   //Block 3, tx 4, output 0, 25 COINS
   utxo = utxos[1];
   ASSERT_EQ(utxo.getHeight(), 3);
   ASSERT_EQ(utxo.getTxIndex(), 4);
   ASSERT_EQ(utxo.getTxOutIndex(), 0);
   ASSERT_EQ(utxo.getAmount(), 25 * COIN);
   ASSERT_EQ(utxo.getTxHash(), TestChain::hash34);

   //Block 3, tx 2, output 0, 5 COINS
   utxo = utxos[2];
   ASSERT_EQ(utxo.getHeight(), 3);
   ASSERT_EQ(utxo.getTxIndex(), 2);
   ASSERT_EQ(utxo.getTxOutIndex(), 0);
   ASSERT_EQ(utxo.getAmount(), 5 * COIN);
   ASSERT_EQ(utxo.getTxHash(), TestChain::hash32);

   //Block 5, tx 2, output 0, 5 COINS
   utxo = utxos[3];
   ASSERT_EQ(utxo.getHeight(), 5);
   ASSERT_EQ(utxo.getTxIndex(), 2);
   ASSERT_EQ(utxo.getTxOutIndex(), 0);
   ASSERT_EQ(utxo.getAmount(), 5 * COIN);
   ASSERT_EQ(utxo.getTxHash(), TestChain::hash52);

   //Block 4, tx 3, output 2, 10 COINS
   utxo = utxos[4];
   ASSERT_EQ(utxo.getHeight(), 4);
   ASSERT_EQ(utxo.getTxIndex(), 3);
   ASSERT_EQ(utxo.getTxOutIndex(), 2);
   ASSERT_EQ(utxo.getAmount(), 10 * COIN);
   ASSERT_EQ(utxo.getTxHash(), TestChain::hash43);

   //Block 5, tx 1, output 0, 10 COINS
   utxo = utxos[5];
   ASSERT_EQ(utxo.getHeight(), 5);
   ASSERT_EQ(utxo.getTxIndex(), 1);
   ASSERT_EQ(utxo.getTxOutIndex(), 0);
   ASSERT_EQ(utxo.getAmount(), 10 * COIN);
   ASSERT_EQ(utxo.getTxHash(), TestChain::hash51);

   //Block 5, tx 1, output 1, 20 COINS
   utxo = utxos[6];
   ASSERT_EQ(utxo.getHeight(), 5);
   ASSERT_EQ(utxo.getTxIndex(), 1);
   ASSERT_EQ(utxo.getTxOutIndex(), 1);
   ASSERT_EQ(utxo.getAmount(), 20 * COIN);
   ASSERT_EQ(utxo.getTxHash(), TestChain::hash51);

   //Block 3, tx 1, output 0, 5 COINS
   utxo = utxos[7];
   ASSERT_EQ(utxo.getHeight(), 3);
   ASSERT_EQ(utxo.getTxIndex(), 1);
   ASSERT_EQ(utxo.getTxOutIndex(), 0);
   ASSERT_EQ(utxo.getAmount(), 5 * COIN);
   ASSERT_EQ(utxo.getTxHash(), TestChain::hash31);

   //grab AFLB utxos
   utxos = getUTXOs(bridge_, walletId_AFLB_, accountId_AFLB_, 0);
   ASSERT_EQ(utxos.size(), 5);

   //Block 4, tx 1, output 1, 5 COINS
   utxo = utxos[0];
   ASSERT_EQ(utxo.getHeight(), 4);
   ASSERT_EQ(utxo.getTxIndex(), 1);
   ASSERT_EQ(utxo.getTxOutIndex(), 1);
   ASSERT_EQ(utxo.getAmount(), 5 * COIN);
   ASSERT_EQ(utxo.getTxHash(), TestChain::hash41);

   //Block 4, tx 3, output 0, 25 COINS
   utxo = utxos[1];
   ASSERT_EQ(utxo.getHeight(), 4);
   ASSERT_EQ(utxo.getTxIndex(), 3);
   ASSERT_EQ(utxo.getTxOutIndex(), 0);
   ASSERT_EQ(utxo.getAmount(), 25 * COIN);
   ASSERT_EQ(utxo.getTxHash(), TestChain::hash43);

   //Block 4, tx 3, output 1, 20 COINS
   utxo = utxos[2];
   ASSERT_EQ(utxo.getHeight(), 4);
   ASSERT_EQ(utxo.getTxIndex(), 3);
   ASSERT_EQ(utxo.getTxOutIndex(), 1);
   ASSERT_EQ(utxo.getAmount(), 20 * COIN);
   ASSERT_EQ(utxo.getTxHash(), TestChain::hash43);

   //Block 3, tx 5, output 0, 10 COINS
   utxo = utxos[3];
   ASSERT_EQ(utxo.getHeight(), 3);
   ASSERT_EQ(utxo.getTxIndex(), 5);
   ASSERT_EQ(utxo.getTxOutIndex(), 0);
   ASSERT_EQ(utxo.getAmount(), 10 * COIN);
   ASSERT_EQ(utxo.getTxHash(), TestChain::hash35);

   //Block 3, tx 5, output 1, 5 COINS
   utxo = utxos[4];
   ASSERT_EQ(utxo.getHeight(), 3);
   ASSERT_EQ(utxo.getTxIndex(), 5);
   ASSERT_EQ(utxo.getTxOutIndex(), 1);
   ASSERT_EQ(utxo.getAmount(), 5 * COIN);
   ASSERT_EQ(utxo.getTxHash(), TestChain::hash35);
}

////////////////////////////////////////////////////////////////////////////////
// tx signing & zc broadcast
TEST_F(BridgeChainDataTests, ZeroConf)
{
   loadWallets({walletId_BCDE_});

   TestUtils::setBlocks({ "0", "1", "2", "3", "4", "5" }, blk0dat_);
   WebSocketServer::initAuthPeers({
      homedir_ / SERVER_AUTH_PEER_FILENAME, authPeersPassLbd_});
   WebSocketServer::start(theBDMt_->bdm(), true);

   ASSERT_TRUE(connectToIp(bridge_, "127.0.0.1", "9001", serverPubkey_));
   ASSERT_TRUE(registerWallets(bridge_));

   //start db, go online and wait on ready notif
   theBDMt_->start(Config::DBSettings::initMode());
   ASSERT_EQ(goOnline(bridge_), 5);

   //check wallet balance
   auto wltBal = getWalletBalance(bridge_, walletId_BCDE_, accountId_BCDE_);
   ASSERT_EQ(wltBal.size(), 4);
   EXPECT_EQ(wltBal[0], TestChain::wltBal_BCDE[5][0]);
   EXPECT_EQ(wltBal[1], TestChain::wltBal_BCDE[5][1]);
   EXPECT_EQ(wltBal[2], TestChain::wltBal_BCDE[5][2]);
   EXPECT_EQ(wltBal[3], TestChain::wltBal_BCDE[5][3]);

   //check addr balances
   auto balances = getAddrBalances(bridge_, walletId_BCDE_, accountId_BCDE_);
   ASSERT_EQ(balances.size(), 4);
   checkBalances(balances, 5, false);

   //setup wallet ledger delegate
   auto delegateId = getLedgerDelegateId(bridge_);
   ASSERT_FALSE(delegateId.empty());
   auto pageCount = getLedgersPageCount(bridge_, delegateId);
   EXPECT_EQ(pageCount, 1);

   //setup address ledger delegates
   auto delegateScrAddrB = getLedgerDelegateIdForScrAddr(bridge_,
      walletId_BCDE_, accountId_BCDE_, TestChain::scrAddrB);
   ASSERT_FALSE(delegateScrAddrB.empty());
   auto delegateScrAddrC = getLedgerDelegateIdForScrAddr(bridge_,
      walletId_BCDE_, accountId_BCDE_, TestChain::scrAddrC);
   ASSERT_FALSE(delegateScrAddrC.empty());
   auto delegateScrAddrD = getLedgerDelegateIdForScrAddr(bridge_,
      walletId_BCDE_, accountId_BCDE_, TestChain::scrAddrD);
   ASSERT_FALSE(delegateScrAddrD.empty());
   auto delegateScrAddrE = getLedgerDelegateIdForScrAddr(bridge_,
      walletId_BCDE_, accountId_BCDE_, TestChain::scrAddrE);
   ASSERT_FALSE(delegateScrAddrE.empty());

   pageCount = getLedgersPageCount(bridge_, delegateScrAddrB);
   EXPECT_EQ(pageCount, 1);
   pageCount = getLedgersPageCount(bridge_, delegateScrAddrC);
   EXPECT_EQ(pageCount, 1);
   pageCount = getLedgersPageCount(bridge_, delegateScrAddrD);
   EXPECT_EQ(pageCount, 1);
   pageCount = getLedgersPageCount(bridge_, delegateScrAddrE);
   EXPECT_EQ(pageCount, 1);

   //check wallet ledgers
   auto ledgers = getLedgersPage(bridge_, delegateId, 0);
   ASSERT_EQ(ledgers.size(), 15);

   for (unsigned i = 0; i < ledgers.size(); i++) {
      EXPECT_TRUE(checkLedgers(ledgers[i], TestChain::ledgersBCDE[i])) << i;
   }

   /* address ledgers */

   //B
   auto ledgersAtBlocks = getLedgersPage(bridge_, delegateScrAddrB, 0);
   ASSERT_EQ(ledgersAtBlocks.size(), 7);
   for (unsigned i = 0; i < ledgersAtBlocks.size(); i++) {
      EXPECT_TRUE(
         checkLedgers(ledgersAtBlocks[i],
            TestChain::ledgersB[i]));
   }

   //C
   ledgersAtBlocks = getLedgersPage(bridge_, delegateScrAddrC, 0);
   ASSERT_EQ(ledgersAtBlocks.size(), 4);
   for (unsigned i = 0; i < ledgersAtBlocks.size(); i++) {
      EXPECT_TRUE(
         checkLedgers(ledgersAtBlocks[i],
            TestChain::ledgersC[i]));
   }

   //D
   ledgersAtBlocks = getLedgersPage(bridge_, delegateScrAddrD, 0);
   ASSERT_EQ(ledgersAtBlocks.size(), 4);
   for (unsigned i = 0; i < ledgersAtBlocks.size(); i++) {
      EXPECT_TRUE(
         checkLedgers(ledgersAtBlocks[i],
            TestChain::ledgersD[i]));
   }

   //E
   ledgersAtBlocks = getLedgersPage(bridge_, delegateScrAddrE, 0);
   ASSERT_EQ(ledgersAtBlocks.size(), 2);
   for (unsigned i = 0; i < ledgersAtBlocks.size(); i++) {
      EXPECT_TRUE(
         checkLedgers(ledgersAtBlocks[i],
            TestChain::ledgersE[i]));
   }

   //create recipient address
   auto recipientPrivKey = Cryptography::ECDSA::createNewPrivateKey();
   auto recipientPubKey = Cryptography::ECDSA::computePublicKey(
      recipientPrivKey, true);
   auto hash160 =  BtcUtils::getHash160(recipientPubKey);
   auto recipientAddr = BtcUtils::scrAddrToSegWitAddress(hash160);

   //get signed tx
   BinaryData signedTx;
   try {
      signedTx = createAndSignTx(bridge_,
         walletId_BCDE_, accountId_BCDE_,
         {}, {{ recipientAddr, 11 * COIN }},
         TestChain::scrAddrA, 2, false,
         "privPass1"
      );
   } catch (const std::exception& e) {
      ASSERT_TRUE(false) << e.what();
   }
   ASSERT_FALSE(signedTx.empty());

   //broadcast
   broadcastTx(bridge_, signedTx);

   //wait on zc notif
   Tx tx(signedTx);
   try {
      auto zcLedgers = waitOnZc();
      ASSERT_EQ(zcLedgers.size(), 1);
      ASSERT_EQ(zcLedgers[0].getTxHash(), tx.getThisHash());
      ASSERT_EQ(zcLedgers[0].getValue(), -20 * (int64_t)COIN);
      ASSERT_EQ(zcLedgers[0].getBlockNum(), UINT32_MAX);
   } catch (const std::exception&) {
      ASSERT_TRUE(false);
   }

   //check wallet balance
   wltBal = getWalletBalance(bridge_, walletId_BCDE_, accountId_BCDE_);
   ASSERT_EQ(wltBal.size(), 4);
   EXPECT_EQ(wltBal[0], TestChain::wltBal_BCDE[5][0] - (20 * COIN));
   EXPECT_EQ(wltBal[1], TestChain::wltBal_BCDE[5][1] - (20 * COIN));
   EXPECT_EQ(wltBal[2], TestChain::wltBal_BCDE[5][2] - (20 * COIN));
   EXPECT_EQ(wltBal[3], TestChain::wltBal_BCDE[5][3] + 1);

   //check addr balances
   balances = getAddrBalances(bridge_, walletId_BCDE_, accountId_BCDE_);
   ASSERT_EQ(balances.size(), 4);
   auto addrBBal = balances.at(TestChain::scrAddrB);
   auto testAddrBBal = TestChain::testAddrBalances[5].at(TestChain::scrAddrB);
   EXPECT_EQ(addrBBal[0], testAddrBBal[0] - (20 * COIN));
   EXPECT_EQ(addrBBal[1], testAddrBBal[1] - (20 * COIN));
   EXPECT_EQ(addrBBal[2], testAddrBBal[2] - (20 * COIN));
   EXPECT_EQ(addrBBal[3], testAddrBBal[3] + 1);

   //check wallet ledgers
   ledgers = getLedgersPage(bridge_, delegateId, 0);
   ASSERT_EQ(ledgers.size(), 16);
   for (unsigned i = 0; i < 15; i++) {
      EXPECT_TRUE(checkLedgers(ledgers[i+1], TestChain::ledgersBCDE[i])) << i;
   }

   auto lastEntry = ledgers[0];
   ASSERT_EQ(lastEntry.getTxHash(), tx.getThisHash());
   ASSERT_EQ(lastEntry.getValue(), -20 * (int64_t)COIN);
   ASSERT_EQ(lastEntry.getBlockNum(), UINT32_MAX);

   /* address ledgers */

   //B
   ledgersAtBlocks = getLedgersPage(bridge_, delegateScrAddrB, 0);
   ASSERT_EQ(ledgersAtBlocks.size(), 8);
   for (unsigned i = 0; i < TestChain::ledgersB.size(); i++) {
      EXPECT_TRUE(
         checkLedgers(ledgersAtBlocks[i+1],
            TestChain::ledgersB[i]));
   }
   lastEntry = ledgersAtBlocks[0];
   ASSERT_EQ(lastEntry.getTxHash(), tx.getThisHash());
   ASSERT_EQ(lastEntry.getValue(), -20 * (int64_t)COIN);
   ASSERT_EQ(lastEntry.getBlockNum(), UINT32_MAX);

   /* mine the tx */
   DBTestUtils::mineNewBlock(theBDMt_, TestChain::addrA, 1);
   ASSERT_EQ(waitOnNewBlock(), 6);

   //check wallet balance
   wltBal = getWalletBalance(bridge_, walletId_BCDE_, accountId_BCDE_);
   ASSERT_EQ(wltBal.size(), 4);
   EXPECT_EQ(wltBal[0], TestChain::wltBal_BCDE[5][0] - (20 * COIN));
   EXPECT_EQ(wltBal[1], TestChain::wltBal_BCDE[5][1] - (20 * COIN));
   EXPECT_EQ(wltBal[2], TestChain::wltBal_BCDE[5][2] - (20 * COIN));
   EXPECT_EQ(wltBal[3], TestChain::wltBal_BCDE[5][3] + 1);

   //check addr balances again
   balances = getAddrBalances(bridge_, walletId_BCDE_, accountId_BCDE_);
   ASSERT_EQ(balances.size(), 4);
   addrBBal = balances.at(TestChain::scrAddrB);
   testAddrBBal = TestChain::testAddrBalances[5].at(TestChain::scrAddrB);
   EXPECT_EQ(addrBBal[0], testAddrBBal[0] - (20 * COIN));
   EXPECT_EQ(addrBBal[1], testAddrBBal[1] - (20 * COIN));
   EXPECT_EQ(addrBBal[2], testAddrBBal[2] - (20 * COIN));
   EXPECT_EQ(addrBBal[3], testAddrBBal[3] + 1);

   //check ledgers
   ledgers = getLedgersPage(bridge_, delegateId, 0);
   ASSERT_EQ(ledgers.size(), 16);
   for (unsigned i = 0; i < 15; i++) {
      EXPECT_TRUE(checkLedgers(ledgers[i+1], TestChain::ledgersBCDE[i])) << i;
   }

   lastEntry = ledgers[0];
   ASSERT_EQ(lastEntry.getTxHash(), tx.getThisHash());
   ASSERT_EQ(lastEntry.getValue(), -20 * (int64_t)COIN);
   ASSERT_EQ(lastEntry.getBlockNum(), 6);

   //B
   ledgersAtBlocks = getLedgersPage(bridge_, delegateScrAddrB, 0);
   ASSERT_EQ(ledgersAtBlocks.size(), 8);
   for (unsigned i = 0; i < TestChain::ledgersB.size(); i++) {
      EXPECT_TRUE(
         checkLedgers(ledgersAtBlocks[i+1],
            TestChain::ledgersB[i]));
   }
   lastEntry = ledgersAtBlocks[0];
   ASSERT_EQ(lastEntry.getTxHash(), tx.getThisHash());
   ASSERT_EQ(lastEntry.getValue(), -20 * (int64_t)COIN);
   ASSERT_EQ(lastEntry.getBlockNum(), 6);
}

TEST_F(BridgeChainDataTests, ZeroConf_Replace)
{
   loadWallets({walletId_BCDE_});

   TestUtils::setBlocks({ "0", "1", "2", "3", "4", "5" }, blk0dat_);
   WebSocketServer::initAuthPeers({
      homedir_ / SERVER_AUTH_PEER_FILENAME, authPeersPassLbd_});
   WebSocketServer::start(theBDMt_->bdm(), true);

   ASSERT_TRUE(connectToIp(bridge_, "127.0.0.1", "9001", serverPubkey_));
   ASSERT_TRUE(registerWallets(bridge_));

   //start db, go online and wait on ready notif
   theBDMt_->start(Config::DBSettings::initMode());
   ASSERT_EQ(goOnline(bridge_), 5);

   //check balances
   auto balances = getAddrBalances(bridge_, walletId_BCDE_, accountId_BCDE_);
   ASSERT_EQ(balances.size(), 4);
   checkBalances(balances, 5, false);

   //check ledgers
   auto delegateId = getLedgerDelegateId(bridge_);
   ASSERT_FALSE(delegateId.empty());

   auto pageCount = getLedgersPageCount(bridge_, delegateId);
   EXPECT_EQ(pageCount, 1);

   auto ledgers = getLedgersPage(bridge_, delegateId, 0);
   ASSERT_EQ(ledgers.size(), 15);

   for (unsigned i = 0; i < ledgers.size(); i++) {
      EXPECT_TRUE(checkLedgers(ledgers[i], TestChain::ledgersBCDE[i])) << i;
   }

   //create recipient address
   auto recipientPrivKey = Cryptography::ECDSA::createNewPrivateKey();
   auto recipientPubKey = Cryptography::ECDSA::computePublicKey(
      recipientPrivKey, true);
   auto hash160 =  BtcUtils::getHash160(recipientPubKey);
   auto recipientAddr = BtcUtils::scrAddrToSegWitAddress(hash160);

   //get signed tx
   BinaryData signedTx;
   try {
      signedTx = createAndSignTx(bridge_,
         walletId_BCDE_, accountId_BCDE_,
         {}, {{ recipientAddr, 11 * COIN }},
         TestChain::scrAddrA, 2, true,
         "privPass1"
      );
   } catch (const std::exception& e) {
      ASSERT_TRUE(false) << e.what();
   }
   ASSERT_FALSE(signedTx.empty());

   //broadcast
   broadcastTx(bridge_, signedTx);

   //wait on zc notif
   Tx tx(signedTx);
   try {
      auto zcLedgers = waitOnZc();
      ASSERT_EQ(zcLedgers.size(), 1);
      ASSERT_EQ(zcLedgers[0].getTxHash(), tx.getThisHash());
      EXPECT_EQ(zcLedgers[0].getValue(), -20 * (int64_t)COIN);
      ASSERT_EQ(zcLedgers[0].getBlockNum(), UINT32_MAX);
   } catch (const std::exception&) {
      ASSERT_TRUE(false);
   }

   //check balances
   balances = getAddrBalances(bridge_, walletId_BCDE_, accountId_BCDE_);
   ASSERT_EQ(balances.size(), 4);
   auto addrBBal = balances.at(TestChain::scrAddrB);
   auto testAddrBBal = TestChain::testAddrBalances[5].at(TestChain::scrAddrB);
   EXPECT_EQ(addrBBal[0], testAddrBBal[0] - (20 * COIN));
   EXPECT_EQ(addrBBal[1], testAddrBBal[1] - (20 * COIN));
   EXPECT_EQ(addrBBal[2], testAddrBBal[2] - (20 * COIN));

   //check ledgers
   ledgers = getLedgersPage(bridge_, delegateId, 0);
   ASSERT_EQ(ledgers.size(), 16);
   for (unsigned i = 0; i < 15; i++) {
      EXPECT_TRUE(checkLedgers(ledgers[i+1], TestChain::ledgersBCDE[i])) << i;
   }

   auto lastEntry = ledgers[0];
   ASSERT_EQ(lastEntry.getTxHash(), tx.getThisHash());
   EXPECT_EQ(lastEntry.getValue(), -20 * (int64_t)COIN);
   ASSERT_EQ(lastEntry.getBlockNum(), UINT32_MAX);

   //get rbf UTXOs
   auto rbfUtxos = getUTXOs(bridge_, walletId_BCDE_, accountId_BCDE_, 2);
   ASSERT_EQ(rbfUtxos.size(), 1);

   /* RBF fee
      There is no actual network requirement for this value. The test suite's
      mocked node rejects RBFs with less than 100000000 satoshis in fees
      as convention.
      This has no bearing on mainnet operations and only serves as an easy
      trick for test purposes. Do not set actual RBF fee to such ridiculous
      amount on the mainnet!
   */
   uint64_t rbfFee = 110000000;
   BinaryData signedTxRbf;
   try {
      //respend the output
      signedTxRbf = createAndSignTx(bridge_,
         walletId_BCDE_, accountId_BCDE_,
         rbfUtxos, {{ recipientAddr, 13 * COIN }},
         TestChain::scrAddrE, rbfFee, true,
         "privPass1"
      );
   } catch (const std::exception& e) {
      ASSERT_TRUE(false) << e.what();
   }
   ASSERT_FALSE(signedTxRbf.empty());

   //broadcast
   broadcastTx(bridge_, signedTxRbf);

   //wait on zc notif
   int64_t changeVal = 7 * COIN - rbfFee;
   int64_t totalDiff = -20 * (int64_t)COIN + changeVal;
   Tx txRbf(signedTxRbf);
   try {
      auto zcLedgers = waitOnZc();
      ASSERT_EQ(zcLedgers.size(), 1);
      ASSERT_EQ(zcLedgers[0].getTxHash(), txRbf.getThisHash());
      EXPECT_EQ(zcLedgers[0].getValue(), totalDiff);
      ASSERT_EQ(zcLedgers[0].getBlockNum(), UINT32_MAX);

      auto invalidatedHashes = waitOnInvalidatedZCs();
      ASSERT_EQ(invalidatedHashes.size(), 1);
      ASSERT_EQ(*invalidatedHashes.begin(), tx.getThisHash());
   } catch (const std::exception& e) {
      ASSERT_TRUE(false) << e.what();
   }

   //check balances
   balances = getAddrBalances(bridge_, walletId_BCDE_, accountId_BCDE_);
   ASSERT_EQ(balances.size(), 4);
   addrBBal = balances.at(TestChain::scrAddrB);
   testAddrBBal = TestChain::testAddrBalances[5].at(TestChain::scrAddrB);
   EXPECT_EQ(addrBBal[0], testAddrBBal[0] -20 * (int64_t)COIN);
   EXPECT_EQ(addrBBal[1], testAddrBBal[1] -20 * (int64_t)COIN);
   EXPECT_EQ(addrBBal[2], testAddrBBal[2] -20 * (int64_t)COIN);

   addrBBal = balances.at(TestChain::scrAddrE);
   testAddrBBal = TestChain::testAddrBalances[5].at(TestChain::scrAddrE);
   EXPECT_EQ(addrBBal[0], testAddrBBal[0] + changeVal);
   EXPECT_EQ(addrBBal[1], testAddrBBal[1]);
   EXPECT_EQ(addrBBal[2], testAddrBBal[2] + changeVal);

   //check ledgers
   ledgers = getLedgersPage(bridge_, delegateId, 0);
   ASSERT_EQ(ledgers.size(), 16);
   for (unsigned i = 0; i < 15; i++) {
      EXPECT_TRUE(checkLedgers(ledgers[i+1], TestChain::ledgersBCDE[i])) << i;
   }

   lastEntry = ledgers[0];
   ASSERT_EQ(lastEntry.getTxHash(), txRbf.getThisHash());
   EXPECT_EQ(lastEntry.getValue(), totalDiff);
   ASSERT_EQ(lastEntry.getBlockNum(), UINT32_MAX);

   /* mine the rbf */
   DBTestUtils::mineNewBlock(theBDMt_, TestChain::addrA, 1);
   ASSERT_EQ(waitOnNewBlock(), 6);

   //check balances again
   balances = getAddrBalances(bridge_, walletId_BCDE_, accountId_BCDE_);
   ASSERT_EQ(balances.size(), 4);
   addrBBal = balances.at(TestChain::scrAddrB);
   testAddrBBal = TestChain::testAddrBalances[5].at(TestChain::scrAddrB);
   EXPECT_EQ(addrBBal[0], testAddrBBal[0] - (20 * COIN));
   EXPECT_EQ(addrBBal[1], testAddrBBal[1] - (20 * COIN));
   EXPECT_EQ(addrBBal[2], testAddrBBal[2] - (20 * COIN));

   addrBBal = balances.at(TestChain::scrAddrE);
   testAddrBBal = TestChain::testAddrBalances[5].at(TestChain::scrAddrE);
   EXPECT_EQ(addrBBal[0], testAddrBBal[0] + changeVal);
   EXPECT_EQ(addrBBal[1], testAddrBBal[1] + changeVal);
   EXPECT_EQ(addrBBal[2], testAddrBBal[2] + changeVal);

   //check ledgers
   ledgers = getLedgersPage(bridge_, delegateId, 0);
   ASSERT_EQ(ledgers.size(), 16);
   for (unsigned i = 0; i < 15; i++) {
      EXPECT_TRUE(checkLedgers(ledgers[i+1], TestChain::ledgersBCDE[i])) << i;
   }

   lastEntry = ledgers[0];
   ASSERT_EQ(lastEntry.getTxHash(), txRbf.getThisHash());
   ASSERT_EQ(lastEntry.getValue(), totalDiff);
   ASSERT_EQ(lastEntry.getBlockNum(), 6);
}

TEST_F(BridgeChainDataTests, ZeroConf_Chain)
{
   loadWallets({walletId_BCDE_});

   TestUtils::setBlocks({ "0", "1", "2", "3", "4", "5" }, blk0dat_);
   WebSocketServer::initAuthPeers({
      homedir_ / SERVER_AUTH_PEER_FILENAME, authPeersPassLbd_});
   WebSocketServer::start(theBDMt_->bdm(), true);

   ASSERT_TRUE(connectToIp(bridge_, "127.0.0.1", "9001", serverPubkey_));
   ASSERT_TRUE(registerWallets(bridge_));

   //start db, go online and wait on ready notif
   theBDMt_->start(Config::DBSettings::initMode());
   ASSERT_EQ(goOnline(bridge_), 5);

   //check balances
   auto balances = getAddrBalances(bridge_, walletId_BCDE_, accountId_BCDE_);
   ASSERT_EQ(balances.size(), 4);
   checkBalances(balances, 5, false);

   //check ledgers
   auto delegateId = getLedgerDelegateId(bridge_);
   ASSERT_FALSE(delegateId.empty());

   auto pageCount = getLedgersPageCount(bridge_, delegateId);
   EXPECT_EQ(pageCount, 1);

   auto ledgers = getLedgersPage(bridge_, delegateId, 0);
   ASSERT_EQ(ledgers.size(), 15);

   for (unsigned i = 0; i < ledgers.size(); i++) {
      EXPECT_TRUE(checkLedgers(ledgers[i], TestChain::ledgersBCDE[i])) << i;
   }

   //create recipient address
   auto recipientPrivKey = Cryptography::ECDSA::createNewPrivateKey();
   auto recipientPubKey = Cryptography::ECDSA::computePublicKey(
      recipientPrivKey, true);
   auto hash160 =  BtcUtils::getHash160(recipientPubKey);
   auto recipientAddr1 = BtcUtils::scrAddrToSegWitAddress(hash160);

   //get signed tx
   BinaryData signedTx;
   try {
      signedTx = createAndSignTx(bridge_,
         walletId_BCDE_, accountId_BCDE_,
         {}, {{ recipientAddr1, 11 * COIN }},
         TestChain::scrAddrC, 2, true,
         "privPass1"
      );
   } catch (const std::exception& e) {
      ASSERT_TRUE(false) << e.what();
   }
   ASSERT_FALSE(signedTx.empty());

   //broadcast
   broadcastTx(bridge_, signedTx);

   //grab amount for change output
   Tx tx(signedTx);
   auto changeOutput = tx.getTxOutCopy(1);
   uint64_t changeAmount = changeOutput.getAmount();
   EXPECT_TRUE(changeAmount > 8 * COIN);
   EXPECT_TRUE(changeAmount < 9 * COIN);

   //wait on zc notif
   try {
      auto zcLedgers = waitOnZc();
      ASSERT_EQ(zcLedgers.size(), 1);
      ASSERT_EQ(zcLedgers[0].getTxHash(), tx.getThisHash());
      EXPECT_EQ(zcLedgers[0].getValue(), -20 * (int64_t)COIN + changeAmount);
      ASSERT_EQ(zcLedgers[0].getBlockNum(), UINT32_MAX);
   } catch (const std::exception&) {
      ASSERT_TRUE(false);
   }

   //check balances
   balances = getAddrBalances(bridge_, walletId_BCDE_, accountId_BCDE_);
   ASSERT_EQ(balances.size(), 4);
   auto addrBBal = balances.at(TestChain::scrAddrB);
   auto testAddrBBal = TestChain::testAddrBalances[5].at(TestChain::scrAddrB);
   EXPECT_EQ(addrBBal[0], testAddrBBal[0] - (20 * COIN));
   EXPECT_EQ(addrBBal[1], testAddrBBal[1] - (20 * COIN));
   EXPECT_EQ(addrBBal[2], testAddrBBal[2] - (20 * COIN));

   addrBBal = balances.at(TestChain::scrAddrC);
   testAddrBBal = TestChain::testAddrBalances[5].at(TestChain::scrAddrC);
   EXPECT_EQ(addrBBal[0], testAddrBBal[0] + changeAmount);
   EXPECT_EQ(addrBBal[1], testAddrBBal[1]);
   EXPECT_EQ(addrBBal[2], testAddrBBal[2] + changeAmount);

   //check ledgers
   ledgers = getLedgersPage(bridge_, delegateId, 0);
   ASSERT_EQ(ledgers.size(), 16);
   for (unsigned i = 0; i < 15; i++) {
      EXPECT_TRUE(checkLedgers(ledgers[i+1], TestChain::ledgersBCDE[i])) << i;
   }

   auto lastEntry = ledgers[0];
   ASSERT_EQ(lastEntry.getTxHash(), tx.getThisHash());
   EXPECT_EQ(lastEntry.getValue(), -20 * (int64_t)COIN + changeAmount);
   ASSERT_EQ(lastEntry.getBlockNum(), UINT32_MAX);

   /* second zc */

   //get ZC utxo
   auto zcUtxos = getUTXOs(bridge_, walletId_BCDE_, accountId_BCDE_, 1);
   ASSERT_EQ(zcUtxos.size(), 1);

   //create second recipient address
   recipientPrivKey = Cryptography::ECDSA::createNewPrivateKey();
   recipientPubKey = Cryptography::ECDSA::computePublicKey(
      recipientPrivKey, true);
   hash160 =  BtcUtils::getHash160(recipientPubKey);
   auto recipientAddr2 = BtcUtils::scrAddrToSegWitAddress(hash160);

   //spend the zc utxo
   BinaryData signedTx2;
   try {
      signedTx2 = createAndSignTx(bridge_,
         walletId_BCDE_, accountId_BCDE_,
         zcUtxos, {{ recipientAddr2, 5 * COIN }},
         TestChain::scrAddrC, 1000, true,
         "privPass1"
      );
   } catch (const std::exception& e) {
      ASSERT_TRUE(false) << e.what();
   }
   ASSERT_FALSE(signedTx2.empty());

   Tx tx2(signedTx2);
   auto changeOutput2 = tx2.getTxOutCopy(1);
   uint64_t changeAmount2 = changeOutput2.getAmount();
   EXPECT_TRUE(changeAmount2 > 3 * COIN);
   EXPECT_TRUE(changeAmount2 < 4 * COIN);

   //broadcast & wait on zc notif
   broadcastTx(bridge_, signedTx2);
   try {
      auto zcLedgers = waitOnZc();
      ASSERT_EQ(zcLedgers.size(), 2);
      ASSERT_EQ(zcLedgers[0].getTxHash(), tx2.getThisHash());
      EXPECT_EQ(zcLedgers[0].getValue(), -changeAmount + changeAmount2);
      ASSERT_EQ(zcLedgers[0].getBlockNum(), UINT32_MAX);
      EXPECT_TRUE(zcLedgers[0].isChainedZC());

      ASSERT_EQ(zcLedgers[1].getTxHash(), tx.getThisHash());
      EXPECT_EQ(zcLedgers[1].getValue(), -20 * (int64_t)COIN + changeAmount);
      ASSERT_EQ(zcLedgers[1].getBlockNum(), UINT32_MAX);
      EXPECT_FALSE(zcLedgers[1].isChainedZC());
   } catch (const std::exception&) {
      ASSERT_TRUE(false);
   }

   //check balances
   balances = getAddrBalances(bridge_, walletId_BCDE_, accountId_BCDE_);
   ASSERT_EQ(balances.size(), 4);
   addrBBal = balances.at(TestChain::scrAddrB);
   testAddrBBal = TestChain::testAddrBalances[5].at(TestChain::scrAddrB);
   EXPECT_EQ(addrBBal[0], testAddrBBal[0] - (20 * COIN));
   EXPECT_EQ(addrBBal[1], testAddrBBal[1] - (20 * COIN));
   EXPECT_EQ(addrBBal[2], testAddrBBal[2] - (20 * COIN));

   addrBBal = balances.at(TestChain::scrAddrC);
   testAddrBBal = TestChain::testAddrBalances[5].at(TestChain::scrAddrC);
   EXPECT_EQ(addrBBal[0], testAddrBBal[0] + changeAmount2);
   EXPECT_EQ(addrBBal[1], testAddrBBal[1]);
   EXPECT_EQ(addrBBal[2], testAddrBBal[2] + changeAmount2);

   //check ledgers
   ledgers = getLedgersPage(bridge_, delegateId, 0);
   ASSERT_EQ(ledgers.size(), 17);
   for (unsigned i = 0; i < 15; i++) {
      EXPECT_TRUE(checkLedgers(ledgers[i+2], TestChain::ledgersBCDE[i])) << i;
   }

   ASSERT_EQ(ledgers[1].getTxHash(), tx.getThisHash());
   EXPECT_EQ(ledgers[1].getValue(), -20 * (int64_t)COIN + changeAmount);
   ASSERT_EQ(ledgers[1].getBlockNum(), UINT32_MAX);
   EXPECT_FALSE(ledgers[1].isChainedZC());

   ASSERT_EQ(ledgers[0].getTxHash(), tx2.getThisHash());
   EXPECT_EQ(ledgers[0].getValue(), -changeAmount + changeAmount2);
   ASSERT_EQ(ledgers[0].getBlockNum(), UINT32_MAX);
   EXPECT_TRUE(ledgers[0].isChainedZC());

   /* mine the 2 tx */
   DBTestUtils::mineNewBlock(theBDMt_, TestChain::addrA, 1);
   ASSERT_EQ(waitOnNewBlock(), 6);

   //check balances
   balances = getAddrBalances(bridge_, walletId_BCDE_, accountId_BCDE_);
   ASSERT_EQ(balances.size(), 4);
   addrBBal = balances.at(TestChain::scrAddrB);
   testAddrBBal = TestChain::testAddrBalances[5].at(TestChain::scrAddrB);
   EXPECT_EQ(addrBBal[0], testAddrBBal[0] - (20 * COIN));
   EXPECT_EQ(addrBBal[1], testAddrBBal[1] - (20 * COIN));
   EXPECT_EQ(addrBBal[2], testAddrBBal[2] - (20 * COIN));

   addrBBal = balances.at(TestChain::scrAddrC);
   testAddrBBal = TestChain::testAddrBalances[5].at(TestChain::scrAddrC);
   EXPECT_EQ(addrBBal[0], testAddrBBal[0] + changeAmount2);
   EXPECT_EQ(addrBBal[1], testAddrBBal[1] + changeAmount2);
   EXPECT_EQ(addrBBal[2], testAddrBBal[2] + changeAmount2);

   //check ledgers
   ledgers = getLedgersPage(bridge_, delegateId, 0);
   ASSERT_EQ(ledgers.size(), 17);
   for (unsigned i = 0; i < 15; i++) {
      EXPECT_TRUE(checkLedgers(ledgers[i+2], TestChain::ledgersBCDE[i])) << i;
   }

   ASSERT_EQ(ledgers[1].getTxHash(), tx.getThisHash());
   EXPECT_EQ(ledgers[1].getValue(), -20 * (int64_t)COIN + changeAmount);
   ASSERT_EQ(ledgers[1].getBlockNum(), 6);
   EXPECT_FALSE(ledgers[1].isChainedZC());

   ASSERT_EQ(ledgers[0].getTxHash(), tx2.getThisHash());
   EXPECT_EQ(ledgers[0].getValue(), -changeAmount + changeAmount2);
   ASSERT_EQ(ledgers[0].getBlockNum(), 6);
   EXPECT_FALSE(ledgers[0].isChainedZC());
}

TEST_F(BridgeChainDataTests, ZeroConf_StaggeredChain)
{
   loadWallets({walletId_BCDE_});

   TestUtils::setBlocks({ "0", "1", "2", "3", "4", "5" }, blk0dat_);
   WebSocketServer::initAuthPeers({
      homedir_ / SERVER_AUTH_PEER_FILENAME, authPeersPassLbd_});
   WebSocketServer::start(theBDMt_->bdm(), true);

   ASSERT_TRUE(connectToIp(bridge_, "127.0.0.1", "9001", serverPubkey_));
   ASSERT_TRUE(registerWallets(bridge_));

   //start db, go online and wait on ready notif
   theBDMt_->start(Config::DBSettings::initMode());
   ASSERT_EQ(goOnline(bridge_), 5);

   //check wallet balance
   auto wltBal = getWalletBalance(bridge_, walletId_BCDE_, accountId_BCDE_);
   ASSERT_EQ(wltBal.size(), 4);
   EXPECT_EQ(wltBal[0], TestChain::wltBal_BCDE[5][0]);
   EXPECT_EQ(wltBal[1], TestChain::wltBal_BCDE[5][1]);
   EXPECT_EQ(wltBal[2], TestChain::wltBal_BCDE[5][2]);
   EXPECT_EQ(wltBal[3], TestChain::wltBal_BCDE[5][3]);

   //check addr balances
   auto balances = getAddrBalances(bridge_, walletId_BCDE_, accountId_BCDE_);
   ASSERT_EQ(balances.size(), 4);
   checkBalances(balances, 5, false);

   //grab wallet ledger delegates
   auto delegateId = getLedgerDelegateId(bridge_);
   ASSERT_FALSE(delegateId.empty());
   auto pageCount = getLedgersPageCount(bridge_, delegateId);
   EXPECT_EQ(pageCount, 1);

   //setup address ledger delegates
   auto delegateScrAddrB = getLedgerDelegateIdForScrAddr(bridge_,
      walletId_BCDE_, accountId_BCDE_, TestChain::scrAddrB);
   ASSERT_FALSE(delegateScrAddrB.empty());
   auto delegateScrAddrC = getLedgerDelegateIdForScrAddr(bridge_,
      walletId_BCDE_, accountId_BCDE_, TestChain::scrAddrC);
   ASSERT_FALSE(delegateScrAddrC.empty());
   auto delegateScrAddrD = getLedgerDelegateIdForScrAddr(bridge_,
      walletId_BCDE_, accountId_BCDE_, TestChain::scrAddrD);
   ASSERT_FALSE(delegateScrAddrD.empty());
   auto delegateScrAddrE = getLedgerDelegateIdForScrAddr(bridge_,
      walletId_BCDE_, accountId_BCDE_, TestChain::scrAddrE);
   ASSERT_FALSE(delegateScrAddrE.empty());

   pageCount = getLedgersPageCount(bridge_, delegateScrAddrB);
   EXPECT_EQ(pageCount, 1);
   pageCount = getLedgersPageCount(bridge_, delegateScrAddrC);
   EXPECT_EQ(pageCount, 1);
   pageCount = getLedgersPageCount(bridge_, delegateScrAddrD);
   EXPECT_EQ(pageCount, 1);
   pageCount = getLedgersPageCount(bridge_, delegateScrAddrE);
   EXPECT_EQ(pageCount, 1);

   //check wallet ledgers
   auto ledgers = getLedgersPage(bridge_, delegateId, 0);
   ASSERT_EQ(ledgers.size(), 15);

   for (unsigned i = 0; i < ledgers.size(); i++) {
      EXPECT_TRUE(checkLedgers(ledgers[i], TestChain::ledgersBCDE[i])) << i;
   }

   /* check address ledgers */

   //B
   auto ledgersAtBlocks = getLedgersPage(bridge_, delegateScrAddrB, 0);
   ASSERT_EQ(ledgersAtBlocks.size(), 7);
   for (unsigned i = 0; i < ledgersAtBlocks.size(); i++) {
      EXPECT_TRUE(
         checkLedgers(ledgersAtBlocks[i],
            TestChain::ledgersB[i]));
   }

   //C
   ledgersAtBlocks = getLedgersPage(bridge_, delegateScrAddrC, 0);
   ASSERT_EQ(ledgersAtBlocks.size(), 4);
   for (unsigned i = 0; i < ledgersAtBlocks.size(); i++) {
      EXPECT_TRUE(
         checkLedgers(ledgersAtBlocks[i],
            TestChain::ledgersC[i]));
   }

   //D
   ledgersAtBlocks = getLedgersPage(bridge_, delegateScrAddrD, 0);
   ASSERT_EQ(ledgersAtBlocks.size(), 4);
   for (unsigned i = 0; i < ledgersAtBlocks.size(); i++) {
      EXPECT_TRUE(
         checkLedgers(ledgersAtBlocks[i],
            TestChain::ledgersD[i]));
   }

   //E
   ledgersAtBlocks = getLedgersPage(bridge_, delegateScrAddrE, 0);
   ASSERT_EQ(ledgersAtBlocks.size(), 2);
   for (unsigned i = 0; i < ledgersAtBlocks.size(); i++) {
      EXPECT_TRUE(
         checkLedgers(ledgersAtBlocks[i],
            TestChain::ledgersE[i]));
   }

   //create recipient address
   auto recipientPrivKey = Cryptography::ECDSA::createNewPrivateKey();
   auto recipientPubKey = Cryptography::ECDSA::computePublicKey(
      recipientPrivKey, true);
   auto hash160 =  BtcUtils::getHash160(recipientPubKey);
   auto recipientAddr1 = BtcUtils::scrAddrToSegWitAddress(hash160);

   //get signed tx
   BinaryData signedTx;
   try {
      signedTx = createAndSignTx(bridge_,
         walletId_BCDE_, accountId_BCDE_,
         {}, {{ recipientAddr1, 11 * COIN }},
         TestChain::scrAddrC, 2, true,
         "privPass1"
      );
   } catch (const std::exception& e) {
      ASSERT_TRUE(false) << e.what();
   }
   ASSERT_FALSE(signedTx.empty());

   //broadcast
   broadcastTx(bridge_, signedTx);

   //grab amount for change output
   Tx tx(signedTx);
   auto changeOutput = tx.getTxOutCopy(1);
   uint64_t changeAmount = changeOutput.getAmount();
   EXPECT_TRUE(changeAmount > 8 * COIN);
   EXPECT_TRUE(changeAmount < 9 * COIN);

   //wait on zc notif
   try {
      auto zcLedgers = waitOnZc();
      ASSERT_EQ(zcLedgers.size(), 1);
      ASSERT_EQ(zcLedgers[0].getTxHash(), tx.getThisHash());
      EXPECT_EQ(zcLedgers[0].getValue(), -20 * (int64_t)COIN + changeAmount);
      ASSERT_EQ(zcLedgers[0].getBlockNum(), UINT32_MAX);
   } catch (const std::exception&) {
      ASSERT_TRUE(false);
   }

   //check wallet balance
   wltBal = getWalletBalance(bridge_, walletId_BCDE_, accountId_BCDE_);
   ASSERT_EQ(wltBal.size(), 4);
   EXPECT_EQ(wltBal[0], TestChain::wltBal_BCDE[5][0] - (20 * COIN) + changeAmount);
   EXPECT_EQ(wltBal[1], TestChain::wltBal_BCDE[5][1] - (20 * COIN));
   EXPECT_EQ(wltBal[2], TestChain::wltBal_BCDE[5][2] - (20 * COIN) + changeAmount);
   EXPECT_EQ(wltBal[3], TestChain::wltBal_BCDE[5][3] + 1);

   //check addr balances
   balances = getAddrBalances(bridge_, walletId_BCDE_, accountId_BCDE_);
   ASSERT_EQ(balances.size(), 4);
   auto addrBBal = balances.at(TestChain::scrAddrB);
   auto testAddrBBal = TestChain::testAddrBalances[5].at(TestChain::scrAddrB);
   EXPECT_EQ(addrBBal[0], testAddrBBal[0] - (20 * COIN));
   EXPECT_EQ(addrBBal[1], testAddrBBal[1] - (20 * COIN));
   EXPECT_EQ(addrBBal[2], testAddrBBal[2] - (20 * COIN));
   EXPECT_EQ(addrBBal[3], testAddrBBal[3] + 1);

   addrBBal = balances.at(TestChain::scrAddrC);
   testAddrBBal = TestChain::testAddrBalances[5].at(TestChain::scrAddrC);
   EXPECT_EQ(addrBBal[0], testAddrBBal[0] + changeAmount);
   EXPECT_EQ(addrBBal[1], testAddrBBal[1]);
   EXPECT_EQ(addrBBal[2], testAddrBBal[2] + changeAmount);
   EXPECT_EQ(addrBBal[3], testAddrBBal[3] + 1);

   //check wallet ledgers
   ledgers = getLedgersPage(bridge_, delegateId, 0);
   ASSERT_EQ(ledgers.size(), 16);
   for (unsigned i = 0; i < 15; i++) {
      EXPECT_TRUE(checkLedgers(ledgers[i+1], TestChain::ledgersBCDE[i])) << i;
   }

   auto lastEntry = ledgers[0];
   ASSERT_EQ(lastEntry.getTxHash(), tx.getThisHash());
   EXPECT_EQ(lastEntry.getValue(), -20 * (int64_t)COIN + changeAmount);
   ASSERT_EQ(lastEntry.getBlockNum(), UINT32_MAX);

   /* address ledgers */
   //B
   ledgersAtBlocks = getLedgersPage(bridge_, delegateScrAddrB, 0);
   ASSERT_EQ(ledgersAtBlocks.size(), 8);
   for (unsigned i = 0; i < TestChain::ledgersB.size(); i++) {
      EXPECT_TRUE(
         checkLedgers(ledgersAtBlocks[i + 1],
            TestChain::ledgersB[i]));
   }

   lastEntry = ledgersAtBlocks[0];
   ASSERT_EQ(lastEntry.getTxHash(), tx.getThisHash());
   EXPECT_EQ(lastEntry.getValue(), -20 * (int64_t)COIN);
   ASSERT_EQ(lastEntry.getBlockNum(), UINT32_MAX);
   EXPECT_FALSE(lastEntry.isChainedZC());

   //C
   ledgersAtBlocks = getLedgersPage(bridge_, delegateScrAddrC, 0);
   ASSERT_EQ(ledgersAtBlocks.size(), 5);
   for (unsigned i = 0; i < TestChain::ledgersC.size(); i++) {
      EXPECT_TRUE(
         checkLedgers(ledgersAtBlocks[i + 1],
            TestChain::ledgersC[i]));
   }

   lastEntry = ledgersAtBlocks[0];
   ASSERT_EQ(lastEntry.getTxHash(), tx.getThisHash());
   EXPECT_EQ(lastEntry.getValue(), changeAmount);
   ASSERT_EQ(lastEntry.getBlockNum(), UINT32_MAX);
   EXPECT_FALSE(lastEntry.isChainedZC());

   /* second zc */

   //get ZC utxo
   auto zcUtxos = getUTXOs(bridge_, walletId_BCDE_, accountId_BCDE_, 1);
   ASSERT_EQ(zcUtxos.size(), 1);

   //create second recipient address
   recipientPrivKey = Cryptography::ECDSA::createNewPrivateKey();
   recipientPubKey = Cryptography::ECDSA::computePublicKey(
      recipientPrivKey, true);
   hash160 =  BtcUtils::getHash160(recipientPubKey);
   auto recipientAddr2 = BtcUtils::scrAddrToSegWitAddress(hash160);

   //spend the zc utxo
   BinaryData signedTx2;
   try {
      signedTx2 = createAndSignTx(bridge_,
         walletId_BCDE_, accountId_BCDE_,
         zcUtxos, {{ recipientAddr2, 5 * COIN }},
         TestChain::scrAddrC, 1000, true,
         "privPass1"
      );
   } catch (const std::exception& e) {
      ASSERT_TRUE(false) << e.what();
   }
   ASSERT_FALSE(signedTx2.empty());

   Tx tx2(signedTx2);
   auto changeOutput2 = tx2.getTxOutCopy(1);
   uint64_t changeAmount2 = changeOutput2.getAmount();
   EXPECT_TRUE(changeAmount2 > 3 * COIN);
   EXPECT_TRUE(changeAmount2 < 4 * COIN);

   //broadcast & wait on zc notif
   //introduce a 1 block delay in mining so the 2 tx do not
   //mine in the same block
   nodePtr_->delayNextZc(1);
   broadcastTx(bridge_, signedTx2);
   try {
      auto zcLedgers = waitOnZc();
      ASSERT_EQ(zcLedgers.size(), 2);
      ASSERT_EQ(zcLedgers[0].getTxHash(), tx2.getThisHash());
      EXPECT_EQ(zcLedgers[0].getValue(), -changeAmount + changeAmount2);
      ASSERT_EQ(zcLedgers[0].getBlockNum(), UINT32_MAX);
      EXPECT_TRUE(zcLedgers[0].isChainedZC());

      ASSERT_EQ(zcLedgers[1].getTxHash(), tx.getThisHash());
      EXPECT_EQ(zcLedgers[1].getValue(), -20 * (int64_t)COIN + changeAmount);
      ASSERT_EQ(zcLedgers[1].getBlockNum(), UINT32_MAX);
      EXPECT_FALSE(zcLedgers[1].isChainedZC());
   } catch (const std::exception&) {
      ASSERT_TRUE(false);
   }

   //check wallet balance
   wltBal = getWalletBalance(bridge_, walletId_BCDE_, accountId_BCDE_);
   ASSERT_EQ(wltBal.size(), 4);
   EXPECT_EQ(wltBal[0], TestChain::wltBal_BCDE[5][0] - (20 * COIN) + changeAmount2);
   EXPECT_EQ(wltBal[1], TestChain::wltBal_BCDE[5][1] - (20 * COIN));
   EXPECT_EQ(wltBal[2], TestChain::wltBal_BCDE[5][2] - (20 * COIN) + changeAmount2);
   EXPECT_EQ(wltBal[3], TestChain::wltBal_BCDE[5][3] + 2);

   //check addr balances
   balances = getAddrBalances(bridge_, walletId_BCDE_, accountId_BCDE_);
   ASSERT_EQ(balances.size(), 4);
   addrBBal = balances.at(TestChain::scrAddrB);
   testAddrBBal = TestChain::testAddrBalances[5].at(TestChain::scrAddrB);
   EXPECT_EQ(addrBBal[0], testAddrBBal[0] - (20 * COIN));
   EXPECT_EQ(addrBBal[1], testAddrBBal[1] - (20 * COIN));
   EXPECT_EQ(addrBBal[2], testAddrBBal[2] - (20 * COIN));
   EXPECT_EQ(addrBBal[3], testAddrBBal[3] + 1);

   addrBBal = balances.at(TestChain::scrAddrC);
   testAddrBBal = TestChain::testAddrBalances[5].at(TestChain::scrAddrC);
   EXPECT_EQ(addrBBal[0], testAddrBBal[0] + changeAmount2);
   EXPECT_EQ(addrBBal[1], testAddrBBal[1]);
   EXPECT_EQ(addrBBal[2], testAddrBBal[2] + changeAmount2);
   EXPECT_EQ(addrBBal[3], testAddrBBal[3] + 2);

   //check wallet ledgers
   ledgers = getLedgersPage(bridge_, delegateId, 0);
   ASSERT_EQ(ledgers.size(), 17);
   for (unsigned i = 0; i < 15; i++) {
      EXPECT_TRUE(checkLedgers(ledgers[i+2], TestChain::ledgersBCDE[i])) << i;
   }

   ASSERT_EQ(ledgers[1].getTxHash(), tx.getThisHash());
   EXPECT_EQ(ledgers[1].getValue(), -20 * (int64_t)COIN + changeAmount);
   ASSERT_EQ(ledgers[1].getBlockNum(), UINT32_MAX);
   EXPECT_FALSE(ledgers[1].isChainedZC());

   ASSERT_EQ(ledgers[0].getTxHash(), tx2.getThisHash());
   EXPECT_EQ(ledgers[0].getValue(), -changeAmount + changeAmount2);
   ASSERT_EQ(ledgers[0].getBlockNum(), UINT32_MAX);
   EXPECT_TRUE(ledgers[0].isChainedZC());

   /* address ledgers */
   //B
   ledgersAtBlocks = getLedgersPage(bridge_, delegateScrAddrB, 0);
   ASSERT_EQ(ledgersAtBlocks.size(), 8);
   for (unsigned i = 0; i < TestChain::ledgersB.size(); i++) {
      EXPECT_TRUE(
         checkLedgers(ledgersAtBlocks[i + 1],
            TestChain::ledgersB[i]));
   }

   lastEntry = ledgersAtBlocks[0];
   ASSERT_EQ(lastEntry.getTxHash(), tx.getThisHash());
   EXPECT_EQ(lastEntry.getValue(), -20 * (int64_t)COIN);
   ASSERT_EQ(lastEntry.getBlockNum(), UINT32_MAX);
   EXPECT_FALSE(lastEntry.isChainedZC());

   //C
   ledgersAtBlocks = getLedgersPage(bridge_, delegateScrAddrC, 0);
   ASSERT_EQ(ledgersAtBlocks.size(), 6);
   for (unsigned i = 0; i < TestChain::ledgersC.size(); i++) {
      EXPECT_TRUE(
         checkLedgers(ledgersAtBlocks[i + 2],
            TestChain::ledgersC[i]));
   }

   lastEntry = ledgersAtBlocks[1];
   ASSERT_EQ(lastEntry.getTxHash(), tx.getThisHash());
   EXPECT_EQ(lastEntry.getValue(), changeAmount);
   ASSERT_EQ(lastEntry.getBlockNum(), UINT32_MAX);
   EXPECT_FALSE(lastEntry.isChainedZC());

   lastEntry = ledgersAtBlocks[0];
   ASSERT_EQ(lastEntry.getTxHash(), tx2.getThisHash());
   EXPECT_EQ(lastEntry.getValue(), -(int64_t)changeAmount + int64_t(changeAmount2));
   ASSERT_EQ(lastEntry.getBlockNum(), UINT32_MAX);
   EXPECT_TRUE(lastEntry.isChainedZC());

   /* mine first tx */
   DBTestUtils::mineNewBlock(theBDMt_, TestChain::addrA, 1);
   ASSERT_EQ(waitOnNewBlock(), 6);

   //check wallet balance
   wltBal = getWalletBalance(bridge_, walletId_BCDE_, accountId_BCDE_);
   ASSERT_EQ(wltBal.size(), 4);
   EXPECT_EQ(wltBal[0], TestChain::wltBal_BCDE[5][0] - (20 * COIN) + changeAmount2);
   EXPECT_EQ(wltBal[1], TestChain::wltBal_BCDE[5][1] - (20 * COIN));
   EXPECT_EQ(wltBal[2], TestChain::wltBal_BCDE[5][2] - (20 * COIN) + changeAmount2);
   EXPECT_EQ(wltBal[3], TestChain::wltBal_BCDE[5][3] + 2);

   //check addr balances
   balances = getAddrBalances(bridge_, walletId_BCDE_, accountId_BCDE_);
   ASSERT_EQ(balances.size(), 4);
   addrBBal = balances.at(TestChain::scrAddrB);
   testAddrBBal = TestChain::testAddrBalances[5].at(TestChain::scrAddrB);
   EXPECT_EQ(addrBBal[0], testAddrBBal[0] - (20 * COIN));
   EXPECT_EQ(addrBBal[1], testAddrBBal[1] - (20 * COIN));
   EXPECT_EQ(addrBBal[2], testAddrBBal[2] - (20 * COIN));
   EXPECT_EQ(addrBBal[3], testAddrBBal[3] + 1);

   addrBBal = balances.at(TestChain::scrAddrC);
   testAddrBBal = TestChain::testAddrBalances[5].at(TestChain::scrAddrC);
   EXPECT_EQ(addrBBal[0], testAddrBBal[0] + changeAmount2);
   EXPECT_EQ(addrBBal[1], testAddrBBal[1]);
   EXPECT_EQ(addrBBal[2], testAddrBBal[2] + changeAmount2);
   EXPECT_EQ(addrBBal[3], testAddrBBal[3] + 2);

   //check wallet ledgers
   ledgers = getLedgersPage(bridge_, delegateId, 0);
   ASSERT_EQ(ledgers.size(), 17);
   for (unsigned i = 0; i < 15; i++) {
      EXPECT_TRUE(checkLedgers(ledgers[i+2], TestChain::ledgersBCDE[i])) << i;
   }

   ASSERT_EQ(ledgers[1].getTxHash(), tx.getThisHash());
   EXPECT_EQ(ledgers[1].getValue(), -20 * (int64_t)COIN + changeAmount);
   ASSERT_EQ(ledgers[1].getBlockNum(), 6);
   EXPECT_FALSE(ledgers[1].isChainedZC());

   ASSERT_EQ(ledgers[0].getTxHash(), tx2.getThisHash());
   EXPECT_EQ(ledgers[0].getValue(), -changeAmount + changeAmount2);
   ASSERT_EQ(ledgers[0].getBlockNum(), UINT32_MAX);
   EXPECT_FALSE(ledgers[0].isChainedZC());

   /* address ledgers */
   //B
   ledgersAtBlocks = getLedgersPage(bridge_, delegateScrAddrB, 0);
   ASSERT_EQ(ledgersAtBlocks.size(), 8);
   for (unsigned i = 0; i < TestChain::ledgersB.size(); i++) {
      EXPECT_TRUE(
         checkLedgers(ledgersAtBlocks[i + 1],
            TestChain::ledgersB[i]));
   }

   lastEntry = ledgersAtBlocks[0];
   ASSERT_EQ(lastEntry.getTxHash(), tx.getThisHash());
   EXPECT_EQ(lastEntry.getValue(), -20 * (int64_t)COIN);
   ASSERT_EQ(lastEntry.getBlockNum(), 6);
   EXPECT_FALSE(lastEntry.isChainedZC());

   //C
   ledgersAtBlocks = getLedgersPage(bridge_, delegateScrAddrC, 0);
   ASSERT_EQ(ledgersAtBlocks.size(), 6);
   for (unsigned i = 0; i < TestChain::ledgersC.size(); i++) {
      EXPECT_TRUE(
         checkLedgers(ledgersAtBlocks[i + 2],
            TestChain::ledgersC[i]));
   }

   lastEntry = ledgersAtBlocks[1];
   ASSERT_EQ(lastEntry.getTxHash(), tx.getThisHash());
   EXPECT_EQ(lastEntry.getValue(), changeAmount);
   ASSERT_EQ(lastEntry.getBlockNum(), 6);
   EXPECT_FALSE(lastEntry.isChainedZC());

   lastEntry = ledgersAtBlocks[0];
   ASSERT_EQ(lastEntry.getTxHash(), tx2.getThisHash());
   EXPECT_EQ(lastEntry.getValue(), -(int64_t)changeAmount + int64_t(changeAmount2));
   ASSERT_EQ(lastEntry.getBlockNum(), UINT32_MAX);
   EXPECT_FALSE(lastEntry.isChainedZC());

   /* mine second tx */
   DBTestUtils::mineNewBlock(theBDMt_, TestChain::addrA, 1);
   ASSERT_EQ(waitOnNewBlock(), 7);

   //check wallet balance
   wltBal = getWalletBalance(bridge_, walletId_BCDE_, accountId_BCDE_);
   ASSERT_EQ(wltBal.size(), 4);
   EXPECT_EQ(wltBal[0], TestChain::wltBal_BCDE[5][0] - (20 * COIN) + changeAmount2);
   EXPECT_EQ(wltBal[1], TestChain::wltBal_BCDE[5][1] - (20 * COIN) + changeAmount2);
   EXPECT_EQ(wltBal[2], TestChain::wltBal_BCDE[5][2] - (20 * COIN) + changeAmount2);
   EXPECT_EQ(wltBal[3], TestChain::wltBal_BCDE[5][3] + 2);

   //check addr balances
   balances = getAddrBalances(bridge_, walletId_BCDE_, accountId_BCDE_);
   ASSERT_EQ(balances.size(), 4);
   addrBBal = balances.at(TestChain::scrAddrB);
   testAddrBBal = TestChain::testAddrBalances[5].at(TestChain::scrAddrB);
   EXPECT_EQ(addrBBal[0], testAddrBBal[0] - (20 * COIN));
   EXPECT_EQ(addrBBal[1], testAddrBBal[1] - (20 * COIN));
   EXPECT_EQ(addrBBal[2], testAddrBBal[2] - (20 * COIN));
   EXPECT_EQ(addrBBal[3], testAddrBBal[3] + 1);

   addrBBal = balances.at(TestChain::scrAddrC);
   testAddrBBal = TestChain::testAddrBalances[5].at(TestChain::scrAddrC);
   EXPECT_EQ(addrBBal[0], testAddrBBal[0] + changeAmount2);
   EXPECT_EQ(addrBBal[1], testAddrBBal[1] + changeAmount2);
   EXPECT_EQ(addrBBal[2], testAddrBBal[2] + changeAmount2);
   EXPECT_EQ(addrBBal[3], testAddrBBal[3] + 2);

   //check wallet ledgers
   ledgers = getLedgersPage(bridge_, delegateId, 0);
   ASSERT_EQ(ledgers.size(), 17);
   for (unsigned i = 0; i < 15; i++) {
      EXPECT_TRUE(checkLedgers(ledgers[i+2], TestChain::ledgersBCDE[i])) << i;
   }

   ASSERT_EQ(ledgers[1].getTxHash(), tx.getThisHash());
   EXPECT_EQ(ledgers[1].getValue(), -20 * (int64_t)COIN + changeAmount);
   ASSERT_EQ(ledgers[1].getBlockNum(), 6);
   EXPECT_FALSE(ledgers[1].isChainedZC());

   ASSERT_EQ(ledgers[0].getTxHash(), tx2.getThisHash());
   EXPECT_EQ(ledgers[0].getValue(), -changeAmount + changeAmount2);
   ASSERT_EQ(ledgers[0].getBlockNum(), 7);
   EXPECT_FALSE(ledgers[0].isChainedZC());

   /* address ledgers */
   //B
   ledgersAtBlocks = getLedgersPage(bridge_, delegateScrAddrB, 0);
   ASSERT_EQ(ledgersAtBlocks.size(), 8);
   for (unsigned i = 0; i < TestChain::ledgersB.size(); i++) {
      EXPECT_TRUE(
         checkLedgers(ledgersAtBlocks[i + 1],
            TestChain::ledgersB[i]));
   }

   lastEntry = ledgersAtBlocks[0];
   ASSERT_EQ(lastEntry.getTxHash(), tx.getThisHash());
   EXPECT_EQ(lastEntry.getValue(), -20 * (int64_t)COIN);
   ASSERT_EQ(lastEntry.getBlockNum(), 6);
   EXPECT_FALSE(lastEntry.isChainedZC());

   //C
   ledgersAtBlocks = getLedgersPage(bridge_, delegateScrAddrC, 0);
   ASSERT_EQ(ledgersAtBlocks.size(), 6);
   for (unsigned i = 0; i < TestChain::ledgersC.size(); i++) {
      EXPECT_TRUE(
         checkLedgers(ledgersAtBlocks[i + 2],
            TestChain::ledgersC[i]));
   }

   lastEntry = ledgersAtBlocks[1];
   ASSERT_EQ(lastEntry.getTxHash(), tx.getThisHash());
   EXPECT_EQ(lastEntry.getValue(), changeAmount);
   ASSERT_EQ(lastEntry.getBlockNum(), 6);
   EXPECT_FALSE(lastEntry.isChainedZC());

   lastEntry = ledgersAtBlocks[0];
   ASSERT_EQ(lastEntry.getTxHash(), tx2.getThisHash());
   EXPECT_EQ(lastEntry.getValue(), -(int64_t)changeAmount + int64_t(changeAmount2));
   ASSERT_EQ(lastEntry.getBlockNum(), 7);
   EXPECT_FALSE(lastEntry.isChainedZC());
}

TEST_F(BridgeChainDataTests, ZeroConf_ChainRBF)
{
   loadWallets({walletId_BCDE_});

   TestUtils::setBlocks({ "0", "1", "2", "3", "4", "5" }, blk0dat_);
   WebSocketServer::initAuthPeers({
      homedir_ / SERVER_AUTH_PEER_FILENAME, authPeersPassLbd_});
   WebSocketServer::start(theBDMt_->bdm(), true);

   ASSERT_TRUE(connectToIp(bridge_, "127.0.0.1", "9001", serverPubkey_));
   ASSERT_TRUE(registerWallets(bridge_));

   //start db, go online and wait on ready notif
   theBDMt_->start(Config::DBSettings::initMode());
   ASSERT_EQ(goOnline(bridge_), 5);

   //check wallet balance
   auto wltBal = getWalletBalance(bridge_, walletId_BCDE_, accountId_BCDE_);
   ASSERT_EQ(wltBal.size(), 4);
   EXPECT_EQ(wltBal[0], TestChain::wltBal_BCDE[5][0]);
   EXPECT_EQ(wltBal[1], TestChain::wltBal_BCDE[5][1]);
   EXPECT_EQ(wltBal[2], TestChain::wltBal_BCDE[5][2]);
   EXPECT_EQ(wltBal[3], TestChain::wltBal_BCDE[5][3]);

   //check addr balances
   auto balances = getAddrBalances(bridge_, walletId_BCDE_, accountId_BCDE_);
   ASSERT_EQ(balances.size(), 4);
   checkBalances(balances, 5, false);

   //setup wallet ledger delegates
   auto delegateId = getLedgerDelegateId(bridge_);
   ASSERT_FALSE(delegateId.empty());
   auto pageCount = getLedgersPageCount(bridge_, delegateId);
   EXPECT_EQ(pageCount, 1);

   //setup address ledger delegates
   auto delegateScrAddrB = getLedgerDelegateIdForScrAddr(bridge_,
      walletId_BCDE_, accountId_BCDE_, TestChain::scrAddrB);
   ASSERT_FALSE(delegateScrAddrB.empty());
   auto delegateScrAddrC = getLedgerDelegateIdForScrAddr(bridge_,
      walletId_BCDE_, accountId_BCDE_, TestChain::scrAddrC);
   ASSERT_FALSE(delegateScrAddrC.empty());
   auto delegateScrAddrD = getLedgerDelegateIdForScrAddr(bridge_,
      walletId_BCDE_, accountId_BCDE_, TestChain::scrAddrD);
   ASSERT_FALSE(delegateScrAddrD.empty());
   auto delegateScrAddrE = getLedgerDelegateIdForScrAddr(bridge_,
      walletId_BCDE_, accountId_BCDE_, TestChain::scrAddrE);
   ASSERT_FALSE(delegateScrAddrE.empty());

   pageCount = getLedgersPageCount(bridge_, delegateScrAddrB);
   EXPECT_EQ(pageCount, 1);
   pageCount = getLedgersPageCount(bridge_, delegateScrAddrC);
   EXPECT_EQ(pageCount, 1);
   pageCount = getLedgersPageCount(bridge_, delegateScrAddrD);
   EXPECT_EQ(pageCount, 1);
   pageCount = getLedgersPageCount(bridge_, delegateScrAddrE);
   EXPECT_EQ(pageCount, 1);

   // check wallet ledgers
   auto ledgers = getLedgersPage(bridge_, delegateId, 0);
   ASSERT_EQ(ledgers.size(), 15);

   for (unsigned i = 0; i < ledgers.size(); i++) {
      EXPECT_TRUE(checkLedgers(ledgers[i], TestChain::ledgersBCDE[i])) << i;
   }

   /* check address ledgers */

   //B
   auto ledgersAtBlocks = getLedgersPage(bridge_, delegateScrAddrB, 0);
   ASSERT_EQ(ledgersAtBlocks.size(), 7);
   for (unsigned i = 0; i < ledgersAtBlocks.size(); i++) {
      EXPECT_TRUE(
         checkLedgers(ledgersAtBlocks[i],
            TestChain::ledgersB[i]));
   }

   //C
   ledgersAtBlocks = getLedgersPage(bridge_, delegateScrAddrC, 0);
   ASSERT_EQ(ledgersAtBlocks.size(), 4);
   for (unsigned i = 0; i < ledgersAtBlocks.size(); i++) {
      EXPECT_TRUE(
         checkLedgers(ledgersAtBlocks[i],
            TestChain::ledgersC[i]));
   }

   //D
   ledgersAtBlocks = getLedgersPage(bridge_, delegateScrAddrD, 0);
   ASSERT_EQ(ledgersAtBlocks.size(), 4);
   for (unsigned i = 0; i < ledgersAtBlocks.size(); i++) {
      EXPECT_TRUE(
         checkLedgers(ledgersAtBlocks[i],
            TestChain::ledgersD[i]));
   }

   //E
   ledgersAtBlocks = getLedgersPage(bridge_, delegateScrAddrE, 0);
   ASSERT_EQ(ledgersAtBlocks.size(), 2);
   for (unsigned i = 0; i < ledgersAtBlocks.size(); i++) {
      EXPECT_TRUE(
         checkLedgers(ledgersAtBlocks[i],
            TestChain::ledgersE[i]));
   }

   //create recipient address
   auto recipientPrivKey = Cryptography::ECDSA::createNewPrivateKey();
   auto recipientPubKey = Cryptography::ECDSA::computePublicKey(
      recipientPrivKey, true);
   auto hash160 =  BtcUtils::getHash160(recipientPubKey);
   auto recipientAddr1 = BtcUtils::scrAddrToSegWitAddress(hash160);

   //get signed tx
   BinaryData signedTx;
   try {
      signedTx = createAndSignTx(bridge_,
         walletId_BCDE_, accountId_BCDE_,
         {}, {{ recipientAddr1, 11 * COIN }},
         TestChain::scrAddrC, 2, true,
         "privPass1"
      );
   } catch (const std::exception& e) {
      ASSERT_TRUE(false) << e.what();
   }
   ASSERT_FALSE(signedTx.empty());

   //broadcast
   broadcastTx(bridge_, signedTx);

   //grab amount for change output
   Tx tx(signedTx);
   auto changeOutput = tx.getTxOutCopy(1);
   uint64_t changeAmount = changeOutput.getAmount();
   EXPECT_TRUE(changeAmount > 8 * COIN);
   EXPECT_TRUE(changeAmount < 9 * COIN);

   //wait on zc notif
   try {
      auto zcLedgers = waitOnZc();
      ASSERT_EQ(zcLedgers.size(), 1);
      ASSERT_EQ(zcLedgers[0].getTxHash(), tx.getThisHash());
      EXPECT_EQ(zcLedgers[0].getValue(), -20 * (int64_t)COIN + changeAmount);
      ASSERT_EQ(zcLedgers[0].getBlockNum(), UINT32_MAX);
      EXPECT_TRUE(zcLedgers[0].isOptInRBF());
   } catch (const std::exception&) {
      ASSERT_TRUE(false);
   }

   //check wallet balance
   wltBal = getWalletBalance(bridge_, walletId_BCDE_, accountId_BCDE_);
   ASSERT_EQ(wltBal.size(), 4);
   EXPECT_EQ(wltBal[0], TestChain::wltBal_BCDE[5][0] - (20 * COIN) + changeAmount);
   EXPECT_EQ(wltBal[1], TestChain::wltBal_BCDE[5][1] - (20 * COIN));
   EXPECT_EQ(wltBal[2], TestChain::wltBal_BCDE[5][2] - (20 * COIN) + changeAmount);
   EXPECT_EQ(wltBal[3], TestChain::wltBal_BCDE[5][3] + 1);

   //check addr balances
   balances = getAddrBalances(bridge_, walletId_BCDE_, accountId_BCDE_);
   ASSERT_EQ(balances.size(), 4);
   auto addrBBal = balances.at(TestChain::scrAddrB);
   auto testAddrBBal = TestChain::testAddrBalances[5].at(TestChain::scrAddrB);
   EXPECT_EQ(addrBBal[0], testAddrBBal[0] - (20 * COIN));
   EXPECT_EQ(addrBBal[1], testAddrBBal[1] - (20 * COIN));
   EXPECT_EQ(addrBBal[2], testAddrBBal[2] - (20 * COIN));
   EXPECT_EQ(addrBBal[3], testAddrBBal[3] + 1);

   addrBBal = balances.at(TestChain::scrAddrC);
   testAddrBBal = TestChain::testAddrBalances[5].at(TestChain::scrAddrC);
   EXPECT_EQ(addrBBal[0], testAddrBBal[0] + changeAmount);
   EXPECT_EQ(addrBBal[1], testAddrBBal[1]);
   EXPECT_EQ(addrBBal[2], testAddrBBal[2] + changeAmount);
   EXPECT_EQ(addrBBal[3], testAddrBBal[3] + 1);

   //check wallet ledgers
   ledgers = getLedgersPage(bridge_, delegateId, 0);
   ASSERT_EQ(ledgers.size(), 16);
   for (unsigned i = 0; i < 15; i++) {
      EXPECT_TRUE(checkLedgers(ledgers[i+1], TestChain::ledgersBCDE[i])) << i;
   }

   auto lastEntry = ledgers[0];
   ASSERT_EQ(lastEntry.getTxHash(), tx.getThisHash());
   EXPECT_EQ(lastEntry.getValue(), -20 * (int64_t)COIN + changeAmount);
   ASSERT_EQ(lastEntry.getBlockNum(), UINT32_MAX);
   EXPECT_TRUE(lastEntry.isOptInRBF());

   /* address ledgers */

   //B
   ledgersAtBlocks = getLedgersPage(bridge_, delegateScrAddrB, 0);
   ASSERT_EQ(ledgersAtBlocks.size(), 8);
   for (unsigned i = 0; i < TestChain::ledgersB.size(); i++) {
      EXPECT_TRUE(
         checkLedgers(ledgersAtBlocks[i + 1],
            TestChain::ledgersB[i]));
   }

   lastEntry = ledgersAtBlocks[0];
   ASSERT_EQ(lastEntry.getTxHash(), tx.getThisHash());
   EXPECT_EQ(lastEntry.getValue(), -20 * (int64_t)COIN);
   ASSERT_EQ(lastEntry.getBlockNum(), UINT32_MAX);
   EXPECT_TRUE(lastEntry.isOptInRBF());
   EXPECT_FALSE(lastEntry.isChainedZC());

   //C
   ledgersAtBlocks = getLedgersPage(bridge_, delegateScrAddrC, 0);
   ASSERT_EQ(ledgersAtBlocks.size(), 5);
   for (unsigned i = 0; i < TestChain::ledgersC.size(); i++) {
      EXPECT_TRUE(
         checkLedgers(ledgersAtBlocks[i + 1],
            TestChain::ledgersC[i]));
   }

   lastEntry = ledgersAtBlocks[0];
   ASSERT_EQ(lastEntry.getTxHash(), tx.getThisHash());
   EXPECT_EQ(lastEntry.getValue(), changeAmount);
   ASSERT_EQ(lastEntry.getBlockNum(), UINT32_MAX);
   EXPECT_TRUE(lastEntry.isOptInRBF());
   EXPECT_FALSE(lastEntry.isChainedZC());

   /* second zc */

   //get ZC utxo
   auto zcUtxos = getUTXOs(bridge_, walletId_BCDE_, accountId_BCDE_, 1);
   ASSERT_EQ(zcUtxos.size(), 1);

   //create second recipient address
   recipientPrivKey = Cryptography::ECDSA::createNewPrivateKey();
   recipientPubKey = Cryptography::ECDSA::computePublicKey(
      recipientPrivKey, true);
   hash160 =  BtcUtils::getHash160(recipientPubKey);
   auto recipientAddr2 = BtcUtils::scrAddrToSegWitAddress(hash160);

   //spend the zc utxo
   BinaryData signedTx2;
   try {
      signedTx2 = createAndSignTx(bridge_,
         walletId_BCDE_, accountId_BCDE_,
         zcUtxos, {{ recipientAddr2, 5 * COIN }},
         TestChain::scrAddrC, 1000, true,
         "privPass1"
      );
   } catch (const std::exception& e) {
      ASSERT_TRUE(false) << e.what();
   }
   ASSERT_FALSE(signedTx2.empty());

   Tx tx2(signedTx2);
   auto changeOutput2 = tx2.getTxOutCopy(1);
   uint64_t changeAmount2 = changeOutput2.getAmount();
   EXPECT_TRUE(changeAmount2 > 3 * COIN);
   EXPECT_TRUE(changeAmount2 < 4 * COIN);

   //broadcast & wait on zc notif
   broadcastTx(bridge_, signedTx2);
   try {
      auto zcLedgers = waitOnZc();
      ASSERT_EQ(zcLedgers.size(), 2);
      ASSERT_EQ(zcLedgers[0].getTxHash(), tx2.getThisHash());
      EXPECT_EQ(zcLedgers[0].getValue(), -changeAmount + changeAmount2);
      ASSERT_EQ(zcLedgers[0].getBlockNum(), UINT32_MAX);
      EXPECT_TRUE(zcLedgers[0].isChainedZC());
      EXPECT_TRUE(zcLedgers[0].isOptInRBF());

      ASSERT_EQ(zcLedgers[1].getTxHash(), tx.getThisHash());
      EXPECT_EQ(zcLedgers[1].getValue(), -20 * (int64_t)COIN + changeAmount);
      ASSERT_EQ(zcLedgers[1].getBlockNum(), UINT32_MAX);
      EXPECT_FALSE(zcLedgers[1].isChainedZC());
      EXPECT_TRUE(zcLedgers[1].isOptInRBF());
   } catch (const std::exception&) {
      ASSERT_TRUE(false);
   }

   //check wallet balance
   wltBal = getWalletBalance(bridge_, walletId_BCDE_, accountId_BCDE_);
   ASSERT_EQ(wltBal.size(), 4);
   EXPECT_EQ(wltBal[0], TestChain::wltBal_BCDE[5][0] - (20 * COIN) + changeAmount2);
   EXPECT_EQ(wltBal[1], TestChain::wltBal_BCDE[5][1] - (20 * COIN));
   EXPECT_EQ(wltBal[2], TestChain::wltBal_BCDE[5][2] - (20 * COIN) + changeAmount2);
   EXPECT_EQ(wltBal[3], TestChain::wltBal_BCDE[5][3] + 2);

   //check addr balances
   balances = getAddrBalances(bridge_, walletId_BCDE_, accountId_BCDE_);
   ASSERT_EQ(balances.size(), 4);
   addrBBal = balances.at(TestChain::scrAddrB);
   testAddrBBal = TestChain::testAddrBalances[5].at(TestChain::scrAddrB);
   EXPECT_EQ(addrBBal[0], testAddrBBal[0] - (20 * COIN));
   EXPECT_EQ(addrBBal[1], testAddrBBal[1] - (20 * COIN));
   EXPECT_EQ(addrBBal[2], testAddrBBal[2] - (20 * COIN));
   EXPECT_EQ(addrBBal[3], testAddrBBal[3] + 1);

   addrBBal = balances.at(TestChain::scrAddrC);
   testAddrBBal = TestChain::testAddrBalances[5].at(TestChain::scrAddrC);
   EXPECT_EQ(addrBBal[0], testAddrBBal[0] + changeAmount2);
   EXPECT_EQ(addrBBal[1], testAddrBBal[1]);
   EXPECT_EQ(addrBBal[2], testAddrBBal[2] + changeAmount2);
   EXPECT_EQ(addrBBal[3], testAddrBBal[3] + 2);

   //check wallet ledgers
   ledgers = getLedgersPage(bridge_, delegateId, 0);
   ASSERT_EQ(ledgers.size(), 17);
   for (unsigned i = 0; i < 15; i++) {
      EXPECT_TRUE(checkLedgers(ledgers[i+2], TestChain::ledgersBCDE[i])) << i;
   }

   ASSERT_EQ(ledgers[1].getTxHash(), tx.getThisHash());
   EXPECT_EQ(ledgers[1].getValue(), -20 * (int64_t)COIN + changeAmount);
   ASSERT_EQ(ledgers[1].getBlockNum(), UINT32_MAX);
   EXPECT_TRUE(ledgers[1].isOptInRBF());
   EXPECT_FALSE(ledgers[1].isChainedZC());

   ASSERT_EQ(ledgers[0].getTxHash(), tx2.getThisHash());
   EXPECT_EQ(ledgers[0].getValue(), -changeAmount + changeAmount2);
   ASSERT_EQ(ledgers[0].getBlockNum(), UINT32_MAX);
   EXPECT_TRUE(ledgers[0].isOptInRBF());
   EXPECT_TRUE(ledgers[0].isChainedZC());

   /* address ledgers */

   //B
   ledgersAtBlocks = getLedgersPage(bridge_, delegateScrAddrB, 0);
   ASSERT_EQ(ledgersAtBlocks.size(), 8);
   for (unsigned i = 0; i < TestChain::ledgersB.size(); i++) {
      EXPECT_TRUE(
         checkLedgers(ledgersAtBlocks[i + 1],
            TestChain::ledgersB[i]));
   }

   lastEntry = ledgersAtBlocks[0];
   ASSERT_EQ(lastEntry.getTxHash(), tx.getThisHash());
   EXPECT_EQ(lastEntry.getValue(), -20 * (int64_t)COIN);
   ASSERT_EQ(lastEntry.getBlockNum(), UINT32_MAX);
   EXPECT_TRUE(lastEntry.isOptInRBF());
   EXPECT_FALSE(lastEntry.isChainedZC());

   //C
   ledgersAtBlocks = getLedgersPage(bridge_, delegateScrAddrC, 0);
   ASSERT_EQ(ledgersAtBlocks.size(), 6);
   for (unsigned i = 0; i < TestChain::ledgersC.size(); i++) {
      EXPECT_TRUE(
         checkLedgers(ledgersAtBlocks[i + 2],
            TestChain::ledgersC[i]));
   }

   lastEntry = ledgersAtBlocks[1];
   ASSERT_EQ(lastEntry.getTxHash(), tx.getThisHash());
   EXPECT_EQ(lastEntry.getValue(), changeAmount);
   ASSERT_EQ(lastEntry.getBlockNum(), UINT32_MAX);
   EXPECT_TRUE(lastEntry.isOptInRBF());
   EXPECT_FALSE(lastEntry.isChainedZC());

   lastEntry = ledgersAtBlocks[0];
   ASSERT_EQ(lastEntry.getTxHash(), tx2.getThisHash());
   EXPECT_EQ(lastEntry.getValue(), -changeAmount + changeAmount2);
   ASSERT_EQ(lastEntry.getBlockNum(), UINT32_MAX);
   EXPECT_TRUE(lastEntry.isOptInRBF());
   EXPECT_TRUE(lastEntry.isChainedZC());

   //get rbf UTXOs
   auto rbfUtxos = getUTXOs(bridge_, walletId_BCDE_, accountId_BCDE_, 2);
   ASSERT_EQ(rbfUtxos.size(), 3);

   //look for the mined output
   int utxoId = -1;
   for (unsigned i = 0; i < rbfUtxos.size(); i++) {
      if (rbfUtxos[i].getHeight() != UINT32_MAX) {
         utxoId = i;
         break;
      }
   }
   ASSERT_NE(utxoId, -1);

   std::vector<UTXO> rbfVec{ rbfUtxos[utxoId] };
   int64_t rbfFee = 110000000;
   BinaryData txRbf;
   try {
      txRbf = createAndSignTx(bridge_,
         walletId_BCDE_, accountId_BCDE_,
         rbfVec, {{ recipientAddr1, 8 * COIN }},
         TestChain::scrAddrE, rbfFee, true,
         "privPass1"
      );
   } catch (const std::exception& e) {
      ASSERT_TRUE(false) << e.what();
   }
   ASSERT_FALSE(txRbf.empty());

   Tx tx3(txRbf);
   auto changeOutput3 = tx3.getTxOutCopy(1);
   uint64_t changeAmountRbf = changeOutput3.getAmount();
   EXPECT_EQ(changeAmountRbf, 12 * COIN - rbfFee);

   //broadcast it
   nodePtr_->delayNextZc(1);
   broadcastTx(bridge_, txRbf);
   try {
      auto zcLedgers = waitOnZc();
      ASSERT_EQ(zcLedgers.size(), 1);
      ASSERT_EQ(zcLedgers[0].getTxHash(), tx3.getThisHash());
      EXPECT_EQ(zcLedgers[0].getValue(), -8 * (int64_t)COIN -rbfFee);
      ASSERT_EQ(zcLedgers[0].getBlockNum(), UINT32_MAX);
      EXPECT_FALSE(zcLedgers[0].isChainedZC());

      auto invalidatedHashes = waitOnInvalidatedZCs();
      ASSERT_EQ(invalidatedHashes.size(), 2);
      std::set<BinaryData> ihSet{
         invalidatedHashes.begin(),
         invalidatedHashes.end()
      };

      ASSERT_NE(ihSet.find(tx.getThisHash()), ihSet.end());
      ASSERT_NE(ihSet.find(tx2.getThisHash()), ihSet.end());
   } catch (const std::exception&) {
      ASSERT_TRUE(false);
   }

   //check wallet balance
   wltBal = getWalletBalance(bridge_, walletId_BCDE_, accountId_BCDE_);
   ASSERT_EQ(wltBal.size(), 4);
   EXPECT_EQ(wltBal[0], TestChain::wltBal_BCDE[5][0] - (20 * COIN) + changeAmountRbf);
   EXPECT_EQ(wltBal[1], TestChain::wltBal_BCDE[5][1] - (20 * COIN));
   EXPECT_EQ(wltBal[2], TestChain::wltBal_BCDE[5][2] - (20 * COIN) + changeAmountRbf);
   EXPECT_EQ(wltBal[3], TestChain::wltBal_BCDE[5][3] + 1);

   //check addr balances
   balances = getAddrBalances(bridge_, walletId_BCDE_, accountId_BCDE_);
   ASSERT_EQ(balances.size(), 4);
   addrBBal = balances.at(TestChain::scrAddrB);
   testAddrBBal = TestChain::testAddrBalances[5].at(TestChain::scrAddrB);
   EXPECT_EQ(addrBBal[0], testAddrBBal[0] - (20 * COIN));
   EXPECT_EQ(addrBBal[1], testAddrBBal[1] - (20 * COIN));
   EXPECT_EQ(addrBBal[2], testAddrBBal[2] - (20 * COIN));
   EXPECT_EQ(addrBBal[3], testAddrBBal[3] + 1);

   addrBBal = balances.at(TestChain::scrAddrC);
   testAddrBBal = TestChain::testAddrBalances[5].at(TestChain::scrAddrC);
   EXPECT_EQ(addrBBal[0], testAddrBBal[0]);
   EXPECT_EQ(addrBBal[1], testAddrBBal[1]);
   EXPECT_EQ(addrBBal[2], testAddrBBal[2]);
   EXPECT_EQ(addrBBal[3], testAddrBBal[3]);

   addrBBal = balances.at(TestChain::scrAddrE);
   testAddrBBal = TestChain::testAddrBalances[5].at(TestChain::scrAddrE);
   EXPECT_EQ(addrBBal[0], testAddrBBal[0] + changeAmountRbf);
   EXPECT_EQ(addrBBal[1], testAddrBBal[1]);
   EXPECT_EQ(addrBBal[2], testAddrBBal[2] + changeAmountRbf);
   EXPECT_EQ(addrBBal[3], testAddrBBal[3] + 1);

   //check wallet ledgers
   ledgers = getLedgersPage(bridge_, delegateId, 0);
   ASSERT_EQ(ledgers.size(), 16);
   for (unsigned i = 0; i < 15; i++) {
      EXPECT_TRUE(checkLedgers(ledgers[i+1], TestChain::ledgersBCDE[i])) << i;
   }

   ASSERT_EQ(ledgers[0].getTxHash(), tx3.getThisHash());
   EXPECT_EQ(ledgers[0].getValue(), -8 * (int64_t)COIN -rbfFee);
   ASSERT_EQ(ledgers[0].getBlockNum(), UINT32_MAX);
   EXPECT_TRUE(ledgers[0].isOptInRBF());
   EXPECT_FALSE(ledgers[0].isChainedZC());

   //B
   ledgersAtBlocks = getLedgersPage(bridge_, delegateScrAddrB, 0);
   ASSERT_EQ(ledgersAtBlocks.size(), 8);
   for (unsigned i = 0; i < TestChain::ledgersB.size(); i++) {
      EXPECT_TRUE(
         checkLedgers(ledgersAtBlocks[i + 1],
            TestChain::ledgersB[i]));
   }

   lastEntry = ledgersAtBlocks[0];
   ASSERT_EQ(lastEntry.getTxHash(), tx3.getThisHash());
   EXPECT_EQ(lastEntry.getValue(), -20 * (int64_t)COIN);
   ASSERT_EQ(lastEntry.getBlockNum(), UINT32_MAX);
   EXPECT_TRUE(lastEntry.isOptInRBF());
   EXPECT_FALSE(lastEntry.isChainedZC());

   //C
   ledgersAtBlocks = getLedgersPage(bridge_, delegateScrAddrC, 0);
   ASSERT_EQ(ledgersAtBlocks.size(), 4);
   for (unsigned i = 0; i < TestChain::ledgersC.size(); i++) {
      EXPECT_TRUE(
         checkLedgers(ledgersAtBlocks[i],
            TestChain::ledgersC[i]));
   }

   //E
   ledgersAtBlocks = getLedgersPage(bridge_, delegateScrAddrE, 0);
   ASSERT_EQ(ledgersAtBlocks.size(), 3);
   for (unsigned i = 0; i < TestChain::ledgersE.size(); i++) {
      EXPECT_TRUE(
         checkLedgers(ledgersAtBlocks[i + 1],
            TestChain::ledgersE[i]));
   }

   lastEntry = ledgersAtBlocks[0];
   ASSERT_EQ(lastEntry.getTxHash(), tx3.getThisHash());
   EXPECT_EQ(lastEntry.getValue(), changeAmountRbf);
   ASSERT_EQ(lastEntry.getBlockNum(), UINT32_MAX);
   EXPECT_TRUE(lastEntry.isOptInRBF());
   EXPECT_FALSE(lastEntry.isChainedZC());
}

TEST_F(BridgeChainDataTests, ZeroConf_Reload)
{
   loadWallets({walletId_BCDE_});

   TestUtils::setBlocks({ "0", "1", "2", "3", "4", "5" }, blk0dat_);
   WebSocketServer::initAuthPeers({
      homedir_ / SERVER_AUTH_PEER_FILENAME, authPeersPassLbd_});
   WebSocketServer::start(theBDMt_->bdm(), true);

   ASSERT_TRUE(connectToIp(bridge_, "127.0.0.1", "9001", serverPubkey_));
   ASSERT_TRUE(registerWallets(bridge_));

   //start db, go online and wait on ready notif
   theBDMt_->start(Config::DBSettings::initMode());
   ASSERT_EQ(goOnline(bridge_), 5);

   //check balances
   auto balances = getAddrBalances(bridge_, walletId_BCDE_, accountId_BCDE_);
   ASSERT_EQ(balances.size(), 4);
   checkBalances(balances, 5, false);

   //check ledgers
   auto delegateId = getLedgerDelegateId(bridge_);
   ASSERT_FALSE(delegateId.empty());

   auto pageCount = getLedgersPageCount(bridge_, delegateId);
   EXPECT_EQ(pageCount, 1);

   auto ledgers = getLedgersPage(bridge_, delegateId, 0);
   ASSERT_EQ(ledgers.size(), 15);

   for (unsigned i = 0; i < ledgers.size(); i++) {
      EXPECT_TRUE(checkLedgers(ledgers[i], TestChain::ledgersBCDE[i])) << i;
   }

   //create recipient address
   auto recipientPrivKey = Cryptography::ECDSA::createNewPrivateKey();
   auto recipientPubKey = Cryptography::ECDSA::computePublicKey(
      recipientPrivKey, true);
   auto hash160 =  BtcUtils::getHash160(recipientPubKey);
   auto recipientAddr1 = BtcUtils::scrAddrToSegWitAddress(hash160);

   //get signed tx
   BinaryData signedTx;
   try {
      signedTx = createAndSignTx(bridge_,
         walletId_BCDE_, accountId_BCDE_,
         {}, {{ recipientAddr1, 11 * COIN }},
         TestChain::scrAddrC, 2, true,
         "privPass1"
      );
   } catch (const std::exception& e) {
      ASSERT_TRUE(false) << e.what();
   }
   ASSERT_FALSE(signedTx.empty());

   //broadcast
   broadcastTx(bridge_, signedTx);

   //grab amount for change output
   Tx tx(signedTx);
   auto changeOutput = tx.getTxOutCopy(1);
   uint64_t changeAmount = changeOutput.getAmount();
   EXPECT_TRUE(changeAmount > 8 * COIN);
   EXPECT_TRUE(changeAmount < 9 * COIN);

   //wait on zc notif
   try {
      auto zcLedgers = waitOnZc();
      ASSERT_EQ(zcLedgers.size(), 1);
      ASSERT_EQ(zcLedgers[0].getTxHash(), tx.getThisHash());
      EXPECT_EQ(zcLedgers[0].getValue(), -20 * (int64_t)COIN + changeAmount);
      ASSERT_EQ(zcLedgers[0].getBlockNum(), UINT32_MAX);
   } catch (const std::exception&) {
      ASSERT_TRUE(false);
   }

   //check balances
   balances = getAddrBalances(bridge_, walletId_BCDE_, accountId_BCDE_);
   ASSERT_EQ(balances.size(), 4);
   auto addrBBal = balances.at(TestChain::scrAddrB);
   auto testAddrBBal = TestChain::testAddrBalances[5].at(TestChain::scrAddrB);
   EXPECT_EQ(addrBBal[0], testAddrBBal[0] - (20 * COIN));
   EXPECT_EQ(addrBBal[1], testAddrBBal[1] - (20 * COIN));
   EXPECT_EQ(addrBBal[2], testAddrBBal[2] - (20 * COIN));

   addrBBal = balances.at(TestChain::scrAddrC);
   testAddrBBal = TestChain::testAddrBalances[5].at(TestChain::scrAddrC);
   EXPECT_EQ(addrBBal[0], testAddrBBal[0] + changeAmount);
   EXPECT_EQ(addrBBal[1], testAddrBBal[1]);
   EXPECT_EQ(addrBBal[2], testAddrBBal[2] + changeAmount);

   //check ledgers
   ledgers = getLedgersPage(bridge_, delegateId, 0);
   ASSERT_EQ(ledgers.size(), 16);
   for (unsigned i = 0; i < 15; i++) {
      EXPECT_TRUE(checkLedgers(ledgers[i+1], TestChain::ledgersBCDE[i])) << i;
   }

   auto lastEntry = ledgers[0];
   ASSERT_EQ(lastEntry.getTxHash(), tx.getThisHash());
   EXPECT_EQ(lastEntry.getValue(), -20 * (int64_t)COIN + changeAmount);
   ASSERT_EQ(lastEntry.getBlockNum(), UINT32_MAX);

   /* second zc */

   //get ZC utxo
   auto zcUtxos = getUTXOs(bridge_, walletId_BCDE_, accountId_BCDE_, 1);
   ASSERT_EQ(zcUtxos.size(), 1);

   //create second recipient address
   recipientPrivKey = Cryptography::ECDSA::createNewPrivateKey();
   recipientPubKey = Cryptography::ECDSA::computePublicKey(
      recipientPrivKey, true);
   hash160 =  BtcUtils::getHash160(recipientPubKey);
   auto recipientAddr2 = BtcUtils::scrAddrToSegWitAddress(hash160);

   //spend the zc utxo
   BinaryData signedTx2;
   try {
      signedTx2 = createAndSignTx(bridge_,
         walletId_BCDE_, accountId_BCDE_,
         zcUtxos, {{ recipientAddr2, 5 * COIN }},
         TestChain::scrAddrC, 1000, true,
         "privPass1"
      );
   } catch (const std::exception& e) {
      ASSERT_TRUE(false) << e.what();
   }
   ASSERT_FALSE(signedTx2.empty());

   Tx tx2(signedTx2);
   auto changeOutput2 = tx2.getTxOutCopy(1);
   uint64_t changeAmount2 = changeOutput2.getAmount();
   EXPECT_TRUE(changeAmount2 > 3 * COIN);
   EXPECT_TRUE(changeAmount2 < 4 * COIN);

   //broadcast & wait on zc notif
   broadcastTx(bridge_, signedTx2);
   try {
      auto zcLedgers = waitOnZc();
      ASSERT_EQ(zcLedgers.size(), 2);
      ASSERT_EQ(zcLedgers[0].getTxHash(), tx2.getThisHash());
      EXPECT_EQ(zcLedgers[0].getValue(), -changeAmount + changeAmount2);
      ASSERT_EQ(zcLedgers[0].getBlockNum(), UINT32_MAX);
      EXPECT_TRUE(zcLedgers[0].isChainedZC());

      ASSERT_EQ(zcLedgers[1].getTxHash(), tx.getThisHash());
      EXPECT_EQ(zcLedgers[1].getValue(), -20 * (int64_t)COIN + changeAmount);
      ASSERT_EQ(zcLedgers[1].getBlockNum(), UINT32_MAX);
      EXPECT_FALSE(zcLedgers[1].isChainedZC());
   } catch (const std::exception&) {
      ASSERT_TRUE(false);
   }

   //check balances
   balances = getAddrBalances(bridge_, walletId_BCDE_, accountId_BCDE_);
   ASSERT_EQ(balances.size(), 4);
   addrBBal = balances.at(TestChain::scrAddrB);
   testAddrBBal = TestChain::testAddrBalances[5].at(TestChain::scrAddrB);
   EXPECT_EQ(addrBBal[0], testAddrBBal[0] - (20 * COIN));
   EXPECT_EQ(addrBBal[1], testAddrBBal[1] - (20 * COIN));
   EXPECT_EQ(addrBBal[2], testAddrBBal[2] - (20 * COIN));

   addrBBal = balances.at(TestChain::scrAddrC);
   testAddrBBal = TestChain::testAddrBalances[5].at(TestChain::scrAddrC);
   EXPECT_EQ(addrBBal[0], testAddrBBal[0] + changeAmount2);
   EXPECT_EQ(addrBBal[1], testAddrBBal[1]);
   EXPECT_EQ(addrBBal[2], testAddrBBal[2] + changeAmount2);

   //check ledgers
   ledgers = getLedgersPage(bridge_, delegateId, 0);
   ASSERT_EQ(ledgers.size(), 17);
   for (unsigned i = 0; i < 15; i++) {
      EXPECT_TRUE(checkLedgers(ledgers[i+2], TestChain::ledgersBCDE[i])) << i;
   }

   ASSERT_EQ(ledgers[1].getTxHash(), tx.getThisHash());
   EXPECT_EQ(ledgers[1].getValue(), -20 * (int64_t)COIN + changeAmount);
   ASSERT_EQ(ledgers[1].getBlockNum(), UINT32_MAX);
   EXPECT_FALSE(ledgers[1].isChainedZC());

   ASSERT_EQ(ledgers[0].getTxHash(), tx2.getThisHash());
   EXPECT_EQ(ledgers[0].getValue(), -changeAmount + changeAmount2);
   ASSERT_EQ(ledgers[0].getBlockNum(), UINT32_MAX);
   EXPECT_TRUE(ledgers[0].isChainedZC());

   /* cycle bridge */
   bridge_.reset();
   replyQueue.clear();
   bridge_ = std::make_shared<Bridge::CppBridge>();
   bridge_->setWriteLambda([](MsgPtr payload) {
      std::unique_lock<std::mutex> lock(commsMutex);
         replyQueue.emplace_back(std::move(payload));
      commsCV.notify_all();
   });

   loadWallets({walletId_BCDE_});
   ASSERT_TRUE(connectToIp(bridge_, "127.0.0.1", "9001", serverPubkey_));
   ASSERT_TRUE(registerWallets(bridge_));
   ASSERT_EQ(goOnline(bridge_), 5);

   //check balances
   balances = getAddrBalances(bridge_, walletId_BCDE_, accountId_BCDE_);
   ASSERT_EQ(balances.size(), 4);
   addrBBal = balances.at(TestChain::scrAddrB);
   testAddrBBal = TestChain::testAddrBalances[5].at(TestChain::scrAddrB);
   EXPECT_EQ(addrBBal[0], testAddrBBal[0] - (20 * COIN));
   EXPECT_EQ(addrBBal[1], testAddrBBal[1] - (20 * COIN));
   EXPECT_EQ(addrBBal[2], testAddrBBal[2] - (20 * COIN));

   addrBBal = balances.at(TestChain::scrAddrC);
   testAddrBBal = TestChain::testAddrBalances[5].at(TestChain::scrAddrC);
   EXPECT_EQ(addrBBal[0], testAddrBBal[0] + changeAmount2);
   EXPECT_EQ(addrBBal[1], testAddrBBal[1]);
   EXPECT_EQ(addrBBal[2], testAddrBBal[2] + changeAmount2);

   //check ledgers
   ledgers = getLedgersPage(bridge_, delegateId, 0);
   ASSERT_EQ(ledgers.size(), 17);
   for (unsigned i = 0; i < 15; i++) {
      EXPECT_TRUE(checkLedgers(ledgers[i+2], TestChain::ledgersBCDE[i])) << i;
   }

   ASSERT_EQ(ledgers[1].getTxHash(), tx.getThisHash());
   EXPECT_EQ(ledgers[1].getValue(), -20 * (int64_t)COIN + changeAmount);
   ASSERT_EQ(ledgers[1].getBlockNum(), UINT32_MAX);
   EXPECT_FALSE(ledgers[1].isChainedZC());

   ASSERT_EQ(ledgers[0].getTxHash(), tx2.getThisHash());
   EXPECT_EQ(ledgers[0].getValue(), -changeAmount + changeAmount2);
   ASSERT_EQ(ledgers[0].getBlockNum(), UINT32_MAX);
   EXPECT_TRUE(ledgers[0].isChainedZC());

   /* mine the 2 tx */
   DBTestUtils::mineNewBlock(theBDMt_, TestChain::addrA, 1);
   ASSERT_EQ(waitOnNewBlock(), 6);

   //check balances
   balances = getAddrBalances(bridge_, walletId_BCDE_, accountId_BCDE_);
   ASSERT_EQ(balances.size(), 4);
   addrBBal = balances.at(TestChain::scrAddrB);
   testAddrBBal = TestChain::testAddrBalances[5].at(TestChain::scrAddrB);
   EXPECT_EQ(addrBBal[0], testAddrBBal[0] - (20 * COIN));
   EXPECT_EQ(addrBBal[1], testAddrBBal[1] - (20 * COIN));
   EXPECT_EQ(addrBBal[2], testAddrBBal[2] - (20 * COIN));

   addrBBal = balances.at(TestChain::scrAddrC);
   testAddrBBal = TestChain::testAddrBalances[5].at(TestChain::scrAddrC);
   EXPECT_EQ(addrBBal[0], testAddrBBal[0] + changeAmount2);
   EXPECT_EQ(addrBBal[1], testAddrBBal[1] + changeAmount2);
   EXPECT_EQ(addrBBal[2], testAddrBBal[2] + changeAmount2);

   //check ledgers
   ledgers = getLedgersPage(bridge_, delegateId, 0);
   ASSERT_EQ(ledgers.size(), 17);
   for (unsigned i = 0; i < 15; i++) {
      EXPECT_TRUE(checkLedgers(ledgers[i+2], TestChain::ledgersBCDE[i])) << i;
   }

   ASSERT_EQ(ledgers[1].getTxHash(), tx.getThisHash());
   EXPECT_EQ(ledgers[1].getValue(), -20 * (int64_t)COIN + changeAmount);
   ASSERT_EQ(ledgers[1].getBlockNum(), 6);
   EXPECT_FALSE(ledgers[1].isChainedZC());

   ASSERT_EQ(ledgers[0].getTxHash(), tx2.getThisHash());
   EXPECT_EQ(ledgers[0].getValue(), -changeAmount + changeAmount2);
   ASSERT_EQ(ledgers[0].getBlockNum(), 6);
   EXPECT_FALSE(ledgers[0].isChainedZC());
}

TEST_F(BridgeChainDataTests, ZeroConf_Reorg)
{
   loadWallets({walletId_BCDE_});

   TestUtils::setBlocks({ "0", "1", "2", "3", "4A" }, blk0dat_);
   WebSocketServer::initAuthPeers({
      homedir_ / SERVER_AUTH_PEER_FILENAME, authPeersPassLbd_});
   WebSocketServer::start(theBDMt_->bdm(), true);

   ASSERT_TRUE(connectToIp(bridge_, "127.0.0.1", "9001", serverPubkey_));
   ASSERT_TRUE(registerWallets(bridge_));

   //start db, go online and wait on ready notif
   theBDMt_->start(Config::DBSettings::initMode());
   ASSERT_EQ(goOnline(bridge_), 4);

   TestUtils::setBlocks({ "0", "1", "2", "3", "4A", "4", "5" }, blk0dat_);
   nodePtr_->notifyNewBlock();
   ASSERT_EQ(waitOnNewBlock(), 5);

   //check wallet balance
   auto wltBal = getWalletBalance(bridge_, walletId_BCDE_, accountId_BCDE_);
   ASSERT_EQ(wltBal.size(), 4);
   EXPECT_EQ(wltBal[0], TestChain::wltBal_BCDE[5][0]);
   EXPECT_EQ(wltBal[1], TestChain::wltBal_BCDE[5][1]);
   EXPECT_EQ(wltBal[2], TestChain::wltBal_BCDE[5][2]);
   EXPECT_EQ(wltBal[3], TestChain::wltBal_BCDE[5][3]);

   //check addr balances
   auto balances = getAddrBalances(bridge_, walletId_BCDE_, accountId_BCDE_);
   ASSERT_EQ(balances.size(), 4);
   checkBalances(balances, 5, false);

   //setup wallet ledger delegate
   auto delegateId = getLedgerDelegateId(bridge_);
   ASSERT_FALSE(delegateId.empty());
   auto pageCount = getLedgersPageCount(bridge_, delegateId);
   EXPECT_EQ(pageCount, 1);

   //setup address ledger delegates
   auto delegateScrAddrB = getLedgerDelegateIdForScrAddr(bridge_,
      walletId_BCDE_, accountId_BCDE_, TestChain::scrAddrB);
   ASSERT_FALSE(delegateScrAddrB.empty());
   auto delegateScrAddrC = getLedgerDelegateIdForScrAddr(bridge_,
      walletId_BCDE_, accountId_BCDE_, TestChain::scrAddrC);
   ASSERT_FALSE(delegateScrAddrC.empty());
   auto delegateScrAddrD = getLedgerDelegateIdForScrAddr(bridge_,
      walletId_BCDE_, accountId_BCDE_, TestChain::scrAddrD);
   ASSERT_FALSE(delegateScrAddrD.empty());
   auto delegateScrAddrE = getLedgerDelegateIdForScrAddr(bridge_,
      walletId_BCDE_, accountId_BCDE_, TestChain::scrAddrE);
   ASSERT_FALSE(delegateScrAddrE.empty());

   pageCount = getLedgersPageCount(bridge_, delegateScrAddrB);
   EXPECT_EQ(pageCount, 1);
   pageCount = getLedgersPageCount(bridge_, delegateScrAddrC);
   EXPECT_EQ(pageCount, 1);
   pageCount = getLedgersPageCount(bridge_, delegateScrAddrD);
   EXPECT_EQ(pageCount, 1);
   pageCount = getLedgersPageCount(bridge_, delegateScrAddrE);
   EXPECT_EQ(pageCount, 1);

   //check wallet ledgers
   auto ledgers = getLedgersPage(bridge_, delegateId, 0);
   ASSERT_EQ(ledgers.size(), 15);

   for (unsigned i = 0; i < ledgers.size(); i++) {
      EXPECT_TRUE(checkLedgers(ledgers[i], TestChain::ledgersBCDE[i])) << i;
   }

   /* address ledgers */

   //B
   auto ledgersAtBlocks = getLedgersPage(bridge_, delegateScrAddrB, 0);
   ASSERT_EQ(ledgersAtBlocks.size(), 7);
   for (unsigned i = 0; i < ledgersAtBlocks.size(); i++) {
      EXPECT_TRUE(
         checkLedgers(ledgersAtBlocks[i],
            TestChain::ledgersB[i]));
   }

   //C
   ledgersAtBlocks = getLedgersPage(bridge_, delegateScrAddrC, 0);
   ASSERT_EQ(ledgersAtBlocks.size(), 4);
   for (unsigned i = 0; i < ledgersAtBlocks.size(); i++) {
      EXPECT_TRUE(
         checkLedgers(ledgersAtBlocks[i],
            TestChain::ledgersC[i]));
   }

   //D
   ledgersAtBlocks = getLedgersPage(bridge_, delegateScrAddrD, 0);
   ASSERT_EQ(ledgersAtBlocks.size(), 4);
   for (unsigned i = 0; i < ledgersAtBlocks.size(); i++) {
      EXPECT_TRUE(
         checkLedgers(ledgersAtBlocks[i],
            TestChain::ledgersD[i]));
   }

   //E
   ledgersAtBlocks = getLedgersPage(bridge_, delegateScrAddrE, 0);
   ASSERT_EQ(ledgersAtBlocks.size(), 2);
   for (unsigned i = 0; i < ledgersAtBlocks.size(); i++) {
      EXPECT_TRUE(
         checkLedgers(ledgersAtBlocks[i],
            TestChain::ledgersE[i]));
   }

   /* create & push zc */

   //get UTXOs
   auto utxos = getUTXOs(bridge_, walletId_BCDE_, accountId_BCDE_, 0);
   ASSERT_EQ(utxos.size(), 8);

   //we want to spend output from block 5, tx2, index 0, since it
   //does not exist on our reorg chain
   unsigned utxoId = UINT32_MAX;
   for (unsigned i = 0; i < utxos.size(); i++) {
      const auto& utxo = utxos[i];
      if (utxo.getHeight() == 5 && utxo.getTxIndex() == 2 && utxo.getTxOutIndex() == 0) {
         utxoId = i;
         break;
      }
   }
   ASSERT_NE(utxoId, UINT32_MAX);

   //create recipient address
   auto recipientPrivKey = Cryptography::ECDSA::createNewPrivateKey();
   auto recipientPubKey = Cryptography::ECDSA::computePublicKey(
      recipientPrivKey, true);
   auto hash160 =  BtcUtils::getHash160(recipientPubKey);
   auto recipientAddr1 = BtcUtils::scrAddrToSegWitAddress(hash160);

   //get signed tx
   BinaryData signedTx;
   try {
      signedTx = createAndSignTx(bridge_,
         walletId_BCDE_, accountId_BCDE_,
         {utxos[utxoId]}, {{ recipientAddr1, 2 * COIN }},
         TestChain::scrAddrC, 2, true,
         "privPass1"
      );
   } catch (const std::exception& e) {
      ASSERT_TRUE(false) << e.what();
   }
   ASSERT_FALSE(signedTx.empty());

   //broadcast
   broadcastTx(bridge_, signedTx);

   //grab amount for change output
   Tx tx(signedTx);
   auto changeOutput = tx.getTxOutCopy(1);
   uint64_t changeAmount = changeOutput.getAmount();
   EXPECT_TRUE(changeAmount > 2 * COIN);
   EXPECT_TRUE(changeAmount < 3 * COIN);

   //wait on zc notif
   try {
      auto zcLedgers = waitOnZc();
      ASSERT_EQ(zcLedgers.size(), 1);
      ASSERT_EQ(zcLedgers[0].getTxHash(), tx.getThisHash());
      EXPECT_EQ(zcLedgers[0].getValue(), -5 * (int64_t)COIN + changeAmount);
      ASSERT_EQ(zcLedgers[0].getBlockNum(), UINT32_MAX);
   } catch (const std::exception&) {
      ASSERT_TRUE(false);
   }

   //check wallet balance
   wltBal = getWalletBalance(bridge_, walletId_BCDE_, accountId_BCDE_);
   ASSERT_EQ(wltBal.size(), 4);
   EXPECT_EQ(wltBal[0], TestChain::wltBal_BCDE[5][0] -(5 * COIN) + changeAmount);
   EXPECT_EQ(wltBal[1], TestChain::wltBal_BCDE[5][1] -(5 * COIN));
   EXPECT_EQ(wltBal[2], TestChain::wltBal_BCDE[5][2] -(5 * COIN) + changeAmount);
   EXPECT_EQ(wltBal[3], TestChain::wltBal_BCDE[5][3] + 1);

   //check addr balances
   balances = getAddrBalances(bridge_, walletId_BCDE_, accountId_BCDE_);
   ASSERT_EQ(balances.size(), 4);
   auto addrBBal = balances.at(TestChain::scrAddrD);
   auto testAddrBBal = TestChain::testAddrBalances[5].at(TestChain::scrAddrD);
   EXPECT_EQ(addrBBal[0], testAddrBBal[0] - (5 * COIN));
   EXPECT_EQ(addrBBal[1], testAddrBBal[1] - (5 * COIN));
   EXPECT_EQ(addrBBal[2], testAddrBBal[2] - (5 * COIN));
   EXPECT_EQ(addrBBal[3], testAddrBBal[3] + 1);

   addrBBal = balances.at(TestChain::scrAddrC);
   testAddrBBal = TestChain::testAddrBalances[5].at(TestChain::scrAddrC);
   EXPECT_EQ(addrBBal[0], testAddrBBal[0] + changeAmount);
   EXPECT_EQ(addrBBal[1], testAddrBBal[1]);
   EXPECT_EQ(addrBBal[2], testAddrBBal[2] + changeAmount);
   EXPECT_EQ(addrBBal[3], testAddrBBal[3] + 1);

   //check wallet ledgers
   ledgers = getLedgersPage(bridge_, delegateId, 0);
   ASSERT_EQ(ledgers.size(), 16);
   for (unsigned i = 0; i < 15; i++) {
      EXPECT_TRUE(checkLedgers(ledgers[i+1], TestChain::ledgersBCDE[i])) << i;
   }

   auto lastEntry = ledgers[0];
   ASSERT_EQ(lastEntry.getTxHash(), tx.getThisHash());
   EXPECT_EQ(lastEntry.getValue(), -5 * (int64_t)COIN + changeAmount);
   ASSERT_EQ(lastEntry.getBlockNum(), UINT32_MAX);

   /* check address ledgers */

   //C
   ledgersAtBlocks = getLedgersPage(bridge_, delegateScrAddrC, 0);
   ASSERT_EQ(ledgersAtBlocks.size(), 5);
   for (unsigned i = 0; i < TestChain::ledgersC.size(); i++) {
      EXPECT_TRUE(
         checkLedgers(ledgersAtBlocks[i + 1],
            TestChain::ledgersC[i]));
   }
   lastEntry = ledgersAtBlocks[0];
   ASSERT_EQ(lastEntry.getTxHash(), tx.getThisHash());
   EXPECT_EQ(lastEntry.getValue(), changeAmount);
   ASSERT_EQ(lastEntry.getBlockNum(), UINT32_MAX);

   //D
   ledgersAtBlocks = getLedgersPage(bridge_, delegateScrAddrD, 0);
   ASSERT_EQ(ledgersAtBlocks.size(), 5);
   for (unsigned i = 0; i < TestChain::ledgersD.size(); i++) {
      EXPECT_TRUE(
         checkLedgers(ledgersAtBlocks[i + 1],
            TestChain::ledgersD[i]));
   }
   lastEntry = ledgersAtBlocks[0];
   ASSERT_EQ(lastEntry.getTxHash(), tx.getThisHash());
   EXPECT_EQ(lastEntry.getValue(), -5 * (int64_t)COIN);
   ASSERT_EQ(lastEntry.getBlockNum(), UINT32_MAX);

   /* reorg to block 5A*/
   TestUtils::setBlocks({ "0", "1", "2", "3", "4A", "4", "5", "5A" }, blk0dat_);
   nodePtr_->notifyNewBlock();
   ASSERT_EQ(waitOnNewBlock(), 5);

   //check wallet balance
   wltBal = getWalletBalance(bridge_, walletId_BCDE_, accountId_BCDE_);
   ASSERT_EQ(wltBal.size(), 4);
   EXPECT_EQ(wltBal[0], TestChain::wltBal_BCDE_Reorg[5][0]);
   EXPECT_EQ(wltBal[1], TestChain::wltBal_BCDE_Reorg[5][1]);
   EXPECT_EQ(wltBal[2], TestChain::wltBal_BCDE_Reorg[5][2]);
   EXPECT_EQ(wltBal[3], TestChain::wltBal_BCDE_Reorg[5][3]);

   //check addr balances
   balances = getAddrBalances(bridge_, walletId_BCDE_, accountId_BCDE_);
   ASSERT_EQ(balances.size(), 4);
   checkBalances(balances, 5, true);

   //wallet ledgers
   pageCount = getLedgersPageCount(bridge_, delegateId);
   EXPECT_EQ(pageCount, 1);

   auto ledgersAt5Blocks = getLedgersPage(bridge_, delegateId, 0);
   ASSERT_EQ(ledgersAt5Blocks.size(), 11);

   for (unsigned i = 0; i < ledgersAt5Blocks.size(); i++) {
      EXPECT_TRUE(
         checkLedgers(ledgersAt5Blocks[i],
         TestChain::ledgersBCDE_Reorg[i]));
   }

   /* address ledgers */

   //B
   ledgersAtBlocks = getLedgersPage(bridge_, delegateScrAddrB, 0);
   ASSERT_EQ(ledgersAtBlocks.size(), 5);
   for (unsigned i = 0; i < ledgersAtBlocks.size(); i++) {
      EXPECT_TRUE(
         checkLedgers(ledgersAtBlocks[i],
            TestChain::ledgersB_Reorg[i]));
   }

   //C
   ledgersAtBlocks = getLedgersPage(bridge_, delegateScrAddrC, 0);
   ASSERT_EQ(ledgersAtBlocks.size(), 2);
   for (unsigned i = 0; i < ledgersAtBlocks.size(); i++) {
      EXPECT_TRUE(
         checkLedgers(ledgersAtBlocks[i],
            TestChain::ledgersC_Reorg[i]));
   }

   //D
   ledgersAtBlocks = getLedgersPage(bridge_, delegateScrAddrD, 0);
   ASSERT_EQ(ledgersAtBlocks.size(), 3);
   for (unsigned i = 0; i < ledgersAtBlocks.size(); i++) {
      EXPECT_TRUE(
         checkLedgers(ledgersAtBlocks[i],
            TestChain::ledgersD_Reorg[i]));
   }

   //E
   ledgersAtBlocks = getLedgersPage(bridge_, delegateScrAddrE, 0);
   ASSERT_EQ(ledgersAtBlocks.size(), 2);
   for (unsigned i = 0; i < ledgersAtBlocks.size(); i++) {
      EXPECT_TRUE(
         checkLedgers(ledgersAtBlocks[i],
            TestChain::ledgersE_Reorg[i]));
   }
}

TEST_F(BridgeChainDataTests, DISABLED_ZeroConf_RegisterWallet)
{
   //NOTE: reenable once supernode scanner is redesigned
   /* this test only works with a supernode db */

   //shutdown bdm, clean up db folder
   theBDMt_->shutdown();
   delete theBDMt_;
   nodePtr_ = nullptr;
   FileUtils::removeDirectory(ldbdir_);
   FileUtils::createDirectory(ldbdir_);

   //set db mode to supernode
   Config::reset();
   Config::DBSettings::setServiceType(SERVICE_UNITTEST_WITHWS);
   Config::parseArgs({
      "--datadir=./fakehomedir",
      "--dbdir=./ldbtestdir",
      "--satoshi-datadir=./blkfiletest",
      "--db-type=DB_SUPER",
      "--thread-count=3",
      "--public"},
      Config::ProcessType::DB
   );

   //reset db
   initBDM();
   nodePtr_ = std::dynamic_pointer_cast<NodeUnitTest>(
      Config::NetworkSettings::bitcoinNodes().first);
   nodePtr_->setBDM(theBDMt_->bdm());

   TestUtils::setBlocks({ "0", "1", "2", "3", "4", "5" }, blk0dat_);
   WebSocketServer::initAuthPeers({
      homedir_ / SERVER_AUTH_PEER_FILENAME, authPeersPassLbd_});
   WebSocketServer::start(theBDMt_->bdm(), true);

   //init bridge
   loadWallets({walletId_BCDE_});
   ASSERT_TRUE(connectToIp(bridge_, "127.0.0.1", "9001", serverPubkey_));
   ASSERT_TRUE(registerWallets(bridge_));

   //start db, go online and wait on ready notif
   theBDMt_->start(Config::DBSettings::initMode());
   ASSERT_EQ(goOnline(bridge_), 5);

   //check wallet balance
   auto wltBal = getWalletBalance(bridge_, walletId_BCDE_, accountId_BCDE_);
   ASSERT_EQ(wltBal.size(), 4);
   EXPECT_EQ(wltBal[0], TestChain::wltBal_BCDE[5][0]);
   EXPECT_EQ(wltBal[1], TestChain::wltBal_BCDE[5][1]);
   EXPECT_EQ(wltBal[2], TestChain::wltBal_BCDE[5][2]);
   EXPECT_EQ(wltBal[3], TestChain::wltBal_BCDE[5][3]);

   //check addr balances
   auto balances = getAddrBalances(bridge_, walletId_BCDE_, accountId_BCDE_);
   ASSERT_EQ(balances.size(), 4);
   checkBalances(balances, 5, false);

   //check ledgers
   auto delegateId = getLedgerDelegateId(bridge_);
   ASSERT_FALSE(delegateId.empty());

   auto pageCount = getLedgersPageCount(bridge_, delegateId);
   EXPECT_EQ(pageCount, 1);

   auto ledgers = getLedgersPage(bridge_, delegateId, 0);
   ASSERT_EQ(ledgers.size(), 15);

   for (unsigned i = 0; i < ledgers.size(); i++) {
      EXPECT_TRUE(checkLedgers(ledgers[i], TestChain::ledgersBCDE[i])) << i;
   }

   //get signed tx
   BinaryData signedTx;
   try {
      signedTx = createAndSignTx(bridge_,
         walletId_BCDE_, accountId_BCDE_,
         {}, {{ BtcUtils::scrAddrToBase58(TestChain::scrAddrA), 11 * COIN }},
         TestChain::scrAddrB, 2, false,
         "privPass1"
      );
   } catch (const std::exception& e) {
      ASSERT_TRUE(false) << e.what();
   }
   ASSERT_FALSE(signedTx.empty());

   //broadcast
   broadcastTx(bridge_, signedTx);

   //wait on zc notif
   Tx tx(signedTx);
   int64_t spentAmount = -1100000516;
   try {
      auto zcLedgers = waitOnZc();
      ASSERT_EQ(zcLedgers.size(), 1);
      ASSERT_EQ(zcLedgers[0].getTxHash(), tx.getThisHash());
      ASSERT_EQ(zcLedgers[0].getValue(), spentAmount);
      ASSERT_EQ(zcLedgers[0].getBlockNum(), UINT32_MAX);
   } catch (const std::exception&) {
      ASSERT_TRUE(false);
   }

   //check wallet balance
   wltBal = getWalletBalance(bridge_, walletId_BCDE_, accountId_BCDE_);
   ASSERT_EQ(wltBal.size(), 4);
   EXPECT_EQ(wltBal[0], TestChain::wltBal_BCDE[5][0] + spentAmount);
   EXPECT_EQ(wltBal[1], TestChain::wltBal_BCDE[5][1] - (20 * COIN));
   EXPECT_EQ(wltBal[2], TestChain::wltBal_BCDE[5][2] + spentAmount);
   EXPECT_EQ(wltBal[3], TestChain::wltBal_BCDE[5][3] + 1);

   //check addr balances
   balances = getAddrBalances(bridge_, walletId_BCDE_, accountId_BCDE_);
   ASSERT_EQ(balances.size(), 4);
   auto addrBBal = balances.at(TestChain::scrAddrB);
   auto testAddrBBal = TestChain::testAddrBalances[5].at(TestChain::scrAddrB);
   EXPECT_EQ(addrBBal[0], testAddrBBal[0] + spentAmount);
   EXPECT_EQ(addrBBal[1], testAddrBBal[1] - (20 * COIN));
   EXPECT_EQ(addrBBal[2], testAddrBBal[2] + spentAmount);

   //check ledgers
   ledgers = getLedgersPage(bridge_, delegateId, 0);
   ASSERT_EQ(ledgers.size(), 16);
   for (unsigned i = 0; i < 15; i++) {
      EXPECT_TRUE(checkLedgers(ledgers[i+1], TestChain::ledgersBCDE[i])) << i;
   }

   auto lastEntry = ledgers[0];
   ASSERT_EQ(lastEntry.getTxHash(), tx.getThisHash());
   ASSERT_EQ(lastEntry.getValue(), spentAmount);
   ASSERT_EQ(lastEntry.getBlockNum(), UINT32_MAX);

   /* load & register wallet AFLB */
   auto stagedIds = loadWallet(walletId_AFLB_);
   auto dbId = stagedIds.at(accountId_AFLB_);
   ASSERT_FALSE(dbId.empty());
   ASSERT_TRUE(registerWallet(bridge_, walletId_AFLB_, accountId_AFLB_, dbId));

   //recheck bcde balances
   wltBal = getWalletBalance(bridge_, walletId_BCDE_, accountId_BCDE_);
   ASSERT_EQ(wltBal.size(), 4);
   EXPECT_EQ(wltBal[0], TestChain::wltBal_BCDE[5][0] + spentAmount);
   EXPECT_EQ(wltBal[1], TestChain::wltBal_BCDE[5][1] - (20 * COIN));
   EXPECT_EQ(wltBal[2], TestChain::wltBal_BCDE[5][2] + spentAmount);
   EXPECT_EQ(wltBal[3], TestChain::wltBal_BCDE[5][3] + 1);

   balances = getAddrBalances(bridge_, walletId_BCDE_, accountId_BCDE_);
   ASSERT_EQ(balances.size(), 4);
   addrBBal = balances.at(TestChain::scrAddrB);
   testAddrBBal = TestChain::testAddrBalances[5].at(TestChain::scrAddrB);
   EXPECT_EQ(addrBBal[0], testAddrBBal[0] + spentAmount);
   EXPECT_EQ(addrBBal[1], testAddrBBal[1] - (20 * COIN));
   EXPECT_EQ(addrBBal[2], testAddrBBal[2] + spentAmount);

   //check aflb balances
   wltBal = getWalletBalance(bridge_, walletId_AFLB_, accountId_AFLB_);
   ASSERT_EQ(wltBal.size(), 4);
   EXPECT_EQ(wltBal[0], TestChain::wltBal_AFLB[5][0] + 11 * COIN);
   EXPECT_EQ(wltBal[1], TestChain::wltBal_AFLB[5][1]);
   EXPECT_EQ(wltBal[2], TestChain::wltBal_AFLB[5][2] + 11 * COIN);
   EXPECT_EQ(wltBal[3], TestChain::wltBal_AFLB[5][3] + 1);

   balances = getAddrBalances(bridge_, walletId_AFLB_, accountId_AFLB_);
   ASSERT_EQ(balances.size(), 6);
   addrBBal = balances.at(TestChain::scrAddrA);
   testAddrBBal = TestChain::testAddrBalances[5].at(TestChain::scrAddrA);
   EXPECT_EQ(addrBBal[0], testAddrBBal[0] + 11 * COIN);
   EXPECT_EQ(addrBBal[1], testAddrBBal[1]);
   EXPECT_EQ(addrBBal[2], testAddrBBal[2] + 11 * COIN);

   //check ledgers
   auto ledgersAtBlocks = getLedgersPage(bridge_, delegateId, 0);
   ASSERT_EQ(ledgersAtBlocks.size(), 28);

   std::vector<TestChain::LedgerEntryValue> combinedLedgers;
   combinedLedgers.insert(combinedLedgers.end(),
      TestChain::ledgersBCDE.begin(), TestChain::ledgersBCDE.end());
   combinedLedgers.insert(combinedLedgers.end(),
      TestChain::ledgersAFLB.begin(), TestChain::ledgersAFLB.end());
   ASSERT_EQ(combinedLedgers.size(), 26);
   std::sort(combinedLedgers.begin(), combinedLedgers.end());

   for (unsigned i = 0; i < combinedLedgers.size(); i++) {
      EXPECT_TRUE(
         checkLedgers(ledgersAtBlocks[i+2],
            combinedLedgers[i])) << i;
   }

   lastEntry = ledgersAtBlocks[1];
   ASSERT_EQ(lastEntry.getTxHash(), tx.getThisHash());
   ASSERT_EQ(lastEntry.getValue(), spentAmount);
   ASSERT_EQ(lastEntry.getBlockNum(), UINT32_MAX);

   lastEntry = ledgersAtBlocks[0];
   ASSERT_EQ(lastEntry.getTxHash(), tx.getThisHash());
   ASSERT_EQ(lastEntry.getValue(), 11 * COIN);
   ASSERT_EQ(lastEntry.getBlockNum(), UINT32_MAX);
}

TEST_F(BridgeChainDataTests, RestoreSynchronize)
{
   /*
   Checks that a restored wallet with history gets its use chain
   synchronized with on chain data; both length and address types.
   */

   //create BIP32 wallet, grab backup
   auto bip32WltId = createAWallet(bridge_, 50ms, 10, true);
   ASSERT_FALSE(bip32WltId.empty());
   auto bip32Backup = getWalletBackup(bridge_, bip32WltId, "pass1",
      Codec::Bridge::WalletBackup::Type::ARMORY200_B);
   ASSERT_FALSE(bip32Backup.empty());

   //create legacy wallet, grab backup
   auto legacyWltId = createAWallet(bridge_, 50ms, 10, false);
   ASSERT_FALSE(legacyWltId.empty());
   auto legacyBackup = getWalletBackup(bridge_, legacyWltId, "pass1",
      Codec::Bridge::WalletBackup::Type::ARMORY200_A);
   ASSERT_FALSE(legacyBackup.empty());

   //load BCDE, the new wallets are loaded as they're created
   loadWallets({walletId_BCDE_});

   //grab account ids for new wallets
   auto bip32AccIds = getWalletAccountIds(bridge_, bip32WltId);
   ASSERT_EQ(bip32AccIds.size(), 3);

   auto legacyAccIds = getWalletAccountIds(bridge_, legacyWltId);
   ASSERT_EQ(legacyAccIds.size(), 1);

   /* grab some addresses */
   auto getNewAddress = [](std::shared_ptr<Bridge::CppBridge> bridge,
      std::string wltId, std::string accId, unsigned count,
      uint32_t addrType = 0)->AddressData
   {
      for (unsigned i = 0; i < count - 1; i++) {
         getAddress(bridge, wltId, accId);
      }
      return getAddress(bridge, wltId, accId, addrType);
   };

   //advance the highest used index per each bip32 account
   auto bip32AccIdIter = bip32AccIds.begin();
   auto addrBip32Acc1 = getNewAddress(bridge_, bip32WltId, *bip32AccIdIter, 5);
   ASSERT_EQ(addrBip32Acc1.index, 5);
   ASSERT_TRUE(addrBip32Acc1.isUsed);
   auto addrBip32Acc1Next = getNewAddress(bridge_, bip32WltId, *bip32AccIdIter++, 1);

   auto addrBip32Acc2 = getNewAddress(bridge_, bip32WltId, *bip32AccIdIter, 3);
   ASSERT_EQ(addrBip32Acc2.index, 3);
   ASSERT_TRUE(addrBip32Acc2.isUsed);
   auto addrBip32Acc2Next = getNewAddress(bridge_, bip32WltId, *bip32AccIdIter++, 1);

   auto addrBip32Acc3 = getNewAddress(bridge_, bip32WltId, *bip32AccIdIter, 7);
   ASSERT_EQ(addrBip32Acc3.index, 7);
   ASSERT_TRUE(addrBip32Acc3.isUsed);
   auto addrBip32Acc3Next = getNewAddress(bridge_, bip32WltId, *bip32AccIdIter++, 1);

   //get a non default address from the legacy wallet
   auto addrLegacy = getNewAddress(bridge_, legacyWltId, *legacyAccIds.begin(), 4,
      uint32_t(AddressEntryType::P2PK | AddressEntryType::P2SH));
   ASSERT_EQ(addrLegacy.index, 4);
   ASSERT_EQ(addrLegacy.type, uint32_t(AddressEntryType::P2PK | AddressEntryType::P2SH));
   auto addrLegacyNext = getNewAddress(bridge_, legacyWltId, *legacyAccIds.begin(), 1);

   //register wallets, go online
   TestUtils::setBlocks({ "0", "1", "2", "3", "4A", "4", "5" }, blk0dat_);
   WebSocketServer::initAuthPeers({
      homedir_ / SERVER_AUTH_PEER_FILENAME, authPeersPassLbd_});
   WebSocketServer::start(theBDMt_->bdm(), true);

   ASSERT_TRUE(connectToIp(bridge_, "127.0.0.1", "9001", serverPubkey_));
   ASSERT_TRUE(registerWallets(bridge_));

   //start db, go online and wait on ready notif
   theBDMt_->start(Config::DBSettings::initMode());
   ASSERT_EQ(goOnline(bridge_), 5);

   //get signed tx
   BinaryData signedTx;
   try {
      signedTx = createAndSignTx(bridge_,
         walletId_BCDE_, accountId_BCDE_,
         {}, {
            { addrBip32Acc1.addrStr , 1 * COIN },
            { addrBip32Acc2.addrStr , 2 * COIN },
            { addrBip32Acc3.addrStr , 3 * COIN },
            { addrLegacy.addrStr    , 4 * COIN }
         }, TestChain::scrAddrC, 2, true,
         "privPass1"
      );
   } catch (const std::exception& e) {
      ASSERT_TRUE(false) << e.what();
   }
   ASSERT_FALSE(signedTx.empty());

   //broadcast
   broadcastTx(bridge_, signedTx);

   //grab amount for change output
   Tx tx(signedTx);
   auto changeOutput = tx.getTxOutCopy(4);
   uint64_t changeAmount = changeOutput.getAmount();
   EXPECT_TRUE(changeAmount > 9 * COIN);
   EXPECT_TRUE(changeAmount < 10 * COIN);

   //wait on zc notif
   try {
      auto zcLedgers = waitOnZc();
      ASSERT_EQ(zcLedgers.size(), 5);
      ASSERT_EQ(zcLedgers[4].getTxHash(), tx.getThisHash());
      EXPECT_EQ(zcLedgers[4].getValue(), -20 * (int64_t)COIN + changeAmount);
      ASSERT_EQ(zcLedgers[4].getBlockNum(), UINT32_MAX);
   } catch (const std::exception&) {
      ASSERT_TRUE(false);
   }

   //check balances and highest used index
   std::filesystem::path wltBip32Path, wltLegacyPath;
   {
      bip32AccIdIter = bip32AccIds.begin();
      auto wltBal = getWalletBalance(bridge_, bip32WltId, *bip32AccIdIter);
      auto wltData1 = getWalletData(bridge_, bip32WltId, *bip32AccIdIter++);
      ASSERT_EQ(wltBal.size(), 4);
      EXPECT_EQ(wltBal[0], 1 * COIN);
      EXPECT_EQ(wltBal[1], 0);
      EXPECT_EQ(wltBal[2], 1 * COIN);
      EXPECT_EQ(wltBal[3], 1);
      EXPECT_EQ(wltData1.useCount, 6);
      EXPECT_EQ(wltData1.lookup, 10);
      wltBip32Path = wltData1.path;

      wltBal = getWalletBalance(bridge_, bip32WltId, *bip32AccIdIter);
      auto wltData2 = getWalletData(bridge_, bip32WltId, *bip32AccIdIter++);
      ASSERT_EQ(wltBal.size(), 4);
      EXPECT_EQ(wltBal[0], 2 * COIN);
      EXPECT_EQ(wltBal[1], 0);
      EXPECT_EQ(wltBal[2], 2 * COIN);
      EXPECT_EQ(wltBal[3], 1);
      EXPECT_EQ(wltData2.useCount, 4);
      EXPECT_EQ(wltData2.lookup, 10);

      wltBal = getWalletBalance(bridge_, bip32WltId, *bip32AccIdIter);
      auto wltData3 = getWalletData(bridge_, bip32WltId, *bip32AccIdIter);
      ASSERT_EQ(wltBal.size(), 4);
      EXPECT_EQ(wltBal[0], 3 * COIN);
      EXPECT_EQ(wltBal[1], 0);
      EXPECT_EQ(wltBal[2], 3 * COIN);
      EXPECT_EQ(wltBal[3], 1);
      EXPECT_EQ(wltData3.useCount, 8);
      EXPECT_EQ(wltData3.lookup, 10);

      wltBal = getWalletBalance(bridge_, legacyWltId, *legacyAccIds.begin());
      auto wltData4 = getWalletData(bridge_, legacyWltId, *legacyAccIds.begin());
      ASSERT_EQ(wltBal.size(), 4);
      EXPECT_EQ(wltBal[0], 4 * COIN);
      EXPECT_EQ(wltBal[1], 0);
      EXPECT_EQ(wltBal[2], 4 * COIN);
      EXPECT_EQ(wltBal[3], 1);
      EXPECT_EQ(wltData4.useCount, 5);
      EXPECT_EQ(wltData4.lookup, 10);
      wltLegacyPath = wltData4.path;
   }

   /* delete the 2 new wallets */

   //check wallet path
   ASSERT_TRUE(FileUtils::pathExists(wltBip32Path, 0));
   ASSERT_TRUE(FileUtils::pathExists(wltLegacyPath, 0));

   //delete bip32 wlt
   {
      uint64_t refId = rand();
      capnp::MallocMessageBuilder message;
      auto toBridge = message.initRoot<Codec::Bridge::ToBridge>();
      toBridge.setReferenceId(refId);
      auto request = toBridge.initWalletManager();
      request.setDeleteWallet(bip32WltId);
      auto rawReq = serializeCapnp(message);
      pushRequest(bridge_, rawReq);

      //validate reply
      while (true) {
         auto result = waitOnReply();
         kj::ArrayPtr<const capnp::word> words(
            reinterpret_cast<const capnp::word*>(result->data.getPtr()),
            result->data.getSize() / sizeof(capnp::word));
         capnp::FlatArrayMessageReader reader(words);
         auto fromBridge = reader.getRoot<Codec::Bridge::FromBridge>();
         if (fromBridge.which() != Codec::Bridge::FromBridge::REPLY) {
            continue;
         }
         auto reply = fromBridge.getReply();
         ASSERT_TRUE(reply.getSuccess());
         ASSERT_EQ(reply.getReferenceId(), refId);
         ASSERT_FALSE(FileUtils::pathExists(wltBip32Path, 0));
         break;
      }
   }

   //delete legacy wlt
   {
      uint64_t refId = rand();
      capnp::MallocMessageBuilder message;
      auto toBridge = message.initRoot<Codec::Bridge::ToBridge>();
      toBridge.setReferenceId(refId);
      auto request = toBridge.initWalletManager();
      request.setDeleteWallet(legacyWltId);
      auto rawReq = serializeCapnp(message);
      pushRequest(bridge_, rawReq);

      //validate reply
      while (true) {
         auto result = waitOnReply();
         kj::ArrayPtr<const capnp::word> words(
            reinterpret_cast<const capnp::word*>(result->data.getPtr()),
            result->data.getSize() / sizeof(capnp::word));
         capnp::FlatArrayMessageReader reader(words);
         auto fromBridge = reader.getRoot<Codec::Bridge::FromBridge>();
         if (fromBridge.which() != Codec::Bridge::FromBridge::REPLY) {
            continue;
         }
         auto reply = fromBridge.getReply();
         ASSERT_TRUE(reply.getSuccess());
         ASSERT_EQ(reply.getReferenceId(), refId);
         ASSERT_FALSE(FileUtils::pathExists(wltLegacyPath, 0));
         break;
      }
   }

   /* cycle bridge */
   bridge_.reset();
   replyQueue.clear();
   bridge_ = std::make_shared<Bridge::CppBridge>();
   bridge_->setWriteLambda([](MsgPtr payload) {
      std::unique_lock<std::mutex> lock(commsMutex);
         replyQueue.emplace_back(std::move(payload));
      commsCV.notify_all();
   });

   //restore bip32 wlt
   restoreWallet(bridge_, bip32Backup, bip32WltId,
      Codec::Bridge::WalletBackup::Type::ARMORY200_B,
      "pass2", 50ms, 0, false, 500);

   {
      bip32AccIdIter = bip32AccIds.begin();
      auto wltData1 = getWalletData(bridge_, bip32WltId, *bip32AccIdIter++);
      EXPECT_EQ(wltData1.useCount, 0);
      EXPECT_EQ(wltData1.lookup, 499);

      auto wltData2 = getWalletData(bridge_, bip32WltId, *bip32AccIdIter++);
      EXPECT_EQ(wltData2.useCount, -1);
      EXPECT_EQ(wltData2.lookup, 499);

      auto wltData3 = getWalletData(bridge_, bip32WltId, *bip32AccIdIter);
      EXPECT_EQ(wltData3.useCount, -1);
      EXPECT_EQ(wltData3.lookup, 499);
   }

   //restore legacy wlt
   restoreWallet(bridge_, legacyBackup, legacyWltId,
      Codec::Bridge::WalletBackup::Type::ARMORY200_A,
      "pass3", 50ms, 0, false, 500);

   {
      auto wltData4 = getWalletData(bridge_, legacyWltId, *legacyAccIds.begin());
      EXPECT_EQ(wltData4.useCount, 0);
      EXPECT_EQ(wltData4.lookup, 500);
   }

   //go online
   ASSERT_TRUE(connectToIp(bridge_, "127.0.0.1", "9001", serverPubkey_));
   ASSERT_TRUE(registerWallets(bridge_));
   ASSERT_EQ(goOnline(bridge_), 5);

   /* check chain lengths */
   {
      bip32AccIdIter = bip32AccIds.begin();
      auto wltBal = getWalletBalance(bridge_, bip32WltId, *bip32AccIdIter);
      auto wltData1 = getWalletData(bridge_, bip32WltId, *bip32AccIdIter);
      auto nextAddr1 = getNewAddress(bridge_, bip32WltId, *bip32AccIdIter++, 1);
      ASSERT_EQ(wltBal.size(), 4);
      EXPECT_EQ(wltBal[0], 1 * COIN);
      EXPECT_EQ(wltBal[1], 0);
      EXPECT_EQ(wltBal[2], 1 * COIN);
      EXPECT_EQ(wltBal[3], 1);
      EXPECT_EQ(wltData1.useCount, 5);
      EXPECT_EQ(wltData1.lookup, 499);
      EXPECT_EQ(nextAddr1.addrStr, addrBip32Acc1Next.addrStr);

      wltBal = getWalletBalance(bridge_, bip32WltId, *bip32AccIdIter);
      auto wltData2 = getWalletData(bridge_, bip32WltId, *bip32AccIdIter);
      auto nextAddr2 = getNewAddress(bridge_, bip32WltId, *bip32AccIdIter++, 1);
      ASSERT_EQ(wltBal.size(), 4);
      EXPECT_EQ(wltBal[0], 2 * COIN);
      EXPECT_EQ(wltBal[1], 0);
      EXPECT_EQ(wltBal[2], 2 * COIN);
      EXPECT_EQ(wltBal[3], 1);
      EXPECT_EQ(wltData2.useCount, 3);
      EXPECT_EQ(wltData2.lookup, 499);
      EXPECT_EQ(nextAddr2.addrStr, addrBip32Acc2Next.addrStr);

      wltBal = getWalletBalance(bridge_, bip32WltId, *bip32AccIdIter);
      auto wltData3 = getWalletData(bridge_, bip32WltId, *bip32AccIdIter);
      auto nextAddr3 = getNewAddress(bridge_, bip32WltId, *bip32AccIdIter, 1);
      ASSERT_EQ(wltBal.size(), 4);
      EXPECT_EQ(wltBal[0], 3 * COIN);
      EXPECT_EQ(wltBal[1], 0);
      EXPECT_EQ(wltBal[2], 3 * COIN);
      EXPECT_EQ(wltBal[3], 1);
      EXPECT_EQ(wltData3.useCount, 7);
      EXPECT_EQ(wltData3.lookup, 499);
      EXPECT_EQ(nextAddr3.addrStr, addrBip32Acc3Next.addrStr);

      wltBal = getWalletBalance(bridge_, legacyWltId, *legacyAccIds.begin());
      auto wltData4 = getWalletData(bridge_, legacyWltId, *legacyAccIds.begin());
      auto nextAddr4 = getNewAddress(bridge_, legacyWltId, *legacyAccIds.begin(), 1);
      ASSERT_EQ(wltBal.size(), 4);
      EXPECT_EQ(wltBal[0], 4 * COIN);
      EXPECT_EQ(wltBal[1], 0);
      EXPECT_EQ(wltBal[2], 4 * COIN);
      EXPECT_EQ(wltBal[3], 1);
      EXPECT_EQ(wltData4.useCount, 4);
      EXPECT_EQ(wltData4.lookup, 500);
      EXPECT_EQ(nextAddr4.addrStr, addrLegacyNext.addrStr);

      //check legacy wallet address type
      auto iter = wltData4.addresses.find(addrLegacy.hash);
      ASSERT_NE(iter, wltData4.addresses.end());
      EXPECT_EQ(iter->addrStr, addrLegacy.addrStr);
      EXPECT_EQ(iter->type, addrLegacy.type);
   }
}

TEST_F(BridgeChainDataTests, ZeroConf_SpendNew)
{
   loadWallets({walletId_BCDE_});
   auto legacyWltId = createAWallet(bridge_, 50ms, 10, false);
   ASSERT_FALSE(legacyWltId.empty());

   auto legacyAccIds = getWalletAccountIds(bridge_, legacyWltId);
   ASSERT_EQ(legacyAccIds.size(), 1);
   auto legacyAccId = *legacyAccIds.begin();
   ASSERT_FALSE(legacyAccId.empty());

   TestUtils::setBlocks({ "0", "1", "2", "3", "4", "5" }, blk0dat_);
   WebSocketServer::initAuthPeers({
      homedir_ / SERVER_AUTH_PEER_FILENAME, authPeersPassLbd_});
   WebSocketServer::start(theBDMt_->bdm(), true);

   ASSERT_TRUE(connectToIp(bridge_, "127.0.0.1", "9001", serverPubkey_));
   ASSERT_TRUE(registerWallets(bridge_));

   //start db, go online and wait on ready notif
   theBDMt_->start(Config::DBSettings::initMode());
   ASSERT_EQ(goOnline(bridge_), 5);

   //check wallet balance
   auto wltBal = getWalletBalance(bridge_, walletId_BCDE_, accountId_BCDE_);
   ASSERT_EQ(wltBal.size(), 4);
   EXPECT_EQ(wltBal[0], TestChain::wltBal_BCDE[5][0]);
   EXPECT_EQ(wltBal[1], TestChain::wltBal_BCDE[5][1]);
   EXPECT_EQ(wltBal[2], TestChain::wltBal_BCDE[5][2]);
   EXPECT_EQ(wltBal[3], TestChain::wltBal_BCDE[5][3]);

   wltBal = getWalletBalance(bridge_, legacyWltId, legacyAccId);
   ASSERT_EQ(wltBal.size(), 4);
   EXPECT_EQ(wltBal[0], 0);
   EXPECT_EQ(wltBal[1], 0);
   EXPECT_EQ(wltBal[2], 0);
   EXPECT_EQ(wltBal[3], 0);

   //check addr balances
   auto balances = getAddrBalances(bridge_, walletId_BCDE_, accountId_BCDE_);
   ASSERT_EQ(balances.size(), 4);
   checkBalances(balances, 5, false);

   //setup wallet ledger delegates
   auto delegateId = getLedgerDelegateId(bridge_);
   ASSERT_FALSE(delegateId.empty());
   auto pageCount = getLedgersPageCount(bridge_, delegateId);
   EXPECT_EQ(pageCount, 1);

   //setup address ledger delegates
   auto delegateScrAddrB = getLedgerDelegateIdForScrAddr(bridge_,
      walletId_BCDE_, accountId_BCDE_, TestChain::scrAddrB);
   ASSERT_FALSE(delegateScrAddrB.empty());
   auto delegateScrAddrC = getLedgerDelegateIdForScrAddr(bridge_,
      walletId_BCDE_, accountId_BCDE_, TestChain::scrAddrC);
   ASSERT_FALSE(delegateScrAddrC.empty());
   auto delegateScrAddrD = getLedgerDelegateIdForScrAddr(bridge_,
      walletId_BCDE_, accountId_BCDE_, TestChain::scrAddrD);
   ASSERT_FALSE(delegateScrAddrD.empty());
   auto delegateScrAddrE = getLedgerDelegateIdForScrAddr(bridge_,
      walletId_BCDE_, accountId_BCDE_, TestChain::scrAddrE);
   ASSERT_FALSE(delegateScrAddrE.empty());

   pageCount = getLedgersPageCount(bridge_, delegateScrAddrB);
   EXPECT_EQ(pageCount, 1);
   pageCount = getLedgersPageCount(bridge_, delegateScrAddrC);
   EXPECT_EQ(pageCount, 1);
   pageCount = getLedgersPageCount(bridge_, delegateScrAddrD);
   EXPECT_EQ(pageCount, 1);
   pageCount = getLedgersPageCount(bridge_, delegateScrAddrE);
   EXPECT_EQ(pageCount, 1);

   // check wallet ledgers
   auto ledgers = getLedgersPage(bridge_, delegateId, 0);
   ASSERT_EQ(ledgers.size(), 15);

   for (unsigned i = 0; i < ledgers.size(); i++) {
      EXPECT_TRUE(checkLedgers(ledgers[i], TestChain::ledgersBCDE[i])) << i;
   }

   /* check address ledgers */

   //B
   auto ledgersAtBlocks = getLedgersPage(bridge_, delegateScrAddrB, 0);
   ASSERT_EQ(ledgersAtBlocks.size(), 7);
   for (unsigned i = 0; i < ledgersAtBlocks.size(); i++) {
      EXPECT_TRUE(
         checkLedgers(ledgersAtBlocks[i],
            TestChain::ledgersB[i]));
   }

   //C
   ledgersAtBlocks = getLedgersPage(bridge_, delegateScrAddrC, 0);
   ASSERT_EQ(ledgersAtBlocks.size(), 4);
   for (unsigned i = 0; i < ledgersAtBlocks.size(); i++) {
      EXPECT_TRUE(
         checkLedgers(ledgersAtBlocks[i],
            TestChain::ledgersC[i]));
   }

   //D
   ledgersAtBlocks = getLedgersPage(bridge_, delegateScrAddrD, 0);
   ASSERT_EQ(ledgersAtBlocks.size(), 4);
   for (unsigned i = 0; i < ledgersAtBlocks.size(); i++) {
      EXPECT_TRUE(
         checkLedgers(ledgersAtBlocks[i],
            TestChain::ledgersD[i]));
   }

   //E
   ledgersAtBlocks = getLedgersPage(bridge_, delegateScrAddrE, 0);
   ASSERT_EQ(ledgersAtBlocks.size(), 2);
   for (unsigned i = 0; i < ledgersAtBlocks.size(); i++) {
      EXPECT_TRUE(
         checkLedgers(ledgersAtBlocks[i],
            TestChain::ledgersE[i]));
   }

   //create recipient address
   auto legacyAddr = getAddress(bridge_, legacyWltId, legacyAccId);

   //get signed tx
   BinaryData signedTx;
   try {
      signedTx = createAndSignTx(bridge_,
         walletId_BCDE_, accountId_BCDE_,
         {}, {{ legacyAddr.addrStr, 11 * COIN }},
         TestChain::scrAddrC, 2, true,
         "privPass1"
      );
   } catch (const std::exception& e) {
      ASSERT_TRUE(false) << e.what();
   }
   ASSERT_FALSE(signedTx.empty());

   //broadcast
   broadcastTx(bridge_, signedTx);

   //grab amount for change output
   Tx tx(signedTx);
   auto changeOutput = tx.getTxOutCopy(1);
   uint64_t changeAmount = changeOutput.getAmount();
   EXPECT_TRUE(changeAmount > 8 * COIN);
   EXPECT_TRUE(changeAmount < 9 * COIN);

   //wait on zc notif
   try {
      auto zcLedgers = waitOnZc();
      ASSERT_EQ(zcLedgers.size(), 2);
      ASSERT_EQ(zcLedgers[1].getTxHash(), tx.getThisHash());
      EXPECT_EQ(zcLedgers[1].getValue(), -20 * (int64_t)COIN + changeAmount);
      ASSERT_EQ(zcLedgers[1].getBlockNum(), UINT32_MAX);
      EXPECT_TRUE(zcLedgers[1].isOptInRBF());

      ASSERT_EQ(zcLedgers[0].getTxHash(), tx.getThisHash());
      EXPECT_EQ(zcLedgers[0].getValue(), 11 * COIN);
      ASSERT_EQ(zcLedgers[0].getBlockNum(), UINT32_MAX);
      EXPECT_TRUE(zcLedgers[0].isOptInRBF());
   } catch (const std::exception&) {
      ASSERT_TRUE(false);
   }

   //check wallet balance
   wltBal = getWalletBalance(bridge_, walletId_BCDE_, accountId_BCDE_);
   ASSERT_EQ(wltBal.size(), 4);
   EXPECT_EQ(wltBal[0], TestChain::wltBal_BCDE[5][0] - (20 * COIN) + changeAmount);
   EXPECT_EQ(wltBal[1], TestChain::wltBal_BCDE[5][1] - (20 * COIN));
   EXPECT_EQ(wltBal[2], TestChain::wltBal_BCDE[5][2] - (20 * COIN) + changeAmount);
   EXPECT_EQ(wltBal[3], TestChain::wltBal_BCDE[5][3] + 1);

   wltBal = getWalletBalance(bridge_, legacyWltId, legacyAccId);
   ASSERT_EQ(wltBal.size(), 4);
   EXPECT_EQ(wltBal[0], 11 * COIN);
   EXPECT_EQ(wltBal[1], 0);
   EXPECT_EQ(wltBal[2], 11 * COIN);
   EXPECT_EQ(wltBal[3], 1);

   //check addr balances
   balances = getAddrBalances(bridge_, walletId_BCDE_, accountId_BCDE_);
   ASSERT_EQ(balances.size(), 4);
   auto addrBBal = balances.at(TestChain::scrAddrB);
   auto testAddrBBal = TestChain::testAddrBalances[5].at(TestChain::scrAddrB);
   EXPECT_EQ(addrBBal[0], testAddrBBal[0] - (20 * COIN));
   EXPECT_EQ(addrBBal[1], testAddrBBal[1] - (20 * COIN));
   EXPECT_EQ(addrBBal[2], testAddrBBal[2] - (20 * COIN));
   EXPECT_EQ(addrBBal[3], testAddrBBal[3] + 1);

   addrBBal = balances.at(TestChain::scrAddrC);
   testAddrBBal = TestChain::testAddrBalances[5].at(TestChain::scrAddrC);
   EXPECT_EQ(addrBBal[0], testAddrBBal[0] + changeAmount);
   EXPECT_EQ(addrBBal[1], testAddrBBal[1]);
   EXPECT_EQ(addrBBal[2], testAddrBBal[2] + changeAmount);
   EXPECT_EQ(addrBBal[3], testAddrBBal[3] + 1);

   balances = getAddrBalances(bridge_, legacyWltId, legacyAccId);
   ASSERT_EQ(balances.size(), 1);
   addrBBal = balances.at(legacyAddr.hash);
   EXPECT_EQ(addrBBal[0], 11 * COIN);
   EXPECT_EQ(addrBBal[1], 0);
   EXPECT_EQ(addrBBal[2], 11 * COIN);
   EXPECT_EQ(addrBBal[3], 1);

   //check wallet ledgers
   ledgers = getLedgersPage(bridge_, delegateId, 0);
   ASSERT_EQ(ledgers.size(), 17);
   for (unsigned i = 0; i < 15; i++) {
      EXPECT_TRUE(checkLedgers(ledgers[i+2], TestChain::ledgersBCDE[i])) << i;
   }

   auto lastEntry = ledgers[1];
   ASSERT_EQ(lastEntry.getTxHash(), tx.getThisHash());
   EXPECT_EQ(lastEntry.getValue(), -20 * (int64_t)COIN + changeAmount);
   ASSERT_EQ(lastEntry.getBlockNum(), UINT32_MAX);
   EXPECT_TRUE(lastEntry.isOptInRBF());

   lastEntry = ledgers[0];
   ASSERT_EQ(lastEntry.getTxHash(), tx.getThisHash());
   EXPECT_EQ(lastEntry.getValue(), 11 * (int64_t)COIN);
   ASSERT_EQ(lastEntry.getBlockNum(), UINT32_MAX);
   EXPECT_TRUE(lastEntry.isOptInRBF());

   /* address ledgers */

   //B
   ledgersAtBlocks = getLedgersPage(bridge_, delegateScrAddrB, 0);
   ASSERT_EQ(ledgersAtBlocks.size(), 8);
   for (unsigned i = 0; i < TestChain::ledgersB.size(); i++) {
      EXPECT_TRUE(
         checkLedgers(ledgersAtBlocks[i + 1],
            TestChain::ledgersB[i]));
   }

   lastEntry = ledgersAtBlocks[0];
   ASSERT_EQ(lastEntry.getTxHash(), tx.getThisHash());
   EXPECT_EQ(lastEntry.getValue(), -20 * (int64_t)COIN);
   ASSERT_EQ(lastEntry.getBlockNum(), UINT32_MAX);
   EXPECT_TRUE(lastEntry.isOptInRBF());
   EXPECT_FALSE(lastEntry.isChainedZC());

   //C
   ledgersAtBlocks = getLedgersPage(bridge_, delegateScrAddrC, 0);
   ASSERT_EQ(ledgersAtBlocks.size(), 5);
   for (unsigned i = 0; i < TestChain::ledgersC.size(); i++) {
      EXPECT_TRUE(
         checkLedgers(ledgersAtBlocks[i + 1],
            TestChain::ledgersC[i]));
   }

   lastEntry = ledgersAtBlocks[0];
   ASSERT_EQ(lastEntry.getTxHash(), tx.getThisHash());
   EXPECT_EQ(lastEntry.getValue(), changeAmount);
   ASSERT_EQ(lastEntry.getBlockNum(), UINT32_MAX);
   EXPECT_TRUE(lastEntry.isOptInRBF());
   EXPECT_FALSE(lastEntry.isChainedZC());

   /* mine the tx */
   DBTestUtils::mineNewBlock(theBDMt_, TestChain::addrA, 10);
   ASSERT_EQ(waitOnNewBlock(), 15);

   //check wallet balance
   wltBal = getWalletBalance(bridge_, walletId_BCDE_, accountId_BCDE_);
   ASSERT_EQ(wltBal.size(), 4);
   EXPECT_EQ(wltBal[0], TestChain::wltBal_BCDE[5][0] - (20 * COIN) + changeAmount);
   EXPECT_EQ(wltBal[1], TestChain::wltBal_BCDE[5][1] - (20 * COIN) + changeAmount);
   EXPECT_EQ(wltBal[2], 100 * COIN);
   EXPECT_EQ(wltBal[3], TestChain::wltBal_BCDE[5][3] + 1);

   wltBal = getWalletBalance(bridge_, legacyWltId, legacyAccId);
   ASSERT_EQ(wltBal.size(), 4);
   EXPECT_EQ(wltBal[0], 11 * COIN);
   EXPECT_EQ(wltBal[1], 11 * COIN);
   EXPECT_EQ(wltBal[2], 0ULL);
   EXPECT_EQ(wltBal[3], 1);

   /* spend from new wallet */

   //create recipient address
   auto recipientPrivKey = Cryptography::ECDSA::createNewPrivateKey();
   auto recipientPubKey = Cryptography::ECDSA::computePublicKey(
      recipientPrivKey, true);
   auto hash160 =  BtcUtils::getHash160(recipientPubKey);
   auto recipientAddr1 = BtcUtils::scrAddrToSegWitAddress(hash160);

   //change address
   auto legacyChangeAddr = getAddress(bridge_, legacyWltId, legacyAccId);

   //sign new tx
   BinaryData signedTx2;
   try {
      signedTx2 = createAndSignTx(bridge_,
         legacyWltId, legacyAccId,
         {}, {{ recipientAddr1, 5 * COIN }},
         legacyChangeAddr.hash, 2, true,
         "pass1"
      );
   } catch (const std::exception& e) {
      ASSERT_TRUE(false) << e.what();
   }
   ASSERT_FALSE(signedTx2.empty());

   //broadcast
   broadcastTx(bridge_, signedTx2);

   //grab amount for change output
   Tx tx2(signedTx2);
   auto changeOutput2 = tx2.getTxOutCopy(1);
   uint64_t changeAmount2 = changeOutput2.getAmount();
   EXPECT_TRUE(changeAmount2 > 5 * COIN);
   EXPECT_TRUE(changeAmount2 < 6 * COIN);

   //wait on zc notif
   try {
      auto zcLedgers = waitOnZc();
      ASSERT_EQ(zcLedgers.size(), 1);
      ASSERT_EQ(zcLedgers[0].getTxHash(), tx2.getThisHash());
      EXPECT_EQ(zcLedgers[0].getValue(), -(11 * (int64_t)COIN - changeAmount2));
      ASSERT_EQ(zcLedgers[0].getBlockNum(), UINT32_MAX);
      EXPECT_TRUE(zcLedgers[0].isOptInRBF());
   } catch (const std::exception&) {
      ASSERT_TRUE(false);
   }

   //check wallet balance
   wltBal = getWalletBalance(bridge_, legacyWltId, legacyAccId);
   ASSERT_EQ(wltBal.size(), 4);
   EXPECT_EQ(wltBal[0], changeAmount2);
   EXPECT_EQ(wltBal[1], 0);
   EXPECT_EQ(wltBal[2], changeAmount2);
   EXPECT_EQ(wltBal[3], 2);
}

////////////////////////////////////////////////////////////////////////////////
// BridgeBlocksAutoDBTests
class BridgeBlocksAutoDBTests : public ::testing::Test
{
protected:

   void prepareWallets()
   {
      auto pubKeyB = Cryptography::ECDSA::computePublicKey(TestChain::privKeyAddrB);
      auto pubKeyC = Cryptography::ECDSA::computePublicKey(TestChain::privKeyAddrC);
      auto pubKeyD = Cryptography::ECDSA::computePublicKey(TestChain::privKeyAddrD);
      auto pubKeyE = Cryptography::ECDSA::computePublicKey(TestChain::privKeyAddrE);

      walletId_ = createWOWallet(homedir_, {
         pubKeyB, pubKeyC, pubKeyD, pubKeyE}
      );
   }

   virtual void SetUp()
   {
      FileUtils::removeDirectory(blkdir_);
      FileUtils::removeDirectory(homedir_);
      FileUtils::removeDirectory(ldbdir_);

      FileUtils::createDirectory(blkdir_ / "blocks");
      FileUtils::createDirectory(homedir_);
      FileUtils::createDirectory(ldbdir_);

      Config::DBSettings::setServiceType(SERVICE_UNITTEST_WITHWS);

      // Put the first 5 blocks into the blkdir
      blk0dat_ = FileUtils::getBlkFilename(blkdir_ / "blocks", 0);
      TestUtils::setBlocks({ "0", "1", "2", "3", "4", "5" }, blk0dat_);

      #ifdef _WIN32
         auto buildPath = fullBinPath.string();
      #else
         auto buildPath = fullBinPath.parent_path().parent_path().string();
      #endif
      char* argv[] = {
         buildPath.data(),
         (char*)"--datadir=./fakehomedir"sv.data(),
         (char*)"--dbdir=./ldbtestdir"sv.data(),
         (char*)"--satoshi-datadir=./blkfiletest"sv.data(),
         (char*)"--db-type=DB_BARE"sv.data(),
         (char*)"--thread-count=3"sv.data()
      };
      Config::parseArgs(6, argv, Config::ProcessType::Bridge);
      prepareWallets();

      replyQueue.clear();
      bridge_ = std::make_shared<Bridge::CppBridge>();
      bridge_->setWriteLambda([](MsgPtr payload) {
         std::unique_lock<std::mutex> lock(commsMutex);
         replyQueue.emplace_back(std::move(payload));
         commsCV.notify_all();
      });
   }

   virtual void TearDown()
   {
      bridge_.reset();
      FileUtils::removeDirectory(blkdir_);
      FileUtils::removeDirectory(homedir_);
      FileUtils::removeDirectory(ldbdir_);
      Config::reset();
   }

   bool disconnectFromDb(bool cleanup)
   {
      if (cleanup) {
         //command db to shutdown
         uint64_t refId = rand();
         capnp::MallocMessageBuilder message;
         auto toBridge = message.initRoot<Codec::Bridge::ToBridge>();
         toBridge.setReferenceId(refId);
         auto request = toBridge.initSetup();
         request.setCleanupDb();

         auto rawReq = serializeCapnp(message);
         pushRequest(bridge_, rawReq);

         //grab reply to cleanupDb
         auto reply = waitOnReply();
         kj::ArrayPtr<const capnp::word> words(
            reinterpret_cast<const capnp::word*>(reply->data.getPtr()),
            reply->data.getSize() / sizeof(capnp::word));
         capnp::FlatArrayMessageReader reader(words);
         auto fromBridge = reader.getRoot<Codec::Bridge::FromBridge>();

         if (fromBridge.which() != Codec::Bridge::FromBridge::REPLY) {
            return false;
         }
         auto repCapnp = fromBridge.getReply();
         if (!repCapnp.getSuccess()) {
            return false;
         }
      } else {
         //only disconnect client from db
         uint64_t refId = rand();
         capnp::MallocMessageBuilder message;
         auto toBridge = message.initRoot<Codec::Bridge::ToBridge>();
         toBridge.setReferenceId(refId);
         auto request = toBridge.initSetup();
         request.setDisconnect();

         auto rawReq = serializeCapnp(message);
         pushRequest(bridge_, rawReq);
      }

      //expecting disconnected notif
      auto reply = waitOnReply();
      auto words = kj::ArrayPtr<const capnp::word>{
         reinterpret_cast<const capnp::word*>(reply->data.getPtr()),
         reply->data.getSize() / sizeof(capnp::word)
      };
      auto reader = capnp::FlatArrayMessageReader{words};
      auto fromBridge = reader.getRoot<Codec::Bridge::FromBridge>();

      if (fromBridge.which() != Codec::Bridge::FromBridge::NOTIFICATION) {
         return false;
      }
      auto notif = fromBridge.getNotification();
      return notif.which() == Codec::Bridge::Notification::DISCONNECTED;
   }

protected:
   std::filesystem::path blkdir_{"./blkfiletest"sv};
   std::filesystem::path homedir_{"./fakehomedir"sv};
   std::filesystem::path ldbdir_{"./ldbtestdir"sv};
   std::filesystem::path blk0dat_;

   std::shared_ptr<Bridge::CppBridge> bridge_;
   std::string walletId_;
};

////////////////////////////////////////////////////////////////////////////////
TEST_F(BridgeBlocksAutoDBTests, Connect)
{
   auto wltList = listWallets(bridge_);
   ASSERT_EQ(wltList.size(), 1);
   auto wltId = wltList.begin()->second.walletId;
   ASSERT_FALSE(wltId.empty());
   ASSERT_EQ(wltId, walletId_);
   auto wallets = loadWallets(bridge_);
   ASSERT_EQ(wallets.size(), 1);

   ASSERT_EQ(wallets.begin()->second.walletId, wltId);
   auto accountId = wallets.begin()->second.accountId;
   ASSERT_FALSE(accountId.empty());

   //setup connection to db
   ASSERT_TRUE(automateDb(bridge_, blkdir_, ldbdir_));
   ASSERT_TRUE(Bridge::isDbRunning());
   ASSERT_TRUE(registerWallets(bridge_));
   ASSERT_EQ(goOnline(bridge_), 5);

   //check balances
   auto balances = getAddrBalances(bridge_, wltId, accountId);
   ASSERT_EQ(balances.size(), 4);

   try {
      for (const auto& balPair : balances) {
         const auto& addrBal = TestChain::testAddrBalances[5].at(balPair.first);
         EXPECT_EQ(addrBal[0], balPair.second[0]);
         EXPECT_EQ(addrBal[1], balPair.second[1]);
         EXPECT_EQ(addrBal[2], balPair.second[2]);
      }
   } catch (const std::exception&) {
      ASSERT_TRUE(false);
   }

   //cleanup
   ASSERT_TRUE(disconnectFromDb(true));

   //confirm db is down
   while (Bridge::isDbRunning()) {
      std::this_thread::sleep_for(100ms);
   }
}

////////////////////////////////////////////////////////////////////////////////
TEST_F(BridgeBlocksAutoDBTests, Connect_NoCleanup)
{
   auto wltList = listWallets(bridge_);
   ASSERT_EQ(wltList.size(), 1);
   auto wltId = wltList.begin()->second.walletId;
   ASSERT_FALSE(wltId.empty());
   ASSERT_EQ(wltId, walletId_);
   auto wallets = loadWallets(bridge_);
   ASSERT_EQ(wallets.size(), 1);

   ASSERT_EQ(wallets.begin()->second.walletId, wltId);
   auto accountId = wallets.begin()->second.accountId;
   ASSERT_FALSE(accountId.empty());

   //setup connection to db
   ASSERT_TRUE(automateDb(bridge_, blkdir_, ldbdir_));
   ASSERT_TRUE(Bridge::isDbRunning());
   ASSERT_TRUE(registerWallets(bridge_));
   ASSERT_EQ(goOnline(bridge_), 5);

   //check balances
   auto balances = getAddrBalances(bridge_, wltId, accountId);
   ASSERT_EQ(balances.size(), 4);

   try {
      for (const auto& balPair : balances) {
         const auto& addrBal = TestChain::testAddrBalances[5].at(balPair.first);
         EXPECT_EQ(addrBal[0], balPair.second[0]);
         EXPECT_EQ(addrBal[1], balPair.second[1]);
         EXPECT_EQ(addrBal[2], balPair.second[2]);
      }
   } catch (const std::exception&) {
      ASSERT_TRUE(false);
   }

   /* ephemeral db should clean itself up after the client disconnects */
   ASSERT_TRUE(disconnectFromDb(false));

   //confirm db is down
   while (Bridge::isDbRunning()) {
      std::this_thread::sleep_for(100ms);
   }
}

////////////////////////////////////////////////////////////////////////////////
// BridgePeersManagement
////////////////////////////////////////////////////////////////////////////////
class BridgePeersManagement : public ::testing::Test
{
protected:
   struct PeerData
   {
      std::string key;
      std::set<std::string> names;
      std::string label;
   };

   void initBDM()
   {
      theBDMt_ = new BlockDataManagerThread();
      iface_ = theBDMt_->bdm()->getIFace();
   }

   bool loadPeersDb(bool succeed)
   {
      uint64_t refId = rand();
      auto callbackId = Cryptography::PRNG::fortuna.generateRandom(4).toHexStr();
      {
         capnp::MallocMessageBuilder message;
         auto toBridge = message.initRoot<Codec::Bridge::ToBridge>();
         toBridge.setReferenceId(refId);
         auto request = toBridge.initSetup();
         request.setLoadPeersDb(callbackId);

         auto rawReq = serializeCapnp(message);
         pushRequest(bridge_, rawReq);
      }

      //deal with unlock request
      {
         auto reply = waitOnReply();
         kj::ArrayPtr<const capnp::word> words(
            reinterpret_cast<const capnp::word*>(reply->data.getPtr()),
            reply->data.getSize() / sizeof(capnp::word));
         capnp::FlatArrayMessageReader reader(words);

         auto fromBridge = reader.getRoot<Codec::Bridge::FromBridge>();
         if (fromBridge.which() != Codec::Bridge::FromBridge::NOTIFICATION) {
            return false;
         }

         auto notif = fromBridge.getNotification();
         if (notif.which() != Codec::Bridge::Notification::UNLOCK_REQUEST) {
            return false;
         }

         //reply to unlock request
         capnp::MallocMessageBuilder message;
         auto toBridge = message.initRoot<Codec::Bridge::ToBridge>();
         toBridge.setReferenceId(refId);
         auto notifReply = toBridge.initNotification();
         notifReply.setCounter(notif.getCounter());
         if (succeed) {
            notifReply.setUnlockRequest(clientPeersDbPass_);
            notifReply.setSuccess(true);
         } else {
            notifReply.setSuccess(false);
         }

         auto rawReq = serializeCapnp(message);
         pushRequest(bridge_, rawReq);
      }

      //wait on success reply
      auto reply = waitOnReply();
      kj::ArrayPtr<const capnp::word> words(
         reinterpret_cast<const capnp::word*>(reply->data.getPtr()),
         reply->data.getSize() / sizeof(capnp::word));
      capnp::FlatArrayMessageReader reader(words);

      auto fromBridge = reader.getRoot<Codec::Bridge::FromBridge>();
      if (fromBridge.which() != Codec::Bridge::FromBridge::REPLY) {
         return false;
      }

      auto capnReply = fromBridge.getReply();
      return capnReply.getSuccess() && capnReply.getReferenceId() == refId;
   }

   bool createPeersDb(const std::string& passphrase)
   {
      uint64_t refId = rand();
      auto callbackId = Cryptography::PRNG::fortuna.generateRandom(4).toHexStr();
      {
         capnp::MallocMessageBuilder message;
         auto toBridge = message.initRoot<Codec::Bridge::ToBridge>();
         toBridge.setReferenceId(refId);
         auto request = toBridge.initSetup();
         request.setLoadPeersDb(callbackId);

         auto rawReq = serializeCapnp(message);
         pushRequest(bridge_, rawReq);
      }

      //deal with setpass request
      {
         auto reply = waitOnReply();
         kj::ArrayPtr<const capnp::word> words(
            reinterpret_cast<const capnp::word*>(reply->data.getPtr()),
            reply->data.getSize() / sizeof(capnp::word));
         capnp::FlatArrayMessageReader reader(words);

         auto fromBridge = reader.getRoot<Codec::Bridge::FromBridge>();
         if (fromBridge.which() != Codec::Bridge::FromBridge::NOTIFICATION) {
            return false;
         }

         auto notif = fromBridge.getNotification();
         if (notif.which() != Codec::Bridge::Notification::SET_PASSPHRASE) {
            return false;
         }

         auto passReq = notif.getSetPassphrase();
         if (passReq.which() != Codec::Bridge::Notification::SetPassphraseRequest::CONTROL_PASS) {
            return false;
         }

         //reply to setpass request
         capnp::MallocMessageBuilder message;
         auto toBridge = message.initRoot<Codec::Bridge::ToBridge>();
         toBridge.setReferenceId(refId);
         auto notifReply = toBridge.initNotification();
         notifReply.setCounter(notif.getCounter());
         notifReply.setSuccess(true);

         auto passReply = notifReply.initSetPassphrase();
         passReply.setPassphrase(passphrase);
         passReply.setKdfTargetMs(1);
         passReply.setKdfTargetMB(0);

         auto rawReq = serializeCapnp(message);
         pushRequest(bridge_, rawReq);
      }

      //wait on success reply
      auto reply = waitOnReply();
      kj::ArrayPtr<const capnp::word> words(
         reinterpret_cast<const capnp::word*>(reply->data.getPtr()),
         reply->data.getSize() / sizeof(capnp::word));
      capnp::FlatArrayMessageReader reader(words);

      auto fromBridge = reader.getRoot<Codec::Bridge::FromBridge>();
      if (fromBridge.which() != Codec::Bridge::FromBridge::REPLY) {
         return false;
      }

      auto capnReply = fromBridge.getReply();
      return capnReply.getSuccess() && capnReply.getReferenceId() == refId;
   }

   std::vector<PeerData> listPeers()
   {
      uint64_t refId = rand();
      capnp::MallocMessageBuilder message;
      auto toBridge = message.initRoot<Codec::Bridge::ToBridge>();
      toBridge.setReferenceId(refId);
      auto request = toBridge.initSetup();
      request.setListPeers();

      auto rawReq = serializeCapnp(message);
      pushRequest(bridge_, rawReq);

      auto result = waitOnReply();
      kj::ArrayPtr<const capnp::word> words(
         reinterpret_cast<const capnp::word*>(result->data.getPtr()),
         result->data.getSize() / sizeof(capnp::word));
      capnp::FlatArrayMessageReader reader(words);
      auto fromBridge = reader.getRoot<Codec::Bridge::FromBridge>();
      auto reply = fromBridge.getReply();
      if (!reply.getSuccess() || reply.getReferenceId() != refId) {
         throw std::runtime_error({});
      }

      auto replyMgr = reply.getSetup();
      auto capnPeers = replyMgr.getListPeers();

      std::vector<PeerData> peerVec;
      for (auto capnPeerData : capnPeers) {
         auto capnPeer = capnPeerData.getPeer();
         std::set<std::string> names;
         for (auto name : capnPeer.getNames()) {
            names.emplace(std::string{name});
         }
         std::string label = capnPeer.hasLabel() ? std::string(capnPeer.getLabel()) : "";
         peerVec.emplace_back(PeerData{
            std::string{capnPeer.getKey()},
            std::move(names),
            label
         });
      }
      return peerVec;
   }

   void addPeer(const BinaryData& key,
      const std::vector<std::string>& names,
      const std::string& label)
   {
      Wallets::PeerKey peer{key.getRef(), true, true};

      uint64_t refId = rand();
      capnp::MallocMessageBuilder message;
      auto toBridge = message.initRoot<Codec::Bridge::ToBridge>();
      toBridge.setReferenceId(refId);
      auto request = toBridge.initSetup();
      auto peerCapnp = request.initAddPeer();
      peerCapnp.setKey(peer.toHumanReadable());
      auto namesCapnp = peerCapnp.initNames(names.size());
      for (unsigned i = 0; i < names.size(); i++) {
         namesCapnp.set(i, names[i]);
      }
      peerCapnp.setLabel(label);

      auto rawReq = serializeCapnp(message);
      pushRequest(bridge_, rawReq);

      auto result = waitOnReply();
      kj::ArrayPtr<const capnp::word> words(
         reinterpret_cast<const capnp::word*>(result->data.getPtr()),
         result->data.getSize() / sizeof(capnp::word));
      capnp::FlatArrayMessageReader reader(words);
      auto fromBridge = reader.getRoot<Codec::Bridge::FromBridge>();
      auto reply = fromBridge.getReply();

      ASSERT_EQ(reply.getReferenceId(), refId);
      if (reply.getSuccess() == false) {
         std::cout << std::string(reply.getError()) << std::endl;
      }
      ASSERT_TRUE(reply.getSuccess());
   }

   void removePeer(const std::string& key)
   {
      uint64_t refId = rand();
      capnp::MallocMessageBuilder message;
      auto toBridge = message.initRoot<Codec::Bridge::ToBridge>();
      toBridge.setReferenceId(refId);
      auto request = toBridge.initSetup();
      request.setRemovePeer(key);

      auto rawReq = serializeCapnp(message);
      pushRequest(bridge_, rawReq);

      auto result = waitOnReply();
      kj::ArrayPtr<const capnp::word> words(
         reinterpret_cast<const capnp::word*>(result->data.getPtr()),
         result->data.getSize() / sizeof(capnp::word));
      capnp::FlatArrayMessageReader reader(words);
      auto fromBridge = reader.getRoot<Codec::Bridge::FromBridge>();
      auto reply = fromBridge.getReply();

      ASSERT_EQ(reply.getReferenceId(), refId);
      if (reply.getSuccess() == false) {
         std::cout << std::string(reply.getError()) << std::endl;
      }
      ASSERT_TRUE(reply.getSuccess());
   }

   void setLabel(const std::string& key, const std::string& label)
   {
      uint64_t refId = rand();
      capnp::MallocMessageBuilder message;
      auto toBridge = message.initRoot<Codec::Bridge::ToBridge>();
      toBridge.setReferenceId(refId);
      auto request = toBridge.initSetup();
      auto labelReq = request.initSetLabel();
      labelReq.setKey(key);
      labelReq.setLabel(label);

      auto rawReq = serializeCapnp(message);
      pushRequest(bridge_, rawReq);

      auto result = waitOnReply();
      kj::ArrayPtr<const capnp::word> words(
         reinterpret_cast<const capnp::word*>(result->data.getPtr()),
         result->data.getSize() / sizeof(capnp::word));
      capnp::FlatArrayMessageReader reader(words);
      auto fromBridge = reader.getRoot<Codec::Bridge::FromBridge>();
      auto reply = fromBridge.getReply();

      ASSERT_EQ(reply.getReferenceId(), refId);
      if (reply.getSuccess() == false) {
         std::cout << std::string(reply.getError()) << std::endl;
      }
      ASSERT_TRUE(reply.getSuccess());
   }

   /////////////////////////////////////////////////////////////////////////////
   virtual void SetUp()
   {
      FileUtils::removeDirectory(blkdir_);
      FileUtils::removeDirectory(homedir_);
      FileUtils::removeDirectory(ldbdir_);

      FileUtils::createDirectory(blkdir_ / "blocks");
      FileUtils::createDirectory(homedir_);
      FileUtils::createDirectory(ldbdir_);

      Config::DBSettings::setServiceType(SERVICE_UNITTEST_WITHWS);

      // Put the first 5 blocks into the blkdir
      blk0dat_ = FileUtils::getBlkFilename(blkdir_ / "blocks", 0);
      TestUtils::setBlocks({ "0", "1", "2", "3", "4", "5" }, blk0dat_);

      WebSocketServer::init();
      Config::parseArgs({
         "--datadir=./fakehomedir",
         "--dbdir=./ldbtestdir",
         "--satoshi-datadir=./blkfiletest",
         "--db-type=DB_BARE",
         "--thread-count=3",
         "--public"},
         Config::ProcessType::DB);

      startupBIP151CTX();
      startupBIP150CTX(4);

      //setup auth peers for server and client
      serverPeersPassLbd_ = [](const std::set<Wallets::EncryptionKeyId>&)
      ->Passphrase::Result
      {
         return { {}, true };
      };

      Wallets::AuthorizedPeers::createWallet({
         homedir_ / SERVER_AUTH_PEER_FILENAME, {
            []()->std::unique_ptr<Passphrase::Params>
            { return std::make_unique<Passphrase::Params>
               (1ms, 0, SecureBinaryData{});
            }
         }
      });
      Wallets::AuthorizedPeers serverPeers(
         {homedir_ / SERVER_AUTH_PEER_FILENAME, serverPeersPassLbd_});

      Wallets::AuthorizedPeers::createWallet({
         homedir_ / CLIENT_AUTH_PEER_FILENAME, {
            [pass=clientPeersDbPass_]()->std::unique_ptr<Passphrase::Params>
            { return std::make_unique<Passphrase::Params>
               (1ms, 0, SecureBinaryData::fromString(pass));
            }
         }
      });
      Wallets::AuthorizedPeers clientPeers(
         {homedir_ / CLIENT_AUTH_PEER_FILENAME,
         [pass=clientPeersDbPass_](const std::set<Wallets::EncryptionKeyId>&)
         ->Passphrase::Result { return { SecureBinaryData::fromString(pass), true }; }
      });

      //share public keys between client and server
      auto btcServerKey = serverPeers.getOwnPublicKey();
      serverPubkey_ = BinaryData{btcServerKey.pubkey, BIP151PUBKEYSIZE};
      auto btcClientKey = clientPeers.getOwnPublicKey();
      clientPubKey_ = BinaryData{btcClientKey.pubkey, BIP151PUBKEYSIZE};

      replyQueue.clear();
      bridge_ = std::make_shared<Bridge::CppBridge>();
      bridge_->setWriteLambda([](MsgPtr payload) {
         std::unique_lock<std::mutex> lock(commsMutex);
         replyQueue.emplace_back(std::move(payload));
         commsCV.notify_all();
      });

      initBDM();
      auto nodePtr = std::dynamic_pointer_cast<NodeUnitTest>(
         Config::NetworkSettings::bitcoinNodes().first);
      nodePtr->setBDM(theBDMt_->bdm());
   }

   /////////////////////////////////////////////////////////////////////////////
   virtual void TearDown()
   {
      bridge_.reset();
      WebSocketServer::shutdown();
      WebSocketServer::waitOnShutdown();
      theBDMt_->shutdown();

      delete theBDMt_;
      theBDMt_ = nullptr;

      FileUtils::removeDirectory(blkdir_);
      FileUtils::removeDirectory(homedir_);
      FileUtils::removeDirectory(ldbdir_);
      Config::reset();
   }

protected:
   BlockDataManagerThread *theBDMt_;
   Passphrase::UnlockFunc serverPeersPassLbd_;
   const std::string clientPeersDbPass_{"client_peers_pass"};
   LMDBBlockDatabase* iface_;

   std::filesystem::path blkdir_{"./blkfiletest"sv};
   std::filesystem::path homedir_{"./fakehomedir"sv};
   std::filesystem::path ldbdir_{"./ldbtestdir"sv};
   std::filesystem::path blk0dat_;

   std::shared_ptr<Bridge::CppBridge> bridge_;
   BinaryData serverPubkey_;
   BinaryData clientPubKey_;
};

////////////////////////////////////////////////////////////////////////////////
TEST_F(BridgePeersManagement, ListAddConnect)
{
   WebSocketServer::initAuthPeers({homedir_ / SERVER_AUTH_PEER_FILENAME, {}});
   WebSocketServer::start(theBDMt_->bdm(), true);

   Wallets::PeerKey clientPeer{clientPubKey_.getRef(), true, false};
   Wallets::PeerKey serverPeer{serverPubkey_.getRef(), true, true};
   auto clientKey = clientPeer.toHumanReadable();
   auto serverKey = serverPeer.toHumanReadable();

   //load peers db
   ASSERT_TRUE(loadPeersDb(true));

   //peer list should be empty at first, but for our own key
   auto peerList = listPeers();
   ASSERT_EQ(peerList.size(), 1);
   const auto& firstPeer = *peerList.begin();
   EXPECT_EQ(firstPeer.key, clientKey);
   ASSERT_EQ(firstPeer.names.size(), 1);
   EXPECT_EQ(*firstPeer.names.begin(), "own");
   EXPECT_EQ(firstPeer.label, "N/A");

   //try to connect to an invalid peer
   ASSERT_FALSE(connectToPeer(bridge_, serverKey));

   //add the server to peers store
   auto serverAddress = std::string{"127.0.0.1:"} + Config::NetworkSettings::dbPort();
   addPeer(serverPubkey_, { serverAddress }, "the server key");

   //list again, server should appear
   peerList = listPeers();
   ASSERT_EQ(peerList.size(), 2);
   for (const auto& keyEntry : peerList) {
      if (keyEntry.key == clientKey) {
         continue;
      }
      EXPECT_EQ(keyEntry.key, serverKey);
      ASSERT_EQ(keyEntry.names.size(), 1);
      EXPECT_EQ(*keyEntry.names.begin(), serverAddress);
      EXPECT_EQ(keyEntry.label, "the server key");
   }

   //connect to db
   ASSERT_TRUE(connectToPeer(bridge_, serverKey));
}

////////////////////////////////////////////////////////////////////////////////
TEST_F(BridgePeersManagement, LoadDeleteCreate)
{
   WebSocketServer::initAuthPeers({homedir_ / SERVER_AUTH_PEER_FILENAME, {}});
   WebSocketServer::start(theBDMt_->bdm(), true);
   Wallets::PeerKey serverPeer{serverPubkey_.getRef(), true, true};
   auto serverKey = serverPeer.toHumanReadable();

   //reject loading of peers db then delete it
   ASSERT_FALSE(loadPeersDb(false));
   ASSERT_TRUE(std::filesystem::remove(homedir_ / CLIENT_AUTH_PEER_FILENAME));

   //create from new call to loadPeersDb
   const std::string newPass{"new_client_peers_pass"};
   ASSERT_TRUE(createPeersDb(newPass));

   //peer list should be empty at first, but for our own key
   auto peerList = listPeers();
   ASSERT_EQ(peerList.size(), 1);
   const auto& firstPeer = *peerList.begin();
   ASSERT_EQ(firstPeer.names.size(), 1);
   ASSERT_EQ(*firstPeer.names.begin(), "own");
   auto clientKey = firstPeer.key;
   ASSERT_EQ(clientKey.size(), 48);

   //add the server to peers store
   auto serverAddress = std::string{"127.0.0.1:"} + Config::NetworkSettings::dbPort();
   addPeer(serverPubkey_, { serverAddress }, "my serv key");

   //list again, server should appear
   peerList = listPeers();
   ASSERT_EQ(peerList.size(), 2);
   for (const auto& keyEntry : peerList) {
      if (keyEntry.key == clientKey) {
         continue;
      }
      EXPECT_EQ(keyEntry.key, serverKey);
      ASSERT_EQ(keyEntry.names.size(), 1);
      EXPECT_EQ(*keyEntry.names.begin(), serverAddress);
      EXPECT_EQ(keyEntry.label, "my serv key");
   }

   //connect to db
   ASSERT_TRUE(connectToPeer(bridge_, serverKey));
}

////////////////////////////////////////////////////////////////////////////////
TEST_F(BridgePeersManagement, Remove)
{
   WebSocketServer::initAuthPeers({homedir_ / SERVER_AUTH_PEER_FILENAME, {}});
   WebSocketServer::start(theBDMt_->bdm(), true);

   Wallets::PeerKey clientPeer{clientPubKey_.getRef(), true, false};
   Wallets::PeerKey serverPeer{serverPubkey_.getRef(), true, true};
   auto clientKey = clientPeer.toHumanReadable();
   auto serverKey = serverPeer.toHumanReadable();

   //load peers db
   ASSERT_TRUE(loadPeersDb(true));

   //peer list should be empty at first, but for our own key
   auto peerList = listPeers();
   ASSERT_EQ(peerList.size(), 1);
   const auto& firstPeer = *peerList.begin();
   EXPECT_EQ(firstPeer.key, clientKey);
   ASSERT_EQ(firstPeer.names.size(), 1);
   EXPECT_EQ(*firstPeer.names.begin(), "own");

   //try to connect to an invalid peer
   ASSERT_FALSE(connectToPeer(bridge_, "abcd"));

   //add the server to peers store
   auto serverAddress = std::string{"127.0.0.1:"} + Config::NetworkSettings::dbPort();
   addPeer(serverPubkey_, { serverAddress }, "serv key");

   //also add a random key
   auto newKey = Cryptography::ECDSA::createNewPrivateKey();
   auto pubkey = Cryptography::ECDSA::computePublicKey(newKey, true);
   addPeer(pubkey, {"1.1.1.1"}, "rando key");
   Wallets::PeerKey newPeer{pubkey.getRef(), true, true};
   auto newPeerKey = newPeer.toHumanReadable();

   //list again, both keys should appear
   peerList = listPeers();
   ASSERT_EQ(peerList.size(), 3);
   int count = 0;
   for (const auto& keyEntry : peerList) {
      if (keyEntry.key == clientKey) {
         count++;
         continue;
      }
      else if (keyEntry.key == newPeerKey) {
         ASSERT_EQ(keyEntry.names.size(), 1);
         EXPECT_EQ(*keyEntry.names.begin(), "1.1.1.1");
         EXPECT_EQ(keyEntry.label, "rando key");
         count++;
      } else if (keyEntry.key == serverKey) {
         ASSERT_EQ(keyEntry.names.size(), 1);
         EXPECT_EQ(*keyEntry.names.begin(), serverAddress);
         EXPECT_EQ(keyEntry.label, "serv key");
         count++;
      } else {
         ASSERT_TRUE(false);
      }
   }
   ASSERT_EQ(count, 3);

   //delete the new key
   removePeer(newPeerKey);
   setLabel(serverKey, "updated serv key label");

   //list again, server should appear
   peerList = listPeers();
   ASSERT_EQ(peerList.size(), 2);
   for (const auto& keyEntry : peerList) {
      if (keyEntry.key == clientKey) {
         continue;
      }
      EXPECT_EQ(keyEntry.key, serverKey);
      ASSERT_EQ(keyEntry.names.size(), 1);
      EXPECT_EQ(*keyEntry.names.begin(), serverAddress);
      EXPECT_EQ(keyEntry.label, "updated serv key label");
   }

   //connect to db
   ASSERT_TRUE(connectToPeer(bridge_, serverKey));
}

////////////////////////////////////////////////////////////////////////////////
GTEST_API_ int main(int argc, char **argv)
{
   Cryptography::ECDSA::setupContext();

   srand(time(0));
   std::cout << "Running main() from gtest_main.cc\n";
   startupBIP151CTX();
   startupBIP150CTX(4);

   SETLOGLEVEL(LogLvlDebug);
   //LOGENABLESTDOUT();
   LOGDISABLESTDOUT();

   fullBinPath = std::filesystem::absolute(std::filesystem::path{argv[0]});
   testing::InitGoogleTest(&argc, argv);
   int exitCode = RUN_ALL_TESTS();

   FLUSHLOG();
   CLEANUPLOG();

   Cryptography::ECDSA::shutdown();
   return exitCode;
}
