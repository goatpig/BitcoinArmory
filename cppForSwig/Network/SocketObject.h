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
      using port_t = uint16_t;

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

      class Socket_WritePayload
      {
      private:
         static std::atomic_uint32_t idCounter_;

      public:
         const uint32_t id;

      public:
         Socket_WritePayload(void);
         Socket_WritePayload(uint32_t);

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

      public:
         typedef std::function<void(AcceptStruct)> AcceptCallback;

      protected:
         struct sockaddr serv_addr_;
         const std::string addr_;
         const port_t port_;
         const std::string name_;
         bool verbose_ = true;

      private:
         void init(void);

      protected:
         SocketPrototype(const std::string&);

         void setBlocking(SOCKET, bool);
         void listen(AcceptCallback, SOCKET&);

      public:
         SocketPrototype(
            const std::string&, port_t,
            const std::string&, bool = true);
         virtual ~SocketPrototype(void) = 0;

         virtual bool testConnection(void);
         bool isBlocking(void) const;
         SOCKET openSocket(bool);

         static void closeSocket(SOCKET&);
         virtual void pushPayload(
            std::unique_ptr<Socket_WritePayload>,
            std::shared_ptr<Socket_ReadPayload>) = 0;
         virtual bool connectToRemote(void) = 0;

         virtual SocketType type(void) const = 0;
         const std::string& getAddrStr(void) const;

         //override me
         virtual bool running(void) const = 0;
      };

      class SimpleSocket : public SocketPrototype
      {
      protected:
         SOCKET sockfd_ = SOCK_MAX;

      private:
         int writeToSocket(std::vector<uint8_t>&);

      public:
         SimpleSocket(const std::string&, port_t, const std::string&);
         SimpleSocket(SOCKET, const std::string&);
         ~SimpleSocket(void);

         SOCKET getSockFD(void) const;
         std::vector<uint8_t> readFromSocket(void);
         void shutdown(void);
         void listen(AcceptCallback);

         //overrides
         bool connectToRemote(void) override;
         SocketType type(void) const override;
         bool running(void) const override;
         void pushPayload(
            std::unique_ptr<Socket_WritePayload>,
            std::shared_ptr<Socket_ReadPayload>) override;

         //statics
         static bool checkSocket(const std::string&, port_t);
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
         std::shared_future<void> shutdownFut_;
         std::promise<void> shutdownProm_;
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
         PersistentSocket(const std::string&, port_t, const std::string&);
         PersistentSocket(SOCKET, const std::string&);
         ~PersistentSocket(void);

         void shutdown(void);
         bool openSocket(bool);
         int getSocketName(struct sockaddr& );
         int getPeerName(struct sockaddr&);
         bool isValid(void) const;
         bool testConnection(void);
         void blockUntilClosed(void) const;

         //overrides
         bool connectToRemote(void) override;
         bool running(void) const override;
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
         ListenServer(const std::string&, port_t, const std::string&);
         ~ListenServer(void);

         void start(ReadCallback);
         void stop(void);
         void join(void);
      };
   } //namespace Network
} //namespace Armory
