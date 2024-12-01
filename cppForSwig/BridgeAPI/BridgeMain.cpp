////////////////////////////////////////////////////////////////////////////////
//                                                                            //
//  Copyright (C) 2019-2024, goatpig                                          //
//  Distributed under the MIT license                                         //
//  See LICENSE-MIT or https://opensource.org/licenses/MIT                    //
//                                                                            //
////////////////////////////////////////////////////////////////////////////////

#include <cstdlib>
#include "BridgeSocket.h"
#include "CppBridge.h"

////////////////////////////////////////////////////////////////////////////////
int main(int argc, char* argv[])
{
#ifdef _WIN32
   WSADATA wsaData;
   WORD wVersion = MAKEWORD(2, 0);
   WSAStartup(wVersion, &wsaData);
#endif

   CryptoECDSA::setupContext();
   startupBIP151CTX();
   startupBIP150CTX(4);

   unsigned count = argc + 1;
   auto args = new char*[count];
   for (unsigned i=0; i<argc; i++) {
      args[i] = argv[i];
   }

   //enable logs
   STARTLOGGING(
      Armory::Config::Pathing::logFilePath("bridgeLog").string(), LogLvlDebug);
   LOGENABLESTDOUT();

   auto pubKeyHex = std::getenv("SERVER_PUBKEY");
   if (pubKeyHex == nullptr) {
      LOGERR << "could not find pubkey env var, aborting!";
      exit(-1);
   }
   auto pubKeyStr = std::string("--uiPubKey=") + pubKeyHex;
   args[argc] = (char*)pubKeyStr.c_str();

   auto bridgePortChar = std::getenv("BRIDGE_PORT");
   if (bridgePortChar == nullptr) {
      LOGERR << "could not find bridge port env var, aborting!";
      exit(-2);
   }
   std::string bridgePortStr(bridgePortChar);

   //init static configuration variables
   LOGINFO << "parsing arguments";
   Armory::Config::parseArgs(count, args,
      Armory::Config::ProcessType::Bridge);

   LOGINFO << "cppbridge args:" <<
      "\n - datadir: " << Armory::Config::getDataDir().string() <<
      "\n - offline: " << Armory::Config::NetworkSettings::isOffline() <<
      "\n - auth mode: " << Armory::Config::NetworkSettings::oneWayAuth() <<
      "\n - db port: " << Armory::Config::NetworkSettings::dbPort() <<
      "\n - bridge port: " << bridgePortStr;

   auto bridgeLogPath = Armory::Config::Pathing::logFilePath("bridgeLog");
   LOGINFO << "bridge log: " << bridgeLogPath.string();
   STARTLOGGING(bridgeLogPath.string(), LogLvlDebug);

   //setup the bridge
   auto bridge = std::make_shared<Armory::Bridge::CppBridge>(
      Armory::Config::getDataDir(),
      Armory::Config::NetworkSettings::dbIP(),
      Armory::Config::NetworkSettings::dbPort(),
      Armory::Config::NetworkSettings::oneWayAuth(),
      Armory::Config::NetworkSettings::isOffline()
   );

   //setup the socket
   auto sockPtr = std::make_shared<Armory::Bridge::CppBridgeSocket>(
      "127.0.0.1", bridgePortStr, bridge);

   //set bridge write lambda
   auto pushPayloadLbd = [sockPtr](
      std::unique_ptr<Armory::Bridge::WritePayload_Bridge> payload)->void
   {
      sockPtr->pushPayload(move(payload), nullptr);
   };
   bridge->setWriteLambda(pushPayloadLbd);

   //connect
   if (!sockPtr->connectToRemote())
   {
      LOGERR << "cannot find ArmoryQt client, shutting down";
      return -1;
   }

   //block main thread till socket dies
   sockPtr->blockUntilClosed();

   //done
   LOGINFO << "exiting";

   shutdownBIP151CTX();
   CryptoECDSA::shutdown();

   return 0;
}
