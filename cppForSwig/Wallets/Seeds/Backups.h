////////////////////////////////////////////////////////////////////////////////
//                                                                            //
//  Copyright (C) 2020 - 2023, goatpig                                        //
//  Distributed under the MIT license                                         //
//  See LICENSE-MIT or https://opensource.org/licenses/MIT                    //
//                                                                            //
////////////////////////////////////////////////////////////////////////////////

#ifndef _SECURE_PRINT_H
#define _SECURE_PRINT_H

#include <vector>
#include <string>
#include <string_view>
#include "SecureBinaryData.h"
#include "EncryptionUtils.h"

#include "Wallets.h"

#define EASY16_INVALID_CHECKSUM_INDEX UINT8_MAX

namespace capnp {
   class MessageBuilder;
}

namespace Armory
{
   namespace Wallets
   {
      struct WalletCreationParams;
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
         RestoreUserException(const std::string& errMsg) :
            std::runtime_error(errMsg)
         {}
      };

      ////
      class Easy16RepairError : public std::runtime_error
      {
      public:
         Easy16RepairError(const std::string& errMsg) :
            std::runtime_error(errMsg)
         {}
      };

      ////
      enum class BackupType : int
      {
         //easy16, seed (2 or 4 lines), hash index is always 0
         Armory135  = 0,

         /*
         easy16, seed (2 lines), hash index defines seed type:
            - a: Armory legacy derivation, P2PKH + P2WPK + P2SH-2WPKH
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
         std::vector<int> checksumIndexes_;
         std::vector<int> repairedIndexes_;
         std::vector<BinaryData> checksums_;
         SecureBinaryData data_;

         bool isInitialized(void) const;
         bool isValid(void) const;
         int getIndex(void) const;
      };

      ////
      struct Easy16Codec
      {
      public:
         /***
         Checksum indexes are an byte appended to the 16 byte line that is passed
         through the hash256 function to generate the checksum. That byte value
         designates the type of wallet this backup was generated from.

         For index 0 (Armory 1.35 wallets), the byte is not appended.
         The indexes for each line in a multiple line easy16 code need to match
         one another.
         ***/

         static const std::set<BackupType> eligibleIndexes_;

      private:
         static BinaryData getHash(const BinaryDataRef&, uint8_t);
         static uint8_t verifyChecksum(const BinaryDataRef&, const BinaryDataRef&);

      public:
         const static std::vector<char> e16chars_;

         static std::vector<SecureBinaryData> encode(const BinaryDataRef, BackupType);
         static BackupEasy16DecodeResult decode(const std::vector<SecureBinaryData>&);
         static BackupEasy16DecodeResult decode(const std::vector<BinaryDataRef>&);
         static bool repair(BackupEasy16DecodeResult&);
      };

      ////
      class SecurePrint
      {
      private:
         const static std::string digits_pi_;
         const static std::string digits_e_;
         const static uint32_t kdfBytes_;

         BinaryData iv16_;
         BinaryData salt_;
         mutable KdfRomix kdf_;

         SecureBinaryData passphrase_;

      public:
         SecurePrint(void);

         std::pair<SecureBinaryData, SecureBinaryData> encrypt(
            BinaryDataRef, BinaryDataRef);
         SecureBinaryData decrypt(
            const SecureBinaryData&, const BinaryDataRef) const;

         const SecureBinaryData& getPassphrase(void) const { return passphrase_; }
      };

      //////////////////////////////////////////////////////////////////////////
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

      public:
         enum class LineIndex : int
         {
            One = 0,
            Two = 1
         };

      private:
         std::vector<SecureBinaryData> rootClear_;
         std::vector<SecureBinaryData> chaincodeClear_;

         std::vector<SecureBinaryData> rootEncr_;
         std::vector<SecureBinaryData> chaincodeEncr_;

         SecureBinaryData spPass_;

      public:
         Backup_Easy16(BackupType);
         ~Backup_Easy16(void) override;

         bool hasChaincode(void) const;
         std::string_view getRoot(LineIndex, bool) const;
         std::string_view getChaincode(LineIndex, bool) const;
         std::string_view getSpPass(void) const;

         static std::unique_ptr<Backup_Easy16> fromLines(
            const std::vector<std::string_view>&,
            std::string_view spPass = {});
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

         //failed to decrypt secure print string
         DecryptError = 5,

         //requesting wallet privkey & control passphrases
         Passphrases = 6,

         //present restored wallet's id
         Id = 7,

         //unknown wallet type
         TypeError = 8,

         Success = 9
      };

      struct RestorePrompt
      {
         const RestorePromptType promptType;
         std::vector<int> checksumResult{};
         std::string walletId{};
         BackupType backupType{};
         std::string error{};

         bool needsReply(void) const;
      };

      struct PromptReply
      {
         const bool success;
         SecureBinaryData privPass{};
         SecureBinaryData controlPass{};
      };

      ////
      struct Helpers
      {
         using UserPrompt = std::function<PromptReply(const RestorePrompt&)>;

         //backup methods
         static std::unique_ptr<WalletBackup> getWalletBackup(
            std::shared_ptr<Wallets::AssetWallet_Single>,
            BackupType bType = BackupType::Invalid);
         static std::unique_ptr<WalletBackup> getWalletBackup(
            std::unique_ptr<ClearTextSeed>, BackupType);
         static std::unique_ptr<WalletBackup> getEasy16BackupString(
            std::unique_ptr<ClearTextSeed>);
         static std::unique_ptr<WalletBackup> getBIP39BackupString(
            std::unique_ptr<ClearTextSeed>);
         static std::unique_ptr<Backup_Base58> getBase58BackupString(
            std::unique_ptr<ClearTextSeed>);

         //restore methods
         static std::shared_ptr<Wallets::AssetWallet> restoreFromBackup(
            std::unique_ptr<WalletBackup>, const UserPrompt&,
               const Wallets::WalletCreationParams&);
         static std::unique_ptr<ClearTextSeed> restoreFromEasy16(
            std::unique_ptr<WalletBackup>, const UserPrompt&, BackupType&);
         static std::unique_ptr<ClearTextSeed> restoreFromBase58(
            std::unique_ptr<WalletBackup>);
         static std::unique_ptr<ClearTextSeed> restoreFromBIP39(
            std::unique_ptr<WalletBackup>);
      };
   }; //namespace Backups
}; //namespace Armory
#endif
