////////////////////////////////////////////////////////////////////////////////
//                                                                            //
//  Copyright (C) 2019-2026, goatpig                                          //
//  Distributed under the MIT license                                         //
//  See LICENSE-MIT or https://opensource.org/licenses/MIT                    //
//                                                                            //
////////////////////////////////////////////////////////////////////////////////

#ifdef _WIN32
   #include <winsock2.h>
   #include <windows.h>
#endif

#include <cstdlib>
#include <memory>

#include "CppBridge.h"
#include <Utils/ArmoryConfig.h>
#include <Utils/BIP150_151.h>
#include <Utils/Cryptography.h>
#include "BridgeSocket.h"
#include "ProtoCommandParser.h"
#include "AsyncClient.h"

using namespace Armory;

////////////////////////////////////////////////////////////////////////////////
int main(int argc, char* argv[])
{
#ifdef _WIN32
   WSADATA wsaData;
   WORD wVersion = MAKEWORD(2, 0);
   WSAStartup(wVersion, &wsaData);
#endif

   Cryptography::ECDSA::setupContext();
   startupBIP151CTX();
   startupBIP150CTX(4);

   unsigned count = argc + 1;
   auto args = new char*[count];
   for (int i = 0; i < argc; i++) {
      args[i] = argv[i];
   }

   //append pubkey to arg list
   auto pubKeyHex = std::getenv("SERVER_PUBKEY");
   if (pubKeyHex == nullptr) {
      LOGERR << "could not find pubkey env var, aborting!";
      exit(-1);
   }
   auto pubKeyStr = std::string("--uiPubKey=") + pubKeyHex;
   args[argc] = (char*)pubKeyStr.c_str();

   //grab ephemeral GUI server port
   auto bridgePortChar = std::getenv("BRIDGE_PORT");
   if (bridgePortChar == nullptr) {
      LOGERR << "could not find bridge port env var, aborting!";
      exit(-2);
   }
   Network::port_t bridgePort;
   auto [ptr, ec] = std::from_chars(bridgePortChar,
      bridgePortChar + strlen(bridgePortChar), bridgePort);
   if (ec != std::errc{}) {
      LOGERR << "invalid bridge port, aborting";
      exit(-3);
   }

   //init static configuration variables
   Config::parseArgs(count, args, Config::ProcessType::Bridge);

   //turn on logging
   auto bridgeLogPath = Config::Pathing::logFilePath("bridgeLog");
   STARTLOGGING(bridgeLogPath, LogLvlInfo);
   LOGENABLESTDOUT();

   LOGINFO << "bridge log: " << bridgeLogPath.string();
   LOGINFO << "cppbridge args:" <<
      "\n - datadir: " << Config::getDataDir().string() <<
      "\n - offline: " << Config::NetworkSettings::isOffline() <<
      "\n - auth mode: " << Config::NetworkSettings::oneWayAuth() <<
      "\n - db port: " << Config::NetworkSettings::dbPort() <<
      "\n - bridge port: " << bridgePort;

   //setup the bridge & socket
   auto sockPtr = std::make_shared<Bridge::CppBridgeSocket>(
      "127.0.0.1", bridgePort);
   auto bridge = std::make_shared<Bridge::CppBridge>(sockPtr);

   //connect
   if (!sockPtr->connectToRemote()) {
      LOGERR << "cannot find ArmoryQt client, shutting down";
      return -1;
   }

   while (true) {
      try {
         auto payload = sockPtr->getPayload();
         if (!Bridge::ProtoCommandParser::processData(bridge, payload)) {
            //if the processing of a request returns false, close the bridge
            LOGDEBUG << "bridge request processing led to fatal error, aborting";
            break;
         }
      } catch (Threading::StopBlockingLoop&) {
         //socket read queue has been terminated, we are done
         LOGDEBUG << "bridge socket has been closed";
         break;
      } catch (const std::exception& e) {
         LOGERR << std::format(
            "bridge socket processing failed with error: \"{}\", aborting",
            e.what());
         break;
      }
   }

   LOGINFO << "bridge is exiting";
   sockPtr->shutdown();
   shutdownBIP151CTX();
   Cryptography::ECDSA::shutdown();
   return 0;
}
