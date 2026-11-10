////////////////////////////////////////////////////////////////////////////////
//                                                                            //
//  Copyright (C) 2025-2026, goatpig                                          //
//  Distributed under the MIT license                                         //
//  See LICENSE-MIT or https://opensource.org/licenses/MIT                    //
//                                                                            //
////////////////////////////////////////////////////////////////////////////////

#include "BridgeTestUtils.h"

namespace BridgeTestUtils {
   int CapnWalletState_Legacy = 1;
   int CapnWalletState_Encrypted = 2;
   int CapnWalletState_Ready = 3;
   std::string legacyAccId = Wallets::AddressAccountId(
      ARMORY_LEGACY_ACCOUNTID).toHexStr();

   /////////////////////////////////////////////////////////////////////////////
   // capnp stuff
   BinaryData serializeCapnp(capnp::MallocMessageBuilder& msg)
   {
      auto flat = capnp::messageToFlatArray(msg);
      auto bytes = flat.asBytes();
      return BinaryData(bytes.begin(), bytes.end());
   }

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

   ////////
   bool checkBDVRequestType(
      const Codec::BDV::Request::Reader& reader,
      Codec::BDV::BdvRequest::Which which)
   {
      if (reader.which() != Codec::BDV::Request::Which::BDV) {
         return false;
      }
      auto bdvReq = reader.getBdv();
      if (bdvReq.which() != which) {
      }
      return bdvReq.which() == which;
   }

   bool checkStaticRequestType(
      const Codec::BDV::Request::Reader& reader,
      Codec::BDV::StaticRequest::Which which)
   {
      if (reader.which() != Codec::BDV::Request::Which::STATIC) {
         return false;
      }
      auto staticReq = reader.getStatic();
      return staticReq.which() == which;
   }

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

   //nullptr if no reply comes within timeout
   MsgPtr waitOnReplyFor(std::chrono::milliseconds timeout)
   {
      std::unique_lock<std::mutex> lock(commsMutex);
      if (!commsCV.wait_for(lock, timeout,
         []{ return !replyQueue.empty(); })) {
         return nullptr;
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

   void replyToKeyPrompt(std::shared_ptr<Bridge::CppBridge> bridge,
      uint64_t refId, uint32_t counter, bool accept)
   {
      capnp::MallocMessageBuilder message;
      auto toBridge = message.initRoot<Codec::Bridge::ToBridge>();
      toBridge.setReferenceId(refId);
      auto notifReply = toBridge.initNotification();
      notifReply.setCounter(counter);
      notifReply.setPresentPubkey();
      notifReply.setSuccess(accept);

      auto rawReq = serializeCapnp(message);
      pushRequest(bridge, rawReq);
   }

   /////////////////////////////////////////////////////////////////////////////
   // wallet creation from manager
   std::string createWOWallet(
      const std::filesystem::path& homedir,
      const std::vector<SecureBinaryData>& pubkeys,
      const std::vector<BinaryData>& scrAddrs /*= {}*/)
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

      auto lock = assetWlt->lockDecryptedContainer(
         [&privPass](const std::set<Wallets::EncryptionKeyId>&)->Passphrase::Result
         { return {privPass, true}; }
      );
      for (const auto& privKey : privateKeys) {
         SecureBinaryData privKeySBD{privKey};
         assetWlt->importPrivateKey(privKeySBD, AddressEntryType(
            AddressEntryType::P2PKH | AddressEntryType::Uncompressed
         ));
      }
      return assetWlt->getID();
   }

   /////////////////////////////////////////////////////////////////////////////
   // wallet creation from bridge
   WalletData progressWalletCreation(std::shared_ptr<Bridge::CppBridge> bridge,
      const std::string& callbackId, const std::string& passphrase,
      std::chrono::milliseconds targetMs, uint32_t targetMB, int lookup,
      bool WO/*=false*/, bool isBIP32/*=false*/, bool isRestore/*=false*/)
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

      auto replyWlt = reply.getWallet();
      return capnToWalletData(replyWlt.getGetData());
   }

