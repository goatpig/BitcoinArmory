////////////////////////////////////////////////////////////////////////////////
//                                                                            //
//  Copyright (C) 2018-2021, goatpig                                          //
//  Distributed under the MIT license                                         //
//  See LICENSE-MIT or https://opensource.org/licenses/MIT                    //
//                                                                            //
////////////////////////////////////////////////////////////////////////////////

/*defines Bitcoin network mode config, has nothing to do with socketing*/

#pragma once

#include <string>
#include <map>

#include "BinaryData.h"

#define TESTNET_MAGIC_BYTES "0b110907"
#define TESTNET_GENESIS_HASH_HEX    "43497fd7f826957108f4a30fd9cec3aeba79972084e90ead01ea330900000000"
#define TESTNET_GENESIS_TX_HASH_HEX "3ba3edfd7a7b12b27ac72c3e67768f617fc81bc3888a51323a9fb8aa4b1e5e4a"

#define REGTEST_MAGIC_BYTES "fabfb5da"
#define REGTEST_GENESIS_HASH_HEX    "06226e46111a0b59caaf126043eb5bbf28c34f3a5e332a1fc7b2b73cf188910f"
#define REGTEST_GENESIS_TX_HASH_HEX "3ba3edfd7a7b12b27ac72c3e67768f617fc81bc3888a51323a9fb8aa4b1e5e4a"

#define MAINNET_MAGIC_BYTES "f9beb4d9"
#define MAINNET_GENESIS_HASH_HEX    "6fe28c0ab6f1b372c1a6a246ae63f74f931e8365e15a089c68d6190000000000"
#define MAINNET_GENESIS_TX_HASH_HEX "3ba3edfd7a7b12b27ac72c3e67768f617fc81bc3888a51323a9fb8aa4b1e5e4a"

#define SEGWIT_ADDRESS_MAINNET_HEADER "bc"
#define SEGWIT_ADDRESS_TESTNET_HEADER "tb"

struct btc_chainparams_;

namespace Armory
{
   enum class ScriptPrefix : uint8_t
   {
      HASH160 = 0x00,
      P2SH = 0x05,
      HASH160_TESTNET = 0x6f,
      PRIVKEY = 0x80,
      P2WPKH = 0x90,
      P2WSH = 0x95,
      P2SH_TESTNET = 0xc4,
      PRIVKEY_TESTNET = 0xef,
      MULTISIG = 0xfe,
      NONSTD = 0xff,
      OPRETURN = 0x6a
   };

   namespace Config
   {
      typedef enum
      {
         NETWORK_MODE_NA = 0,
         NETWORK_MODE_MAINNET,
         NETWORK_MODE_TESTNET,
         NETWORK_MODE_REGTEST,
      } NETWORK_MODE;

      class BitcoinSettings
      {
      private:
         static BinaryData genesisBlockHash_;
         static BinaryData genesisTxHash_;
         static BinaryData magicBytes_;

         static uint8_t pubkeyHashPrefix_;
         static uint8_t scriptHashPrefix_;
         static uint8_t privKeyPrefix_;

         static NETWORK_MODE mode_;
         static const btc_chainparams_* chainParams_;
         static std::string bech32Prefix_;
         static uint32_t BIP32_CoinType_;

      private:
         static void selectNetwork(NETWORK_MODE);

      public:
         static void processArgs(const std::map<std::string, std::string>&);

         static uint8_t getPubkeyHashPrefix(void);
         static uint8_t getScriptHashPrefix(void);
         static uint8_t getPrivKeyPrefix(void);

         static const BinaryData& getGenesisBlockHash(void);
         static const BinaryData& getGenesisTxHash(void);
         static const BinaryData& getMagicBytes(void);
         static BinaryData getMainnetMagicBytes(void);

         static NETWORK_MODE getMode(void);
         static bool isInitialized(void);
         static const btc_chainparams_* getChainParams(void);
         static uint32_t getCoinType(void);
      };
   } //namespace Config
} //namespace ArmoryConfig
