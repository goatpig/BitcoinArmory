////////////////////////////////////////////////////////////////////////////////
//                                                                            //
//  Copyright (C) 2018-2026, goatpig.                                         //
//  Distributed under the MIT license                                         //
//  See LICENSE-MIT or https://opensource.org/licenses/MIT                    //
//                                                                            //
////////////////////////////////////////////////////////////////////////////////

#pragma once

#include <functional>
#include "Utils/ThreadSafeClasses.h"
#include "SocketIncludes.h"

namespace Armory
{
   namespace Network
   {
      struct SocketStruct
      {
         SOCKET sockfd_ = SOCK_MAX;
         bool singleUse_ = false;

         std::function<void(void)> serviceRead_;
         std::function<void(void)> serviceClose_;
      };

      #ifdef _WIN32
      struct SocketService
      {
      private:
         WSAEVENT event_;
         std::thread thr_;
         std::atomic<bool> run_;

         Threading::Queue<SocketStruct> socketQueue_;

      private:
         void serviceSockets(void);

      public:
         SocketService(void)
         {
            event_ = nullptr;
            run_.store(true, std::memory_order_relaxed);
         }

         void addSocket(SocketStruct&);
         void startService(void);
         void shutdown(void);
      };

      #else
      struct SocketService
      {
      private:
         SOCKET pipes_[2];
         std::thread thr_;
         std::atomic<bool> run_;

         Threading::Queue<SocketStruct> socketQueue_;

      private:
         void serviceSockets(void);

      public:
         SocketService(void)
         {
            pipes_[0] = pipes_[1] = SOCK_MAX;
         }

         void addSocket(SocketStruct&);
         void startService(void);
         void shutdown(void);
      };
      #endif
   } //namespace Network
} //namespace Armory
