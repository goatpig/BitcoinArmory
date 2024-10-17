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

#ifndef BLOCKDATAMANAGERCONFIG_H
#define BLOCKDATAMANAGERCONFIG_H

#include <exception>
#include <thread>
#include <tuple>
#include <list>
#include <filesystem>

#include "bdmenums.h"
#include "BinaryData.h"
#include "BitcoinSettings.h"

#define DEFAULT_ZCTHREAD_COUNT 100
#define WEBSOCKET_PORT 7681

#define BROADCAST_ID_LENGTH 6
#define REGISTER_ID_LENGH 5

class BitcoinNodeInterface;

namespace CoreRPC
{
   class NodeRPCInterface;
};

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
         std::vector<std::string> getLines(const std::filesystem::path& path);
         std::map<std::string, std::string> getKeyValsFromLines(
            const std::vector<std::string>&, char delim);
         std::pair<std::string, std::string> getKeyValFromLine(
            const std::string&, char delim);
            
         std::string stripQuotes(const std::string& input);
         std::vector<std::string> keyValToArgv(
            const std::map<std::string, std::string>&);
         std::vector<std::string> tokenizeLine(
            const std::string&, const std::string&);
            
         std::string portToString(unsigned);
         bool testConnection(const std::string& ip, const std::string& port);
         std::string getPortFromCookie(const std::string& datadir);
         std::string hasLocalDB(const std::string& datadir,
            const std::string& port);
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

         static BDM_INIT_MODE initMode_;

         static unsigned ramUsage_;
         static unsigned threadCount_;
         static unsigned zcThreadCount_;

         static bool reportProgress_;
         static bool checkChain_;
         static bool clearMempool_;
         static bool checkTxHints_;

      private:
         static void processArgs(const std::map<std::string, std::string>&);
         static void reset(void);

      public:
         static std::string getCookie(const std::string& datadir);

         static ARMORY_DB_TYPE getDbType(void)
         {
            return armoryDbType_;
         }

         static void setServiceType(SOCKET_SERVICE _type)
         {
            service_ = _type;
         }

         static SOCKET_SERVICE getServiceType(void)
         {
            return service_;
         }

         static std::string getDbModeStr(void);
         static unsigned threadCount(void) { return threadCount_; }
         static unsigned ramUsage(void) { return ramUsage_; }
         static unsigned zcThreadCount(void) { return zcThreadCount_; }

         static bool checkChain(void) { return checkChain_; }
         static BDM_INIT_MODE initMode(void) { return initMode_; }
         static bool clearMempool(void) { return clearMempool_; }
         static bool reportProgress(void) { return reportProgress_; }
         static bool checkTxHints(void) { return checkTxHints_; }
      };

      //////////////////////////////////////////////////////////////////////////
      class NetworkSettings
      {
         using RpcPtr = std::shared_ptr<CoreRPC::NodeRPCInterface>;
         using NodePair = std::pair<
            std::shared_ptr<BitcoinNodeInterface>, 
            std::shared_ptr<BitcoinNodeInterface>>;

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

         static bool useCookie_;
         static bool ephemeralPeers_;
         static bool oneWayAuth_;

         static bool offline_;
         static std::string cookie_;

         static BinaryData uiPublicKey_;

      private:
         static void createNodes(void);
         static void createCookie(void);

         static void processArgs(
            const std::map<std::string, std::string>&, ProcessType);
         static void reset(void);

      public:
         static void selectNetwork(NETWORK_MODE);

         static const std::string& btcPort(void);
         static const std::string& dbPort(void);
         static const std::string& dbIP(void);
         static const std::string& rpcPort(void);

         static void randomizeDbPort(void);

         static const NodePair& bitcoinNodes(void);
         static RpcPtr rpcNode(void);

         static bool useCookie(void) { return useCookie_; }
         static const std::string& cookie(void) { return cookie_; }
         
         static bool ephemeralPeers(void) { return ephemeralPeers_; }
         static bool oneWayAuth(void) { return oneWayAuth_; }
         static bool isOffline(void) { return offline_; }

         static BinaryData uiPublicKey(void) { return uiPublicKey_; }
         static void injectUiPubkey(BinaryData&);
      };

      //////////////////////////////////////////////////////////////////////////
      class Pathing
      {
         friend void Config::parseArgs(
            const std::vector<std::string>&, ProcessType);
         friend void Config::reset(void);

      private:
         static std::filesystem::path blkFilePath_;
         static std::filesystem::path dbDir_;

      private:
         static void processArgs(
            const std::map<std::string, std::string>&, ProcessType);
         static void reset(void);

      public:
         static std::filesystem::path logFilePath(const std::string&);
         static const std::filesystem::path& blkFilePath(void) { return blkFilePath_; }
         static const std::filesystem::path& dbDir(void) { return dbDir_; }
      };

      //////////////////////////////////////////////////////////////////////////
      struct File
      {
         std::map<std::string, std::string> keyvalMap_;

         File(const std::filesystem::path& path);
         static std::vector<BinaryData> fleshOutArgs(
            const std::string& path, const std::vector<BinaryData>& argv);
      };
   }; //namespace Config
}; //namespace Armory

////////////////////////////////////////////////////////////////////////////////
struct BDV_Error_Struct
{
   std::string errorStr_;
   BinaryData errData_;
   int errCode_;

   BinaryData serialize(void) const;
   void deserialize(const BinaryData&);
};
#endif

