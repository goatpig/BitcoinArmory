////////////////////////////////////////////////////////////////////////////////
//                                                                            //
//  Copyright (C) 2011-2015, Armory Technologies, Inc.                        //
//  Distributed under the GNU Affero General Public License (AGPL v3)         //
//  See LICENSE-ATI or http://www.gnu.org/licenses/agpl.html                  //
//                                                                            //
//                                                                            //
//  Copyright (C) 2016-2021, goatpig                                          //
//  Distributed under the MIT license                                         //
//  See LICENSE-MIT or https://opensource.org/licenses/MIT                    //
//                                                                            //
////////////////////////////////////////////////////////////////////////////////

/*general config for all things client and server*/

#pragma once

#include <exception>
#include <thread>
#include <tuple>
#include <list>
#include <filesystem>

#include "bdmenums.h"
#include "BinaryData.h"
#include "BitcoinSettings.h"

#define DEFAULT_ZCTHREAD_COUNT 100
#define DEFAULT_RAM_USAGE 8
#define WEBSOCKET_PORT 7681

namespace Node
{
   namespace Core
   {
      namespace RPC
      {
         class Iface;
      }

      namespace P2P
      {
         class Iface;
      }
   }
}

namespace Armory
{
   namespace Config
   {
      class Error : public std::runtime_error
      {
         Error(const std::string& err) :
            std::runtime_error(err)
         {}
      };

      ////
      enum class ProcessType
      {
         Bridge,
         DB,
         KeyManager,
      };

      //////////////////////////////////////////////////////////////////////////
      namespace SettingsUtils
      {
         std::vector<std::string> getLines(const std::filesystem::path&);
         std::map<std::string, std::string> getKeyValsFromLines(
            const std::vector<std::string>&, char);
         std::pair<std::string_view, std::string_view> getKeyValFromLine(
            const std::string_view&, char);

         std::string_view stripQuotes(const std::string_view& input);
         std::vector<std::string> keyValToArgv(
            const std::map<std::string, std::string>&);

         bool testConnection(const std::string&, const std::string&);
         std::string getPortFromCookie(const std::string&);
         std::string hasLocalDB(const std::string&, const std::string&);
      };

      //////////////////////////////////////////////////////////////////////////
      void printHelp(void);
      void parseArgs(int, char**, ProcessType);
      void parseArgs(const std::vector<std::string>&, ProcessType);
      const std::filesystem::path& getDataDir(void);
      void reset(void);

      //////////////////////////////////////////////////////////////////////////
      class BaseSettings
      {
         friend void Config::parseArgs(
            const std::vector<std::string>&, ProcessType);
         friend void Config::reset(void);
         friend const std::filesystem::path& Config::getDataDir(void);

      private:
         static std::mutex configMutex_;
         static std::filesystem::path dataDir_;
         static unsigned initCount_;

      private:
         static void detectDataDir(std::map<std::string, std::string>&);
         static void reset(void);
      };

      //////////////////////////////////////////////////////////////////////////
      class DBSettings
      {
         friend void Config::parseArgs(
            const std::vector<std::string>&, ProcessType);
         friend void Config::reset(void);

      private:
         static ARMORY_DB_TYPE armoryDbType_;
         static SOCKET_SERVICE service_;

         static BdmInitMode initMode_;

         static unsigned ramUsage_;
         static unsigned threadCount_;
         static unsigned zcThreadCount_;
         static unsigned rewindCount_; 

         static bool reportProgress_;
         static bool checkChain_;
         static bool disableZC_;
         static bool clearMempool_;
         static bool checkTxHints_;
         static bool automatedNode_;
         static uint64_t xorKey_;

      private:
         static void processArgs(const std::map<std::string, std::string>&);
         static void reset(void);

      public:
         static std::string getCookie(const std::filesystem::path&);

         static ARMORY_DB_TYPE getDbType(void);
         static void setServiceType(SOCKET_SERVICE);
         static SOCKET_SERVICE getServiceType(void);

         static std::string getDbModeStr(void);
         static unsigned threadCount(void);
         static unsigned ramUsage(void);
         static unsigned zcThreadCount(void);
         static unsigned rewindCount(void);

         static bool checkChain(void);
         static bool enableZC(void);
         static BdmInitMode initMode(void);
         static bool clearMempool(void);
         static bool reportProgress(void);
         static bool checkTxHints(void);
         static bool automatedNode(void);

         static bool isXored(void);
         static void setXorKey(uint64_t);
         static uint64_t getXorKey(void);
      };

      //////////////////////////////////////////////////////////////////////////
      class NetworkSettings
      {
         using RpcPtr = std::shared_ptr<Node::Core::RPC::Iface>;
         using NodePair = std::pair<
            std::shared_ptr<Node::Core::P2P::Iface>,
            std::shared_ptr<Node::Core::P2P::Iface>
         >;

         friend void Config::parseArgs(
            const std::vector<std::string>&, ProcessType);
         friend void Config::reset(void);

      private:
         static NodePair bitcoinNodes_;
         static RpcPtr rpcNode_;

         static std::string btcPort_;
         static std::string dbPort_;
         static std::string dbIP_;
         static std::string rpcPort_;

         static bool customDbPort_;
         static bool customBtcPort_;

         static bool ephemeralPeers_;
         static bool oneWayAuth_;

         static bool offline_;

         static BinaryData uiPublicKey_;

      private:
         static void createNodes(void);

         static void processArgs(
            const std::map<std::string, std::string>&, ProcessType);
         static void reset(void);

      public:
         static void selectNetwork(NETWORK_MODE);

         static const std::string& btcPort(void);
         static std::wstring btcPortW(void);
         static const std::string& dbPort(void);
         static const std::string& dbIP(void);
         static const std::string& rpcPort(void);
         static std::wstring rpcPortW(void);

         static const NodePair& bitcoinNodes(void);
         static RpcPtr rpcNode(void);
         static void setDbPort(const std::string&);

         static bool ephemeralPeers(void) { return ephemeralPeers_; }
         static bool oneWayAuth(void) { return oneWayAuth_; }
         static bool isOffline(void) { return offline_; }

         static BinaryData uiPublicKey(void) { return uiPublicKey_; }
      };

      //////////////////////////////////////////////////////////////////////////
      class Pathing
      {
         friend void Config::parseArgs(
            const std::vector<std::string>&, ProcessType);
         friend void Config::parseArgs(int, char*[], ProcessType);
         friend void Config::reset(void);

      private:
         static std::filesystem::path blkFilePath_;
         static std::filesystem::path dbDir_;
         static std::filesystem::path own_;

      private:
         static void processArgs(
            const std::map<std::string, std::string>&, ProcessType);
         static void reset(void);

      public:
         static std::filesystem::path logFilePath(const std::string&);
         static const std::filesystem::path& blkFilePath(void);
         static const std::filesystem::path& dbDir(void);
         static const std::filesystem::path& runningDir(void);
      };

      //////////////////////////////////////////////////////////////////////////
      struct File
      {
         std::map<std::string, std::string> keyvalMap_;

         File(const std::filesystem::path&);
         static std::vector<BinaryData> fleshOutArgs(
            const std::string&, const std::vector<BinaryData>&);
      };
   } //namespace Config
} //namespace Armory
