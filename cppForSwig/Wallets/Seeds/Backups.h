////////////////////////////////////////////////////////////////////////////////
//                                                                            //
//  Copyright (C) 2020 - 2025, goatpig                                        //
//  Distributed under the MIT license                                         //
//  See LICENSE-MIT or https://opensource.org/licenses/MIT                    //
//                                                                            //
////////////////////////////////////////////////////////////////////////////////

#pragma once

#include <vector>
#include <string>
#include <string_view>
#include <map>

#include <Utils/SecureBinaryData.h>
#include "../GetPassphrase.h"

#define EASY16_INVALID_CHECKSUM_INDEX UINT8_MAX

namespace Armory
{
   namespace Wallets
   {
      namespace IO
      {
         struct CreateWalletParams;
      }

      class AssetWallet;
      class AssetWallet_Single;
   }

   namespace Seeds
   {
      //forward declarations
      class ClearTextSeed;
      class ClearTextSeed_BIP39;

      //////////////////////////////////////////////////////////////////////////
      class RestoreUserException : public std::runtime_error
      {
      public:
         RestoreUserException(const std::string&);
      };

      ////
      class Easy16RepairError : public std::runtime_error
      {
      public:
         Easy16RepairError(const std::string&);
      };

      ////
      enum class BackupType : int
      {
         //legacy easy16 root + chaincode (4 lines) hash index is always 0
         Armory135a  = 0,

         //legacy easy16 root (2 lines) hash index is always 0
         Armory135c  = 1,

         /*
         easy16, seed (2 lines), hash index defines seed type:
            - a: Armory legacy derivation, P2PKH/P2WPK/P2SH-P2WPKH
                 addresses in a single address account
            - b: BIP32 with BIP44/49/84 chains, as individual address accounts
            - c: BIP32 with no accounts
            - d: BIP39 seed with BIP44/49/84 chains, as individual
                 address accounts, Trezor English dicionnary
         */
         Armory200a  = 3,
         Armory200b  = 4,
         Armory200c  = 5,

         Armory200d  = 10,

         //state of an easy16 backup prior to decode
         Easy16_Unkonwn = 30,

         //bip32 mnemonic phrase (12~24 words), english dictionnary
         BIP39       = 0xFFFF,

         Base58      = 58,

         //raw binary of the seed in hexits, no extra info provided
         Raw         = INT32_MAX - 1,

         //end marker
         Invalid     = INT32_MAX
      };

      ////
      struct BackupEasy16DecodeResult
      {
         std::vector<int> checksumIndexes;
         std::vector<int> repairedIndexes;
         std::vector<BinaryData> checksums;
         SecureBinaryData data;

         bool isInitialized(void) const;
         bool isValid(void) const;
         int getIndex(void) const;
      };

      ////
      namespace Easy16Codec
      {
         extern const std::set<BackupType> eligibleIndexes;
         extern const char characters[];
         extern const std::map<char, uint8_t> easy16Vals;

         std::vector<SecureBinaryData> encode(const BinaryDataRef,
            BackupType, bool=true);
         BackupEasy16DecodeResult decode(const std::vector<SecureBinaryData>&);
         BackupEasy16DecodeResult decode(const std::vector<BinaryDataRef>&);
         bool repair(BackupEasy16DecodeResult&);
      };

      ////
      class SecurePrint
      {
      private:
         BinaryData iv16_;
         BinaryData salt_;
         SecureBinaryData passphrase_;

      public:
         SecurePrint(void);

         std::pair<SecureBinaryData, SecureBinaryData> encrypt(
            BinaryDataRef, BinaryDataRef);
         SecureBinaryData decrypt(
            const SecureBinaryData&, const BinaryDataRef) const;
         const SecureBinaryData& getPassphrase(void) const;
      };

      //////////////////////////////////////////////////////////////////////////
      enum class LineIndex : int
      {
         One = 0,
         Two = 1
      };

      ////
      class WalletBackup
      {
         friend struct Helpers;

      protected:
         const BackupType type_;
         std::string wltId_;

      public:
         WalletBackup(BackupType);
         virtual ~WalletBackup(void) = 0;

         const BackupType& type(void) const;
         const std::string& getWalletId(void) const;
      };

      ////
      class Backup_Easy16 : public WalletBackup
      {
         friend class Helpers;

      private:
         std::vector<SecureBinaryData> rootClear_;
         std::vector<SecureBinaryData> chaincodeClear_;

         std::vector<SecureBinaryData> rootEncr_;
         std::vector<SecureBinaryData> chaincodeEncr_;

