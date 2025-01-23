////////////////////////////////////////////////////////////////////////////////
//                                                                            //
//  Copyright (C) 2016-19, goatpig.                                           //
//  Distributed under the MIT license                                         //
//  See LICENSE-MIT or https://opensource.org/licenses/MIT                    //                                      
//                                                                            //
////////////////////////////////////////////////////////////////////////////////

#include <string>
#include <iostream>
#include <sstream>
#include <btc/ecc.h>

#include "ArmoryConfig.h"
#include "BDM_mainthread.h"
#include "BDM_Server.h"
#include "TerminalPassphrasePrompt.h"

using namespace Armory::Config;

#define LOG_FILE_NAME "dbLog"

int main(int argc, char* argv[])
{
   CryptoECDSA::setupContext();
   startupBIP151CTX();
   startupBIP150CTX(4);

#ifdef _WIN32
   WSADATA wsaData;
   WORD wVersion = MAKEWORD(2, 0);
   WSAStartup(wVersion, &wsaData);
#endif

   try {
      Armory::Config::parseArgs(argc, argv, Armory::Config::ProcessType::DB);
   } catch (const DbErrorMsg& e) {
      std::cout << "Failed to setup with error:" << std::endl;
      std::cout << "   " << e.what() << std::endl;
      std::cout << "Aborting!" << std::endl;

      return -1;
   }

   auto logFilePath = Pathing::logFilePath(LOG_FILE_NAME).string();
   std::cout << "logging in " << logFilePath << std::endl;
   STARTLOGGING(logFilePath, LogLvlDebug);
   if (!NetworkSettings::useCookie()) {
      LOGENABLESTDOUT();
   } else {
      LOGDISABLESTDOUT();
   }

   LOGINFO << "Running on " << DBSettings::threadCount() << " threads";
   LOGINFO << "Ram usage level: " << DBSettings::ramUsage();

   //init state
   DBSettings::setServiceType(SERVICE_WEBSOCKET);
   BlockDataManagerThread bdmThread;

   if (!DBSettings::checkChain()) {
      //check we can listen on this ip:port
      if (SimpleSocket::checkSocket("127.0.0.1", NetworkSettings::dbPort())) {
         LOGERR << "There is already a process listening on port " << 
            NetworkSettings::dbPort();
         LOGERR << "ArmoryDB cannot start under these conditions. Shutting down!";
         LOGERR << "Make sure to shutdown the conflicting process" <<
            "before trying again (most likely another ArmoryDB instance)";

         exit(1);
      }
   }

   {
      //setup remote peers db, this will block the init process until 
      //peers db is unlocked
      LOGINFO << "datadir: " << Armory::Config::getDataDir().string();
      auto passLbd = TerminalPassphrasePrompt::getLambda("peers db");
      WebSocketServer::initAuthPeers(passLbd);
   }

   //start up blockchain service
   bdmThread.start(DBSettings::initMode());

   if (!DBSettings::checkChain()) {
      //start websocket server
      WebSocketServer::start(&bdmThread, false);
   } else {
      bdmThread.join();
   }

   //stop all threads and clean up
   WebSocketServer::shutdown();

   shutdownBIP151CTX();
   CryptoECDSA::shutdown();

   return 0;
}
