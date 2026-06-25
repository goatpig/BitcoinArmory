////////////////////////////////////////////////////////////////////////////////
//                                                                            //
//  Copyright (C) 2016-2026, goatpig.                                         //
//  Distributed under the MIT license                                         //
//  See LICENSE-MIT or https://opensource.org/licenses/MIT                    //
//                                                                            //
////////////////////////////////////////////////////////////////////////////////

#pragma once

#include <sys/types.h>
#include <string>
#include <sstream>
#include <stdint.h>
#include <functional>
#include <memory>

#ifndef _WIN32
#include <poll.h>
#define socketService socketService_nix
#else
#define socketService socketService_win
#endif

#include "Utils/ThreadSafeClasses.h"
#include "Utils/BinaryData.h"
#include "SocketIncludes.h"

typedef std::function<bool(std::vector<uint8_t>, std::exception_ptr)> ReadCallback;
enum class SocketType : int;

namespace Armory
{
   namespace Network
   {
      //////////////////////////////////////////////////////////////////////////
      struct CallbackReturn
      {
         virtual ~CallbackReturn(void) = 0;
         virtual void callback(BinaryDataRef) = 0;
      };

      //////////////////////////////////////////////////////////////////////////
      struct Socket_ReadPayload
      {
      public:
         uint16_t id_ = UINT16_MAX;
         std::unique_ptr<CallbackReturn> callbackReturn_ = nullptr;

      public:
         Socket_ReadPayload(void);
         Socket_ReadPayload(unsigned);
      };

      struct Socket_WritePayload
      {
      public:
         unsigned id_;

      public:
         virtual ~Socket_WritePayload(void) = 0;
         virtual void serialize(std::vector<uint8_t>&) = 0;
         virtual std::string serializeToText(void) = 0;
         virtual size_t getSerializedSize(void) const = 0;
         virtual bool isSingleSegment(void) const;
      };

      //////////////////////////////////////////////////////////////////////////
      struct AcceptStruct
      {
      public:
         SOCKET sockfd_;
         sockaddr saddr_;
         socklen_t addrlen_;
         ReadCallback readCallback_;

      public:
         AcceptStruct(void);
      };

      ////////
      class SocketPrototype
      {
         friend class ListenServer;

      private: 
         bool blocking_ = true;

      protected:

      public:
         typedef std::function<bool(const std::vector<uint8_t>&)>  SequentialReadCallback;
         typedef std::function<void(AcceptStruct)> AcceptCallback;

      protected:
         const size_t maxread_ = 4*1024*1024;
         
         struct sockaddr serv_addr_;
         const std::string addr_;
         const std::string port_;

         bool verbose_ = true;

      private:
         void init(void);

      protected:
         SocketPrototype(void);

         void setBlocking(SOCKET, bool);
         void listen(AcceptCallback, SOCKET& sockfd);

      public:
         SocketPrototype(const std::string& addr, const std::string& port, bool init = true);
         virtual ~SocketPrototype(void) = 0;

         virtual bool testConnection(void);
         bool isBlocking(void) const;
         SOCKET openSocket(bool blocking);

         static void closeSocket(SOCKET&);
         virtual void pushPayload(
            std::unique_ptr<Socket_WritePayload>,
            std::shared_ptr<Socket_ReadPayload>) = 0;
         virtual bool connectToRemote(void) = 0;

         virtual SocketType type(void) const = 0;
         const std::string& getAddrStr(void) const;
         const std::string& getPortStr(void) const;

         //override me
         virtual bool running(void) const;
      };

      class SimpleSocket : public SocketPrototype
      {
      protected:
         SOCKET sockfd_ = SOCK_MAX;

      private:
         int writeToSocket(std::vector<uint8_t>&);

      public:
         SimpleSocket(const std::string& addr, const std::string& port);
         SimpleSocket(SOCKET);
         ~SimpleSocket(void);

         SocketType type(void) const override;
         SOCKET getSockFD(void) const;
         void pushPayload(
            std::unique_ptr<Socket_WritePayload>,
            std::shared_ptr<Socket_ReadPayload>) override;
         std::vector<uint8_t> readFromSocket(void);
         void shutdown(void);
         void listen(AcceptCallback);
         bool connectToRemote(void) override;

         //
         static bool checkSocket(const std::string& ip, const std::string& port);
      };

      class PersistentSocket : public SocketPrototype
      {
         friend class ListenServer;

      private:
         SOCKET sockfd_ = SOCK_MAX;
         std::vector<std::thread> threads_;

         std::vector<uint8_t> writeLeftOver_;
         size_t writeOffset_ = 0;

         std::atomic<bool> run_;
         std::shared_future<bool> shutdownFut_;
         std::unique_ptr<std::promise<bool>> shutdownProm_;
         std::mutex shutdownMutex_;

      #ifdef _WIN32
         WSAEVENT events_[2];
      #else
         SOCKET pipes_[2];
      #endif

         Threading::BlockingQueue<std::vector<uint8_t>> readQueue_;
         Threading::Queue<std::vector<uint8_t>> writeQueue_;

      private:
         void signalService(uint8_t);
      #ifdef _WIN32
         void socketService_win(void);
      #else
         void socketService_nix(void);
      #endif
         void readService(void);
         void initPipes(void);
         void cleanUpPipes(void);
         void init(void);

      protected:
         virtual bool processPacket(std::vector<uint8_t>&, std::vector<uint8_t>&);
         virtual void respond(std::vector<uint8_t>&) = 0;
         void queuePayloadForWrite(std::vector<uint8_t>&);

      public:
         PersistentSocket(const std::string& addr, const std::string& port);
         PersistentSocket(SOCKET);
         ~PersistentSocket(void);

         void shutdown();
         bool openSocket(bool);
         int getSocketName(struct sockaddr& );
         int getPeerName(struct sockaddr&);
         bool connectToRemote(void);
         bool isValid(void) const;
         bool testConnection(void);
         void blockUntilClosed(void) const;
      };

      ////////
      class ListenServer
      {
      private:
         struct SocketStruct
         {
         private:
            SocketStruct(const SocketStruct&) = delete;

         public:
            SocketStruct(void);

         public:
            std::shared_ptr<SimpleSocket> sock_;
            std::thread thr_;
         };

      private:
         std::unique_ptr<SimpleSocket> listenSocket_;
         std::map<SOCKET, std::unique_ptr<SocketStruct>> acceptMap_;
         Threading::Queue<SOCKET> cleanUpStack_;

         std::thread listenThread_;
         std::mutex mu_;

      private:
         void listenThread(ReadCallback);
         void acceptProcess(AcceptStruct);
         ListenServer(const ListenServer&) = delete;

      public:
         ListenServer(const std::string& addr, const std::string& port);
         ~ListenServer(void);

         void start(ReadCallback);
         void stop(void);
         void join(void);
      };
   } //namespace Network
} //namespace Armory