   bool checkHasImports(std::shared_ptr<Bridge::CppBridge> bridge,
      const std::string& walletId)
   {
      uint64_t refId = rand();

      capnp::MallocMessageBuilder message;
      auto toBridge = message.initRoot<Codec::Bridge::ToBridge>();
      toBridge.setReferenceId(refId);
      auto request = toBridge.initWallet();
      request.setWalletId(walletId);
      request.setHasImports();

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

      auto replyWlt = reply.getWallet();
      return replyWlt.getHasImports();
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

   std::string chainRoleForAssetAccount(
      const std::shared_ptr<Accounts::AddressAccount>& accPtr,
      const Wallets::AssetAccountId& assetAccId)
   {
      const auto& outerId = accPtr->getOuterAccountID();
      const auto& innerId = accPtr->getInnerAccountID();
      if (outerId == innerId) {
         return {};
      }
      if (assetAccId == outerId) {
         return "Receive";
      }
      if (assetAccId == innerId) {
         return "Change";
      }
      return {};
   }

   ExportedKey parseCapnExportedKey(
      const Codec::Bridge::WalletReply::ExportedPrivateKey::Reader& k)
   {
      ExportedKey key;
      auto capnId = k.getAssetId();
      key.assetId = BinaryData(capnId.begin(), capnId.size());
      auto capnPriv = k.getPrivKey();
      if (capnPriv.size() > 0) {
         key.privKey = BinaryData(capnPriv.begin(), capnPriv.size());
      }
      auto capnPub = k.getPublicKey();
      if (capnPub.size() > 0) {
         key.pubKey = BinaryData(capnPub.begin(), capnPub.size());
      }
      key.addressString = std::string(k.getAddressString());
      key.addrType = k.getAddrType();
      key.index = k.getIndex();
      key.accountId = std::string(k.getAccountId());
      key.isUsed = k.getIsUsed();
      key.chainRole = std::string(k.getChainRole());
      return key;
   }

   void fillExpectedExportMetadata(ExportedKey& key,
      const std::shared_ptr<Accounts::AddressAccount>& accPtr,
      const Wallets::AssetId& assetID)
   {
      try {
         auto addrEntry = accPtr->getAddressEntryForID(assetID);
         if (addrEntry == nullptr) {
            return;
         }

         uint32_t addrType = (uint32_t)addrEntry->getType();
         auto addrNested = std::dynamic_pointer_cast<
            AddressEntry_Nested>(addrEntry);
         if (addrNested != nullptr) {
            addrType |= (uint32_t)addrNested->getPredecessor()->getType();
         }
         key.addrType = addrType;

         try {
            if (addrNested != nullptr) {
               key.pubKey = addrNested->getPredecessor()->getPreimage();
            } else {
               key.pubKey = addrEntry->getPreimage();
            }
         } catch (const AddressException&) {
         }

         try {
            key.addressString = addrEntry->getAddress();
         } catch (const AddressException&) {
         }
      } catch (const std::exception&) {
      }
   }

   std::pair<size_t, std::map<BinaryData, ExportedKey>> buildExpectedExportKeys(
      const Wallets::AssetWallet_Single& assetWlt)
   {
      size_t expectedKeyCount = 0;
      std::map<BinaryData, ExportedKey> expectedKeys;

      for (const auto& addrAccId : assetWlt.getAccountIDs()) {
         auto accPtr = assetWlt.getAccountForID(addrAccId);
         if (!accPtr) {
            continue;
         }
         for (const auto& assetAccId : accPtr->getAccountIdSet()) {
            auto assetAcc = accPtr->getAccountForID(assetAccId);
            if (!assetAcc) {
               continue;
            }
            auto lastIdx = assetAcc->getLastComputedIndex();
            for (int32_t i = 0; i <= lastIdx; i++) {
               auto assetPtr = assetAcc->getAssetForKey(i);
               if (!assetPtr || !assetPtr->hasPrivateKey()) {
                  continue;
               }
               auto assetSingle = std::dynamic_pointer_cast<
                  Assets::AssetEntry_Single>(assetPtr);
               if (!assetSingle) {
                  continue;
               }
               ++expectedKeyCount;
               const auto& assetID = assetPtr->getID();

               ExportedKey expected;
               expected.assetId = assetID.getSerializedKey(
                  PROTO_ASSETID_PREFIX);
               expected.index = assetSingle->getIndex();
               expected.chainRole = chainRoleForAssetAccount(accPtr, assetAccId);
               fillExpectedExportMetadata(expected, accPtr, assetID);
               expectedKeys.emplace(expected.assetId, std::move(expected));
            }
         }
      }

      return {expectedKeyCount, std::move(expectedKeys)};
   }

   void fillExpectedPrivateKeys(
      Wallets::AssetWallet_Single& assetWlt,
      const SecureBinaryData& privPass,
      std::map<BinaryData, ExportedKey>& expectedKeys)
   {
      auto lock = assetWlt.lockDecryptedContainer(
         [&privPass](const std::set<Wallets::EncryptionKeyId>&)->Passphrase::Result
         { return {privPass, true}; }
      );

      for (const auto& addrAccId : assetWlt.getAccountIDs()) {
         auto accPtr = assetWlt.getAccountForID(addrAccId);
         if (!accPtr) {
            continue;
         }
         for (const auto& assetAccId : accPtr->getAccountIdSet()) {
            auto assetAcc = accPtr->getAccountForID(assetAccId);
            if (!assetAcc) {
               continue;
            }
            auto lastIdx = assetAcc->getLastComputedIndex();
            for (int32_t i = 0; i <= lastIdx; i++) {
               auto assetPtr = assetAcc->getAssetForKey(i);
               if (!assetPtr || !assetPtr->hasPrivateKey()) {
                  continue;
               }
               auto assetSingle = std::dynamic_pointer_cast<
                  Assets::AssetEntry_Single>(assetPtr);
               if (!assetSingle) {
                  continue;
               }
               const auto& assetID = assetPtr->getID();
               auto serId = assetID.getSerializedKey(PROTO_ASSETID_PREFIX);
               auto it = expectedKeys.find(serId);
               if (it == expectedKeys.end()) {
                  continue;
               }
               it->second.privKey = assetWlt.getDecryptedPrivateKeyForAsset(
                  assetSingle);
            }
         }
      }
   }

   void expectExportedKeyMatchesExpected(const ExportedKey& exported,
      const ExportedKey& expected)
   {
      EXPECT_EQ(exported.assetId, expected.assetId);
      EXPECT_EQ(exported.index, expected.index);
      EXPECT_EQ(exported.addressString, expected.addressString);
      EXPECT_EQ(exported.addrType, expected.addrType);
      EXPECT_EQ(exported.pubKey, expected.pubKey);
      EXPECT_EQ(exported.chainRole, expected.chainRole);
   }

   void verifyExportSetMatchesExpected(
      const std::vector<ExportedKey>& exported,
      const std::map<BinaryData, ExportedKey>& expected,
      bool requirePrivateKeys /*= false*/)
   {
      ASSERT_EQ(exported.size(), expected.size());
      std::set<BinaryData> seenIds;
      for (const auto& key : exported) {
         EXPECT_GE(key.assetId.getSize(), 1ULL);

         auto it = expected.find(key.assetId);
         ASSERT_NE(it, expected.end())
            << "unexpected asset id: " << key.assetId.toHexStr();

         if (requirePrivateKeys) {
            EXPECT_GE(key.privKey.getSize(), 32ULL);
            ASSERT_FALSE(it->second.privKey.empty())
               << "expected private key missing for asset "
               << key.assetId.toHexStr();
            EXPECT_EQ(key.privKey, it->second.privKey);
         } else {
            EXPECT_TRUE(key.privKey.empty());
         }

         expectExportedKeyMatchesExpected(key, it->second);
         seenIds.insert(key.assetId);
      }
      EXPECT_EQ(seenIds.size(), expected.size());
   }

   ExportResult exportKeys(
      std::shared_ptr<Bridge::CppBridge> bridge,
      const std::string& walletId,
      bool publicOnly,
      const std::string& passphrase /*= {}*/,
      bool provideCorrectPass /*= true*/,
      const std::string& accountId /*= {}*/)
   {
      ExportResult out;
      auto callbackId = Cryptography::PRNG::fortuna.generateRandom(10).toHexStr();
      uint64_t refId = rand();

      {
         capnp::MallocMessageBuilder message;
         auto toBridge = message.initRoot<Codec::Bridge::ToBridge>();
         toBridge.setReferenceId(refId);
         auto request = toBridge.initWallet();
         request.setWalletId(walletId);
         if (!accountId.empty()) {
            request.setAccountId(accountId);
         }

         auto exportReq = request.initExportKeys();
         if (publicOnly) {
            exportReq.setPublicDataOnly();
         } else {
            exportReq.setWithPrivateKeys(callbackId);
         }

         auto rawReq = serializeCapnp(message);
         pushRequest(bridge, rawReq);
      }

      if (publicOnly) {
         auto result = waitOnReply();
         kj::ArrayPtr<const capnp::word> words(
            reinterpret_cast<const capnp::word*>(result->data.getPtr()),
            result->data.getSize() / sizeof(capnp::word));
         capnp::FlatArrayMessageReader reader(words);
         auto fromBridge = reader.getRoot<Codec::Bridge::FromBridge>();

         if (fromBridge.which() != Codec::Bridge::FromBridge::REPLY) {
            out.error = "unexpected message type";
            return out;
         }

         auto reply = fromBridge.getReply();
         out.success = reply.getSuccess();
         if (!out.success) {
            out.error = reply.getError();
            return out;
         }

         auto walletReply = reply.getWallet();
         auto capnKeys = walletReply.getExportKeys();
         for (auto k : capnKeys) {
            out.keys.emplace_back(parseCapnExportedKey(k));
         }
         return out;
      }

      while (true) {
         auto result = waitOnReply();
         kj::ArrayPtr<const capnp::word> words(
            reinterpret_cast<const capnp::word*>(result->data.getPtr()),
            result->data.getSize() / sizeof(capnp::word));
         capnp::FlatArrayMessageReader reader(words);

         auto fromBridge = reader.getRoot<Codec::Bridge::FromBridge>();

         if (fromBridge.which() == Codec::Bridge::FromBridge::REPLY) {
            auto reply = fromBridge.getReply();
            out.success = reply.getSuccess();
            if (!out.success) {
               out.error = reply.getError();
            } else {
               auto walletReply = reply.getWallet();
               auto capnKeys = walletReply.getExportKeys();
               for (auto k : capnKeys) {
                  out.keys.emplace_back(parseCapnExportedKey(k));
               }
            }
            break;
         }

         if (fromBridge.which() != Codec::Bridge::FromBridge::NOTIFICATION) {
            out.error = "unexpected message type";
            break;
         }

         auto notif = fromBridge.getNotification();
         if (notif.getCallbackId() != callbackId) {
            out.error = "callback id mismatch";
            break;
         }

         auto notifType = notif.which();
         if (notifType == Codec::Bridge::Notification::UNLOCK_REQUEST) {
            capnp::MallocMessageBuilder notifMsg;
            auto notifBridge = notifMsg.initRoot<Codec::Bridge::ToBridge>();
            auto notifReply = notifBridge.initNotification();
            notifReply.setCounter(notif.getCounter());
            if (provideCorrectPass) {
               notifReply.setSuccess(true);
               notifReply.setUnlockRequest(passphrase);
            } else {
               notifReply.setSuccess(false);
            }
            auto rawNotif = serializeCapnp(notifMsg);
            pushRequest(bridge, rawNotif);
         } else if (notifType == Codec::Bridge::Notification::CLEANUP) {
            continue;
         } else {
            out.error = "unexpected notification type";
            break;
         }
      }

      return out;
   }

   ExportResult exportPrivateKeys(
      std::shared_ptr<Bridge::CppBridge> bridge,
      const std::string& walletId,
      const std::string& passphrase,
      bool provideCorrectPass /*= true*/,
      const std::string& accountId /*= {}*/)
   {
      return exportKeys(bridge, walletId, false, passphrase,
         provideCorrectPass, accountId);
   }

   ExportResult exportPublicKeys(
      std::shared_ptr<Bridge::CppBridge> bridge,
      const std::string& walletId,
      const std::string& accountId /*= {}*/)
   {
      return exportKeys(bridge, walletId, true, {}, true, accountId);
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

      //expect refresh and cleanup notif
      unsigned cleanupNotifCount = refreshId.empty() ? 0 : 11;
      while (cleanupNotifCount < 14) {
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
         std::string capnCbId = notif.getCallbackId();
         if (capnCbId == "progress") {
            continue;
         } else if (capnCbId != "bdm_callback") {
            if (capnCbId != callbackId) {
               throw std::runtime_error(std::format(
                  "unexpected cleanup callbackId: {} vs {}",
                  capnCbId, callbackId));
            }
            if (notif.which() != Codec::Bridge::Notification::CLEANUP) {
               throw std::runtime_error("expected cleanup notif");
            }
            cleanupNotifCount += 3;
         } else {
            if (notif.which() != Codec::Bridge::Notification::REFRESH) {
               throw std::runtime_error("expected refresh notif");
            }

            auto refreshIds = notif.getRefresh();
            if (refreshIds.size() != 1) {
               throw std::runtime_error("unexpected refreshId");
            }
            refreshId = refreshIds[0];
            cleanupNotifCount += 11;
         }
      }

      if (refreshId != dbId) {
         throw std::runtime_error("dbId mismatch");
      } else if (cleanupNotifCount != 14) {
         throw std::runtime_error("extendAddressPool cleanup notif count mismatch");
      }

      //lastly, grab updated wallet data
      return getWalletData(bridge, walletId, accountId);
   }

   AddressData getAddress(
      std::shared_ptr<Bridge::CppBridge> bridge,
      const std::string& walletId,
      const std::string& accountId,
      uint32_t addressType/*=0*/)
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
      bool notifSuccess = false;
      bool replySuccess = false;
      unsigned count = 0;
      while (count != 13) {
         auto reply = waitOnReply();
         kj::ArrayPtr<const capnp::word> words(
            reinterpret_cast<const capnp::word*>(reply->data.getPtr()),
            reply->data.getSize() / sizeof(capnp::word));
         capnp::FlatArrayMessageReader reader(words);

         auto fromBridge = reader.getRoot<Codec::Bridge::FromBridge>();
         switch (fromBridge.which()) {
            case Codec::Bridge::FromBridge::NOTIFICATION:
            {
               count += 2;
               auto notif = fromBridge.getNotification();
               switch (notif.which()) {
                  case Codec::Bridge::Notification::SETUP_DONE:
                     notifSuccess = true;
                     break;

                  case Codec::Bridge::Notification::DISCONNECTED:
                     notifSuccess = false;
                     break;

                  default:
                     throw std::runtime_error("unexpected connection notif which");
               }
               break;
            }

            case Codec::Bridge::FromBridge::REPLY:
            {
               count += 11;
               auto repCapnp = fromBridge.getReply();
               if (repCapnp.getReferenceId() != refId) {
                  throw std::runtime_error("referenceId mismatch");
               }
               if (repCapnp.getSuccess() == false) {
                  std::cout << std::format("connection error: {}",
                     std::string(repCapnp.getError())) << std::endl;
               }
               replySuccess = repCapnp.getSuccess();
               break;
            }

            default:
               EXPECT_TRUE(false);
               break;
         }
      }
      if (notifSuccess != replySuccess) {
         throw std::runtime_error("connection state mismatch");
      }
      return replySuccess;
   }

