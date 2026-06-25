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
   std::cout << "logging in " << logFilePath << std::endl;
   auto logLevel = Config::NetworkSettings::ephemeralPeers() ?
      LogLvlWarn : LogLvlDebug;
   STARTLOGGING(logFilePath, logLevel);
   LOGENABLESTDOUT();

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

   if (Config::NetworkSettings::ephemeralPeers()) {
      if (Config::NetworkSettings::oneWayAuth()) {
         LOGERR << "--ephemeral and --oneWayAuth are mutually exclusive for db";
         exit(-3);
      }
      //initAuthPeers will setup the ephemeral keys
      try {
         WebSocketServer::initAuthPeers(
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
      auto serverPeersFile = Config::getDataDir() / SERVER_AUTH_PEER_FILENAME;
      if (!FileUtils::pathExists(serverPeersFile, 0) &&
         !Config::NetworkSettings::ephemeralPeers()) {
         LOGINFO << "no server peers store found, creating one...";
         auto passWrapper = []()->std::unique_ptr<Passphrase::Params>
         {
            auto passLbd = TerminalPassphrasePrompt::getLambda(
               "new server peers store");
            auto result = passLbd({});
            if (!result.success) {
               throw std::runtime_error("peers store init was rejected");
            }
            return std::make_unique<Passphrase::Params>(
               250ms, 0, std::move(result.passphrase));
         };
         auto peers = Wallets::AuthorizedPeers::createWallet(
            {serverPeersFile, {passWrapper}});
         WebSocketServer::initAuthPeers(peers);
      } else {
         auto passLbd = TerminalPassphrasePrompt::getLambda(
            "server peers store");
         WebSocketServer::initAuthPeers({serverPeersFile, passLbd});
      }
      Wallets::PeerKey myKey{WebSocketServer::getPublicKey(),
         Config::NetworkSettings::oneWayAuth(), true};
      LOGINFO << "This is my key: " << myKey.toHumanReadable();
   }

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
