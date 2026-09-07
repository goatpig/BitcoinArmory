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
#include "AsyncClient.h"

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
   std::string bridgePortStr(bridgePortChar);

   //init static configuration variables
   Armory::Config::parseArgs(count, args,
      Armory::Config::ProcessType::Bridge);

   //turn on logging
   auto bridgeLogPath = Armory::Config::Pathing::logFilePath("bridgeLog");
   STARTLOGGING(bridgeLogPath, LogLvlDebug);
   LOGENABLESTDOUT();

   LOGINFO << "bridge log: " << bridgeLogPath.string();
   LOGINFO << "cppbridge args:" <<
      "\n - datadir: " << Armory::Config::getDataDir().string() <<
      "\n - offline: " << Armory::Config::NetworkSettings::isOffline() <<
      "\n - auth mode: " << Armory::Config::NetworkSettings::oneWayAuth() <<
      "\n - db port: " << Armory::Config::NetworkSettings::dbPort() <<
      "\n - bridge port: " << bridgePortStr;

   //setup the bridge & socket
   auto bridge = std::make_shared<Armory::Bridge::CppBridge>();
   auto sockPtr = std::make_shared<Armory::Bridge::CppBridgeSocket>(
      "127.0.0.1", bridgePortStr, bridge);

   //set bridge write lambda
   auto pushPayloadLbd = [sockPtr](
      std::unique_ptr<Armory::Bridge::WritePayload_Bridge> payload)->void
   {
      sockPtr->pushPayload(std::move(payload), nullptr);
   };
   bridge->setWriteLambda(pushPayloadLbd);

   //connect
   if (!sockPtr->connectToRemote()) {
      LOGERR << "cannot find ArmoryQt client, shutting down";
      return -1;
   }

   //block main thread till socket dies
   sockPtr->blockUntilClosed();

   //done
   LOGINFO << "exiting";

   shutdownBIP151CTX();
   Cryptography::ECDSA::shutdown();

   return 0;
}