   bool connectToIp(std::shared_ptr<Bridge::CppBridge> bridge,
      const std::string& ip, Network::port_t port,
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

   int automateDb(std::shared_ptr<Bridge::CppBridge> bridge,
      const std::filesystem::path& satoshiDir,
      const std::filesystem::path& dbDir,
      BdmInitMode dbInitMode, bool clearMempool)
   {
      //setup automation context
      uint64_t refId = rand();
      {
         capnp::MallocMessageBuilder message;
         auto toBridge = message.initRoot<Codec::Bridge::ToBridge>();
         toBridge.setReferenceId(refId);
         auto request = toBridge.initSetup();
         auto autoDbReq = request.initInitAutomationContext();
         autoDbReq.setSatoshiDir(satoshiDir.string());
         autoDbReq.setDbDir(dbDir.string());
         autoDbReq.setAutomateDb();
         using DbInitMode =
            Codec::Bridge::DbSetupRequest::AutomationContext::DbInitMode;
         switch (dbInitMode)
         {
            case BdmInitMode::RESCAN:
               autoDbReq.setDbInitMode(DbInitMode::RESCAN);
               break;

            case BdmInitMode::REBUILD:
               autoDbReq.setDbInitMode(DbInitMode::REBUILD);
               break;

            default:
               break;
         }
         autoDbReq.setClearMempool(clearMempool);
         auto rawReq = serializeCapnp(message);
         pushRequest(bridge, rawReq);
      }

      //validate reply
      {
         auto reply = waitOnReply();
         kj::ArrayPtr<const capnp::word> words(
            reinterpret_cast<const capnp::word*>(reply->data.getPtr()),
            reply->data.getSize() / sizeof(capnp::word));
         capnp::FlatArrayMessageReader reader(words);

         auto fromBridge = reader.getRoot<Codec::Bridge::FromBridge>();
         if (fromBridge.which() != Codec::Bridge::FromBridge::REPLY) {
            return -1;
         }
         auto response = fromBridge.getReply();
         if (!response.getSuccess() || response.getReferenceId() != refId) {
            return -2;
         }
      }

      //run automation context
      refId = rand();
      auto callbackId = Cryptography::PRNG::fortuna.generateRandom(10).toHexStr();
      {
         capnp::MallocMessageBuilder message;
         auto toBridge = message.initRoot<Codec::Bridge::ToBridge>();
         toBridge.setReferenceId(refId);
         auto request = toBridge.initSetup();
         request.setRunAutomationContext(callbackId);
         auto rawReq = serializeCapnp(message);
         pushRequest(bridge, rawReq);
      }

      bool gotSuccess = false, gotSetupDone = false, gotCleanup = false;
      int step = 0;
      while (!gotSuccess || !gotSetupDone || !gotCleanup) {
         auto reply = waitOnReply();
         kj::ArrayPtr<const capnp::word> words(
            reinterpret_cast<const capnp::word*>(reply->data.getPtr()),
            reply->data.getSize() / sizeof(capnp::word));
         capnp::FlatArrayMessageReader reader(words);

         auto fromBridge = reader.getRoot<Codec::Bridge::FromBridge>();
         switch (fromBridge.which())
         {
            case Codec::Bridge::FromBridge::NOTIFICATION:
            {
               auto notif = fromBridge.getNotification();
               switch (notif.which()) {
                  case Codec::Bridge::Notification::SETUP_DONE:
                     gotSetupDone = true;
                     break;

                  case Codec::Bridge::Notification::CLEANUP:
                  {
                     gotCleanup = true;
                     break;
                  }

                  case Codec::Bridge::Notification::DISCONNECTED:
                     return -4;

                  case Codec::Bridge::Notification::AUTOMATION:
                  {
                     if (notif.getCallbackId() != callbackId) {
                        return -5;
                     }
                     auto automationStep = notif.getAutomation();
                     switch (automationStep.which())
                     {
                        case Codec::Bridge::Notification::AutomationContextNotif::SPAWN_DB:
                        {
                           if (step != 0) {
                              return -6;
                           }
                           step = 1;
                           break;
                        }

                        case Codec::Bridge::Notification::AutomationContextNotif::CONNECT_TO_DB:
                        {
                           if (step != 1) {
                              return -7;
                           }
                           step = 2;
                           break;
                        }

                        case Codec::Bridge::Notification::AutomationContextNotif::DONE:
                        {
                           if (step != 2) {
                              return -8;
                           }
                           step = 3;
                           break;
                        }

                        default:
                           return -9;
                     }
                     break;
                  }

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
                  return -20;
               }
               gotSuccess = true;
               break;
            }

            default:
               return -21;
         }
      }
      return step == 3 ? 0 : -100;
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

   int goOnline(std::shared_ptr<Bridge::CppBridge> bridge, bool preReg /*= false*/)
   {
      uint64_t refId = rand();
      capnp::MallocMessageBuilder message;
      auto toBridge = message.initRoot<Codec::Bridge::ToBridge>();
      toBridge.setReferenceId(refId);
      auto request = toBridge.initSetup();
      request.setBeginDbSession();
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
            {
               if (preReg) {
               auto scanProg = notif.getScanProgress();
                  auto progIds = scanProg.getIds();
                  if (progIds.size() > 0) {
                     for (auto progId : progIds) {
                        std::cout << "got progId " << std::string(progId) << std::endl;
                     }
                     return -10;
                  }
               }
               break;
            }

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
      auto req = toBridge.initWalletManager();
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
      if (reply.which() != Codec::Bridge::RpcReply::WALLET_MANAGER) {
         return {};
      }

      auto managerReply = reply.getWalletManager();
      auto capnTxs = managerReply.getGetTxsByHash();
      std::map<BinaryData, BinaryData> result;
      for (auto capnTx : capnTxs) {
         auto capnHash = capnTx.getHash();
         BinaryDataRef hash{capnHash.begin(), capnHash.size()};

         auto capnBody = capnTx.getBody();
         auto capnRaw = capnBody.getRaw();
         BinaryDataRef raw{capnRaw.begin(), capnRaw.size()};
         result.emplace(hash, raw);
      }
      return result;
   }

   //keyed by the hash of the raw tx, computed here
   std::map<BinaryData, FetchedTx> getTxDataByHash(
      std::shared_ptr<Bridge::CppBridge> bridge,
      const std::set<BinaryData>& hashSet, bool& success)
   {
      uint64_t refId = rand();
      capnp::MallocMessageBuilder message;
      auto toBridge = message.initRoot<Codec::Bridge::ToBridge>();
      toBridge.setReferenceId(refId);
      auto req = toBridge.initWalletManager();
      auto txsReq = req.initGetTxsByHash(hashSet.size());
      unsigned i=0;
      for (const auto& hash : hashSet) {
         txsReq.set(i++, capnp::Data::Builder(
            (uint8_t*)hash.getPtr(), hash.getSize()));
      }
      auto rawReq = serializeCapnp(message);
      pushRequest(bridge, rawReq);

      auto resp = waitOnReply();
      kj::ArrayPtr<const capnp::word> words(
         reinterpret_cast<const capnp::word*>(resp->data.getPtr()),
         resp->data.getSize() / sizeof(capnp::word));
      capnp::FlatArrayMessageReader reader(words);

      success = false;
      auto fromBridge = reader.getRoot<Codec::Bridge::FromBridge>();
      if (fromBridge.which() != Codec::Bridge::FromBridge::REPLY) {
         return {};
      }
      auto reply = fromBridge.getReply();
      if (reply.getReferenceId() != refId) {
         return {};
      }
      success = reply.getSuccess();
      if (!success || reply.which() != Codec::Bridge::RpcReply::WALLET_MANAGER) {
         return {};
      }

      std::map<BinaryData, FetchedTx> result;
      for (auto capnTx : reply.getWalletManager().getGetTxsByHash()) {
         auto capnRaw = capnTx.getBody().getRaw();
         BinaryData raw{capnRaw.begin(), capnRaw.size()};
         result.emplace(Tx(raw).getThisHash(), FetchedTx{raw,
            capnTx.getHeight(), capnTx.getTxIndex(), capnTx.getTimestamp()});
      }
      return result;
   }

   //FROM_TX_SIG_COLLECT, returns the reply's success flag
   bool loadSignerString(std::shared_ptr<Bridge::CppBridge> bridge,
      const std::string& signerId, const std::string& str)
   {
      uint64_t refId = rand();
      capnp::MallocMessageBuilder message;
      auto toBridge = message.initRoot<Codec::Bridge::ToBridge>();
      toBridge.setReferenceId(refId);
      auto req = toBridge.initSigner();
      req.setId(signerId);
      req.setFromTxSigCollect(str);
      pushRequest(bridge, serializeCapnp(message));

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
      return reply.getReferenceId() == refId && reply.getSuccess();
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
      const BinaryData& rawTx, bool viaRpc /*= false*/)
   {
      uint64_t refId = rand();
      capnp::MallocMessageBuilder message;
      auto toBridge = message.initRoot<Codec::Bridge::ToBridge>();
      toBridge.setReferenceId(refId);
      auto req = toBridge.initService();
      auto broadcastReq = req.initBroadcastTx();
      if (viaRpc) {
         broadcastReq.setViaRpc();
      } else {
         broadcastReq.setViaP2p();
      }
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
