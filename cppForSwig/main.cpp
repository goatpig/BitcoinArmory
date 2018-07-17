#include <string>
#include <iostream>
#include <sstream>


using namespace std;

#include "BlockDataManagerConfig.h"
#include "BDM_mainthread.h"
#include "BDM_Server.h"

int main(int argc, char* argv[])
{
   GOOGLE_PROTOBUF_VERIFY_VERSION;

#ifdef _WIN32
   WSADATA wsaData;
   WORD wVersion = MAKEWORD(2, 0);
   WSAStartup(wVersion, &wsaData);
#endif

   BlockDataManagerConfig bdmConfig;
   bdmConfig.parseArgs(argc, argv);
   
   cout << "logging in " << bdmConfig.logFilePath_ << endl;
   STARTLOGGING(bdmConfig.logFilePath_, LogLvlDebug);
   if (!bdmConfig.useCookie_)
      LOGENABLESTDOUT();
   else
      LOGDISABLESTDOUT();

   LOGINFO << "Running on " << bdmConfig.threadCount_ << " threads";
   LOGINFO << "Ram usage level: " << bdmConfig.ramUsage_;

   if (FCGX_Init())
      throw runtime_error("failed to initialize FCGI engine");

   //init db
   BlockDataManagerConfig::setServiceType(SERVICE_WEBSOCKET);
   BlockDataManagerThread bdmThread(bdmConfig);
   bdmThread.start(bdmConfig.initMode_);

   //init listen loop
   WebSocketServer server;
   
   if (!bdmConfig.checkChain_)
   {
      //check we can listen on this ip:port
      if (SimpleSocket::checkSocket("127.0.0.1", bdmConfig.listenPort_))
      {
         LOGERR << "There is already a process listening on port " << 
            bdmConfig.listenPort_;
         LOGERR << "ArmoryDB cannot start under these conditions. Shutting down!";
         LOGERR << "Make sure to shutdown the conflicting process" <<
            "before trying again (most likely another ArmoryDB instance)";

         exit(1);
      }
   }


   //create cookie file if applicable
   bdmConfig.createCookie();
   
   if (!bdmConfig.checkChain_)
   {
      //process incoming connections
      server.start(&bdmThread, false);
   }
   else
   {
      bdmThread.join();
   }

   //stop all threads and clean up
   server.shutdown();
   google::protobuf::ShutdownProtobufLibrary();

   return 0;
}
