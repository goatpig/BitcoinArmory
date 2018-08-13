////////////////////////////////////////////////////////////////////////////////
//                                                                            //
//  Copyright (C) 2018, goatpig.                                              //
//  Distributed under the MIT license                                         //
//  See LICENSE-MIT or https://opensource.org/licenses/MIT                    //                                      
//                                                                            //
////////////////////////////////////////////////////////////////////////////////

#ifndef _H_SOCKETSERVICE
#define _H_SOCKETSERVICE

#include <functional>
#include "SocketIncludes.h"
#include "ThreadSafeClasses.h"
#include "log.h"

struct SocketStruct
{
   SOCKET sockfd_ = SOCK_MAX;
   bool singleUse_ = false;

   function<void(void)> serviceRead_;
   function<void(void)> serviceClose_;
};

#ifdef _WIN32
struct SocketService
{
private:
   WSAEVENT event_;
   thread thr_;
   atomic<bool> run_;

   Queue<SocketStruct> socketQueue_;

private:
   void serviceSockets(void);

public:
   SocketService(void)
   {
      event_ = nullptr;
      run_.store(true, memory_order_relaxed);
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
   thread thr_;
   atomic<bool> run_;

   Queue<SocketStruct> socketQueue_;

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

#endif