         SecureBinaryData spPass_;

      public:
         Backup_Easy16(BackupType);
         ~Backup_Easy16(void);

         bool hasChaincode(void) const;
         std::string_view getRoot(LineIndex, bool) const;
         std::string_view getChaincode(LineIndex, bool) const;
         std::string_view getSpPass(void) const;

         static std::unique_ptr<Backup_Easy16> fromLines(
            const std::vector<std::string_view>&,
            std::string_view = {});
      };

      ////
      class Backup_Easy16Public : public WalletBackup
      {
      private:
         SecureBinaryData backupId_;
         std::vector<SecureBinaryData> publicRoot_;
         std::vector<SecureBinaryData> chaincode_;

      private:
         Backup_Easy16Public(BackupType);

      public:
         Backup_Easy16Public(BackupType,
            const SecureBinaryData&, //pubroot
            const SecureBinaryData& //chaincode
         );
         ~Backup_Easy16Public(void) override;

         std::string_view getBackupId(void) const;
         std::string_view getPublicRoot(LineIndex) const;
         std::string_view getChaincode(LineIndex) const;

         static std::unique_ptr<Backup_Easy16Public> fromLines(
            const std::vector<std::string_view>&);
      };

      ////
      class Backup_Base58 : public WalletBackup
      {
      private:
         SecureBinaryData b58String_;

      public:
         Backup_Base58(SecureBinaryData);
         ~Backup_Base58(void) override;

         std::string_view getBase58String(void) const;
         static std::unique_ptr<Backup_Base58> fromString(
            const std::string_view&);
      };

      ////
      class Backup_BIP39 : public WalletBackup
      {
      private:
         SecureBinaryData mnemonicString_;

      private:
         Backup_BIP39(void);

      public:
         ~Backup_BIP39(void) override;

         std::string_view getMnemonicString(void) const;
         static std::unique_ptr<Backup_BIP39> fromMnemonicString(
            std::string_view);
      };

      ////////
      enum class RestorePromptType : int
      {
         //invalid backup format
         FormatError = 1,

         //failed to decode backup string
         Failure = 2,

         ChecksumError = 3,

         //mismatch between expected backup type and resolved type
         ChecksumMismatch = 4,
         PubkeyChecksumMismatch = 44,

         //failed to decrypt secure print string
         DecryptError = 5,

         //requesting wallet privkey & control passphrases
         ControlPassphrase = 6,
         PrivatePassphrase = 7,

         //present restored wallet's id
         Id = 8,

         //unknown wallet type
         TypeError = 9,
      };

      struct RestorePrompt
      {
         const RestorePromptType promptType;
         std::map<uint8_t, int> checksumResult{};
         std::string walletId{};
         BackupType backupType{};
         std::string error{};

         bool needsReply(void) const;
      };

      struct PromptReply
      {
         const bool success;
         const bool merge;

         Passphrase::Params passParams;
      };

      struct RestoreResult
      {
         std::shared_ptr<Wallets::AssetWallet> wltPtr;
         const bool merge;
      };

      ////
      struct Helpers
      {
         using UserPrompt = std::function<PromptReply(const RestorePrompt&)>;

         //backup methods
         static std::unique_ptr<WalletBackup> getWalletBackup(
            std::shared_ptr<Wallets::AssetWallet_Single>, bool,
            BackupType=BackupType::Invalid);
         static std::unique_ptr<WalletBackup> getWalletBackup(
            std::unique_ptr<ClearTextSeed>, BackupType);
         static std::unique_ptr<WalletBackup> getEasy16BackupString(
            std::unique_ptr<ClearTextSeed>);
         static std::unique_ptr<WalletBackup> getBIP39BackupString(
            std::unique_ptr<ClearTextSeed>);
         static std::unique_ptr<Backup_Base58> getBase58BackupString(
            std::unique_ptr<ClearTextSeed>);

         //primary restore call
         static RestoreResult restoreFromBackup(
            std::unique_ptr<WalletBackup>, const UserPrompt&,
            const Wallets::IO::CreateWalletParams&
         );

         //seed restore methods
         static std::unique_ptr<ClearTextSeed> restoreFromEasy16(
            std::unique_ptr<WalletBackup>, const UserPrompt&, BackupType&);
         static std::unique_ptr<ClearTextSeed> restoreFromBase58(
            std::unique_ptr<WalletBackup>);
         static std::unique_ptr<ClearTextSeed> restoreFromBIP39(
            std::unique_ptr<WalletBackup>);
      };
   } //namespace Backups
} //namespace Armory
