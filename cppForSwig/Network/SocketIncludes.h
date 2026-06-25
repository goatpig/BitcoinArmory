////////////////////////////////////////////////////////////////////////////////
//                                                                            //
//  Copyright (C) 2016-2026, goatpig.                                         //
//  Distributed under the MIT license                                         //
//  See LICENSE-MIT or https://opensource.org/licenses/MIT                    //
//                                                                            //
////////////////////////////////////////////////////////////////////////////////

#pragma once

#include <stdexcept>
#include <string>

#ifdef _WIN32
   #include <WinSock2.h>
   #include <ws2tcpip.h>

   #define SOCK_MAX SIZE_MAX
#else
   #include <sys/types.h>
   #include <sys/socket.h>
   #include <netdb.h>
   #include <unistd.h>
   #include <fcntl.h>
   #include <limits.h>
   #include <poll.h>
   #define closesocket close

   typedef int SOCKET;
   #define SOCK_MAX INT_MAX
#endif

namespace Armory
{
   namespace Network
   {
      struct SocketError : public std::runtime_error
      {
      public:
         SocketError(const std::string& e) : std::runtime_error(e)
         {}
      };
   }
}
