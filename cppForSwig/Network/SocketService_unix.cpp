////////////////////////////////////////////////////////////////////////////////
//                                                                            //
//  Copyright (C) 2018-2026, goatpig.                                         //
//  Distributed under the MIT license                                         //
//  See LICENSE-MIT or https://opensource.org/licenses/MIT                    //
//                                                                            //
////////////////////////////////////////////////////////////////////////////////

#include <cstring>
#include "SocketService.h"
#include "Utils/log.h"

using namespace Armory;
using namespace Armory::Network;

////////////////////////////////////////////////////////////////////////////////
// SocketService
void SocketService::addSocket(SocketStruct& obj)
{
   socketQueue_.push_back(std::move(obj));
   char b = 0;
   write(pipes_[1], &b, 1);
}

void SocketService::startService()
{
   pipe(pipes_);
   thr_ = std::thread([this](){ serviceSockets(); });
}

void SocketService::shutdown()
{
   char b = 1;
   write(pipes_[1], &b, 1);

   if (thr_.joinable()) {
      thr_.join();
   }

   for (unsigned i = 0; i < 2; i++) {
      if (pipes_[i] != SOCK_MAX) {
         close(pipes_[i]);
      }
   }
}

////////
void SocketService::serviceSockets()
{
   std::map<SOCKET, SocketStruct> sockets;
   std::set<SOCKET> toCleanUp;
   std::vector<pollfd> vpfd;
   {
      vpfd.push_back(pollfd());
      auto& pfd = vpfd.back();
      pfd.events = POLLIN;
      pfd.fd = pipes_[0];
   }

   bool update = false;
   auto updateSocketVector = [&update, &sockets, &vpfd, this]()
   {
      if (!update) {
         return;
      }

      update = false;
      try {
         while (true) {
            auto sockStruct = this->socketQueue_.pop_front();
            if (sockStruct.sockfd_ == SOCK_MAX)
               continue;

            vpfd.push_back(pollfd());
            auto& pfd = vpfd.back();
            pfd.events = POLLIN;
            pfd.fd = sockStruct.sockfd_;

            sockets.emplace(
               sockStruct.sockfd_, std::move(sockStruct));
         }
      } catch (Threading::IsEmpty&) {
         return;
      }
   };

   auto cleanUp = [&sockets, &toCleanUp, &vpfd, this](void)->void
   {
      for (unsigned i = 1; i < vpfd.size(); i++) {
         if (toCleanUp.find(vpfd[i].fd) != toCleanUp.end()) {
            sockets.erase(vpfd[i].fd);
            if (vpfd.size() > 2) {
               vpfd[i] = vpfd[vpfd.size() - 1];
            }
            vpfd.pop_back();
            --i;
         }
      }
      toCleanUp.clear();
   };

   auto serviceRead = [&sockets, &toCleanUp, this](SOCKET sockfd)->void
   {
      auto iter = sockets.find(sockfd);
      if (iter == sockets.end()) {
         toCleanUp.emplace(sockfd);
         return;
      }

      if (iter->second.serviceRead_) {
         iter->second.serviceRead_();
      }

      if (iter->second.singleUse_) {
         toCleanUp.emplace(sockfd);
      }
   };

   auto serviceClose = [&toCleanUp, &sockets](SOCKET sockfd)->void
   {
      auto iter = sockets.find(sockfd);
      if (iter == sockets.end()) {
         toCleanUp.emplace(sockfd);
         return;
      }

      if (iter->second.serviceClose_) {
         iter->second.serviceClose_();
      }
      toCleanUp.emplace(sockfd);
   };

   int timeout = 100000;
   while (true) {
      cleanUp();
      updateSocketVector();

      auto status = poll(&vpfd[0], vpfd.size(), timeout);
      if (status == 0) {
         continue;
      }

      if (status == -1) {
         LOGERR << "poll failed in serviceSockets";
         break;
      }

      if (vpfd[0].revents & POLLIN) {
         uint8_t b;
         auto readAmt = read(vpfd[0].fd, (char*)&b, 1);

         if (readAmt == 1) {
            if (b == 0) {
               update = true;
            } else if (b == 1) {
               //clean up and return
               for (auto sockPair : sockets) {
                  if (sockPair.second.serviceClose_) {
                     sockPair.second.serviceClose_();
                  }
               }
               return;
            }
         }
      }

      for (unsigned i=1; i<vpfd.size(); i++) {
         auto& pfd = vpfd[i];
         if (pfd.revents & POLLNVAL || pfd.revents & POLLERR) {
            toCleanUp.insert(pfd.fd);
            continue;
         }

         //service socket
         if (pfd.revents & POLLIN) {
            //serviceRead
            serviceRead(pfd.fd);
         }

         if (pfd.revents & POLLOUT) {
            //place holder
         }

         if (pfd.revents & POLLHUP) {
            serviceClose(pfd.fd);
         }
      }
   }
}
