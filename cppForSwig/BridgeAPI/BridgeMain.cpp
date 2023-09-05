////////////////////////////////////////////////////////////////////////////////
//                                                                            //
//  Copyright (C) 2019-23, goatpig                                            //
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
   CryptoECDSA::setupContext();
   startupBIP151CTX();
   startupBIP150CTX(4);

   unsigned count = argc + 1;
   auto args = new char*[count];
   for (int i=0; i<argc; i++)
      args[i] = argv[i];


   auto pubKeyHex = std::getenv("SERVER_PUBKEY");
   if (pubKeyHex == nullptr)
      throw std::runtime_error("could not find pubkey env var, aborting!");

   auto pubKeyStr = std::string("--uiPubKey=") + pubKeyHex;
   args[argc] = (char*)pubKeyStr.c_str();

   //init static configuration variables
   Armory::Config::parseArgs(
      count, args, Armory::Config::ProcessType::Bridge);


   //enable logs
   STARTLOGGING(
      Armory::Config::Pathing::logFilePath("bridgeLog"), LogLvlDebug);
   //LOGDISABLESTDOUT();

   //setup the bridge
   auto bridge = std::make_shared<Armory::Bridge::CppBridge>(
      Armory::Config::getDataDir(),
      "127.0.0.1", Armory::Config::NetworkSettings::listenPort(),
      Armory::Config::NetworkSettings::oneWayAuth(),
      Armory::Config::NetworkSettings::isOffline());

   //setup the socket
   auto sockPtr = std::make_shared<Armory::Bridge::CppBridgeSocket>(
      "127.0.0.1", "46122", bridge);

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
