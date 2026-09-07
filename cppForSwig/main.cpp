////////////////////////////////////////////////////////////////////////////////
//                                                                            //
//  Copyright (C) 2016-2025, goatpig.                                         //
//  Distributed under the MIT license                                         //
//  See LICENSE-MIT or https://opensource.org/licenses/MIT                    //
//                                                                            //
////////////////////////////////////////////////////////////////////////////////

#ifdef _WIN32
   #include <winsock2.h>
   #include <windows.h>
#endif

#include <string>
#include <iostream>
#include <sstream>
#include <chrono>

#include <Utils/ArmoryErrors.h>
#include <Utils/ArmoryConfig.h>
#include <Utils/Cryptography.h>
#include <Utils/BIP150_151.h>
#include <Utils/FileUtils.h>
#include <BlockchainDatabase/BlockUtils.h>
#include <Wallets/IOHeader.h>
#include <Wallets/AuthorizedPeers.h>
#include <Network/SocketObject.h>

#include "BDM_mainthread.h"
#include "Server.h"
#include "TerminalPassphrasePrompt.h"
#include <btc/ecc.h>

using namespace Armory;
using namespace std::chrono_literals;

#define LOG_FILE_NAME "dbLog"

int main(int argc, char* argv[])
{
   Cryptography::ECDSA::setupContext();
   startupBIP151CTX();
   startupBIP150CTX(4);
   WebSocketServer::init();

#ifdef _WIN32
   WSADATA wsaData;
   WORD wVersion = MAKEWORD(2, 0);
   WSAStartup(wVersion, &wsaData);
#endif

   try {
      Config::parseArgs(argc, argv, Config::ProcessType::DB);
   } catch (const DbErrorMsg& e) {
      std::cout << "Failed to setup with error:" << std::endl;
      std::cout << "   " << e.what() << std::endl;
      std::cout << "Aborting!" << std::endl;
      exit(-1);
   }

   auto logFilePath = Config::Pathing::logFilePath(LOG_FILE_NAME).string();
   STARTLOGGING(logFilePath, LogLvlDebug);
   if (Config::NetworkSettings::ephemeralPeers()) {
      LOGDISABLESTDOUT();
      if (Config::NetworkSettings::oneWayAuth()) {
         LOGERR << "--ephemeral and --oneWayAuth are mutually exclusive for db";
         exit(-3);
      }
      //initAuthPeers will setup the ephemeral keys
      try {
         WebSocketServer::initPeerStore(
            Wallets::IO::ReadOnlyFileParams{{}, nullptr});
      } catch (const std::exception &e) {
         LOGERR << "ephemeral peer db setup failed with this error: "
            << e.what();
         LOGERR << "aborting...";
         exit(-4);
      }
   } else {
      //setup remote peers db, this will block the init process until
      //peers db is unlocked
      LOGENABLESTDOUT();
      LOGINFO << "logging in " << logFilePath;

      auto peerFilePath = Config::getDataDir() / SERVER_AUTH_PEER_FILENAME;
      if (!FileUtils::pathExists(peerFilePath, 0) &&
         !Config::NetworkSettings::ephemeralPeers()) {
         LOGINFO << "no server peers store found, creating one...";
         auto passLbd = TerminalPassphrasePrompt::getLambda(
            "new server peers store");
         auto passWrapper = [&passLbd]()->std::unique_ptr<Passphrase::Params>
         {
            auto result = passLbd({});
            if (!result.success) {
               throw std::runtime_error("peers store init was rejected");
            }
            return std::make_unique<Passphrase::Params>(
               250ms, 0, std::move(result.passphrase));
         };
         NetworkPeers::PeerStore::bootstrapWallet(
            {peerFilePath, {passWrapper}});
         WebSocketServer::initPeerStore(
            {peerFilePath, passLbd});
      } else {
         auto passLbd = TerminalPassphrasePrompt::getLambda(
            "server peers store");
         WebSocketServer::initPeerStore({peerFilePath, passLbd});
      }
      NetworkPeers::PeerKey myKey{WebSocketServer::getOwnPublicKey(),
         Config::NetworkSettings::oneWayAuth() ?
         NetworkPeers::PeerType::ServerOneWay :
         NetworkPeers::PeerType::ServerTwoWay
      };
      LOGINFO << "This is my key: " << myKey.toHumanReadable();
   }

   LOGINFO << "Running on " << Config::DBSettings::threadCount() << " threads";
   LOGINFO << "Ram usage level: " << Config::DBSettings::ramUsage();

   //init state
   Config::DBSettings::setServiceType(SERVICE_WEBSOCKET);
   BlockDataManagerThread bdmThread;

   if (!Config::DBSettings::checkChain()) {
      //check we can listen on this ip:port
      if (Network::SimpleSocket::checkSocket(
         "127.0.0.1", Config::NetworkSettings::dbPort())) {
         LOGERR << "There is already a process listening on port " <<
            Config::NetworkSettings::dbPort();
         LOGERR << "ArmoryDB cannot start under these conditions. Shutting down!";
         LOGERR << "Make sure to shutdown the conflicting process" <<
            "before trying again (most likely another ArmoryDB instance)";
         exit(-2);
      }
   }
   LOGINFO << "datadir: " << Config::getDataDir().string();

   //start blockchain service
   bdmThread.start(Config::DBSettings::initMode());
   if (!Config::DBSettings::checkChain()) {
      WebSocketServer::start(bdmThread.bdm(), false);
      LOGINFO << "WS server has shut down" << std::endl;
   } else {
      bdmThread.join();
   }

   //shutdown BDM and cleanup crypto contexts
   bdmThread.shutdown();
   shutdownBIP151CTX();
   Cryptography::ECDSA::shutdown();
   return 0;
}
