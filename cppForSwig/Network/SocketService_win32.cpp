////////////////////////////////////////////////////////////////////////////////
//                                                                            //
//  Copyright (C) 2018, goatpig.                                              //
//  Distributed under the MIT license                                         //
//  See LICENSE-MIT or https://opensource.org/licenses/MIT                    //
//                                                                            //
////////////////////////////////////////////////////////////////////////////////

#include "SocketService.h"
#include <Utils/log.h>

using namespace std;
using namespace Armory::Threading;

////////////////////////////////////////////////////////////////////////////////
//
// SocketService
//
////////////////////////////////////////////////////////////////////////////////
void SocketService::addSocket(SocketStruct& obj)
{
   socketQueue_.push_back(move(obj));
   WSASetEvent(event_);
}

////////////////////////////////////////////////////////////////////////////////
void SocketService::startService()
{
   event_ = WSACreateEvent();

   auto service = [this](void)->void
   {
      this->serviceSockets();
   };

   thr_ = thread(service);
}

////////////////////////////////////////////////////////////////////////////////
void SocketService::shutdown()
{
   run_.store(false, memory_order_relaxed);
   WSASetEvent(event_);

   if (thr_.joinable())
      thr_.join();

   WSACloseEvent(event_);
}

////////////////////////////////////////////////////////////////////////////////
void SocketService::serviceSockets()
{
   map<SOCKET, SocketStruct> sockets;
   set<SOCKET> toCleanUp;

   auto updateSocketVector = [&sockets, this](void)->void
   {
      try
      {
         while (1)
         {
            auto&& sockStruct = this->socketQueue_.pop_front();
            if (sockStruct.sockfd_ == SOCK_MAX)
               continue;

            WSAEventSelect(sockStruct.sockfd_, event_, FD_READ | FD_CLOSE);
            sockets.insert(move(make_pair(
               sockStruct.sockfd_, move(sockStruct))));
         }
      }
      catch (Armory::Threading::IsEmpty&)
      {
         return;
      }
   };

   auto cleanUp = [&sockets, &toCleanUp, this](void)->void
   {
      for (auto& sockfd : toCleanUp)
      {
         WSAEventSelect(sockfd, this->event_, 0);
         sockets.erase(sockfd);
      }

      toCleanUp.clear();
   };

   auto serviceRead = [&sockets, &toCleanUp, this](SocketStruct& sockObj)->void
   {
      if(sockObj.serviceRead_) 
         sockObj.serviceRead_();

      if (sockObj.singleUse_)
         toCleanUp.insert(sockObj.sockfd_);
   };

   auto serviceClose = [&toCleanUp](SocketStruct& sockObj)->void
   {
      if(sockObj.serviceClose_)
         sockObj.serviceClose_();

      toCleanUp.insert(sockObj.sockfd_);
   };

   DWORD timeout = 100000;
   while (run_.load(memory_order_relaxed))
   {
      cleanUp();
      auto ev = WSAWaitForMultipleEvents(1, &event_, false, timeout, false);
      if (ev == WSA_WAIT_TIMEOUT)
         continue;

      if (ev == WSA_WAIT_FAILED)
      {
         LOGERR << "WSAWaitForMultipleEvents failed in serviceSockets";
         break;
      }

      if (ev == WSA_WAIT_EVENT_0)
      {
         //reset user event
         WSAResetEvent(event_);
         updateSocketVector();
      }

      for (auto& sockObj : sockets)
      {
         WSANETWORKEVENTS networkevents;
         if (WSAEnumNetworkEvents(sockObj.first, 0, &networkevents) ==
            SOCKET_ERROR)
         {
            LOGERR << "error getting network events for socket";
            toCleanUp.insert(sockObj.first);
            continue;
         }

         //service socket
         if (networkevents.lNetworkEvents & FD_READ)
         {
            //serviceRead
            serviceRead(sockObj.second);
         }

         if (networkevents.lNetworkEvents & FD_WRITE)
         {
            //writeReady = true;
         }

         if (networkevents.lNetworkEvents & FD_CLOSE)
         {
            serviceClose(sockObj.second);
         }
      }
   }

   for (auto& sockPair : sockets)
   {
      if(sockPair.second.serviceClose_)
         sockPair.second.serviceClose_();
   }
}